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
    MutNodeOperator node_operator{ this, pgid };
    assert(node_operator.IsLeaf());
    auto& node = node_operator.node();
    node_operator.CellFree(std::move(node.body.leaf[pos].value));
    node.body.leaf[pos].value = node_operator.CellAlloc(value);
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
    ImmNodeOperator node_operator{ this, pgid };
    auto& node = node_operator.node();
    if (node_operator.IsBranch()) {
        node_operator.BranchCheck();
        Print(str, node.body.tail_child, level + 1);
        for (int i = node.element_count - 1; i >= 0; i--) {
            std::string key;
            auto [buf, size, ref] = node_operator.CellLoad(node.body.branch[i].key);
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
        //node_operator.BlockPrint(); printf("\n");
    }
    else {
        assert(node_operator.IsLeaf());
        node_operator.LeafCheck();
        for (int i = node.element_count - 1; i >= 0; i--) {
            std::string key, value;
            auto [key_buf, key_size, key_ref] = node_operator.CellLoad(node.body.leaf[i].key);
            auto [value_buf, value_size, value_ref] = node_operator.CellLoad(node.body.leaf[i].value);
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
        //node_operator.BlockPrint(); printf("\n");
    }
}

void BTree::Print(bool str) const {
    Print(str, *root_pgid_, 0);
}



std::tuple<MutNodeOperator, uint16_t, MutNodeOperator, bool> BTree::GetSibling(Iterator* iter) {
    auto [parent_pgid, parent_pos] = iter->Front();
    MutNodeOperator parent{ this, parent_pgid };
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

    MutNodeOperator sibling{ this, sibling_pgid };

    iter->Pop();
    return { std::move(parent), parent_pos, std::move(sibling), left_sibling };
}


/*
* ��֧�ڵ�ĺϲ�
*/
void BTree::Merge(NodeOperator&& left, NodeOperator&& right, Cell&& down_key) {
    auto& left_node = left.node();
    auto& right_node = right.node();
    left.BranchAlloc(1);
    left_node.body.branch[left_node.element_count-1].key = std::move(down_key);
    left_node.body.branch[left_node.element_count-1].left_child = left_node.body.tail_child;
    auto original_count = left_node.element_count;
    left.BranchAlloc(right_node.element_count);
    for (uint16_t i = 0; i < right_node.element_count; i++) {
        auto key = right.CellMove(&left, std::move(right_node.body.branch[i].key));
        left.BranchSet(i + original_count, std::move(key), right_node.body.branch[i].left_child);
    }
    left_node.body.tail_child = right_node.body.tail_child;
    right.BranchFree(right_node.element_count);
    right.CellClear();
    bucket_->pager().Free(right.page_id(), 1);
}

/*
* ��֧�ڵ��ɾ��
*/
void BTree::Delete(Iterator* iter, NodeOperator&& node_operator, uint16_t left_del_pos) {
    auto& node = node_operator.node();

    node_operator.BranchDelete(left_del_pos, true);
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
            auto parent_key = parent.CellMove(&node_operator, std::move(parent_node.body.branch[parent_pos].key));
            auto sibling_key = sibling.CellMove(&parent, std::move(sibling_node.body.branch[sibling_node.element_count - 1].key));

            node_operator.BranchInsert(0, std::move(parent_key), sibling_node.body.tail_child, false);
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

            auto parent_key = parent.CellMove(&node_operator, std::move(parent_node.body.branch[parent_pos].key));
            auto sibling_key = sibling.CellMove(&parent, std::move(sibling_node.body.branch[0].key));

            node_operator.BranchInsert(node.element_count, std::move(parent_key), sibling_node.body.branch[0].left_child, true);
            sibling.BranchDelete(0, false);

            parent_node.body.branch[parent_pos].key = std::move(sibling_key);
        }
        return;
    }

    // �ϲ�
    if (left_sibling) {
        auto down_key = parent.CellMove(&sibling, std::move(parent_node.body.branch[parent_pos].key));
        Merge(std::move(sibling), std::move(node_operator), std::move(down_key));
    }
    else {
        auto down_key = parent.CellMove(&node_operator, std::move(parent_node.body.branch[parent_pos].key));
        Merge(std::move(node_operator), std::move(sibling), std::move(down_key));
    }

    // ����ɾ����Ԫ��
    Delete(iter, std::move(parent), parent_pos);
}

