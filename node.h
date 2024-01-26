#pragma once

#include "txid.h"
#include "page.h"
#include "cell.h"
#include "block.h"
#include "page_space.h"

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
            TxId last_modified_txid;
            Type type : 2;
            uint16_t element_count : 14;
            BlockTableDescriptor block_info;
            PageSpace page_space;
            uint16_t padding;
            union {
                struct {
                    PageId tail_child;
                    BranchElement branch[1];
                };
                LeafElement leaf[1];
            } body;
        };
        uint8_t page[1];
    };
};
#pragma pack(pop)


constexpr auto aaa = sizeof(Node) - sizeof(Node::body);
static_assert(sizeof(Node) - sizeof(Node::body) >= sizeof(Node::LeafElement) * 2, "abnormal length of head node.");

} // namespace