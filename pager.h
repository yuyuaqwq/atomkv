#pragma once

#include <memory>
#include <map>
#include <vector>

#include "noncopyable.h"
#include "page_reference.h"
#include "tx_format.h"
#include "cache_manager.h"

namespace yudb {

class DBImpl;
class UpdateTx;

class Pager : noncopyable {
public:
    Pager(DBImpl* db, PageSize page_size);
    ~Pager() = default;

    /*
    * ���̰߳�ȫ����д����ʹ��
    */
    void Read(PageId pgid, void* cache, PageCount count);
    void Write(PageId pgid, const void* cache, PageCount count);
    void SyncAllPage();

    PageId Alloc(PageCount count);
    void Free(PageId pgid, PageCount count);
    void RollbackPending();
    void CommitPending();
    void ClearPending(TxId min_view_txid);
    PageReference Copy(const PageReference& page_ref);
    PageReference Copy(PageId pgid);

    // �̰߳�ȫ
    PageReference Reference(PageId pgid, bool dirty);
    void Dereference(uint8_t* page_cache);
    PageId CacheToPageId(uint8_t* page_cache);

    auto& page_size() const { return page_size_; }

private:
    DBImpl* const db_;
    const PageSize page_size_;
    CacheManager cache_manager_;
    std::map<TxId, std::vector<std::pair<PageId, PageCount>>> pending_;
    
};

} // namespace yudb