/*
* Ҷ�ӽڵ�ĺϲ�
*/
void BTree::Merge(NodeOperator&& left, NodeOperator&& right) {
    auto& left_node = left.node();
    auto& right_node = right.node();
    auto original_count = left_node.element_count;
    left.LeafAlloc(right_node.element_count);
    for (uint16_t i = 0; i < right_node.element_count; i++) {
        auto key = right.CellMove(&left, std::move(right_node.body.leaf[i].key));
        auto value = right.CellMove(&left, std::move(right_node.body.leaf[i].value));
        left.LeafSet(i + original_count, std::move(key), std::move(value));
    }
    right.LeafFree(right_node.element_count);
    right.CellClear();
    bucket_->pager().Free(right.page_id(), 1);
}

/*
* Ҷ�ӽڵ��ɾ��
*/
void BTree::Delete(Iterator* iter) {
    auto [pgid, pos] = iter->Front();
    MutNodeOperator node_operator{ this, pgid };
    auto& node = node_operator.node();

    node_operator.LeafDelete(pos);
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
        Cell new_key;
        if (left_sibling) {
            // ���ֵܽڵ��ĩβ��Ԫ�ز��뵽��ǰ�ڵ��ͷ��
            // ���¸�Ԫ��keyΪ��ǰ�ڵ������Ԫ��key
            auto key = sibling.CellMove(&node_operator, std::move(sibling_node.body.leaf[sibling_node.element_count - 1].key));
            auto value = sibling.CellMove(&node_operator, std::move(sibling_node.body.leaf[sibling_node.element_count - 1].value));
            sibling.LeafDelete(sibling_node.element_count - 1);

            new_key = node_operator.CellCopy(&parent, key);
            node_operator.LeafInsert(0, std::move(key), std::move(value));
        }
        else {
            // ���ֵܽڵ��ͷ����Ԫ�ز��뵽��ǰ�ڵ��β��
            // ���¸�Ԫ��keyΪ���ֵܵ�����Ԫ��
            auto key = sibling.CellMove(&node_operator, std::move(sibling_node.body.leaf[0].key));
            auto value = sibling.CellMove(&node_operator, std::move(sibling_node.body.leaf[0].value));
            sibling.LeafDelete(0);

            new_key = sibling.CellCopy(&parent, sibling_node.body.leaf[0].key);
            node_operator.LeafInsert(node.element_count, std::move(key), std::move(value));
        }
        parent.CellFree(std::move(parent_node.body.branch[parent_pos].key));
        parent_node.body.branch[parent_pos].key = std::move(new_key);
        return;
    }

    // �ϲ�
    if (left_sibling) {
        Merge(std::move(sibling), std::move(node_operator));
    }
    else {
        Merge(std::move(node_operator), std::move(sibling));
    }

    // ����ɾ����Ԫ��
    Delete(iter, std::move(parent), parent_pos);
}


/*
* ��֧�ڵ�ķ���
* �������ڵ���ĩβ������Ԫ�أ����ҽڵ�
*/
std::tuple<Cell, NodeOperator> BTree::Split(NodeOperator* left, uint16_t insert_pos, Cell&& insert_key, PageId insert_right_child) {
    MutNodeOperator right{ this, bucket_->pager().Alloc(1) };
    right.BranchBuild();

    auto& left_node = left->node();
    auto& right_node = right.node();

    uint16_t mid = left_node.element_count / 2;
    uint16_t right_count = mid + (left_node.element_count % 2);

    int insert_right = 0;
    right.BranchAlloc(right_count);
    for (uint16_t i = 0; i < right_count; i++) {
        auto left_pos = mid + i;
        if (insert_right == 0 && left_pos == insert_pos) {
            // ����ڵ���ӽڵ�ʹ�����Ԫ�ص��ӽڵ㣬�������ӽڵ㽻������
            auto left_child = left_node.body.branch[left_pos].left_child;
            left_node.body.branch[left_pos].left_child = insert_right_child;
            right.BranchAlloc(1);
            right.BranchSet(i,
                left->CellMove(&right, std::move(insert_key)),
                left_child
            );
            insert_right = 1;
            --i;
            continue;
        }
        auto key = left->CellMove(&right, std::move(left_node.body.branch[left_pos].key));
        right.BranchSet(i + insert_right,
            std::move(key),
            left_node.body.branch[left_pos].left_child
        );
    }

    left->BranchFree(right_count);
    assert(left_node.element_count > 2);
    right_node.body.tail_child = left_node.body.tail_child;
    if (insert_right == 0) {
        if (insert_pos == bucket_->max_branch_ele_count()) {
            right.BranchAlloc(1);
            right.BranchSet(right_node.element_count - 1, left->CellMove(&right, std::move(insert_key)), right_node.body.tail_child);
            right_node.body.tail_child = insert_right_child;
        }
        else {
            left->BranchInsert(insert_pos, std::move(insert_key), insert_right_child, true);
        }
    }

    assert(right_node.element_count > 2);

    // ���ĩβԪ����������left_child��Ϊtail_child
    Cell span{ std::move(left_node.body.branch[left_node.element_count-1].key) };
    left_node.body.tail_child = left_node.body.branch[left_node.element_count-1].left_child;
    left->BranchFree(1);

    return { std::move(span), std::move(right) };
}

