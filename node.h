#pragma once

#include "page.h"
#include "overflow.h"

namespace yudb {

#pragma pack(push, 1)
struct Node {
    enum class Type : uint16_t {
        kBranch = 0,
        kLeaf,
    };

    struct Cell {
        enum class Type : uint16_t {
            kEmbed = 0,
            kBlock,
            kPageTable,
        };

        union {
            Type type : 2;
            struct {
                uint8_t type : 2;
                uint8_t invalid : 2;
                uint8_t size : 4;
                uint8_t data[5];
            } embed;
            struct {
                Type type : 2;
                uint16_t size : 14;
                uint16_t overflow_index;
                PageOffset offset;
            } block;
            struct {
                Type type : 2;
                uint16_t size : 14;
                uint16_t overflow_index;
                PageOffset offset;
            } page_table;
        };
    };

    struct BranchElement {
        Cell key;
        PageId min_child;
    };

    struct LeafElement {
        Cell key;
        Cell value;
    };

    // ������ڶ�ҳʱ��ͬʱ�ڵڶ�ҳǰ���ִ���һ��������ҳ���С�Ŀռ����(������)�����������пռ�
    // �������ҳ���ҳ�š����ʣ��ռ䣬�������������ҳ�͸��Թ�����Ե�ҳ�ڿ��пռ伴��
    // �ռ����̬��չ�������ҳ�з����µ�

    union {
        struct {
            Type type : 2;
            uint16_t element_count : 14;

            Overflow overflow;

            //TxId last_write_txid;
            union {
                BranchElement branch[];
                LeafElement leaf[];
            };
        };
        uint8_t full[];
    };
};
#pragma pack(pop)

} // namespace