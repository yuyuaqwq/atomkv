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
    auto continue_ = iter.Top(key);
    while (continue_) {
        continue_ = iter.Down(key);
    }
    return iter;
}

BTree::Iterator BTree::Get(std::span<const uint8_t> key) const {
    auto iter = LowerBound(key);
    if (iter.status() != Iterator::Status::kEq) {
        return Iterator{ this };
    }
    return iter;
}

void BTree::Insert(std::span<const uint8_t> key, std::span<const uint8_t> value) {
    auto iter = LowerBound(key);
    iter.PathCopy();
    Put(&iter, key, value, true);
}

void BTree::Put(std::span<const uint8_t> key, std::span<const uint8_t> value) {
    auto iter = LowerBound(key);
    iter.PathCopy();
    Put(&iter, key, value, false);
}

void BTree::Update(Iterator* iter, std::span<const uint8_t> value) {
    auto [pgid, pos] = iter->Front();
    MutNode node{ this, pgid };
    assert(node.IsLeaf());
    node.CellFree(std::move(node.leaf_value(pos)));
    node.set_leaf_value(pos, node.CellAlloc(value));
}

bool BTree::Delete(std::span<const uint8_t> key) {
    auto iter = LowerBound(key);
    if (iter.status() != Iterator::Status::kEq) {
        return false;
    }
    iter.PathCopy();
    Delete(&iter);
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
    ImmNode node{ this, pgid };
    if (node.IsBranch()) {
        node.BranchCheck();
        Print(str, node.tail_child(), level + 1);
        for (int i = node.element_count() - 1; i >= 0; i--) {
            std::string key;
            auto [buf, size, ref] = node.CellLoad(node.branch_key(i));
            if (str) {
                std::cout << std::format("{}branch[{}]::key::{}::level::{}\n", indent, pgid, std::string_view{ reinterpret_cast<const char*>(buf), size }, level);
            }
            else {
                for (int j = 0; j < size; j++) {
                    key += std::format("{:02x}", buf[j]) + " ";
                }
                std::cout << std::format("{}branch[{}]::key::{}::level::{}\n", indent, pgid, key, level);
            }
            Print(str, node.branch_left_child(i), level + 1);
        }
        //node.BlockPrint(); printf("\n");
    }
    else {
        assert(node.IsLeaf());
        node.LeafCheck();
        for (int i = node.element_count() - 1; i >= 0; i--) {
            std::string key, value;
            auto [key_buf, key_size, key_ref] = node.CellLoad(node.leaf_key(i));
            auto [value_buf, value_size, value_ref] = node.CellLoad(node.leaf_value(i));
            if (str) {
                std::cout << std::format("{}leaf[{}]::key::{}::value::{}::level::{}\n", indent, pgid, std::string_view{ reinterpret_cast<const char*>(key_buf), key_size }, std::string_view{ reinterpret_cast<const char*>(value_buf), value_size }, level);
            }
            else {
                for (int j = 0; j < key_size; j++) {
                    key += std::format("{:02x}", key_buf[j]) + " ";
                }
                for (int j = 0; j < value_size; j++) {
                    value += std::format("{:02x}", value_buf[j]) + " ";
                }
                std::cout << std::format("{}leaf[{}]::key::{}::value::{}::level::{}\n", indent, pgid, key, value, level);
            }
        }
        //node.BlockPrint(); printf("\n");
    }
}

void BTree::Print(bool str) const {
    Print(str, *root_pgid_, 0);
}



std::tuple<MutNode, uint16_t, MutNode, bool> BTree::GetSibling(Iterator* iter) {
    auto [parent_pgid, parent_pos] = iter->Front();
    MutNode parent{ this, parent_pgid };

    bool left_sibling = false;
    PageId sibling_pgid;
    if (parent_pos == parent.element_count()) {
        // �Ǹ��ڵ�������Ԫ�أ�ֻ��ѡ�����ֵܽڵ�
        left_sibling = true;
        sibling_pgid = parent.BranchGetLeftChild(parent_pos - 1);
    }
    else {
        sibling_pgid = parent.BranchGetRightChild(parent_pos);
    }

    MutNode sibling{ this, sibling_pgid };

    iter->Pop();
    return { std::move(parent), parent_pos, std::move(sibling), left_sibling };
}


