#pragma once

#include "page.h"
#include "span.h"
#include "overflow.h"

namespace yudb {

#pragma pack(push, 1)
struct Node {
    enum class Type : uint16_t {
        kInvalid = 0,
        kBranch,
        kLeaf,
    };

    struct BranchElement {
        Span key;
        PageId little_child;
    };

    struct LeafElement {
        Span key;
        Span value;
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