#pragma once

#include "page.h"
#include "txid.h"
#include "page_space.h"

namespace yudb {

constexpr uint16_t kRecordInvalidIndex = 0xffff;

#pragma pack(push, 1)
struct FreeBlock {
    PageOffset next;
    uint16_t size;
};

struct BlockTableDescriptor {
    PageId pgid;
    uint16_t entry_index;
    uint16_t count;
};

struct BlockTableEntry {
    PageId pgid;
    uint16_t max_free_size;
};

struct BlockPage {
    union {
        struct {
            TxId last_modified_txid;
            PageSpace page_space;
            PageOffset first_block_pos;
            PageSize fragment_size;
        };
        uint8_t page[1];
    };
    BlockTableEntry block_table[1];
};



/*
* ���˳����£�ÿһ��span����Ҫ����һҳ���洢
* ��ÿ��leaf_element����2��span��leaf_element_size��12�ֽ�
* 
* �豣֤һҳ�ܹ�װ��block_table
* Ҫ�� node_size >= leaf_element_size(12) * 2 = 24 (4��span��4��entry)
* ������ block_page_header_size(16) + entry(6) = 22 ��ʹ�õĿռ� (4��entry)
*/

#pragma pack(pop)

} // namespace yudb