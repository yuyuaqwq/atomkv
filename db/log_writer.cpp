#include "yudb/log_writer.h"

#include "yudb/crc32.h"
#include "yudb/error.h"

namespace yudb {
namespace log {

Writer::Writer() = default;
Writer::~Writer() = default;

void Writer::Open(std::string_view path) {
    std::error_code error_code;
    file_.open(path, tinyio::access_mode::write, error_code);
    if (error_code) {
        throw IoError{ "failed to open log file." };
    }
    path_ = path;
    file_.seekg(0, error_code);
    if (error_code) {
        throw IoError{ "failed to seekg log file." };
    }
    rep_.reserve(kBlockSize);
}

void Writer::Close() {
    file_.close();
    block_offset_ = 0;
    size_ = 0;
}

void Writer::Reset() {
    Close();
    std::filesystem::remove(path_);
    Open(path_);
}

void Writer::AppendRecordToBuffer(std::span<const uint8_t> data) {
    auto length = kHeaderSize + data.size();
    size_ += length;
    if (block_offset_ + length > kBlockSize) {
        WriteBuffer();
        AppendRecord(data);
        return;
    }
    LogRecord record {
        .type = RecordType::kFullType,
    };
    record.size = data.size();

    Crc32 crc32;
    crc32.Append(&record.size, kHeaderSize - sizeof(record.checksum));
    crc32.Append(data.data(), data.size());
    record.checksum = crc32.End();

    auto raw_size = rep_.size();
    rep_.resize(raw_size + kHeaderSize);
    std::memcpy(&rep_[raw_size], &record, kHeaderSize);
    raw_size = rep_.size();
    if (data.size() > 0) {
        rep_.resize(raw_size + data.size());
        std::memcpy(&rep_[raw_size], data.data(), data.size());
    }
    block_offset_ += length;
}

void Writer::AppendRecordToBuffer(std::string_view data) {
    AppendRecordToBuffer({ reinterpret_cast<const uint8_t*>(data.data()), data.size() });
}

void Writer::WriteBuffer() {
    if (rep_.size() > 0) {
        std::error_code error_code;
        file_.write(rep_.data(), rep_.size(), error_code);
        if (error_code) {
            throw IoError{ "failed to write log file." };
        }
        rep_.resize(0);
    }
}

void Writer::AppendRecord(std::span<const uint8_t> data) {
    assert(block_offset_ <= kBlockSize);
    auto ptr = data.data();
    auto left = data.size();
    bool begin = true;
    do {
        const size_t leftover = kBlockSize - block_offset_;
        if (leftover < kHeaderSize) {
            if (leftover > 0) {
                std::error_code error_code;
                file_.write(kBlockPadding, leftover, error_code);
                if (error_code) {
                    throw IoError{ "failed to write log file." };
                }
            }
            block_offset_ = 0;
        }

        const size_t avail = kBlockSize - block_offset_ - kHeaderSize;
        const size_t fragment_length = (left < avail) ? left : avail;

        RecordType type;
        const bool end = (left == fragment_length);
        if (begin && end) {
            type = RecordType::kFullType;
        } else if (begin) {
            type = RecordType::kFirstType;
        } else if (end) {
            type = RecordType::kLastType;
        } else {
            type = RecordType::kMiddleType;
        }

        EmitPhysicalRecord(type, ptr, fragment_length);
        ptr += fragment_length;
        left -= fragment_length;
        begin = false;
    } while (left > 0);
}

void Writer::EmitPhysicalRecord(RecordType type, const uint8_t* ptr, size_t size) {
    assert(size < 0xffff);
    assert(block_offset_ + kHeaderSize + size <= kBlockSize);

    uint8_t buf[kHeaderSize];
    auto record = reinterpret_cast<LogRecord*>(buf);
    record->type = type;
    record->size = size;

    Crc32 crc32;
    crc32.Append(&record->size, kHeaderSize - sizeof(record->checksum));
    crc32.Append(ptr, size);
    record->checksum = crc32.End();

    std::error_code error_code;
    file_.write(buf, kHeaderSize, error_code);
    if (error_code) {
        throw IoError{ "failed to write log file." };
    }
    file_.write(ptr, size, error_code);
    if (error_code) {
        throw IoError{ "failed to write log file." };
    }
    block_offset_ += kHeaderSize + size;
}

} // namespace log
} // namespace yudb