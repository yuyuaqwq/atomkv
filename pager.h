#pragma once

#include <memory>
#include <map>
#include <vector>

#include "noncopyable.h"
#include "page.h"
#include "tx_format.h"
#include "cache_manager.h"

namespace yudb {

class DBImpl;
class UpdateTx;

class Pager : noncopyable {
public:
    Pager(DBImpl* db, PageSize page_size);
    ~Pager();

    // ���̰߳�ȫ��������д����ʹ��
    void Read(PageId pgid, void* cache, PageCount count);
    void Write(PageId pgid, const void* cache, PageCount count);
    void SyncAllPage();

    PageId Alloc(PageCount count);
    void Free(PageId pgid, PageCount count);
    void RollbackPending();
    void CommitPending();
    void ClearPending(TxId min_view_txid);
    Page Copy(const Page& page_ref);
    Page Copy(PageId pgid);

    // �̰߳�ȫ����
    Page Reference(PageId pgid, bool dirty);
    Page AddReference(uint8_t* page_cache);
    void Dereference(const uint8_t* page_cache);
    PageId GetPageIdByCache(const uint8_t* page_cache);

    auto& db() const { return *db_; }
    auto& page_size() const { return page_size_; }
    auto& tmp_page() { return tmp_page_; }

private:
    DBImpl* const db_;
    const PageSize page_size_;
    CacheManager cache_manager_;
    std::map<TxId, std::vector<std::pair<PageId, PageCount>>> pending_;

    uint8_t* tmp_page_;
};

} // namespace yudb