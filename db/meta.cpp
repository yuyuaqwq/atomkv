#include "yudb/meta.h"

#include "yudb/version.h"
#include "yudb/db_impl.h"
#include "yudb/crc32.h"

namespace yudb {

Meta::Meta(DBImpl* db) : db_{ db } {};

Meta::~Meta() = default;

bool Meta::Load() {
    auto ptr = db_->db_file_mmap().data();


    // У�����Ԫ��Ϣ
    auto first = reinterpret_cast<MetaStruct*>(ptr);
    auto second = reinterpret_cast<MetaStruct*>(ptr + db_->pager().page_size());

    if (first->sign != YUDB_SIGN && second->sign != YUDB_SIGN) {
        return false;
    }
    if (YUDB_VERSION < first->min_version) {
        return false;
    }

    // ����ѡ���°汾
    cur_meta_index_ = 0;
    if (first->txid < second->txid) {
        cur_meta_index_ = 1;
        meta_struct_ = second;
    } else {
        meta_struct_ = first;
    }

    // У��Ԫ��Ϣ�Ƿ���������������ʹ����һ��
    Crc32 crc32;
    crc32.Append(meta_struct_, kMetaSize - sizeof(uint32_t));
    auto crc32_value = crc32.End();
    if (crc32_value != meta_struct_->crc32) {
        if (cur_meta_index_ == 1) {
            cur_meta_index_ = 0;
            meta_struct_ = first;
        } else {
            cur_meta_index_ = 1;
            meta_struct_ = second;
        }
        crc32.Append(meta_struct_, kMetaSize - sizeof(uint32_t));
        crc32_value = crc32.End();
        if (crc32_value != meta_struct_->crc32) {
            return false;
        }
    }

    // ҳ��ߴ�Ҫ��һ��
    if (meta_struct_->page_size != db_->options()->page_size) {
        return false;
    }

    return true;
}

void Meta::Save() {
    Crc32 crc32;
    crc32.Append(meta_struct_, kMetaSize - sizeof(uint32_t));
    meta_struct_->crc32 = crc32.End();
}

void Meta::Switch() { 
    cur_meta_index_ = cur_meta_index_ == 0 ? 1 : 0;
}

void Meta::Set(const MetaStruct& meta_struct) {
    CopyMetaInfo(meta_struct_, meta_struct);
}

void Meta::Get(MetaStruct* meta_struct) {
    CopyMetaInfo(meta_struct, *meta_struct_);
}

} // namespace yudb