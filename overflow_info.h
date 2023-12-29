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

struct OverflowPage {
    TxId last_modified_txid;
    PageOffset first;
    uint16_t pedding;
    uint8_t data[];
};

struct OverflowRecord {
    PageId pgid;
    uint16_t max_free_size;
};

struct OverflowInfo {
    PageId record_pgid;
    TxId last_modified_txid;
    PageOffset record_offset;
    uint16_t record_index;
    uint16_t record_count;
    uint32_t pedding;
};

/*
* ���˳����£�ÿһ��span����Ҫ����һҳ���洢
* ��ÿ��leaf_element����2��span��leaf_element��12�ֽ�

* record_count = (page_size - overflow_page_header_size) / recoud_size
* record������Ҫһҳ���棬��� record_count -= 1
* (page_size - node_header_size) / leaf_element_size * 2 <= record_count
* 
* Ҫ���ɱ�֤һҳ�ܹ�װ��record_arr
* ��Ҫ��֤ noder_size >= leaf_element_size * 2 (���ռ��4ҳ����4��record)
* ������ overflow_page_header_size(8) + record_arr_page(6) ��ռ�õĿռ�(3��record)
*/

#pragma pack(pop)

} // namespace yudb