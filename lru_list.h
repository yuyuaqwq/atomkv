#pragma once

#include <cassert>

#include <vector>
#include <optional>
#include <unordered_map>
#include <functional>

#include "unordered_dense.h"
#include "cache.h"

namespace yudb {

template<typename LruList, typename K, typename V>
class LruListIterator {
public:
    using iterator_category = std::bidirectional_iterator_tag;

    using value_type = LruListIterator;
    using difference_type = typename std::ptrdiff_t;
    using pointer = LruListIterator*;
    using reference = const value_type&;

    LruListIterator(LruList* list, CacheId id) : list_{ list }, id_{ id } {}

    reference operator*() const noexcept {
        return *this;
    }

    pointer operator->() const noexcept {
        return this;
    }

    LruListIterator& operator++() noexcept {
        auto node = list_->LruListGetNode(id_);
        id_ = node.next;
        return *this;
    }

    LruListIterator operator++(int) noexcept {
        auto node = list_->LruListGetNode(id_);
        LruListIterator tmp = *this;
        id_ = node.next;
        return tmp;
    }

    LruListIterator& operator--() noexcept {
        auto node = list_->LruListGetNode(id_);
        id_ = node.prev;
        return *this;
    }

    LruListIterator operator--(int) noexcept {
        auto node = list_->LruListGetNode(id_);
        LruListIterator tmp = *this;
        id_ = node.prev;
        return tmp;
    }

    bool operator==(const LruListIterator& right) const noexcept {
        return id_ == right.id_;
    }

    const K& key() const {
        return list_->PoolNode(id_).key;
    }

    const V& value() const {
        return list_->PoolNode(id_).value;
    }


private:
    LruList* list_;
    CacheId id_;
};

template<typename K, typename V>
class LruList {
private:
    struct List {
        struct Node {
            CacheId prev;
            CacheId next;
        };

        CacheId front{ kCacheInvalidId };
        CacheId tail{ kCacheInvalidId };
    };

    struct Node {
        K key;
        union {
            List::Node free_node;
            V value;
        };
        List::Node lru_node;
    };

    using GetListNodeFunc = List::Node&(LruList::*)(CacheId);

    using Iterator = LruListIterator<LruList<K, V>, K, V>;
public:

    LruList(size_t max_count) : pool_(max_count) {
        for (CacheId i = max_count - 1; i > 0; i--) {
            auto& node = PoolNode(i);
            std::construct_at<V>(&node.value);
            ListPushFront(free_list_, &LruList::FreeListGetNode, i);
        }
        ListPushFront(free_list_, &LruList::FreeListGetNode, 0);
    }
    ~LruList() = default;

    /*
    * ���ر���̭�Ķ���
    */
    std::optional<std::tuple<CacheId, K, V>> Put(const K& key, V&& value) {
        auto free = ListFront(free_list_);
        std::optional<std::tuple<CacheId, K, V>> evict{};
        if (free == kCacheInvalidId) {
            auto tail = ListTail(lru_list_);
            assert(tail != kCacheInvalidId);
            auto& tail_node = PoolNode(tail);
            auto iter = map_.find(tail_node.key);
            evict = { iter->second, tail_node.key, tail_node.value };
            free = tail;
            map_.erase(iter);
            ListPopTail(lru_list_, &LruList::LruListGetNode);
        }
        else {
            ListPopFront(free_list_, &LruList::FreeListGetNode);
        }

        auto& node = PoolNode(free);
        node.key = key;
        node.value = std::move(value);

        ListPushFront(lru_list_, &LruList::LruListGetNode, free);
        map_.insert(std::pair{ key, free });
        return evict;
    }
    std::pair<V*, CacheId> Get(const K& key, bool put_front = true) {
        auto iter = map_.find(key);
        if (iter == map_.end()) {
            return { nullptr, kCacheInvalidId };
        }
        if (put_front) {
            ListDelete(lru_list_, &LruList::LruListGetNode, iter->second);
            ListPushFront(lru_list_, &LruList::LruListGetNode, iter->second);
        }
        return { &PoolNode(iter->second).value, iter->second };
    }
    bool Del(const K& key) {
        auto iter = map_.find(key);
        if (iter == map_.end()) {
            return false;
        }
        ListDelete(lru_list_, &LruList::LruListGetNode, iter->second);
        ListPushFront(free_list_, &LruList::FreeListGetNode, iter->second);
        map_.erase(iter);
        return true;
    }
    V& Front() {
        return PoolNode(ListFront(lru_list_)).value;
    }

    const Node& GetNodeByCacheId(CacheId cache_id) const {
        return PoolNode(cache_id);
    }
    Node& GetNodeByCacheId(CacheId cache_id) {
        return PoolNode(cache_id);
    }

    Iterator begin() noexcept {
        return Iterator{ this, ListFront(lru_list_) };
    }
    Iterator end() noexcept {
        return Iterator{ this, kCacheInvalidId };
    }

    void Print() {
        std::cout << "LruList:";
        for (auto& iter : *this) {
            std::cout << iter.key() << " ";
        }
        std::cout << std::endl;
    }

private:
    List::Node& FreeListGetNode(CacheId i) {
        return PoolNode(i).free_node;
    }
    List::Node& LruListGetNode(CacheId i) {
        return PoolNode(i).lru_node;
    }
    Node& PoolNode(CacheId i) {
        return pool_[i];
    }
    const Node& PoolNode(CacheId i) const {
        return pool_[i];
    }

    void ListPushFront(List& list, GetListNodeFunc get_node, CacheId i) {
        auto& node = (this->*get_node)(i);
        if (list.front == kCacheInvalidId) {
            node.prev = kCacheInvalidId;
            node.next = kCacheInvalidId;
            list.tail = i;
        }
        else {
            auto& old_front_node = (this->*get_node)(list.front);
            node.prev = kCacheInvalidId;
            node.next = list.front;
            old_front_node.prev = i;
        }
        list.front = i;
    }
    void ListDelete(List& list, GetListNodeFunc get_node, CacheId i) {
        assert(i != kCacheInvalidId);
        if (list.front == i && list.tail == i) {
            list.front = kCacheInvalidId;
            list.tail = kCacheInvalidId;
            return;
        }
        auto& del_node = (this->*get_node)(i);
        auto prev = del_node.prev;
        auto next = del_node.next;
        
        if (prev != kCacheInvalidId) {
            auto& prev_node = (this->*get_node)(prev);
            prev_node.next = next;
        }
        else {
            list.front = next;
        }

        if (next != kCacheInvalidId) {
            auto& next_node = (this->*get_node)(next);
            next_node.prev = prev;
        }
        else {
            list.tail = prev;
        }
    }
    void ListPopFront(List& list, GetListNodeFunc get_node) {
        ListDelete(list, get_node, list.front);
    }
    void ListPopTail(List& list, GetListNodeFunc get_node) {
        ListDelete(list, get_node, list.tail);
    }
    CacheId ListFront(List& list) {
        return list.front;
    }
    CacheId ListTail(List& list) {
        return list.tail;
    }

private:
    friend Iterator;

    List free_list_;
    List lru_list_;

    std::vector<Node> pool_;
    //std::unordered_map<K, CacheId> map_;
    ankerl::unordered_dense::map<K, CacheId> map_;
};

} // namespace yudb