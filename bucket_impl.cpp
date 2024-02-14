#include "bucket_impl.h"

#include "tx_manager.h"
#include "pager.h"
#include "db_impl.h"

namespace yudb {

std::strong_ordering DefalutComparator(std::span<const uint8_t> key1, std::span<const uint8_t> key2) {
    auto res = std::memcmp(key1.data(), key2.data(), std::min(key1.size(), key2.size()));
    if (res == 0) {
        return std::strong_ordering::equal;
    } else if (res < 0) {
        return std::strong_ordering::less;
    } else {
        return std::strong_ordering::greater;
    }
}

BucketImpl::BucketImpl(TxImpl* tx, BucketId bucket_id, PageId* root_pgid, bool writable) :
    tx_{ tx },
    bucket_id_{ bucket_id },
    writable_{ writable },
    inlineable_{ false },
    max_leaf_ele_count_{ (pager().page_size() - (sizeof(NodeFormat) - sizeof(NodeFormat::body))) / sizeof(NodeFormat::LeafElement) },
    max_branch_ele_count_{ ((pager().page_size() - (sizeof(NodeFormat) - sizeof(NodeFormat::body))) - sizeof(PageId)) / sizeof(NodeFormat::BranchElement) }
{
    btree_.emplace(this, root_pgid, DefalutComparator);
}

BucketImpl::BucketImpl(TxImpl* tx, BucketId bucket_id, std::span<const uint8_t> inline_bucket_data, bool writable) :
    tx_{ tx },
    bucket_id_{ bucket_id },
    btree_{ std::nullopt },
    writable_{ writable },
    inlineable_{ true },
    max_leaf_ele_count_{ (pager().page_size() - (sizeof(NodeFormat) - sizeof(NodeFormat::body))) / sizeof(NodeFormat::LeafElement) },
    max_branch_ele_count_{ ((pager().page_size() - (sizeof(NodeFormat) - sizeof(NodeFormat::body))) - sizeof(PageId)) / sizeof(NodeFormat::BranchElement) }
{
    inline_bucket_.Deserialize(inline_bucket_data);
}


BucketImpl::Iterator BucketImpl::Get(const void* key_buf, size_t key_size) {
    if (inlineable_) {
        return Iterator{ inline_bucket_.Get(key_buf, key_size) };
    } else {
        return Iterator{ btree_->Get({ reinterpret_cast<const uint8_t*>(key_buf), key_size }) };
    }
}

BucketImpl::Iterator BucketImpl::Get(std::string_view key) {
    return Get(key.data(), key.size());
}

BucketImpl::Iterator BucketImpl::LowerBound(const void* key_buf, size_t key_size) {
    return Iterator{ btree_->LowerBound({ reinterpret_cast<const uint8_t*>(key_buf), key_size }) };
}

BucketImpl::Iterator BucketImpl::LowerBound(std::string_view key) {
    return LowerBound(key.data(), key.size());
}

void BucketImpl::Insert(const void* key_buf, size_t key_size, const void* value_buf, size_t value_size) {
    std::span<const uint8_t> key_span{ reinterpret_cast<const uint8_t*>(key_buf), key_size };
    std::span<const uint8_t> value_span{ reinterpret_cast<const uint8_t*>(value_buf), value_size };
    tx_->AppendInsertLog(bucket_id_, key_span, value_span);

    btree_->Insert(key_span, value_span);
}

void BucketImpl::Insert(std::string_view key, std::string_view value) {
    Insert(key.data(), key.size(), value.data(), value.size());
}

void BucketImpl::Put(const void* key_buf, size_t key_size, const void* value_buf, size_t value_size) {
    std::span<const uint8_t> key_span{ reinterpret_cast<const uint8_t*>(key_buf), key_size };
    std::span<const uint8_t> value_span{ reinterpret_cast<const uint8_t*>(value_buf), value_size };
    tx_->AppendPutLog(bucket_id_, key_span, value_span);

    btree_->Put(key_span, value_span);
}

void BucketImpl::Put(std::string_view key, std::string_view value) {
    Put(key.data(), key.size(), value.data(), value.size());
}

void BucketImpl::Update(Iterator* iter, const void* value_buf, size_t value_size) {
    auto key = iter->key();
    std::span<const uint8_t> key_span{ reinterpret_cast<const uint8_t*>(key.data()), key.size()};
    tx_->AppendPutLog(bucket_id_, key_span, { reinterpret_cast<const uint8_t*>(value_buf), value_size });

    btree_->Update(&std::get<BTreeIterator>(iter->iterator_), { reinterpret_cast<const uint8_t*>(value_buf), value_size });
}

void BucketImpl::Update(Iterator* iter, std::string_view value) {
    Update(iter, value.data(), value.size());
}

bool BucketImpl::Delete(const void* key_buf, size_t key_size) {
    std::span<const uint8_t> key_span{ reinterpret_cast<const uint8_t*>(key_buf), key_size };
    tx_->AppendDeleteLog(bucket_id_, key_span);

    return btree_->Delete(key_span);
}

bool BucketImpl::Delete(std::string_view key) {
    return Delete(key.data(), key.size());
}

void BucketImpl::Delete(Iterator* iter) {
    btree_->Delete(&std::get<BTreeIterator>(iter->iterator_));
}

BucketImpl& BucketImpl::SubBucket(std::string_view key, bool writable) {
    auto map_iter = sub_bucket_map_.find({ key.data(), key.size() });
    uint32_t index;
    if (map_iter == sub_bucket_map_.end()) {
        auto res = sub_bucket_map_.insert({ { key.data(), key.size() }, { 0, kPageInvalidId } });
        map_iter = res.first;
        auto iter = Get(key);
        if (iter == end()) {
            // 提前预留空间，以避免Commit时的Put触发分裂
            PageId pgid = kPageInvalidId;
            Put(key.data(), key.size(), &pgid, sizeof(pgid));
            iter = Get(key);
            iter.set_is_bucket();
            assert(iter.is_bucket());
        }
        else {
            auto data = iter->value();
            auto pgid = *reinterpret_cast<PageId*>(data.data());
            map_iter->second.second = pgid;
            if (!iter.is_bucket()) {
                throw std::runtime_error("This is not a bucket.");
            }
        }
        if (!iter.is_inline_bucket()) {
            index = tx_->NewSubBucket(&map_iter->second.second, writable);
        }
        //else {
        //    map_iter->second.second = kPageInvalidId;
        //    index = tx_->NewSubBucket({ bucket_info->data, data.size() - 1 }, writable);
        //}
        map_iter->second.first = index;
    }
    else {
        index = map_iter->second.first;
    }

    return tx_->AtSubBucket(index);
}


BucketImpl::Iterator BucketImpl::begin() noexcept {
    return Iterator{ btree_->begin() };
}
BucketImpl::Iterator BucketImpl::end() noexcept {
    return Iterator{ btree_->end() };
}

void BucketImpl::Print(bool str) { btree_->Print(str); }



Pager& BucketImpl::pager() const { return tx_->pager(); }


} // namespace yudb