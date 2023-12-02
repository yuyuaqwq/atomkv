#pragma once

#include <span>
#include <algorithm>

#include "stack.h"
#include "page.h"
#include "noder.h"
#include "pager.h"

#undef min

namespace yudb {

/*
* b+tree
* |        |
*/


class BTree {
public:
    class Iterator {
    public:
        typedef enum class Status {
            kDown,
            kNext,
            kEnd,
        };

        typedef enum class CompResults {
            kEq,
            kNe,
        };

    public:
        Iterator(BTree* btree) : btree_{ btree } {}

        Status Top(std::span<const uint8_t> key) {
            return Down(key);
        }

        Status Down(std::span<const uint8_t> key) {
            PageId pgid;
            if (stack_.empty()) {
                pgid = btree_->root_pgid_;
                if (pgid == kPageInvalidId) {
                    return Status::kEnd;
                }
            }
            else {
                auto [parent_pgid, pos] = stack_.front();
                auto [ref, parent_page] = btree_->pager_->Reference(parent_pgid);

                Noder parent{ btree_, parent_page };
                if (parent.IsLeaf()) {
                    return Status::kEnd;
                }
                pgid = parent.node()->body.branch[pos].left_child;
            }

            auto [ref, page] = btree_->pager_->Reference(pgid);
            Noder noder{ btree_, page };

            // �ڽڵ��н��ж��ֲ���
            uint16_t index;
            if (noder.node()->element_count > 0) {
                auto iter = std::lower_bound(noder.begin(), noder.end(), key, [&](const Span& span, std::span<const uint8_t> search_key) -> bool {
                    auto [buf, size] = noder.SpanLoad(span);
                    auto res = memcmp(buf, search_key.data(), std::min(size, search_key.size()));
                    if (res == 0 && size != search_key.size()) {
                        comp_results_ = CompResults::kNe;
                        return size < search_key.size();
                    }
                    if (res < 0) {
                        comp_results_ = CompResults::kNe;
                        return true;
                    }
                    else {
                        comp_results_ = CompResults::kEq;
                        return false;
                    }
                });
                index = iter.index();
            }
            else {
                index = 0;
            }
            stack_.push_back(std::pair{ pgid, index });
            return Status::kDown;
        }

        std::pair<PageId, uint16_t> Cur() {
            return stack_.front();
        }

        bool Empty() {
            return stack_.empty();
        }

        CompResults comp_results() { return comp_results_; }

    private:
        BTree* btree_;
        detail::Stack<std::pair<PageId, uint16_t>, 8> stack_;       // �����ض���С�ڵ�������ʱkey�Ľڵ�
        CompResults comp_results_{ CompResults::kNe };
    };


public:
    BTree(Pager* pager, PageId& root_pgid) : 
        pager_{ pager }, 
        root_pgid_ { root_pgid }
    {
        max_leaf_element_count_ = (pager_->page_count() - sizeof(Node) - sizeof(Node::body)) / sizeof(Node::LeafElement);
        max_branch_element_count_ = (pager_->page_count() - sizeof(Node) - sizeof(Node::body)) / sizeof(Node::BranchElement);
    }
    
    /*
    * Ҷ�ӽڵ�ķ���
    * �������ҽڵ������
    */
    Pager::PageReference Split(Noder* left, uint16_t insert_pos, Span&& insert_key, Span&& insert_value) {
        auto right_pgid = pager_->Alloc(1);
        auto [right_ref, right_page] = pager_->Reference(right_pgid);
        Noder right{ this, right_page };
        right.LeafBuild();
        
        uint16_t mid = left->node()->element_count / 2;
        uint16_t right_count = mid + (left->node()->element_count % 2);
        
        int insert_right = 0;
        for (uint16_t i = 0; i < right_count; i++) {
            auto left_pos = mid + i;
            if (left_pos == insert_pos) {
                right.LeafSet(i, 
                    left->SpanMove(&right, std::move(insert_key)), 
                    left->SpanMove(&right, std::move(insert_value))
                );
                insert_right = 1;
                continue;
            }
            auto key = left->SpanMove(&right, std::move(left->node()->body.leaf[left_pos].key));
            auto value = left->SpanMove(&right, std::move(left->node()->body.leaf[left_pos].value));

            right.LeafSet(i + insert_right, std::move(key), std::move(value));
        }
        
        left->node()->element_count -= right_count;
        right.node()->element_count = right_count + insert_right;
        if (insert_right == 0) {
            left->LeafInsert(insert_pos, std::move(insert_key), std::move(insert_value));
        }

        return std::move(right_ref);
    }

