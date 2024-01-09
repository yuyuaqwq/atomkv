#include "metaer.h"

#include "version.h"
#include "db.h"
#include "crc32.h"

namespace yudb {

bool Metaer::Load() {
    db_->file_.Seek(0, File::PointerMode::kDbFilePointerSet);
    auto success = db_->file_.Read(&meta_, sizeof(meta_));
    if (!success) {
        // Initialize Meta Information
        meta_.sign = YUDB_SIGN;
        meta_.min_version = YUDB_VERSION;
        meta_.page_size = kPageSize;
        meta_.page_count = 2;
        meta_.txid = 1;
        meta_.root = kPageInvalidId;
        Crc32 crc32;
        crc32.Append(&meta_, kMetaSize - sizeof(uint32_t));
        auto crc32_value = crc32.End();
        meta_.crc32 = crc32_value;

        db_->file_.Seek(0, File::PointerMode::kDbFilePointerSet);
        db_->file_.Write(&meta_, kMetaSize);

        meta_.txid = 0;
        db_->file_.Seek(kPageSize, File::PointerMode::kDbFilePointerSet);
        db_->file_.Write(&meta_, kMetaSize);

        meta_.txid = 1;
        return true;
    }

    // Check Meta information
    Meta meta_list[2];
    std::memcpy(&meta_list[0], &meta_, kMetaSize);

    db_->file_.Seek(kPageSize, File::PointerMode::kDbFilePointerSet);
    if (db_->file_.Read(&meta_list[1], kMetaSize) != kMetaSize) {
        return false;
    }
    if (meta_list[0].sign != YUDB_SIGN && meta_list[1].sign != YUDB_SIGN) {
        return false;
    }
    if (YUDB_VERSION < meta_list[0].min_version) {
        return false;
    }

    // ѡ�����µĳ־û��汾Ԫ��Ϣ
    meta_index_ = 0;
    if (meta_list[0].txid < meta_list[1].txid) {
        meta_index_ = 1;
    }

    // У��Ԫ��Ϣ�Ƿ���������������ʹ����һ��
    Crc32 crc32;
    crc32.Append(&meta_list[meta_index_], kMetaSize - sizeof(uint32_t));
    auto crc32_value = crc32.End();
    if (crc32_value != meta_list[meta_index_].crc32) {
        if (meta_index_ == 1) {
            return false;
        }
        
        crc32.Append(&meta_list[1], kMetaSize - sizeof(uint32_t));
        crc32_value = crc32.End();
        if (crc32_value != meta_list[1].crc32) {
            return false;
        }
    }

    // ҳ��ߴ粻ƥ���������
    if (meta_list[meta_index_].page_size != kPageSize) {
        return false;
    }
    std::memcpy(&meta_, &meta_list[meta_index_], kMetaSize);
    return true;
}

void Metaer::Save() {
    Crc32 crc32;
    crc32.Append(&meta_, kMetaSize - sizeof(uint32_t));
    meta_.crc32 = crc32.End();
    db_->file_.Seek(meta_index_ * kPageSize, File::PointerMode::kDbFilePointerSet);
    db_->file_.Write(&meta_, kMetaSize);
}

} // namespace yudb