#include "yudb/db_impl.h"

#include <fstream>
#include <filesystem>

#include "yudb/log_reader.h"
#include "yudb/operator_log_format.h"
#include "yudb/error.h"

namespace yudb{

namespace fs = std::filesystem;

 DB::~DB() = default;

 std::unique_ptr<DB> DB::Open(const Options& options, std::string_view path) {
     auto db = std::make_unique<DBImpl>();
     db->options_.emplace(options);

     if (!fs::exists(path)) {
         std::fstream fs;
         fs.open(path, std::ios_base::out | std::ios_base::binary);
         if (!fs.is_open()) {
             return {};
         }
     }

     if (!db->file().Open(path, false)) {
         return {};
     }
     if (!db->meta().Load()) {
         return {};
     }
     db->pager_.emplace(db.get(), db->meta().meta_format().page_size);
     std::string log_path = path.data();
     log_path += "-wal";

     namespace fs = std::filesystem;
     std::error_code error;
     auto file_status = fs::status(path, error);
     if (!error) {
         if (fs::exists(file_status)) {
             db->Recover(log_path);
             fs::remove(log_path);
         }
     }
     db->log_writer().Open(log_path);
     return db;
 }


 DBImpl::~DBImpl() {
     if (pager_.has_value()) {
         pager_->WriteAllDirtyPages();
         meta_.Switch();
         meta_.Save();
         file_.Sync();
         log_writer_.Close();
         std::filesystem::remove(log_writer_.path());
     }
 }

 UpdateTx DBImpl::Update() {
     return tx_manager_.Update();
 }

 ViewTx DBImpl::View() {
     return tx_manager_.View();
 }

void DBImpl::Recover(std::string_view path) {
    // 实际上的恢复过程还需要判断是否已经进行了恢复但中途又发生了崩溃
    recovering_ = true;
    yudb::log::Reader reader;
    reader.Open(path);
    std::optional<UpdateTx> current_tx;
    do {
        auto record = reader.ReadRecord();
        if (!record) {
            break;
        }
        if (record->size() < sizeof(OperationType)) {
            break;
        }
        bool end = false;
        auto type = *reinterpret_cast<OperationType*>(record->data());
        switch (type) {
        case OperationType::kBegin: {
            if (current_tx.has_value()) {
                throw RecoverError{ "abnormal logging." };
            }
            current_tx = Update();
            break;
        }
        case OperationType::kRollback: {
            if (!current_tx.has_value()) {
                throw RecoverError{ "abnormal logging." };
            }
            current_tx->RollBack();
            current_tx = std::nullopt;
            break;
        }
        case OperationType::kCommit: {
            if (!current_tx.has_value()) {
                throw RecoverError{ "abnormal logging." };
            }
            current_tx->Commit();
            current_tx = std::nullopt;
            break;
        }
        case OperationType::kPut: {
            assert(record->size() == kBucketPutLogHeaderSize);
            auto format = reinterpret_cast<BucketLogHeader*>(record->data());
            auto& tx = tx_manager_.update_tx();
            auto& bucket = tx.AtSubBucket(format->bucket_id);
            auto key = reader.ReadRecord();
            if (!key) {
                end = true;
                break;
            }
            auto value = reader.ReadRecord();
            if (!value) {
                end = true;
                break;
            }
            bucket.Put(key->data(), key->size(), value->data(), value->size());
            break;
        }
        case OperationType::kDelete: {
            assert(record->size() == kBucketDeleteLogHeaderSize);
            auto format = reinterpret_cast<BucketLogHeader*>(record->data());
            auto& tx = tx_manager_.update_tx();
            auto& bucket = tx.AtSubBucket(format->bucket_id);
            auto key = reader.ReadRecord();
            if (!key) {
                end = true;
                break;
            }
            bucket.Delete(key->data(), key->size());
            break;
        }
        }
    } while (true);
    recovering_ = false;
    pager_->WriteAllDirtyPages();
    if (current_tx.has_value()) {
        // 不完整的日志记录，丢弃最后的事务
        current_tx->RollBack();
    }
    meta_.Save();
    file_.Sync();
}

void DBImpl::Checkpoint() {
    if (!tx_manager_.has_update_tx()) {
        throw CheckpointError{ "checkpoint execution is not allowed when there is a write transaction." };
    }

    pager_->UpdateFreeList();
    auto& tx = tx_manager_.update_tx();
    CopyMetaInfo(&meta_.meta_format(), tx.meta_format());

    pager_->WriteAllDirtyPages();
    meta_.Switch();
    meta_.Save();
    file_.Sync();

    log_writer_.Reset();
}

} // namespace yudb