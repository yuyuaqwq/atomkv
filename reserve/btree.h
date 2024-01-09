#pragma once

#include <span>
#include <algorithm>
#include <iostream>
#include <format>
#include <string>

#include "btree_iterator.h"
#include "page.h"
#include "noder.h"

#undef min

namespace yudb {

class Bucket;

class BTree {
public:
    using Iterator = BTreeIterator;

public:
    BTree(Bucket* bucket, PageId* root_pgid);

    ~BTree() = default;

    Iterator LowerBound(std::span<const uint8_t> key) const;

    BTreeIterator Get(std::span<const uint8_t> key) const;

    void Put(std::span<const uint8_t> key, std::span<const uint8_t> value);

    bool Delete(std::span<const uint8_t> key);

    void Print(bool str = false) const;


    Iterator begin() const noexcept;

    Iterator end() const noexcept;


    Bucket& bucket() const { return *bucket_; }

private:
    std::tuple<Noder, uint16_t, Noder, bool> GetSibling(Iterator* iter);

    void Print(bool str, PageId pgid, int level) const;

    /*
    * ��֧�ڵ�ĺϲ�
    */
    void Merge(Noder&& left, Noder&& right, Span&& down_key);

    /*
    * ��֧�ڵ��ɾ��
    */
    void Delete(Iterator* iter, Noder&& noder, uint16_t left_del_pos);

    /*
    * Ҷ�ӽڵ�ĺϲ�
    */
    void Merge(Noder&& left, Noder&& right);

    /*
    * Ҷ�ӽڵ��ɾ��
    */
    void Delete(Iterator* iter, std::span<const uint8_t> key);


    /*
    * ��֧�ڵ�ķ���
    * �������ڵ���ĩβ������Ԫ�أ����ҽڵ�
    */
    std::tuple<Span, Noder> Split(Noder* left, uint16_t insert_pos, Span&& insert_key, PageId insert_right_child_pgid);

    /*
    * ��֧�ڵ�Ĳ���
    */
    void Put(Iterator* iter, Noder&& left, Noder&& right, Span* key, bool from_branch = false);
    
    /*
    * Ҷ�ӽڵ�ķ���
    * �������ҽڵ�
    */
    Noder Split(Noder* left, uint16_t insert_pos, std::span<const uint8_t> key, std::span<const uint8_t> value);

    /*
    * Ҷ�ӽڵ�Ĳ���
    */
    void Put(Iterator* iter, std::span<const uint8_t> key, std::span<const uint8_t> value);


    void PathCopy(Iterator* iter);

private:
    friend class BTreeIterator;

    Bucket* bucket_;
    PageId* root_pgid_; 
};

} // namespace yudb