#include "pager.h"

#include "db.h"

namespace yudb {

void Pager::Read(PageId pgid, void* cache, PageCount count) {
    if (!db_->file_.Read(cache, count * page_size())) {
        memset(cache, 0, count * page_size());
    }
}

} // namespace yudb