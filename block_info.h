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

struct BlockPage {
    TxId last_modified_txid;
    PageSpace page_space;
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
    uint16_t record_index;
    uint16_t record_count;
};

/*
* ���˳����£�ÿһ��span����Ҫ����һҳ���洢
* ��ÿ��leaf_element����2��span��leaf_element_size��12�ֽ�
* 
* �豣֤һҳ�ܹ�װ��record_arr
* Ҫ�� node_size >= leaf_element_size(12) * 2 = 24 (4��span��4��record)
* ������ block_page_header_size(16) + record_arr_page(6) = 22 ��ʹ�õĿռ� (4��record)
*/

#pragma pack(pop)

} // namespace yudb