/*
* ��֧�ڵ�ĺϲ�
*/
void BTree::Merge(Node&& left, Node&& right, Cell&& down_key) {
    left.BranchAlloc(1);
    left.set_branch_key(left.element_count() - 1, std::move(down_key));
    left.set_branch_left_child(left.element_count() - 1, left.tail_child());
    auto original_count = left.element_count();
    left.BranchAlloc(right.element_count());
    for (uint16_t i = 0; i < right.element_count(); i++) {
        auto key = right.CellMove(&left, std::move(right.branch_key(i)));
        left.BranchSet(i + original_count, std::move(key), right.branch_left_child(i));
    }
    left.set_tail_child(right.tail_child());
    right.BranchFree(right.element_count());
    right.CellClear();
    bucket_->pager().Free(right.page_id(), 1);
}

/*
* ��֧�ڵ��ɾ��
*/
void BTree::Delete(Iterator* iter, Node&& node, uint16_t left_del_pos) {
    node.BranchDelete(left_del_pos, true);
    if (node.element_count() >= (bucket_->max_branch_ele_count() >> 1)) {
        return;
    }

    if (iter->Empty()) {
        // ���û�и��ڵ�
        // �ж��Ƿ�û���κ��ӽڵ��ˣ��������������һ���ӽڵ�Ϊ���ڵ�
        if (node.element_count() == 0) {
            auto old_root = *root_pgid_;
            *root_pgid_ = node.tail_child();
            bucket_->pager().Free(old_root, 1);
        }
        return;
    }

    assert(node.element_count() > 0);

    auto [parent, parent_pos, sibling, left_sibling] = GetSibling(iter);
    if (left_sibling) --parent_pos;
    if (bucket_->tx().NeedCopy(sibling.last_modified_txid())) {
        sibling = sibling.Copy();
        if (left_sibling) {
            parent.BranchSetLeftChild(parent_pos, sibling.page_id());
        }
        else {
            parent.BranchSetLeftChild(parent_pos + 1, sibling.page_id());
        }
    }
    if (sibling.element_count() > (bucket_->max_branch_ele_count() >> 1)) {
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
            auto parent_key = parent.CellMove(&node, std::move(parent.branch_key(parent_pos)));
            auto sibling_key = sibling.CellMove(&parent, std::move(sibling.branch_key(sibling.element_count() - 1)));

            node.BranchInsert(0, std::move(parent_key), sibling.tail_child(), false);
            sibling.BranchDelete(sibling.element_count() - 1, true);

            parent.set_branch_key(parent_pos, std::move(sibling_key));
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

            auto parent_key = parent.CellMove(&node, std::move(parent.branch_key(parent_pos)));
            auto sibling_key = sibling.CellMove(&parent, std::move(sibling.branch_key(0)));

            node.BranchInsert(node.element_count(), std::move(parent_key), sibling.branch_left_child(0), true);
            sibling.BranchDelete(0, false);

            parent.set_branch_key(parent_pos, std::move(sibling_key));
        }
        return;
    }

    // �ϲ�
    if (left_sibling) {
        auto down_key = parent.CellMove(&sibling, std::move(parent.branch_key(parent_pos)));
        Merge(std::move(sibling), std::move(node), std::move(down_key));
    }
    else {
        auto down_key = parent.CellMove(&node, std::move(parent.branch_key(parent_pos)));
        Merge(std::move(node), std::move(sibling), std::move(down_key));
    }

    // ����ɾ����Ԫ��
    Delete(iter, std::move(parent), parent_pos);
}

/*
* Ҷ�ӽڵ�ĺϲ�
*/
void BTree::Merge(Node&& left, Node&& right) {
    auto original_count = left .element_count();
    left.LeafAlloc(right.element_count());
    for (uint16_t i = 0; i < right.element_count(); i++) {
        auto key = right.CellMove(&left, std::move(right.leaf_key(i)));
        auto value = right.CellMove(&left, std::move(right.leaf_value(i)));
        left.LeafSet(i + original_count, std::move(key), std::move(value));
    }
    right.LeafFree(right.element_count());
    right.CellClear();
    bucket_->pager().Free(right.page_id(), 1);
}

