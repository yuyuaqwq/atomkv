#include <filesystem>

#include "gtest/gtest.h"

#include "yudb/db_impl.h"

namespace yudb {

static std::unique_ptr<yudb::DB> db;

TEST(CacheManagerTest, HitAndMiss) {

    std::filesystem::resize_file("Z:/test_map.ydb", 100);


    // �������һ���ڴ�ӳ���ļ�
    mio::mmap_sink file_sink("Z:/test_map.ydb", 0);

    file_sink.size();

    // ��ȡӳ����ڴ�ָ��
    auto data = file_sink.data();

    // ������д���ڴ�ӳ���ļ�
    std::string message = "Hello, mio!";
    std::copy(message.begin(), message.end(), data);

    // ǿ��ˢ�����ݵ��ļ�
    std::error_code code;
    file_sink.sync(code);

    // �ر��ļ�ӳ��
    file_sink.unmap();




    yudb::Options options{
        .page_size = 1024,
        .cache_pool_page_count = 1024,
        .log_file_limit_bytes = 1024 * 1024 * 64,
    };
    db = yudb::DB::Open(options, "Z:/cache_manager_test.ydb");
    ASSERT_TRUE(db.operator bool());
    auto db_impl = static_cast<DBImpl*>(db.get());
    auto& pager = db_impl->pager();
    auto& cache_manager = pager.cache_manager();
    {
        auto tx = db_impl->Update();
        
        for (auto i = 2; i < 10000; ++i) {
            auto [_, ptr] = cache_manager.Reference(i, true);
            auto uint32_ptr = reinterpret_cast<uint32_t*>(ptr);
            for (auto j = 0; j < options.page_size / sizeof(uint32_t); ++j) {
                uint32_ptr[j] = i;
            }
            cache_manager.Dereference(ptr);
        }
        
        for (auto i = 2; i < 10000; ++i) {
            auto [_, ptr] = cache_manager.Reference(i, false);
            auto uint32_ptr = reinterpret_cast<uint32_t*>(ptr);
            for (auto j = 0; j < options.page_size / sizeof(uint32_t); ++j) {
                ASSERT_EQ(uint32_ptr[j], i);
            }
            cache_manager.Dereference(ptr);
        }
    }

}


} // namespace yudb