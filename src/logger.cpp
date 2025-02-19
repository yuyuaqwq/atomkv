//The MIT License(MIT)
//Copyright © 2024 https://github.com/yuyuaqwq
//
//Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and /or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :
//
//The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "logger.h"

#include <wal/reader.h>

#include <atomkv/tx.h>

#include "db_impl.h"

namespace atomkv{

Logger::Logger(DBImpl* db, std::string_view log_path)
    :  db_(db)
    , log_path_(log_path)
{
    writer_.Open(log_path, db_->options()->sync ? tinyio::access_mode::sync_needed : tinyio::access_mode::write);
}

Logger::~Logger() {
    if (!db_->options()->read_only) {
        auto tx = db_->Update();
        Checkpoint();
        tx.Commit();
        writer_.Close();
        std::filesystem::remove(log_path_);
    }
}

void Logger::AppendLog(const std::span<const uint8_t>* begin, const std::span<const uint8_t>* end) {
    if (disable_writing_) return;
    for (auto it = begin; it != end; ++it) {
        writer_.AppendRecordToBuffer(*it);
    }
    if (!checkpoint_needed_ && writer_.size() >= db_->options()->max_wal_size) {
        checkpoint_needed_ = true;
    }
}

void Logger::FlushLog() {
    if (disable_writing_) return;
    writer_.FlushBuffer();
    if (db_->options()->sync) {
        writer_.Sync();
    }
}

void Logger::Reset() {
    writer_.Close();
    writer_.Open(log_path_, db_->options()->sync ? tinyio::access_mode::sync_needed : tinyio::access_mode::write);
}

bool Logger::RecoverNeeded() {
    return writer_.file().size() > 0;
}

void Logger::Recover() {
    disable_writing_ = true;
    wal::Reader reader;
    reader.Open(log_path_);
    std::optional<UpdateTx> current_tx;
    bool end = false, init = false;
    auto& meta = db_->meta();
    auto& pager = db_->pager();
    auto& tx_manager = db_->tx_manager();
    auto& raw_txid = meta.meta_struct().txid;
    do {
        if (end) {
            break;
        }

        auto record = reader.ReadRecord();
        if (!record
            || record->size() < sizeof(LogType)) {
            break;
        }

        auto type = *reinterpret_cast<LogType*>(record->data());
        switch (type) {
        case LogType::kWalTxId: {
            if (init) {
                throw std::runtime_error("Unrecoverable logs.");
            }
            auto log = reinterpret_cast<WalTxIdLogHeader*>(record->data());
            if (meta.meta_struct().txid > log->txid) {
                end = true;
            }
            init = true;
            break;
        }
        case LogType::kBegin: {
            if (!init) {
                throw std::runtime_error("unrecoverable logs.");
            }
            if (current_tx.has_value()) {
                throw std::runtime_error("unrecoverable logs.");
            }
            current_tx.emplace(tx_manager.Update());
            break;
        }
        case LogType::kRollback: {
            if (!init) {
                throw std::runtime_error("unrecoverable logs.");
            }
            if (!current_tx.has_value()) {
                throw std::runtime_error("unrecoverable logs.");
            }
            current_tx->RollBack();
            current_tx = std::nullopt;
            break;
        }
        case LogType::kCommit: {
            if (!init) {
                throw std::runtime_error("unrecoverable logs.");
            }
            if (!current_tx.has_value()) {
                throw std::runtime_error("unrecoverable logs.");
            }
            current_tx->Commit();
            current_tx = std::nullopt;
            break;
        }
        case LogType::kSubBucket: {
            if (!init) {
                throw std::runtime_error("unrecoverable logs.");
            }
            assert(record->size() == kBucketDeleteLogHeaderSize);
            auto log = reinterpret_cast<BucketLogHeader*>(record->data());
            auto& tx = tx_manager.update_tx();
            BucketImpl* bucket;
            if (log->bucket_id == kUserRootBucketId) {
                bucket = &tx.user_bucket();
            } else {
                bucket = &tx.AtSubBucket(log->bucket_id);
            }
            auto key = reader.ReadRecord();
            if (!key) {
                end = true;
                break;
            }
            bucket->SubBucket(key->data(), true);
            break;
        }
        case LogType::kPut_IsBucket:
        case LogType::kPut_NotBucket: {
            if (!init) {
                throw std::runtime_error("unrecoverable logs.");
            }
            assert(record->size() == kBucketPutLogHeaderSize);
            auto log = reinterpret_cast<BucketLogHeader*>(record->data());
            auto& tx = tx_manager.update_tx();
            BucketImpl* bucket;
            if (log->bucket_id == kUserRootBucketId) {
                bucket = &tx.user_bucket();
            } else {
                bucket = &tx.AtSubBucket(log->bucket_id);
            }
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
            bucket->Put(key->data(), key->size(), value->data(), value->size(), type == LogType::kPut_IsBucket);
            break;
        }
        case LogType::kDelete: {
            if (!init) {
                throw std::runtime_error("unrecoverable logs.");
            }
            assert(record->size() == kBucketDeleteLogHeaderSize);
            auto log = reinterpret_cast<BucketLogHeader*>(record->data());
            auto& tx = tx_manager.update_tx();
            BucketImpl* bucket;
            if (log->bucket_id == kUserRootBucketId) {
                bucket = &tx.user_bucket();
            }
            else {
                bucket = &tx.AtSubBucket(log->bucket_id);
            }
            auto key = reader.ReadRecord();
            if (!key) {
                end = true;
                break;
            }
            bucket->Delete(key->data(), key->size());
            break;
        }
        }
    } while (true);
    disable_writing_ = false;
    pager.WriteAllDirtyPages();
    if (current_tx.has_value()) {
        // Incomplete log record, discard the last transaction
        current_tx->RollBack();
    }
    if (meta.meta_struct().txid > raw_txid) {
        std::error_code ec;
        db_->db_file_mmap().sync(ec);
        if (ec) {
            throw std::system_error(ec, "Failed to sync db file.");
        }
        meta.Switch();
        meta.Save();
        tx_manager.set_persisted_txid(meta.meta_struct().txid);
    }
}

void Logger::Checkpoint() {
    auto& meta = db_->meta();
    auto& pager = db_->pager();
    auto& tx_manager = db_->tx_manager();
    if (!tx_manager.has_update_tx()) {
        throw std::runtime_error("Checkpoint can only be invoked within a write transaction.");
    }

    pager.SaveFreeList();
    pager.WriteAllDirtyPages();

    meta.Switch();
    meta.Save();

    tx_manager.set_persisted_txid(meta.meta_struct().txid);

    Reset();

    AppendWalTxIdLog();

    if (checkpoint_needed_) checkpoint_needed_ = false;
}

void Logger::AppendWalTxIdLog() {
    WalTxIdLogHeader log;
    log.type = LogType::kWalTxId;
    log.txid = db_->meta().meta_struct().txid;
    std::span<const uint8_t> arr[1];
    arr[0] = { reinterpret_cast<const uint8_t*>(&log), sizeof(WalTxIdLogHeader) };
    AppendLog(std::begin(arr), std::end(arr));
}

} // namespace atomkv