/*
* Ҷ�ӽڵ��ɾ��
*/
void BTree::Delete(Iterator* iter) {
    auto [pgid, pos] = iter->Front();
    MutNode node{ this, pgid };

    node.LeafDelete(pos);
    if (node.element_count() >= (bucket_->max_leaf_ele_count() >> 1)) {
        return;
    }

    iter->Pop();
    if (iter->Empty()) {
        // ���û�и��ڵ�
        // ��Ҷ�ӽڵ������
        return;
    }

    assert(node.element_count() > 1);

    auto [parent, parent_pos, sibling, left_sibling] = GetSibling(iter);
    if (left_sibling) --parent_pos;

    if (bucket_->tx().NeedCopy(sibling.last_modified_txid())) {
        sibling = sibling.Copy();
        if (left_sibling) {
            parent.BranchSetLeftChild(parent_pos, sibling.page_id());
        }
        else {
            parent.BranchSetLeftChild(parent_pos + 1, sibling.page_id());
        }
    }
    if (sibling.element_count() > (bucket_->max_leaf_ele_count() >> 1)) {
        // ���ֵܽڵ���Ԫ��������
        Cell new_key;
        if (left_sibling) {
            // ���ֵܽڵ��ĩβ��Ԫ�ز��뵽��ǰ�ڵ��ͷ��
            // ���¸�Ԫ��keyΪ��ǰ�ڵ������Ԫ��key
            auto key = sibling.CellMove(&node, std::move(sibling.leaf_key(sibling.element_count() - 1)));
            auto value = sibling.CellMove(&node, std::move(sibling.leaf_value(sibling.element_count() - 1)));
            sibling.LeafDelete(sibling.element_count() - 1);

            new_key = node.CellCopy(&parent, key);
            node.LeafInsert(0, std::move(key), std::move(value));
        }
        else {
            // ���ֵܽڵ��ͷ����Ԫ�ز��뵽��ǰ�ڵ��β��
            // ���¸�Ԫ��keyΪ���ֵܵ�����Ԫ��
            auto key = sibling.CellMove(&node, std::move(sibling.leaf_key(0)));
            auto value = sibling.CellMove(&node, std::move(sibling.leaf_value(0)));
            sibling.LeafDelete(0);

            new_key = sibling.CellCopy(&parent, sibling.leaf_key(0));
            node.LeafInsert(node.element_count(), std::move(key), std::move(value));
        }
        parent.CellFree(std::move(parent.branch_key(parent_pos)));
        parent.set_branch_key(parent_pos, std::move(new_key));
        return;
    }

    // �ϲ�
    if (left_sibling) {
        Merge(std::move(sibling), std::move(node));
    }
    else {
        Merge(std::move(node), std::move(sibling));
    }

    // ����ɾ����Ԫ��
    Delete(iter, std::move(parent), parent_pos);
}


/*
* ��֧�ڵ�ķ���
* �������ڵ���ĩβ������Ԫ�أ����ҽڵ�
*/
std::tuple<Cell, Node> BTree::Split(Node* left, uint16_t insert_pos, Cell&& insert_key, PageId insert_right_child) {
    MutNode right{ this, bucket_->pager().Alloc(1) };
    right.BranchBuild();


    uint16_t mid = left->element_count() / 2;
    uint16_t right_count = mid + (left->element_count() % 2);

    int insert_right = 0;
    right.BranchAlloc(right_count);
    for (uint16_t i = 0; i < right_count; i++) {
        auto left_pos = mid + i;
        if (insert_right == 0 && left_pos == insert_pos) {
            // ����ڵ���ӽڵ�ʹ�����Ԫ�ص��ӽڵ㣬�������ӽڵ㽻������
            auto left_child = left->branch_left_child(left_pos);
            left->set_branch_left_child(left_pos, insert_right_child);
            right.BranchAlloc(1);
            right.BranchSet(i,
                left->CellMove(&right, std::move(insert_key)),
                left_child
            );
            insert_right = 1;
            --i;
            continue;
        }
        auto key = left->CellMove(&right, std::move(left->branch_key(left_pos)));
        right.BranchSet(i + insert_right,
            std::move(key),
            left->branch_left_child(left_pos)
        );
    }

    left->BranchFree(right_count);
    assert(left->element_count() > 2);
    right.set_tail_child(left->tail_child());
    if (insert_right == 0) {
        if (insert_pos == bucket_->max_branch_ele_count()) {
            right.BranchAlloc(1);
            right.BranchSet(right.element_count() - 1, left->CellMove(&right, std::move(insert_key)), right.tail_child());
            right.set_tail_child(insert_right_child);
        }
        else {
            left->BranchInsert(insert_pos, std::move(insert_key), insert_right_child, true);
        }
    }

    assert(right.element_count() > 2);

    // ���ĩβԪ����������left_child��Ϊtail_child
    Cell span{ std::move(left->branch_key(left->element_count() - 1))};
    left->set_tail_child(left->branch_left_child(left->element_count() - 1));
    left->BranchFree(1);

    return { std::move(span), std::move(right) };
}

