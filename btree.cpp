#include "btree.h"

#include "tx.h"
#include "bucket.h"
#include "pager.h"

namespace yudb {

BTree::BTree(Bucket* bucket, PageId* root_pgid, Comparator comparator) :
    bucket_{ bucket },
    root_pgid_ { root_pgid },
    comparator_{ comparator } {}


BTree::Iterator BTree::LowerBound(std::span<const uint8_t> key) const {
    Iterator iter{ this };
    auto status = iter.Top(key);
    while (status == Iterator::Status::kDown) {
        status = iter.Down(key);
    }
    return iter;
}

BTree::Iterator BTree::Get(std::span<const uint8_t> key) const {
    auto iter = LowerBound(key);
    if (iter.comp_result() != Iterator::CompResult::kEq) {
        return Iterator{ this };
    }
    return iter;
}

void BTree::Put(std::span<const uint8_t> key, std::span<const uint8_t> value) {
    auto iter = LowerBound(key);
    iter.PathCopy();
    Put(&iter, key, value);
}

bool BTree::Delete(std::span<const uint8_t> key) {
    auto iter = LowerBound(key);
    if (iter.comp_result() != Iterator::CompResult::kEq) {
        return false;
    }
    iter.PathCopy();
    Delete(&iter, key);
    return true;
}


BTree::Iterator BTree::begin() const noexcept {
    Iterator iter { this };
    iter.First(*root_pgid_);
    return iter;
}

BTree::Iterator BTree::end() const noexcept {
    return Iterator{ this };
}


void BTree::Print(bool str, PageId pgid, int level) const {
    std::string indent(level * 4, ' ');
    Noder noder{ this, pgid };
    auto& node = noder.node();
    if (noder.IsBranch()) {
        Print(str, node.body.tail_child, level + 1);
        for (int i = node.element_count - 1; i >= 0; i--) {
            std::string key;
            auto [buf, size, ref] = noder.SpanLoad(node.body.branch[i].key);
            if (str) {
                std::cout << std::format("{}branch[{}]::key::{}::level::{}\n", indent, pgid, std::string_view{ reinterpret_cast<const char*>(buf), size }, level);
            }
            else {
                for (int j = 0; j < size; j++) {
                    key += std::format("{:02x}", buf[j]) + " ";
                }
                std::cout << std::format("{}branch[{}]::key::{}::level::{}\n", indent, pgid, key, level);
            }
            Print(str, node.body.branch[i].left_child, level + 1);
        }
        //noder.BlockPrint(); printf("\n");
    }
    else {
        assert(noder.IsLeaf());
        for (int i = node.element_count - 1; i >= 0; i--) {
            std::string key;
            auto [buf, size, ref] = noder.SpanLoad(node.body.leaf[i].key);
            if (str) {
                std::cout << std::format("{}leaf[{}]::key::{}::level::{}\n", indent, pgid, std::string_view{ reinterpret_cast<const char*>(buf), size }, level);
            }
            else {
                for (int j = 0; j < size; j++) {
                    key += std::format("{:02x}", buf[j]) + " ";
                }
                std::cout << std::format("{}leaf[{}]::key::{}::level::{}\n", indent, pgid, key, level);
            }
        }
        //noder.BlockPrint(); printf("\n");
    }
}

void BTree::Print(bool str) const {
    Print(str, *root_pgid_, 0);
}



std::tuple<Noder, uint16_t, Noder, bool> BTree::GetSibling(Iterator* iter) {
    auto [parent_pgid, parent_pos] = iter->Front();
    Noder parent{ this, parent_pgid };
    auto& parent_node = parent.node();
        
    bool left_sibling = false;
    PageId sibling_pgid;
    if (parent_pos == parent_node.element_count) {
        // �Ǹ��ڵ�������Ԫ�أ�ֻ��ѡ�����ֵܽڵ�
        left_sibling = true;
        sibling_pgid = parent.BranchGetLeftChild(parent_pos - 1);
    }
    else {
        sibling_pgid = parent.BranchGetRightChild(parent_pos);
    }

    Noder sibling{ this, sibling_pgid };

    iter->Pop();
    return { std::move(parent), parent_pos, std::move(sibling), left_sibling };
}


/*
* ��֧�ڵ�ĺϲ�
*/
void BTree::Merge(Noder&& left, Noder&& right, Span&& down_key) {
    auto& left_node = left.node();
    auto& right_node = right.node();
    left_node.body.branch[left_node.element_count].key = std::move(down_key);
    left_node.body.branch[left_node.element_count].left_child = left_node.body.tail_child;
    ++left_node.element_count;
    for (uint16_t i = 0; i < right_node.element_count; i++) {
        auto key = right.SpanMove(&left, std::move(right_node.body.branch[i].key));
        left.BranchSet(i + left_node.element_count, std::move(key), right_node.body.branch[i].left_child);
    }
    left_node.body.tail_child = right_node.body.tail_child;
    left_node.element_count += right_node.element_count;

    right.SpanClear();
    bucket_->pager().Free(right.page_id(), 1);
}

/*
* ��֧�ڵ��ɾ��
*/
void BTree::Delete(Iterator* iter, Noder&& noder, uint16_t left_del_pos) {
    auto& node = noder.node();

    noder.BranchDelete(left_del_pos, true);
    if (node.element_count >= (bucket_->max_branch_ele_count() >> 1)) {
        return;
    }

    if (iter->Empty()) {
        // ���û�и��ڵ�
        // �ж��Ƿ�û���κ��ӽڵ��ˣ��������������һ���ӽڵ�Ϊ���ڵ�
        if (node.element_count == 0) {
            auto old_root = *root_pgid_;
            *root_pgid_ = node.body.tail_child;
            bucket_->pager().Free(old_root, 1);
        }
        return;
    }

    assert(node.element_count > 0);

    auto [parent, parent_pos, sibling, left_sibling] = GetSibling(iter);
    if (left_sibling) --parent_pos;
    auto& parent_node = parent.node();

    if (bucket_->tx().NeedCopy(sibling.node().last_modified_txid)) {
        sibling = sibling.Copy();
        if (left_sibling) {
            parent.BranchSetLeftChild(parent_pos, sibling.page_id());
        }
        else {
            parent.BranchSetLeftChild(parent_pos + 1, sibling.page_id());
        }
    }
    auto& sibling_node = sibling.node();
    if (sibling_node.element_count > (bucket_->max_branch_ele_count() >> 1)) {
        // ���ֵܽڵ���Ԫ��������
        if (left_sibling) {
            // ���ֵܽڵ��ĩβԪ�����������ڵ�ָ��λ��
            // ���ڵ�Ķ�ӦԪ���½�����ǰ�ڵ��ͷ��
            // ����Ԫ�������ӽڵ�����½��ĸ��ڵ�Ԫ�ص����

            //                                27
            //      12          17                               37
            // 1 5      10 15       20 25                30 35         40 45

            //                       17
            //      12                                    27            37
            // 1 5      10 15                    20 25         30 35         40 45
            auto parent_key = parent.SpanMove(&noder, std::move(parent_node.body.branch[parent_pos].key));
            auto sibling_key = sibling.SpanMove(&parent, std::move(sibling_node.body.branch[sibling_node.element_count - 1].key));

            noder.BranchInsert(0, std::move(parent_key), sibling_node.body.tail_child, false);
            sibling.BranchDelete(sibling_node.element_count - 1, true);

            parent_node.body.branch[parent_pos].key = std::move(sibling_key);
        }
        else {
            parent.BranchSetLeftChild(parent_pos + 1, sibling.page_id());
            // ���ֵܽڵ��ͷԪ�����������ڵ��ָ��λ��
            // ���ڵ�Ķ�ӦԪ���½�����ǰ�ڵ��β��
            // ����Ԫ�������ӽڵ�����½��ĸ��ڵ�Ԫ�ص��Ҳ�

            //                       17
            //      12                                    27            37
            // 1 5      10 15                    20 25         30 35         40 45

            //                                27
            //      12          17                               37
            // 1 5      10 15       20 25                30 35         40 45

            auto parent_key = parent.SpanMove(&noder, std::move(parent_node.body.branch[parent_pos].key));
            auto sibling_key = sibling.SpanMove(&parent, std::move(sibling_node.body.branch[0].key));

            noder.BranchInsert(node.element_count, std::move(parent_key), sibling_node.body.branch[0].left_child, true);
            sibling.BranchDelete(0, false);

            parent_node.body.branch[parent_pos].key = std::move(sibling_key);

        }
        return;
    }

    // �ϲ�
    if (left_sibling) {
        auto down_key = parent.SpanMove(&sibling, std::move(parent_node.body.branch[parent_pos].key));
        Merge(std::move(sibling), std::move(noder), std::move(down_key));
    }
    else {
        auto down_key = parent.SpanMove(&noder, std::move(parent_node.body.branch[parent_pos].key));
        Merge(std::move(noder), std::move(sibling), std::move(down_key));
    }

    // ����ɾ����Ԫ��
    Delete(iter, std::move(parent), parent_pos);
}

/*
* Ҷ�ӽڵ�ĺϲ�
*/
void BTree::Merge(Noder&& left, Noder&& right) {
    auto& left_node = left.node();
    auto& right_node = right.node();
    for (uint16_t i = 0; i < right_node.element_count; i++) {
        auto key = right.SpanMove(&left, std::move(right_node.body.leaf[i].key));
        auto value = right.SpanMove(&left, std::move(right_node.body.leaf[i].value));

        left.LeafSet(i + left_node.element_count, std::move(key), std::move(value));
    }
    left_node.element_count += right_node.element_count;

    right.SpanClear();
    bucket_->pager().Free(right.page_id(), 1);
}

/*
* Ҷ�ӽڵ��ɾ��
*/
void BTree::Delete(Iterator* iter, std::span<const uint8_t> key) {
    auto [pgid, pos] = iter->Front();
    Noder noder{ this, pgid };
    auto& node = noder.node();

    noder.LeafDelete(pos);
    if (node.element_count >= (bucket_->max_leaf_ele_count() >> 1)) {
        return;
    }

    iter->Pop();
    if (iter->Empty()) {
        // ���û�и��ڵ�
        // ��Ҷ�ӽڵ������
        return;
    }

    assert(node.element_count > 1);

    auto [parent, parent_pos, sibling, left_sibling] = GetSibling(iter);
    if (left_sibling) --parent_pos;
    auto& parent_node = parent.node();

    if (bucket_->tx().NeedCopy(sibling.node().last_modified_txid)) {
        sibling = sibling.Copy();
        if (left_sibling) {
            parent.BranchSetLeftChild(parent_pos, sibling.page_id());
        }
        else {
            parent.BranchSetLeftChild(parent_pos + 1, sibling.page_id());
        }
    }
    auto& sibling_node = sibling.node();
    if (sibling_node.element_count > (bucket_->max_leaf_ele_count() >> 1)) {
        // ���ֵܽڵ���Ԫ��������
        Span new_key;
        if (left_sibling) {
            // ���ֵܽڵ��ĩβ��Ԫ�ز��뵽��ǰ�ڵ��ͷ��
            // ���¸�Ԫ��keyΪ��ǰ�ڵ������Ԫ��key
            auto key = sibling.SpanMove(&noder, std::move(sibling_node.body.leaf[sibling_node.element_count - 1].key));
            auto value = sibling.SpanMove(&noder, std::move(sibling_node.body.leaf[sibling_node.element_count - 1].value));
            sibling.LeafDelete(sibling_node.element_count - 1);

            new_key = noder.SpanCopy(&parent, key);
            noder.LeafInsert(0, std::move(key), std::move(value));
        }
        else {
            // ���ֵܽڵ��ͷ����Ԫ�ز��뵽��ǰ�ڵ��β��
            // ���¸�Ԫ��keyΪ���ֵܵ�����Ԫ��
            auto key = sibling.SpanMove(&noder, std::move(sibling_node.body.leaf[0].key));
            auto value = sibling.SpanMove(&noder, std::move(sibling_node.body.leaf[0].value));
            sibling.LeafDelete(0);

            new_key = sibling.SpanCopy(&parent, sibling_node.body.leaf[0].key);
            noder.LeafInsert(node.element_count, std::move(key), std::move(value));
        }
        parent.SpanFree(std::move(parent_node.body.branch[parent_pos].key));
        parent_node.body.branch[parent_pos].key = std::move(new_key);
        return;
    }

    // �ϲ�
    if (left_sibling) {
        Merge(std::move(sibling), std::move(noder));
    }
    else {
        Merge(std::move(noder), std::move(sibling));
    }

    // ����ɾ����Ԫ��
    Delete(iter, std::move(parent), parent_pos);
}


/*
* ��֧�ڵ�ķ���
* �������ڵ���ĩβ������Ԫ�أ����ҽڵ�
*/
std::tuple<Span, Noder> BTree::Split(Noder* left, uint16_t insert_pos, Span&& insert_key, PageId insert_right_child) {
    Noder right{ this, bucket_->pager().Alloc(1) };
    right.BranchBuild();

    auto& left_node = left->node();
    auto& right_node = right.node();

    uint16_t mid = left_node.element_count / 2;
    uint16_t right_count = mid + (left_node.element_count % 2);

    int insert_right = 0;
    for (uint16_t i = 0; i < right_count; i++) {
        auto left_pos = mid + i;
        if (insert_right == 0 && left_pos == insert_pos) {
            // ����ڵ���ӽڵ�ʹ�����Ԫ�ص��ӽڵ㣬�������ӽڵ㽻������
            auto left_child = left_node.body.branch[left_pos].left_child;
            left_node.body.branch[left_pos].left_child = insert_right_child;
            right.BranchSet(i,
                left->SpanMove(&right, std::move(insert_key)),
                left_child
            );
            insert_right = 1;
            --i;
            continue;
        }
        auto key = left->SpanMove(&right, std::move(left_node.body.branch[left_pos].key));
        right.BranchSet(i + insert_right,
            std::move(key),
            left_node.body.branch[left_pos].left_child
        );
    }

    left_node.element_count -= right_count;
    assert(left_node.element_count > 2);
    right_node.element_count = right_count + insert_right;

    right_node.body.tail_child = left_node.body.tail_child;
    if (insert_right == 0) {
        if (insert_pos == bucket_->max_branch_ele_count()) {
            right.BranchSet(right_node.element_count++, left->SpanMove(&right, std::move(insert_key)), right_node.body.tail_child);
            right_node.body.tail_child = insert_right_child;
        }
        else {
            left->BranchInsert(insert_pos, std::move(insert_key), insert_right_child, true);
        }
    }

    assert(right_node.element_count > 2);

    // ���ĩβԪ����������left_child��Ϊtail_child
    Span span{ std::move(left_node.body.branch[--left_node.element_count].key) };
    left_node.body.tail_child = left_node.body.branch[left_node.element_count].left_child;

    return { std::move(span), std::move(right) };
}

/*
* ��֧�ڵ�Ĳ���
*/
void BTree::Put(Iterator* iter, Noder&& left, Noder&& right, Span* key, bool branch_put) {
    if (iter->Empty()) {
        *root_pgid_ = bucket_->pager().Alloc(1);
        Noder noder{ this, *root_pgid_ };

        noder.BranchBuild();
        noder.node().body.tail_child = left.page_id();

        Span noder_key_span;
        if (branch_put) {
            noder_key_span = left.SpanMove(&noder, std::move(*key));
        }
        else {
            noder_key_span = right.SpanCopy(&noder, *key);
        }
        noder.BranchInsert(0, std::move(noder_key_span), right.page_id(), true);
        return;
    }

    auto [pgid, pos] = iter->Front();
    Noder noder{ this, pgid };

    Span noder_key_span;
    if (branch_put) {
        noder_key_span = left.SpanMove(&noder, std::move(*key));
    }
    else {
        noder_key_span = right.SpanCopy(&noder, *key);
    }

    //      3       7
    // 1 2     3 4     7 8

    //      3       5    7 
    // 1 2     3 4     5    7 8
    if (noder.node().element_count < bucket_->max_branch_ele_count()) {
        noder.BranchInsert(pos, std::move(noder_key_span), right.page_id(), true);
        return;
    }

    auto [branch_key, branch_right] = Split(&noder, pos, std::move(noder_key_span), right.page_id());
    iter->Pop();
    Put(iter, std::move(noder), std::move(branch_right), &branch_key, true);
}

/*
* Ҷ�ӽڵ�ķ���
* �������ҽڵ�
*/
Noder BTree::Split(Noder* left, uint16_t insert_pos, std::span<const uint8_t> key, std::span<const uint8_t> value) {
    Noder right{ this, bucket_->pager().Alloc(1) };
    right.LeafBuild();

    auto& left_node = left->node();
    auto& right_node = right.node();

    uint16_t mid = left_node.element_count / 2;
    uint16_t right_count = mid + (left_node.element_count % 2);

    int insert_right = 0;
    for (uint16_t i = 0; i < right_count; i++) {
        auto left_pos = mid + i;
        if (insert_right == 0 && left_pos == insert_pos) {
            right.LeafSet(i, right.SpanAlloc(key), right.SpanAlloc(value));
            insert_right = 1;
            --i;
            continue;
        }
        auto key = left->SpanMove(&right, std::move(left_node.body.leaf[left_pos].key));
        auto value = left->SpanMove(&right, std::move(left_node.body.leaf[left_pos].value));

        right.LeafSet(i + insert_right, std::move(key), std::move(value));
    }

    left_node.element_count -= right_count;
    assert(left_node.element_count > 2);
    if (insert_right == 0) {
        if (insert_pos == bucket_->max_leaf_ele_count()) {
            insert_right = 1;
            right.LeafSet(right_count, right.SpanAlloc(key), right.SpanAlloc(value));
        }
        else {
            left->LeafInsert(insert_pos, left->SpanAlloc(key), left->SpanAlloc(value));
        }
    }
    right_node.element_count = right_count + insert_right;
    assert(right_node.element_count > 2);

    return right;
}

/*
* Ҷ�ӽڵ�Ĳ���
*/
void BTree::Put(Iterator* iter, std::span<const uint8_t> key, std::span<const uint8_t> value) {
    if (iter->Empty()) {
        *root_pgid_ = bucket_->pager().Alloc(1);
        Noder noder{ this, *root_pgid_ };
        noder.LeafBuild();
        noder.LeafInsert(0, noder.SpanAlloc(key), noder.SpanAlloc(value));
        return;
    }

    auto [pgid, pos] = iter->Front();
    Noder noder{ this, pgid };
    auto& node = noder.node();
    if (iter->comp_result() == Iterator::CompResult::kEq) {
        noder.SpanFree(std::move(node.body.leaf->value));
        node.body.leaf->value = noder.SpanAlloc(value);
        return;
    }

    if (noder.node().element_count < bucket_->max_leaf_ele_count()) {
        noder.LeafInsert(pos, noder.SpanAlloc(key), noder.SpanAlloc(key));
        return;
    }

    // ��Ҫ���������ϲ���
    Noder right = Split(&noder, pos, key, value);

    iter->Pop();
    // �����ҽڵ�ĵ�һ���ڵ�
    Put(iter, std::move(noder),
        std::move(right),
        &right.node().body.leaf[0].key
    );
}

} // namespace yudb