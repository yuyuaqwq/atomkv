#include "overflower.h"

#include "noder.h"
#include "pager.h"

namespace yudb {

using Element = Overflow::Element;

std::optional<std::pair<PageId, PageOffset>> Overflower::Alloc(PageSize size, bool alloc_new_page) {
    auto pager = noder_->pager_;
    auto ref = pager->Reference(overflow_->pgid);

    auto overflow_arr = reinterpret_cast<Element*>(ref.page_cache()[overflow_->offset]);
    for (uint16_t i = 0; i < overflow_->element_count; i++) {
        if (overflow_arr[i].max_free_size >= size) {
            auto ref = pager->Reference(overflow_arr[i].pgid);
            ref.page_cache()[overflow_arr[i].free_list];

            return std::pair{ overflow_arr[i].pgid, 0 };
        }
    }
    if (!alloc_new_page) return {};

    PageOffset ret_offset = 0;

    // ��ǰ��overflow������û���㹻����Ŀռ䣬������ҳ
    auto new_pgid = pager->Alloc(1);
    auto new_ref = pager->Reference(new_pgid);

    // ����չoverflow����
    auto new_overflow_arr_block_size = sizeof(Element) * ++overflow_->element_count;
    auto new_overflow_arr_block = Alloc(new_overflow_arr_block_size, false);
    if (!new_overflow_arr_block) {
        // ��ǰ�Ŀ���overflow�ռ�Ҳ��������չoverflow�����ˣ�ʹ���·����ҳ���
        Free(std::pair{ overflow_->pgid, overflow_->offset });
        overflow_->pgid = new_pgid;
        overflow_->offset = 0;

        memcpy(new_ref.page_cache(), overflow_arr, new_overflow_arr_block_size - sizeof(Element));
        overflow_arr = reinterpret_cast<Element*>(new_ref.page_cache());
        auto& tail_overflow_ele = overflow_arr[overflow_->element_count];
        tail_overflow_ele.Init(
            new_pgid,
            pager->page_size() - new_overflow_arr_block_size,
            new_overflow_arr_block_size
        );

        // ��������overflow����󣬲����Է��������ݣ����ٴδ�����ҳ
        if (tail_overflow_ele.max_free_size < size) {
            tail_overflow_ele.max_free_size -= sizeof(Element);
            tail_overflow_ele.free_list += sizeof(Element);

            new_pgid = pager->Alloc(1);
            auto new_ref = pager->Reference(new_pgid);

            // ����ԭ����չ
            overflow_arr[++overflow_->element_count].Init(
                new_pgid,
                pager->page_size() - size,
                size
            );

        }
        else {
            ret_offset = overflow_arr[overflow_->element_count].free_list;
        }
    }
    return std::pair{ new_pgid, ret_offset };
}

} // namespace yudb