/*
* ��֧�ڵ�Ĳ���
*/
void BTree::Put(Iterator* iter, Node&& left, Node&& right, Cell* key, bool branch_put) {
    if (iter->Empty()) {
        *root_pgid_ = bucket_->pager().Alloc(1);
        MutNode node{ this, *root_pgid_ };

        node.BranchBuild();
        node.set_tail_child(left.page_id());

        Cell node_key_span;
        if (branch_put) {
            node_key_span = left.CellMove(&node, std::move(*key));
        }
        else {
            node_key_span = right.CellCopy(&node, *key);
        }
        node.BranchInsert(0, std::move(node_key_span), right.page_id(), true);
        return;
    }

    auto [pgid, pos] = iter->Front();
    MutNode node{ this, pgid };

    Cell node_key_span;
    if (branch_put) {
        node_key_span = left.CellMove(&node, std::move(*key));
    }
    else {
        node_key_span = right.CellCopy(&node, *key);
    }

    //      3       7
    // 1 2     3 4     7 8

    //      3       5    7 
    // 1 2     3 4     5    7 8
    if (node.element_count() < bucket_->max_branch_ele_count()) {
        node.BranchInsert(pos, std::move(node_key_span), right.page_id(), true);
        return;
    }

    auto [branch_key, branch_right] = Split(&node, pos, std::move(node_key_span), right.page_id());
    iter->Pop();
    Put(iter, std::move(node), std::move(branch_right), &branch_key, true);
}

/*
* Ҷ�ӽڵ�ķ���
* �������ҽڵ�
*/
Node BTree::Split(Node* left, uint16_t insert_pos, std::span<const uint8_t> key, std::span<const uint8_t> value) {
    MutNode right{ this, bucket_->pager().Alloc(1) };
    right.LeafBuild();

    uint16_t mid = left->element_count() / 2;
    uint16_t right_count = mid + (left->element_count() % 2);

    int insert_right = 0;
    right.LeafAlloc(right_count);
    for (uint16_t i = 0; i < right_count; i++) {
        auto left_pos = mid + i;
        if (insert_right == 0 && left_pos == insert_pos) {
            right.LeafAlloc(1);
            right.LeafSet(i, right.CellAlloc(key), right.CellAlloc(value));
            insert_right = 1;
            --i;
            continue;
        }
        auto key = left->CellMove(&right, std::move(left->leaf_key(left_pos)));
        auto value = left->CellMove(&right, std::move(left->leaf_value(left_pos)));

        right.LeafSet(i + insert_right, std::move(key), std::move(value));
    }

    left->LeafFree(right_count);
    assert(left->element_count() > 2);
    if (insert_right == 0) {
        if (insert_pos == bucket_->max_leaf_ele_count()) {
            insert_right = 1;
            right.LeafAlloc(1);
            right.LeafSet(right_count, right.CellAlloc(key), right.CellAlloc(value));
        }
        else {
            left->LeafInsert(insert_pos, left->CellAlloc(key), left->CellAlloc(value));
        }
    }
    assert(right.element_count() > 2);
    return right;
}

/*
* Ҷ�ӽڵ�Ĳ���
*/
void BTree::Put(Iterator* iter, std::span<const uint8_t> key, std::span<const uint8_t> value, bool insert_only) {
    if (iter->Empty()) {
        *root_pgid_ = bucket_->pager().Alloc(1);
        MutNode node{ this, *root_pgid_ };
        node.LeafBuild();
        node.LeafInsert(0, node.CellAlloc(key), node.CellAlloc(value));
        return;
    }

    auto [pgid, pos] = iter->Front();
    MutNode node{ this, pgid };
    if (!insert_only && iter->status() == Iterator::Status::kEq) {
        node.CellFree(std::move(node.leaf_value(pos)));
        node.set_leaf_value(pos, node.CellAlloc(value));
        return;
    }

    if (node.element_count() < bucket_->max_leaf_ele_count()) {
        node.LeafInsert(pos, node.CellAlloc(key), node.CellAlloc(value));
        return;
    }

    // ��Ҫ���������ϲ���
    Node right = Split(&node, pos, key, value);

    iter->Pop();
    // �����ҽڵ�ĵ�һ���ڵ�
    Put(iter, std::move(node),
        std::move(right),
        &right.leaf_key(0)
    );
}


} // namespace yudb