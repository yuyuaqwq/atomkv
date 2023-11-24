#pragma once

#include <cstdint>

namespace yudb {

using PageId = uint32_t;
using PageCount = uint32_t;

using PageSize = uint16_t;
using PageOffset = int16_t;

constexpr PageSize kPageSize = 4096;


#pragma pack(push, 1)
struct Page {
    enum class Type : uint16_t {
        kBranch = 0,
        kLeaf,
        kOverflow,
    };

    struct Cell {
        enum class Type : uint16_t {
            kEmbed = 0,
            kBlock,
            kTable,
        };

        union {
            Type type : 2;
            struct {
                uint8_t type : 2;
                uint8_t invalid : 2;
                uint8_t size : 4;
                uint8_t data[7];
            } embed;
            struct {
                Type type : 2;
                uint16_t size : 14;
                PageOffset pos;
                PageId pgid;
            } block;
            struct {
                Type type : 2;
                uint16_t size : 14;
                PageOffset pos;
                PageId pgid;
            } table;
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

    union {
        struct {
            Type type : 2;
            uint16_t element_count : 14;
            PageId overflow;

            //TxId last_write_txid;
            union {
                BranchElement branch[];
                LeafElement leaf[];
                uint8_t data[];
            };
        };
        uint8_t full[];
    };
};
#pragma pack(pop)


//auto test = sizeof(Page::Cell);

} // namespace yudb