#pragma once

#include <cassert>

#include <span>
#include <stdexcept>
#include <optional>
#include <variant>
#include <vector>

#include "noncopyable.h"
#include "node.h"
#include "overflower.h"
#include "pager.h"

namespace yudb {

class BTree;
class Pager;

class Noder : noncopyable {
private:
    class Iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;

        using difference_type = uint16_t;
        using value_type = Span;
        using difference_type = uint16_t;
        using pointer = Span*;
        using reference = Span&;

    public:
        Iterator(Node* node, uint16_t index) : node_{ node }, index_{ index } {}

        reference operator*() const { 
            if (node_->type == Node::Type::kBranch) {
                return node_->body.branch[index_].key;
            }
            else {
                assert(node_->type == Node::Type::kLeaf);
                return node_->body.leaf[index_].key;
            }
        }

        Iterator& operator--() noexcept {
            --index_;
            return *this;
        }

        Iterator& operator++() noexcept {
            ++index_;
            return *this;
        }

        Iterator operator+(const difference_type n) const {
            return Iterator{ node_, uint16_t(index_ + n) };
        }

        Iterator operator-(const difference_type n) const {
            return Iterator{ node_, uint16_t(index_ - n) };
        }

        difference_type operator-(const Iterator& right) const noexcept {
            return index_ - right.index_;
        }

        Iterator& operator-=(const difference_type off) noexcept {
            index_ -= off;
            return *this;
        }

        Iterator& operator+=(const difference_type off) noexcept {
            index_ += off;
            return *this;
        }

        uint16_t index() { return index_; }

    private:
        Node* node_;
        uint16_t index_;
    };

public:
    Noder(BTree* btree, PageId page_id);

    Noder(Noder&& right) noexcept : 
        page_ref_{ std::move(right.page_ref_) },
        btree_{ right.btree_ },
        pager_{ right.pager_ },
        node_{ right.node_ },
        overflower_{ std::move(right.overflower_) } {}

    /*
    * 将数据保存到Node中
    */
    Span SpanAlloc(std::span<const uint8_t> data) {
        Span span;
        if (data.size() <= sizeof(Span::embed.data)) {
            span.type = Span::Type::kEmbed;
            span.embed.size = data.size();
            memcpy(span.embed.data, data.data(), data.size());
        }
        else if (data.size() < kPageSize) {
            auto res = overflower_.Alloc(data.size());
            if (!res) {
                throw std::runtime_error("overflower alloc error.");
            }
            auto [index, offset] = *res;
            span.type = Span::Type::kBlock;
            span.block.record_index = index;
            span.block.offset = offset;
            span.block.size = data.size();

            auto [buf, page] = overflower_.Load(span.block.record_index, span.block.offset);
            memcpy(buf, data.data(), data.size());
        }
        else {
            throw std::runtime_error("unrealized types.");
        }
        return span;
    }

    void SpanFree(Span&& span) {
        if (span.type == Span::Type::kEmbed) { }
        else if (span.type == Span::Type::kBlock) {
            overflower_.Free({ span.block.record_index, span.block.offset, span.block.size });
        }
        else {
            throw std::runtime_error("unrealized types.");
        }
        span.type = Span::Type::kInvalid;
    }

    /*
    * 将span移动到新节点
    */
    Span SpanMove(Noder* new_noder, Span&& span) {
        auto [buf, size, ref] = SpanLoad(span);
        auto new_span =  new_noder->SpanAlloc({ buf, size });
        SpanFree(std::move(span));
        return new_span;
    }

    /*
    * 将span拷贝到新节点
    */
    Span SpanCopy(Noder* new_noder, const Span& span) {
        auto [buf, size, ref] = SpanLoad(span);
        auto new_span = new_noder->SpanAlloc({ buf, size });
        return new_span;
    }

    std::tuple<const uint8_t*, size_t, std::optional<std::variant<PageReferencer, std::vector<uint8_t>>>>
    SpanLoad(const Span& span) {
        if (span.type == Span::Type::kEmbed) {
            return { span.embed.data, span.embed.size, std::nullopt };
        }
        else if (span.type == Span::Type::kBlock) {
            auto [buf, page] = overflower_.Load(span.block.record_index, span.block.offset);
            return { buf, span.block.size, std::move(page) };
        }
        else {
            throw std::runtime_error("unrealized types.");
        }
    }


    void OverflowInit() {
        node_->overflow.record_pgid = kPageInvalidId;
        node_->overflow.record_index = kRecordInvalidIndex;
        node_->overflow.record_count = 0;
    }


    void LeafBuild() {
        OverflowInit();
        node_->type = Node::Type::kLeaf;
        node_->element_count = 0;
    }

    void LeafSet(uint16_t pos, Span&& key, Span&& value) {
        auto key_span = std::move(node_->body.leaf[pos].key);
        auto value_span = std::move(node_->body.leaf[pos].value);

        node_->body.leaf[pos].key = std::move(key);
        node_->body.leaf[pos].value = std::move(value);
    }

