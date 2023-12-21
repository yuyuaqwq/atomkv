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

class ViewBucket;
class UpdateBucket;

class BTree {
public:
    BTree(ViewBucket* bucket, PageId& root_pgid);

    BTreeIterator Get(std::span<const uint8_t> key) const;

    void Put(std::span<const uint8_t> key, std::span<const uint8_t> value);

    bool Delete(std::span<const uint8_t> key);


    BTreeIterator begin() const noexcept;

    BTreeIterator end() const noexcept;


    void Print() const;

private:
    std::tuple<Noder, uint16_t, Noder, bool> GetSibling(BTreeIterator* iter);


    void Print(PageId pgid, int level) const;


    /*
    * ��֧�ڵ�ĺϲ�
    */
    void Merge(Noder&& left, Noder&& right, Span&& down_key);

    /*
    * ��֧�ڵ��ɾ��
    */
    void Delete(BTreeIterator* iter, Noder&& noder, uint16_t left_del_pos);


    /*
    * Ҷ�ӽڵ�ĺϲ�
    */
    void Merge(Noder&& left, Noder&& right);


    /*
    * Ҷ�ӽڵ��ɾ��
    */
    void Delete(BTreeIterator* iter, std::span<const uint8_t> key);



    /*
    * Ҷ�ӽڵ�ķ���
    * �������ҽڵ�
    */
    Noder Split(Noder* left, uint16_t insert_pos, Span&& insert_key, Span&& insert_value);

    /*
    * Ҷ�ӽڵ�Ĳ���
    */
    void Put(BTreeIterator* iter, std::span<const uint8_t> key, std::span<const uint8_t> value);


    /*
    * ��֧�ڵ�ķ���
    * �������ڵ���ĩβ������Ԫ�أ����ҽڵ�
    */
    std::tuple<Span, Noder> Split(Noder* left, uint16_t insert_pos, Span&& insert_key, PageId insert_right_child);

    /*
    * ��֧�ڵ�Ĳ���
    */
    void Put(BTreeIterator* iter, Noder&& left, Noder&& right, Span* key, bool branch_put = false);


    Pager* pager() const;

private:
    friend class Noder;
    friend class BTreeIterator;
    friend class ViewBucket;
    friend class UpdateBucket;

    ViewBucket* bucket_;

    PageId& root_pgid_; 

    uint16_t max_leaf_element_count_;
    uint16_t max_branch_element_count_;
};

} // namespace yudb