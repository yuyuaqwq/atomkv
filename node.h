#pragma once

#include "txid.h"
#include "page.h"
#include "cell.h"
#include "block_info.h"

namespace yudb {

#pragma pack(push, 1)
struct Node {
    enum class Type : uint16_t {
        kInvalid = 0,
        kBranch,
        kLeaf,
    };

    struct BranchElement {
        Cell key;
        PageId left_child;
    };

    struct LeafElement {
        Cell key;
        Cell value;
    };

    Node(const Node&) = delete;
    void operator=(const Node&) = delete;

    // ������ڶ�ҳʱ��ͬʱ�ڵڶ�ҳǰ���ִ���һ��������ҳ���С�Ŀռ����(������)�����������пռ�
    // �������ҳ���ҳ�š����ʣ��ռ䣬�������������ҳ�͸��Թ�����Ե�ҳ�ڿ��пռ伴��
    // �ռ����̬��չ�������ҳ�з����µ�

    union {
        struct {
            Type type : 2;
            uint16_t element_count : 14;
            uint16_t free_size;
            TxId last_modified_txid;
            BlockInfo block_info;
            union {
                struct {
                    PageId tail_child;
                    BranchElement branch[1];
                };
                LeafElement leaf[1];
            } body;
        };
        uint8_t full[1];
    };
};
#pragma pack(pop)

static_assert(sizeof(Node) - sizeof(Node::body) >= sizeof(Node::LeafElement) * 2, "abnormal length of head node.");

} // namespace