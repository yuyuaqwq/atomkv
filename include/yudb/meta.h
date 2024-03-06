#pragma once

#include <cstdint>

#include "yudb/meta_format.h"
#include "yudb/noncopyable.h"

namespace yudb {

class DBImpl;

class Meta : noncopyable {
public:
    Meta(DBImpl* db);
    ~Meta();

    bool Load();
    void Save();

    void Switch();
    void Set(const MetaStruct& meta_struct);
    void Get(MetaStruct* meta_struct);

    const auto& meta_struct() const { return *meta_struct_; }
    auto& meta_struct() { return *meta_struct_; }

private:
    DBImpl* const db_;
    MetaStruct* meta_struct_{ nullptr };
    uint32_t cur_meta_index_{ 0 };
};

} // namespace yudb