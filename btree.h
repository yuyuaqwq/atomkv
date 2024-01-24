#pragma once

#include <span>
#include <algorithm>
#include <iostream>
#include <format>
#include <string>

#include "noncopyable.h"
#include "btree_iterator.h"
#include "page.h"
#include "noder.h"

#undef min

namespace yudb {

class Bucket;

class BTree : noncopyable {
public:
    using Iterator = BTreeIterator;
    using Comparator = std::strong_ordering(*)(std::span<const uint8_t> key1, std::span<const uint8_t> key2);

public:
    BTree(Bucket* bucket, PageId* root_pgid, Comparator comparator);

    ~BTree() = default;

    BTree(BTree&& right) noexcept {
        operator=(std::move(right));
    }

    void operator=(BTree&& right) noexcept {
        bucket_ = right.bucket_;
        root_pgid_ = right.root_pgid_;
        comparator_ = std::move(right.comparator_);
        right.root_pgid_ = nullptr;
    }


    Iterator LowerBound(std::span<const uint8_t> key) const;

    Iterator Get(std::span<const uint8_t> key) const;

    void Insert(std::span<const uint8_t> key, std::span<const uint8_t> value);

    void Put(std::span<const uint8_t> key, std::span<const uint8_t> value);

    void Update(Iterator* iter, std::span<const uint8_t> value);

    bool Delete(std::span<const uint8_t> key);

    void Delete(Iterator* iter);


    void Print(bool str = false) const;


    Iterator begin() const noexcept;

    Iterator end() const noexcept;


    void set_bucket(Bucket* bucket) { bucket_ = bucket; }

    Bucket& bucket() const { return *bucket_; }

private:
    std::tuple<MutNoder, uint16_t, MutNoder, bool> GetSibling(Iterator* iter);

    void Print(bool str, PageId pgid, int level) const;

    /*
    * ��֧�ڵ�ĺϲ�
    */
    void Merge(Noder&& left, Noder&& right, Cell&& down_key);

    /*
    * ��֧�ڵ��ɾ��
    */
    void Delete(Iterator* iter, Noder&& noder, uint16_t left_del_pos);

    /*
    * Ҷ�ӽڵ�ĺϲ�
    */
    void Merge(Noder&& left, Noder&& right);


    /*
    * ��֧�ڵ�ķ���
    * �������ڵ���ĩβ������Ԫ�أ����ҽڵ�
    */
    std::tuple<Cell, Noder> Split(Noder* left, uint16_t insert_pos, Cell&& insert_key, PageId insert_right_child);

    /*
    * ��֧�ڵ�Ĳ���
    */
    void Put(Iterator* iter, Noder&& left, Noder&& right, Cell* key, bool branch_put = false);
    
    /*
    * Ҷ�ӽڵ�ķ���
    * �������ҽڵ�
    */
    Noder Split(Noder* left, uint16_t insert_pos, std::span<const uint8_t> key, std::span<const uint8_t> value);

    /*
    * Ҷ�ӽڵ�Ĳ���
    */
    void Put(Iterator* iter, std::span<const uint8_t> key, std::span<const uint8_t> value, bool insert_only);

private:
    friend class BTreeIterator;

    Bucket* bucket_;
    PageId* root_pgid_; 

    Comparator comparator_;
};

} // namespace yudb