//The MIT License(MIT)
//Copyright © 2024 https://github.com/yuyuaqwq
//
//Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and /or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :
//
//The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <atomkv/node.h>

#include <atomkv/bucket_impl.h>
#include <atomkv/tx.h>

#include "pager.h"

namespace atomkv {

Node::Node(BTree* btree, PageId page_id, bool dirty)
    : btree_(btree)
    , page_(btree_->bucket().pager().Reference(page_id, dirty))
    , data_(page_->get<NodeData>()) {}

Node::Node(BTree* btree, Page page_ref)
    : btree_(btree)
    , page_(std::move(page_ref))
    , data_(page_->get<NodeData>()) {}

Node::Node(BTree* btree, uint8_t* page_buf)
    : btree_(btree)
    , data_(reinterpret_cast<NodeData*>(page_buf)) {}

Node::~Node() = default;

Node::Node(Node&& right) noexcept
    : btree_(right.btree_)
    , page_(std::move(right.page_))
    , data_(page_->get<NodeData>()) {}

void Node::operator=(Node&& right) noexcept {
    assert(btree_ == right.btree_);
    page_ = std::move(right.page_);
    data_ = page_->get<NodeData>();
}

bool Node::IsLeaf() const {
    return data_->header.type == NodeType::kLeaf;
}

bool Node::IsBranch() const {
    return data_->header.type == NodeType::kBranch;
}

std::span<const uint8_t> Node::GetKey(SlotId slot_id) {
    assert(slot_id < count());
    auto& slot = data_->slots[slot_id];
    return { GetRecordPtr(slot_id), slot.key_size };
}

std::pair<SlotId, bool> Node::LowerBound(std::span<const uint8_t> key) {
    bool eq = false;
    auto pos = std::lower_bound(data_->slots, data_->slots + count(), key
        , [&](const Slot& slot, std::span<const uint8_t> search_key) -> bool
    {
        auto diff = &slot - data_->slots;
        SlotId slot_id = diff;
        auto slot_key = GetKey(slot_id);
        auto res = btree_->comparator()(slot_key, search_key);
        if (res == std::strong_ordering::equal) eq = true;
        return res == std::strong_ordering::less;
    });
    auto diff = pos - data_->slots;
    return { diff, eq };
}

double Node::GetFillRate() {
    auto max_space = page_size() - sizeof(data_->header) - sizeof(data_->padding);
    auto used_space = max_space - FreeSpaceAfterCompaction();
    return double(used_space) / max_space;
}

Node Node::Copy() const {
    auto& bucket = btree_->bucket();
    auto& pager = bucket.pager();
    auto& tx = bucket.tx();

    assert(page_.has_value());
    auto node = Node(btree_, pager.Copy(*page_));

    node.set_last_modified_txid(tx.txid());

    return node;
}

Node Node::AddReference() const {
    assert(page_.has_value());
    return Node(btree_, page_->AddReference());
}

Page Node::Release() {
    assert(page_.has_value());
    return std::move(*page_);
}

PageId Node::page_id() const {
    return page_->page_id();
}

TxId Node::last_modified_txid() const {
    return data_->header.last_modified_txid;
}
 
uint16_t Node::count() const {
    return data_->header.count;
}

PageSize Node::page_size() const {
    return btree_->bucket().pager().page_size();
}

PageSize Node::MaxInlineRecordSize() {
    PageSize max_size = page_size() -
        sizeof(NodeData::header) -
        sizeof(NodeData::padding);
    // Ensure that each node can store at least two records
    max_size -= sizeof(Slot) * 2;
    assert(max_size % 2 == 0);
    return max_size / 2;
}

size_t Node::SpaceNeeded(size_t record_size, bool slot_needed) {
    if (!slot_needed) {
        return record_size;
    }
    return record_size + sizeof(Slot);
}

bool Node::RequestSpaceFor(std::span<const uint8_t> key, std::span<const uint8_t> value, bool slot_needed) {
    auto size = key.size() + value.size();

    size_t space_needed;
    if (size > MaxInlineRecordSize()) {
        space_needed = SpaceNeeded(sizeof(OverflowRecord), slot_needed);
    }
    else {
        space_needed = SpaceNeeded(size, slot_needed);
    }

    if (space_needed <= FreeSpace()) {
        return true;
    }

    if (space_needed <= FreeSpaceAfterCompaction()) {
        Compactify();
        return true;
    }

    return false;
}

PageSize Node::SlotSpace() {
    auto slot_space = reinterpret_cast<const uint8_t*>(data_->slots + data_->header.count) - Ptr();
    assert(slot_space < page_size());
    return slot_space;
}

PageSize Node::FreeSpace() {
    assert(data_->header.data_offset >= SlotSpace());
    auto free_space = data_->header.data_offset - SlotSpace();
    assert(free_space < page_size());
    return free_space;
}

PageSize Node::FreeSpaceAfterCompaction() {
    assert(page_size() >= SlotSpace() + data_->header.space_used);
    PageSize free_space = page_size() - SlotSpace() - data_->header.space_used;
    assert(free_space < page_size());
    return free_space;
}

void Node::Compactify() {
    auto& pager = btree_->bucket().pager();
    auto& tmp_page = pager.tmp_page();

    auto tmp_node = Node(btree_, tmp_page);
    std::memset(&tmp_node.data_->header, 0, sizeof(tmp_node.data_->header));

    tmp_node.data_->header.data_offset = pager.page_size();

    CopyRecordRange(&tmp_node);
    assert(data_->header.space_used == tmp_node.data_->header.space_used);

    data_->header.data_offset = tmp_node.data_->header.data_offset;
    std::memcpy(Ptr() + data_->header.data_offset, 
        tmp_node.Ptr() + tmp_node.data_->header.data_offset,
        tmp_node.data_->header.space_used);
}

uint8_t* Node::Ptr() {
    return reinterpret_cast<uint8_t*>(data_);
}

uint8_t* Node::GetRawRecordPtr(SlotId slot_id) {
    auto& slot = data_->slots[slot_id];
    assert(slot_id < count());
    return Ptr() + slot.record_offset;
}

uint8_t* Node::GetRecordPtr(SlotId slot_id) {
    auto raw_record = GetRawRecordPtr(slot_id);
    auto& slot = data_->slots[slot_id];
    if (!slot.is_overflow_pages) {
        return raw_record;
    }

    auto& pager = btree_->bucket().pager();
    auto overflow_record = reinterpret_cast<OverflowRecord*>(raw_record);
    return pager.GetPtr(overflow_record->pgid, 0);
}

PageId Node::StoreRecordToOverflowPages(SlotId slot_id, std::span<const uint8_t> key, std::span<const uint8_t> value) {
    auto& pager = btree_->bucket().pager();

    auto page_size_ = page_size();
    auto size = key.size() + value.size();

    auto pgid = pager.Alloc(pager.GetPageCount(size));
    pager.WriteByBytes(pgid, 0, key.data(), key.size());
    if (!value.empty()) {
        PageCount key_page_count = key.size() / page_size_;
        pager.WriteByBytes(pgid + key_page_count, key.size() % page_size_, value.data(), value.size());
    }

    return pgid;
}

void Node::StoreRecord(SlotId slot_id, std::span<const uint8_t> key, std::span<const uint8_t> value) {
    if (key.size() > page_size() || key.size() > kKeyMaxSize) {
        throw std::invalid_argument("Key size exceeds the limit.");
    }
    if (value.size() > kValueMaxSize) {
        throw std::invalid_argument("Value size exceeds the limit.");
    }

    auto size = key.size() + value.size();
    auto& slot = data_->slots[slot_id];
    slot.key_size = key.size();

    if (IsLeaf()) {
        slot.value_size = value.size();
    }
    if (size > MaxInlineRecordSize()) {
        // Need to create overflow pages to store
        auto pgid = StoreRecordToOverflowPages(slot_id, key, value);
        auto record = OverflowRecord{ .pgid = pgid };

        assert(data_->header.data_offset >= sizeof(record));
        data_->header.data_offset -= sizeof(record);
        data_->header.space_used += sizeof(record);

        slot.record_offset = data_->header.data_offset;
        slot.is_overflow_pages = true;

        auto overflow_reocrd_ptr = GetRawRecordPtr(slot_id);
        std::memcpy(overflow_reocrd_ptr, &record, sizeof(record));
    }
    else {
        assert(data_->header.data_offset >= size);
        data_->header.data_offset -= size;
        data_->header.space_used += size;

        slot.record_offset = data_->header.data_offset;
        slot.is_overflow_pages = false;

        std::memcpy(GetRawRecordPtr(slot_id), key.data(), key.size());
        if (!value.empty()) {
            std::memcpy(GetRawRecordPtr(slot_id) + key.size(), value.data(), value.size());
        }
    }
    assert(SlotSpace() + FreeSpace() == data_->header.data_offset);
}

void Node::CopyRecordRange(Node* dst) {
    for (size_t i = 0; i < data_->header.count; ++i) {
        size_t size;
        auto& slot = data_->slots[i];
        if (slot.is_overflow_pages) {
            size = sizeof(OverflowRecord);
        }
        else {
            size = slot.key_size;
            if (IsLeaf()) {
                size += slot.value_size;
            }
        }

        dst->data_->header.data_offset -= size;
        dst->data_->header.space_used += size;

        auto dst_ptr = dst->Ptr() + dst->data_->header.data_offset;
        std::memcpy(dst_ptr, GetRawRecordPtr(i), size);

        slot.record_offset = dst->data_->header.data_offset;
    }
}

void Node::DeleteRecord(SlotId slot_id) {
    assert(slot_id < count());
    auto& slot = data_->slots[slot_id];
    if (slot.is_overflow_pages) {
        data_->header.space_used -= sizeof(OverflowRecord);
    }
    else {
        data_->header.space_used -= slot.key_size;
        slot.key_size = 0;
        if (IsLeaf()) {
            data_->header.space_used -= slot.value_size;
            slot.value_size = 0;
        }
    }
}

void Node::RestoreRecord(SlotId slot_id, const Slot& saved_slot) {
    assert(slot_id < count());
    auto& slot = data_->slots[slot_id];
    if (saved_slot.is_overflow_pages) {
        data_->header.space_used += sizeof(OverflowRecord);
    }
    else {
        slot.key_size = saved_slot.key_size;
        data_->header.space_used += slot.key_size;
        if (IsLeaf()) {
            slot.value_size = saved_slot.value_size;
            data_->header.space_used += slot.value_size;
        }
    }
}


void BranchNode::Build(PageId tail_child) {
    auto& header = data_->header;

    header.type = NodeType::kBranch;
    header.count = 0;

    header.data_offset = page_size();
    header.space_used = 0;

    data_->tail_child = tail_child;
    header.last_modified_txid = btree_->bucket().tx().txid();
}

void BranchNode::Destroy() { }

PageId BranchNode::GetLeftChild(SlotId slot_id) {
    assert(slot_id <= count());
    if (slot_id == count()) {
        return GetTailChild();
    }
    return data_->slots[slot_id].left_child;
}

void BranchNode::SetLeftChild(SlotId slot_id, PageId child) {
    assert(slot_id <= count());
    if (slot_id == count()) {
        SetTailChild(child);
        return;
    }
    data_->slots[slot_id].left_child = child;
}

PageId BranchNode::GetRightChild(SlotId slot_id) {
    assert(slot_id < count());
    if (slot_id == count() - 1) {
        return GetTailChild();
    }
    return data_->slots[slot_id + 1].left_child;
}

void BranchNode::SetRightChild(uint16_t slot_id, PageId child) {
    assert(slot_id < count());
    if (slot_id == count() - 1) {
        SetTailChild(child);
        return;
    }
    data_->slots[slot_id + 1].left_child = child;
}

PageId BranchNode::GetTailChild() {
    return data_->tail_child;
}

void BranchNode::SetTailChild(PageId child) {
    data_->tail_child = child;
}

bool BranchNode::Update(SlotId slot_id, std::span<const uint8_t> key, PageId child, bool is_right_child) {
    assert(slot_id < count());
    if (!Update(slot_id, key)) {
        return false;
    }
    auto& slot = data_->slots[slot_id];
    if (is_right_child) {
        if (slot_id == count() - 1) {
            data_->tail_child = child;
        }
        else {
            data_->slots[slot_id + 1].left_child = child;
        }
    }
    else {
        slot.left_child = child;
    }
    return true;
}

bool BranchNode::Update(SlotId slot_id, std::span<const uint8_t> key) {
    assert(slot_id < count());
    auto saved_slot = data_->slots[slot_id];
    DeleteRecord(slot_id);
    if (!RequestSpaceFor(key, {}, false)) {
        // Insufficient space, restore the deleted record
        RestoreRecord(slot_id, saved_slot);
        return false;
    }
    auto& slot = data_->slots[slot_id];
    auto size = slot.key_size;
    data_->header.space_used -= size;
    StoreRecord(slot_id, key, {});
    assert(SlotSpace() + FreeSpace() == data_->header.data_offset);
    return true;
}

bool BranchNode::Append(std::span<const uint8_t> key, PageId child, bool is_right_child) {
    return Insert(count(), key, child, is_right_child);
}

bool BranchNode::Insert(SlotId slot_id, std::span<const uint8_t> key, PageId child, bool is_right_child) {
    assert(slot_id <= count());
    if (!RequestSpaceFor(key, {}, true)) {
        return false;
    }

    auto& slot = data_->slots[slot_id];
    std::memmove(data_->slots + slot_id + 1, data_->slots + slot_id,
        sizeof(Slot) * (count() - slot_id));

    if (is_right_child) {
        if (slot_id == count()) {
            slot.left_child = data_->tail_child;
            data_->tail_child = child;
        }
        else {
            slot.left_child = data_->slots[slot_id + 1].left_child;
            data_->slots[slot_id + 1].left_child = child;
        }
    }
    else {
        slot.left_child = child;
    }

    ++data_->header.count;
    StoreRecord(slot_id, key, {});
    return true;
}

void BranchNode::Delete(SlotId slot_id, bool right_child) {
    assert(slot_id < count());
    auto& slot = data_->slots[slot_id];
    if (slot.is_overflow_pages) {
        data_->header.space_used -= sizeof(OverflowRecord);
        auto overflow_record = reinterpret_cast<OverflowRecord*>(GetRawRecordPtr(slot_id));
        auto& pager = btree_->bucket().pager();
        slot.is_overflow_pages = false;
        pager.Free(overflow_record->pgid, pager.GetPageCount(slot.key_size));
    }
    else {
        data_->header.space_used -= slot.key_size;
    }

    if (right_child) {
        if (slot_id + 1 < count()) {
            data_->slots[slot_id + 1].left_child = slot.left_child;
            slot = data_->slots[slot_id + 1];
        }
        else {
            data_->tail_child = data_->slots[count() - 1].left_child;
        }

        if (count() - slot_id > 1) {
            std::memmove(data_->slots + slot_id + 1, data_->slots + slot_id + 2,
                sizeof(Slot) * (count() - slot_id - 2));
        }
    }
    else {
        std::memmove(data_->slots + slot_id, data_->slots + slot_id + 1,
            sizeof(Slot) * (count() - slot_id - 1));
    }

    --data_->header.count;
    assert(SlotSpace() + FreeSpace() == data_->header.data_offset);
}

void BranchNode::Pop(bool right_cbild) {
    Delete(count() - 1, right_cbild);
}


void LeafNode::Build() {
    auto& header = data_->header;

    header.type = NodeType::kLeaf;
    header.count = 0;

    header.data_offset = page_size();
    header.space_used = 0;

    data_->header.last_modified_txid = btree_->bucket().tx().txid();
}

void LeafNode::Destroy() {

}

Slot& LeafNode::GetSlot(SlotId slot_id) {
    return data_->slots[slot_id];
}

bool LeafNode::IsBucket(SlotId slot_id) const {
    assert(slot_id < count());
    return slots()[slot_id].is_bucket;
}

void LeafNode::SetIsBucket(SlotId slot_id, bool b) {
    assert(slot_id < count());
    slots()[slot_id].is_bucket = b;
}

std::span<const uint8_t> LeafNode::GetValue(SlotId slot_id) {
    assert(slot_id < count());
    auto& slot = data_->slots[slot_id];
    return { GetRecordPtr(slot_id) + slot.key_size, slot.value_size };
}

bool LeafNode::Update(SlotId slot_id, std::span<const uint8_t> key, std::span<const uint8_t> value) {
    assert(slot_id < count());
    auto saved_slot = data_->slots[slot_id];
    DeleteRecord(slot_id);
    if (!RequestSpaceFor(key, value, false)) {
        // Insufficient space, restore the deleted record
        RestoreRecord(slot_id, saved_slot);
        return false;
    }
    auto& slot = data_->slots[slot_id];
    StoreRecord(slot_id, key, value);
    assert(SlotSpace() + FreeSpace() == data_->header.data_offset);
    return true;
}

bool LeafNode::Append(std::span<const uint8_t> key, std::span<const uint8_t> value) {
    return Insert(count(), key, value);
}

bool LeafNode::Insert(SlotId slot_id, std::span<const uint8_t> key, std::span<const uint8_t> value) {
    assert(slot_id <= count());
    if (!RequestSpaceFor(key, value, true)) {
        return false;
    }
    std::memmove(data_->slots + slot_id + 1, data_->slots + slot_id,
        sizeof(Slot) * (count() - slot_id));
    ++data_->header.count;
    StoreRecord(slot_id, key, value);
    return true;
}

void LeafNode::Delete(SlotId slot_id) {
    assert(slot_id < count());
    auto& slot = data_->slots[slot_id];
    auto size = slot.key_size + slot.value_size;
    if (slot.is_overflow_pages) {
        data_->header.space_used -= sizeof(OverflowRecord);
        auto overflow_record = reinterpret_cast<OverflowRecord*>(GetRawRecordPtr(slot_id));
        auto& pager = btree_->bucket().pager();
        slot.is_overflow_pages = false;
        pager.Free(overflow_record->pgid, pager.GetPageCount(size));
    }
    else {
        data_->header.space_used -= size;
    }

    std::memmove(data_->slots + slot_id, data_->slots + slot_id + 1,
        sizeof(Slot) * (count() - slot_id - 1));
    --data_->header.count;
    assert(SlotSpace() + FreeSpace() == data_->header.data_offset);
}

void LeafNode::Pop() {
    Delete(count() - 1);
}

} // namespace atomkv