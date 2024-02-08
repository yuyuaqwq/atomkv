#include "block_manager.h"

#include <format>

#include "node.h"
#include "pager.h"

#include "btree.h"
#include "tx_impl.h"

#include <windows.h>
#undef min

namespace yudb {

PageSize BlockManager::MaxSize() const {
    auto& pager = node_->btree().bucket().pager();
    auto size = pager.page_size() - (sizeof(BlockPageFormat) - sizeof(BlockPageFormat::block_table));
    return size;
}

std::pair<const uint8_t*, PageReference> BlockManager::Load(uint16_t index, PageOffset pos) {
    auto& pager = node_->btree().bucket().pager();
    ImmBlockPage block_page{ this, descriptor_->pgid, descriptor_->index_of_table };
    ImmBlockPage data_block_page{ this, block_page.block_table()[index].pgid, &block_page.block_table()[index] };
    return { data_block_page.content(pos), data_block_page.ReleasePageReference() };
}

std::pair<uint8_t*, PageReference> BlockManager::MutLoad(uint16_t index, PageOffset pos) {
    auto& pager = node_->btree().bucket().pager();
    MutBlockPage block_page{ this, descriptor_->pgid, descriptor_->index_of_table };
    MutBlockPage data_block_page{ this, block_page.block_table()[index].pgid, &block_page.block_table()[index] };
    return { data_block_page.content(pos), data_block_page.ReleasePageReference() };
}

std::optional<std::pair<uint16_t, PageOffset>> BlockManager::Alloc(PageSize size) {
    auto& pager = node_->btree().bucket().pager();
    assert(size <= MaxSize());

    if (descriptor_->pgid == kPageInvalidId) {
        Build();
    }
    CopyPageOfBlockTable();

    MutBlockPage page_of_table{ this, descriptor_->pgid, descriptor_->index_of_table };
    auto block_table = page_of_table.block_table();
    for (uint16_t i = 0; i < descriptor_->count; i++) {
        auto& entry = block_table[i];
        if (entry.pgid == descriptor_->pgid) {
            // ��Table���ڵ�ҳ�棬����ռ䲻�����Է���
            auto& arena = page_of_table.arena();
            if (arena.rest_size() > 0 && entry.max_free_size < size) {
                auto alloc_size = std::min(size, arena.rest_size());
                auto res = arena.AllocRight(alloc_size);
                assert(res.has_value());
                page_of_table.Free(*res, alloc_size);
            }
        }
        if (entry.max_free_size >= size) {
            MutBlockPage block_page{ this, entry.pgid, &block_table[i] };
            if (entry.need_rebuild_page == 0 && block_page.fragment_size() > MaxFragmentSize()) {
                descriptor_->need_rebuild_page = 1;
                entry.need_rebuild_page = 1;
            }
            auto pos = block_page.Alloc(size);
            return std::pair{ i, pos };
        }
    }

    // û���㹻����Ŀռ䣬������ҳ
    AppendPage(&page_of_table);
    return Alloc(size);
}
void BlockManager::Free(const std::tuple<uint16_t, PageOffset, PageSize>& free_block) {
    CopyPageOfBlockTable();
    auto& pager = node_->btree().bucket().pager();
    auto [index, free_pos, free_size] = free_block;

    MutBlockPage table_block_page{ this, descriptor_->pgid, descriptor_->index_of_table };
    auto& entry = table_block_page.block_table()[index];
    MutBlockPage block_page{ this, entry.pgid, &entry };
    block_page.Free(free_pos, free_size);
}

void BlockManager::Print() {
    auto& pager = node_->btree().bucket().pager();
    ImmBlockPage table_page{ this, descriptor_->pgid , descriptor_->index_of_table };
    auto table = table_page.block_table();
    for (uint16_t i = 0; i < descriptor_->count; i++) {
        ImmBlockPage page{ this, table[i].pgid, &table[i] };

        auto j = 0;
        auto prev_pos = kPageInvalidOffset;
        auto cur_pos = page.format().first_block_pos;
        while (cur_pos != kPageInvalidOffset) {
            assert(cur_pos < pager.page_size());
            auto free_block = reinterpret_cast<const FreeBlock*>(&page.format().page[cur_pos]);
            std::cout << std::format("[FreeBlock][pgid:{}][pos:{}]:{}", table[i].pgid, cur_pos, free_block->size) << std::endl;
            prev_pos = cur_pos;
            cur_pos = free_block->next;
            ++j;
        }
        if (j == 0) {
            std::cout << std::format("[FreeBlock][pgid:{}]:null", table[i].pgid) << std::endl;
        }
    }
}

void BlockManager::Build() {
    auto& pager = node_->btree().bucket().pager();

    descriptor_->pgid = pager.Alloc(1);
    descriptor_->need_rebuild_page = 0;
    descriptor_->index_of_table = 0;
    descriptor_->count = 1;

    MutBlockPage page_of_table{ this, descriptor_->pgid, nullptr };
    page_of_table.Build();
    BuildTableEntry(&page_of_table.block_table()[0], &page_of_table);
}
void BlockManager::Clear() {
    if (descriptor_->pgid == kPageInvalidId) {
        return;
    }
    CopyPageOfBlockTable();

    auto& pager = node_->btree().bucket().pager();
    auto page_ref = pager.Reference(descriptor_->pgid, true);
    auto& block_page = page_ref.content<BlockPageFormat>();
    for (uint16_t i = 0; i < descriptor_->count; i++) {
        pager.Free(block_page.block_table[i].pgid, 1);
    }
    descriptor_->pgid = kPageInvalidId;
}

void BlockManager::TryDefragmentSpace() {
    auto& pager = node_->btree().bucket().pager();
    if (descriptor_->need_rebuild_page == 0 || descriptor_->pgid == kPageInvalidId) {
        return;
    }
    MutBlockPage page_of_table{ this, descriptor_->pgid, descriptor_->index_of_table };
    auto block_table = page_of_table.block_table();
    for (uint16_t i = 0; i < descriptor_->count; i++) {
        auto& entry = block_table[i];
        if (entry.need_rebuild_page == 1) {
            MutBlockPage page{ this, entry.pgid, &entry };
            node_->DefragmentSpace(i);
            entry.need_rebuild_page = 0;
        }
    }
    descriptor_->need_rebuild_page = 0;
}
BlockPage BlockManager::PageRebuildBegin(uint16_t index) {
    auto& pager = node_->btree().bucket().pager();
    auto pgid = pager.Alloc(1);
    MutBlockPage page{ this, pgid, nullptr };
    page.Build();
    return BlockPage{ this, page.ReleasePageReference(), nullptr };
}
PageOffset BlockManager::PageRebuildAppend(BlockPage* new_page, const std::tuple<uint16_t, PageOffset, uint16_t>& block) {
    auto& pager = node_->btree().bucket().pager();
    auto [index, pos, size] = block;
    auto [src_buff, src_ref] = Load(index, pos);
    auto new_pos = new_page->arena().AllocRight(size);
    assert(new_pos.has_value());
    auto dst_buff = new_page->Load(*new_pos);
    std::memcpy(dst_buff, src_buff, size);
    return *new_pos;
}
void BlockManager::PageRebuildEnd(BlockPage* new_page, uint16_t index) {
    auto& pager = node_->btree().bucket().pager();
    MutBlockPage page_of_table{ this, descriptor_->pgid, descriptor_->index_of_table };
    BlockTableEntry* table = page_of_table.block_table();
    if (index == descriptor_->index_of_table) {
        auto byte_size = sizeof(BlockTableEntry) * descriptor_->count;
        auto res = new_page->arena().AllocLeft(byte_size);
        assert(res.has_value());
        std::memcpy(new_page->block_table(), page_of_table.block_table(), byte_size);
        table = new_page->block_table();
        descriptor_->pgid = new_page->page_id();
    }
    auto old_pgid = table[index].pgid;
    auto& entry = table[index];
    entry.pgid = new_page->page_id();
    if (index == descriptor_->index_of_table) {
        entry.max_free_size = 0;
    } else {
        auto& arena = new_page->arena();
        auto rest_size = arena.rest_size();
        auto pos = arena.AllocLeft(rest_size); assert(pos.has_value());
        new_page->set_table_entry(&entry);
        new_page->Free(*pos, rest_size);
    }
    pager.Free(old_pgid, 1);
}

void BlockManager::BuildTableEntry(BlockTableEntry* entry, BlockPage* block_page) {
    auto& pager = node_->btree().bucket().pager();
    block_page->set_table_entry(entry);
    entry->pgid = block_page->page_id();
    entry->max_free_size = 0;
    auto& arena = block_page->arena();
    if (entry->pgid == descriptor_->pgid) {
        arena.AllocLeft(sizeof(BlockTableEntry));
        return;
    }
    auto rest_size = arena.rest_size();
    auto pos = arena.AllocLeft(rest_size); assert(pos.has_value());
    block_page->Free(*pos, rest_size);
}
void BlockManager::CopyPageOfBlockTable() {
    MutBlockPage block_page{ this, descriptor_->pgid, descriptor_->index_of_table };
    if (block_page.Copy()) {
        descriptor_->pgid = block_page.page_id();
    }
}
void BlockManager::AppendPage(BlockPage* page_of_table) {
    assert(sizeof(BlockTableEntry) * (descriptor_->count + 1) <= MaxSize());
    auto& pager = node_->btree().bucket().pager();

    BlockTableEntry* table = page_of_table->block_table();
    auto& arena_of_table = page_of_table->arena();

    auto new_pgid = pager.Alloc(1);
    MutBlockPage new_page{this, new_pgid, nullptr }; // new_block_table[block_table_descriptor_->count]
    new_page.Build();
    if (!arena_of_table.AllocLeft(sizeof(BlockTableEntry))) {
        // ����ʧ�ܵĻ�����λ��Left�����Table�ƶ�����ҳ��
        BlockTableEntry* new_table = new_page.block_table();
        auto byte_size = sizeof(BlockTableEntry) * descriptor_->count;
        std::memcpy(new_table, table, byte_size);

        // �ͷ�Left���䣬���䵽Right����
        arena_of_table.FreeLeft(byte_size);
        auto rest_size = arena_of_table.rest_size();
        auto res = arena_of_table.AllocRight(rest_size);
        assert(res.has_value());

        // Freeʹ���µ�ҳ��
        auto old_index = descriptor_->index_of_table;
        page_of_table->set_table_entry(&new_table[old_index]);
        descriptor_->pgid = new_pgid;
        descriptor_->index_of_table = descriptor_->count;
        page_of_table->Free(*res, rest_size);
        new_page.arena().AllocLeft(sizeof(BlockTableEntry) * descriptor_->count);
        BuildTableEntry(&new_table[descriptor_->count], &new_page);
    }
    else {
        BuildTableEntry(&table[descriptor_->count], &new_page);
    }
    ++descriptor_->count;
}
PageSize BlockManager::MaxFragmentSize() {
    auto& pager = node_->btree().bucket().pager();
    return pager.page_size() / 16;
}

} // namespace yudb