    void LeafInsert(uint16_t pos, Span&& key, Span&& value) {
        assert(pos <= node_->element_count);
        std::memmove(&node_->body.leaf[pos + 1], &node_->body.leaf[pos], (node_->element_count - pos) * sizeof(node_->body.leaf[pos]));
        node_->body.leaf[pos].key = std::move(key);
        node_->body.leaf[pos].value = std::move(value);
        ++node_->element_count;
    }

    void LeafDelete(uint16_t pos) {
        assert(pos < node_->element_count);
        auto key = std::move(node_->body.leaf[pos].key);
        auto value = std::move(node_->body.leaf[pos].value);

        std::memmove(&node_->body.leaf[pos], &node_->body.leaf[pos + 1], (node_->element_count - pos - 1) * sizeof(node_->body.leaf[pos]));
        --node_->element_count;
    }


    void BranchBuild() {
        OverflowInit();
        node_->type = Node::Type::kBranch;
        node_->element_count = 0;
    }

    void BranchInsert(uint16_t pos, Span&& key, PageId child, bool right_child) {
        assert(pos <= node_->element_count);
        if (right_child) {
            if (pos == node_->element_count) {
                node_->body.branch[pos].left_child = node_->body.tail_child;
                node_->body.tail_child = child;
            }
            else {
                std::memmove(&node_->body.branch[pos + 1], &node_->body.branch[pos], (node_->element_count - pos) * sizeof(node_->body.branch[pos]));
                node_->body.branch[pos].left_child = node_->body.branch[pos + 1].left_child;
                node_->body.branch[pos + 1].left_child = child;
            }
        }
        else {
            std::memmove(&node_->body.branch[pos + 1], &node_->body.branch[pos], (node_->element_count - pos) * sizeof(node_->body.branch[pos]));
            node_->body.branch[pos].left_child = child;
        }
        node_->body.branch[pos].key = std::move(key);
        ++node_->element_count;
    }

    void BranchDelete(uint16_t pos, bool right_child) {
        assert(pos < node_->element_count);
        auto copy_count = node_->element_count - pos - 1;
        if (right_child) {
            if (pos + 1 < node_->element_count) {
                node_->body.branch[pos].key = node_->body.branch[pos + 1].key;
            }
            else {
                node_->body.tail_child = node_->body.branch[node_->element_count - 1].left_child;
            }
            if (copy_count > 0) {
                std::memmove(&node_->body.branch[pos + 1], &node_->body.branch[pos + 2], (copy_count - 1) * sizeof(node_->body.branch[pos]));
            }
        }
        else {
            std::memmove(&node_->body.branch[pos], &node_->body.branch[pos + 1], copy_count * sizeof(node_->body.branch[pos]));
        }
        --node_->element_count;
    }

    /*
    * 不处理tail_child的关系，需要自己保证tail_child的正确性
    */
    void BranchSet(uint16_t pos, Span&& key, PageId left_child) {
        node_->body.branch[pos].key = std::move(key);
        node_->body.branch[pos].left_child = left_child;
    }

    PageId BranchGetLeftChild(uint16_t pos) {
        assert(pos <= node_->element_count);
        if (pos == node_->element_count) {
            return node_->body.tail_child;
        }
        return node_->body.branch[pos].left_child;
    }

    PageId BranchGetRightChild(uint16_t pos) {
        assert(pos < node_->element_count);
        if (pos == node_->element_count - 1) {
            return node_->body.tail_child;
        }
        return node_->body.branch[pos + 1].left_child;
    }

    void BranchSetLeftChild(uint16_t pos, PageId left_child) {
        assert(pos <= node_->element_count);
        if (pos == node_->element_count) {
            node_->body.tail_child = left_child;
        }
        node_->body.branch[pos].left_child = left_child;
    }

    void BranchSetRightChild(uint16_t pos, PageId right_child) {
        assert(pos < node_->element_count);
        if (pos == node_->element_count - 1) {
            node_->body.tail_child = right_child;
        }
        node_->body.branch[pos + 1].left_child = right_child;
    }


    bool IsLeaf() const {
        return node_->type == Node::Type::kLeaf;
    }

    bool IsBranch() const {
        return node_->type == Node::Type::kBranch;
    }


    Node* node() { return node_; }

    PageId page_id() { return page_ref_.page_id(); }
    uint8_t* page_cache() { return page_ref_.page_cache(); }

    Iterator begin() { return Iterator{ node_, 0 }; }
    Iterator end() { return Iterator{ node_, node_->element_count }; }


private:
    friend class Overflower;

    BTree* btree_;
    Pager* pager_;

    PageReferencer page_ref_;
    Node* node_;

    Overflower overflower_;
};  

} // namespace yudb