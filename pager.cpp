#include "pager.h"

#include "db.h"
#include "noder.h"
#include "tx.h"

namespace yudb {

void Pager::Read(PageId pgid, void* cache, PageCount count) {
    db_->file_.Seek(pgid * page_size());
    auto read_size = db_->file_.Read(cache, count * page_size());
    assert(read_size == 0 || read_size == count * page_size());
    if (read_size == 0) {
        memset(cache, 0, count * page_size());
    }
}

void Pager::Write(PageId pgid, void* cache, PageCount count) {
    db_->file_.Seek(pgid * page_size());
    db_->file_.Write(cache, count * page_size());
}


PageId Pager::Alloc(PageCount count) {
    auto update_tx = db_->txer_.update_tx();

    PageId pgid = update_tx->meta().page_count;
    update_tx->meta().page_count += count;

    auto [cache_info, page_cache] = cacher_.Reference(pgid);
    cache_info->dirty = true;

    Noder noder{ &update_tx->RootBucket().btree_, pgid};
    noder.node()->last_modified_txid = update_tx->txid();

    cacher_.Dereference(page_cache);
    return pgid;
}

void Pager::Free(PageId pgid, PageCount count) {
    // ����pending��pending��ҳ����map�д��
    // ��������д����ʱ�������в����ܱ��������ȡ��pendingҳ��д�뵽��ǰд�����freedb��

    // span��cow�������
    // block�Ļ������������spanָ��ͬһ��block����block��free��ʱ�򴥷�copy

    std::cout << "free" << pgid << std::endl;
}

} // namespace yudb