/*
* ��֧�ڵ�Ĳ���
*/
void BTree::Put(Iterator* iter, NodeOperator&& left, NodeOperator&& right, Cell* key, bool branch_put) {
    if (iter->Empty()) {
        *root_pgid_ = bucket_->pager().Alloc(1);
        MutNodeOperator node_operator{ this, *root_pgid_ };

        node_operator.BranchBuild();
        node_operator.node().body.tail_child = left.page_id();

        Cell node_operator_key_span;
        if (branch_put) {
            node_operator_key_span = left.CellMove(&node_operator, std::move(*key));
        }
        else {
            node_operator_key_span = right.CellCopy(&node_operator, *key);
        }
        node_operator.BranchInsert(0, std::move(node_operator_key_span), right.page_id(), true);
        return;
    }

    auto [pgid, pos] = iter->Front();
    MutNodeOperator node_operator{ this, pgid };

    Cell node_operator_key_span;
    if (branch_put) {
        node_operator_key_span = left.CellMove(&node_operator, std::move(*key));
    }
    else {
        node_operator_key_span = right.CellCopy(&node_operator, *key);
    }

    //      3       7
    // 1 2     3 4     7 8

    //      3       5    7 
    // 1 2     3 4     5    7 8
    if (node_operator.node().element_count < bucket_->max_branch_ele_count()) {
        node_operator.BranchInsert(pos, std::move(node_operator_key_span), right.page_id(), true);
        return;
    }

    auto [branch_key, branch_right] = Split(&node_operator, pos, std::move(node_operator_key_span), right.page_id());
    iter->Pop();
    Put(iter, std::move(node_operator), std::move(branch_right), &branch_key, true);
}

/*
* Ҷ�ӽڵ�ķ���
* �������ҽڵ�
*/
NodeOperator BTree::Split(NodeOperator* left, uint16_t insert_pos, std::span<const uint8_t> key, std::span<const uint8_t> value) {
    MutNodeOperator right{ this, bucket_->pager().Alloc(1) };
    right.LeafBuild();

    auto& left_node = left->node();
    auto& right_node = right.node();

    uint16_t mid = left_node.element_count / 2;
    uint16_t right_count = mid + (left_node.element_count % 2);

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
        auto key = left->CellMove(&right, std::move(left_node.body.leaf[left_pos].key));
        auto value = left->CellMove(&right, std::move(left_node.body.leaf[left_pos].value));

        right.LeafSet(i + insert_right, std::move(key), std::move(value));
    }

    left->LeafFree(right_count);
    assert(left_node.element_count > 2);
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
    assert(right_node.element_count > 2);
    return right;
}

/*
* Ҷ�ӽڵ�Ĳ���
*/
void BTree::Put(Iterator* iter, std::span<const uint8_t> key, std::span<const uint8_t> value, bool insert_only) {
    if (iter->Empty()) {
        *root_pgid_ = bucket_->pager().Alloc(1);
        MutNodeOperator node_operator{ this, *root_pgid_ };
        node_operator.LeafBuild();
        node_operator.LeafInsert(0, node_operator.CellAlloc(key), node_operator.CellAlloc(value));
        return;
    }

    auto [pgid, pos] = iter->Front();
    MutNodeOperator node_operator{ this, pgid };
    auto& node = node_operator.node();
    if (!insert_only && iter->status() == Iterator::Status::kEq) {
        node_operator.CellFree(std::move(node.body.leaf[pos].value));
        node.body.leaf[pos].value = node_operator.CellAlloc(value);
        return;
    }

    if (node_operator.node().element_count < bucket_->max_leaf_ele_count()) {
        node_operator.LeafInsert(pos, node_operator.CellAlloc(key), node_operator.CellAlloc(value));
        return;
    }

    // ��Ҫ���������ϲ���
    NodeOperator right = Split(&node_operator, pos, key, value);

    iter->Pop();
    // �����ҽڵ�ĵ�һ���ڵ�
    Put(iter, std::move(node_operator),
        std::move(right),
        &right.node().body.leaf[0].key
    );
}



} // namespace yudb