    /*
    * Ҷ�ӽڵ�Ĳ���
    */
    void Put(Iterator* iter, std::span<const uint8_t> key, std::span<const uint8_t> value) {
        if (iter->Empty()) {
            root_pgid_ = pager_->Alloc(1);
            auto [ref, page] = pager_->Reference(root_pgid_);
            Noder noder{ this, page };
            noder.LeafBuild();
            noder.LeafInsert(0, noder.SpanAlloc(key), noder.SpanAlloc(value));
            return;
        }

        auto [pgid, pos] = iter->Cur();
        auto [ref, page] = pager_->Reference(pgid);
        Noder noder{ this, page };
        auto key_span = noder.SpanAlloc(key);
        auto value_span = noder.SpanAlloc(value);
        if (iter->comp_results() == Iterator::CompResults::kEq) {
            noder.LeafSet(pos, std::move(key_span), std::move(value_span));
            return;
        }

        if (noder.node()->element_count < max_leaf_element_count_) {
            noder.LeafInsert(pos, std::move(key_span), std::move(value_span));
            return;
        }
        
        // ��Ҫ���������ϲ���
        auto right_ref = Split(&noder, pos, std::move(key_span), std::move(value_span));
        Noder right{ this, right_ref.page_cache()};

        // �����ҽڵ�ĵ�һ���ڵ�
        Put(iter, std::move(ref), 
            std::move(right_ref),
            &right.node()->body.leaf[0].key
        );
    }
    
    /*
    * ��֧�ڵ�ķ���
    * �������ڵ���ĩβ������Ԫ�أ����ҽڵ������
    */
    std::tuple<Span, Pager::PageReference> Split(Noder* left, uint16_t insert_pos, Span&& insert_key, PageId insert_left_child) {
        auto right_pgid = pager_->Alloc(1);
        auto [right_ref, right_page] = pager_->Reference(right_pgid);
        Noder right{ this, right_page };
        right.BranchBuild();

        uint16_t mid = left->node()->element_count / 2;
        uint16_t right_count = mid + (left->node()->element_count % 2);

        int insert_right = 0;
        for (uint16_t i = 0; i < right_count; i++) {
            auto left_pos = mid + i;
            if (left_pos == insert_pos) {
                right.BranchSet(i,
                    left->SpanMove(&right, std::move(insert_key)),
                    insert_left_child
                );
                insert_right = 1;
                continue;
            }
            auto key = left->SpanMove(&right, std::move(left->node()->body.leaf[left_pos].key));
            right.BranchSet(i + insert_right, std::move(key), left->node()->body.branch[left_pos].left_child);
        }

        left->node()->element_count -= right_count;
        right.node()->element_count = right_count + insert_right;
        if (insert_right == 0) {
            left->BranchInsert(insert_pos, std::move(insert_key), insert_left_child);
        }

        // ���ĩβԪ����������left_child��Ϊ���ڵ��tail_child
        Span span = left->node()->body.branch[left->node()->element_count].key;
        left->node()->body.tail_child = left->node()->body.branch[left->node()->element_count--].left_child;

        return { span, std::move(right_ref) };
    }


    /*
    * ��֧�ڵ�Ĳ���
    */
    void Put(Iterator* iter, Pager::PageReference&& left_ref, Pager::PageReference&& right_ref, Span* key) {
        Noder left{ this, left_ref.page_cache() };
        Noder right{ this, right_ref.page_cache() };

        if (iter->Empty()) {
            auto new_branch = pager_->Alloc(1);
            auto [ref, page] = pager_->Reference(new_branch);
            Noder noder{ this, page };
            noder.BranchBuild();
            noder.node()->body.tail_child = right_ref.page_id();

            noder.BranchInsert(0, right.SpanCopy(&noder, *key), left_ref.page_id());
            return;
        }
        
        auto [pgid, pos] = iter->Cur();
        auto [ref, page] = pager_->Reference(pgid);
        Noder noder{ this, page };

        if (noder.node()->element_count < max_leaf_element_count_) {
            noder.BranchInsert(pos, right.SpanCopy(&noder, *key), left_ref.page_id());
            return;
        }

        auto right_ref = Split(&noder, pos, right.SpanCopy(&noder, *key), );

    }



    void Put(std::span<const uint8_t> key, std::span<const uint8_t> value) {
        Iterator iter{ this };
        auto status = iter.Top(key);
        while (status == Iterator::Status::kDown) {
            status = iter.Down(key);
        }
        Put(&iter, key, value);
    }


private:
    void SpanMove(Span& dst, Span& src) {

    }

private:
    friend class Noder;

    Pager* pager_;
    // Tx* tx_;

    PageId& root_pgid_; // PageId&

    uint16_t max_leaf_element_count_;
    uint16_t max_branch_element_count_;
};

} // namespace yudb