#pragma once

#include "page.h"
#include "txid.h"

namespace yudb {

constexpr uint16_t kRecordInvalidIndex = 0xffff;
constexpr PageOffset kFreeInvalidPos = 0xffff;

#pragma pack(push, 1)
struct FreeBlock {
    PageOffset next;
    uint16_t size;
};

struct BlockPage {
    TxId last_modified_txid;
    PageOffset first;
    uint16_t pedding;
    uint8_t data[1];
};

struct BlockRecord {
    PageId pgid;
    uint16_t max_free_size;
};

struct BlockInfo {
    PageId record_pgid;
    TxId last_modified_txid;
    uint16_t record_count;
    uint16_t record_index;
    PageOffset record_offset;
    uint16_t pedding;
};

/*
* ���˳����£�ÿһ��span����Ҫ����һҳ���洢
* ��ÿ��leaf_element����2��span��leaf_element_size��12�ֽ�
* 
* �豣֤һҳ�ܹ�װ��record_arr
* Ҫ�� node_size >= leaf_element_size(12) * 2 = 24 (4��span��4��record)
* ������ block_page_header_size(8) + record_arr_page(6) = 14 �� 16 ��ʹ�õĿռ� (3��record)
*/

#pragma pack(pop)

} // namespace yudb