#include "gtest/gtest.h"

#include "yudb/db_impl.h"

namespace yudb {

static std::unique_ptr<yudb::DB> db;

TEST(PagerTest, AllocAndPending) {
    yudb::Options options{
        .page_size = 1024,
        .cache_pool_page_count = size_t(options.page_size) * 1024,
        .log_file_max_bytes = 1024 * 1024 * 64,
    };
    db = yudb::DB::Open(options, "Z:/pager_test.ydb");
    ASSERT_TRUE(db.operator bool());
    auto db_impl = static_cast<DBImpl*>(db.get());
    auto& pager = db_impl->pager();
    {
        auto tx = db_impl->Update();

        // ���������pgidΪ2��ҳ����Ϊroot page
        auto root = tx.UserBucket();

        auto pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 3);

        pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 4);

        pgid = pager.Alloc(10);
        ASSERT_EQ(pgid, 5);

        pgid = pager.Alloc(100);
        ASSERT_EQ(pgid, 15);

        pgid = pager.Alloc(1000);
        ASSERT_EQ(pgid, 115);

        pager.Free(3, 1);
        pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 1115);

        pager.Free(115, 1000);

        // ΪPendingDB��root������1116��Pending��3��115
        tx.Commit();
    }
    {
        auto tx = db_impl->Update();

        auto pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 1117);

        // root������Copy������1118��Pending��2
        tx.Commit();
    }
    {
        // FreeDB�ͷ�3��115��ΪFreeDB������1119
        auto tx = db_impl->Update();

        auto pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 3);

        pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 115);
        pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 116);
        pgid = pager.Alloc(998);
        ASSERT_EQ(pgid, 117);

        pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 1120);

        // root������Copy��������1121.Pending��1118
        tx.Commit();
    }

    {
        // FreeDB�ͷ�2ʱ������Copy��������1122��Pending��1119
        auto tx = db_impl->Update();

        auto pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 2);

        pgid = pager.Alloc(1);
        ASSERT_EQ(pgid, 1123);
        
        // PendingDB��Root������Copy��������1124��Pending��1116
        // root������Copy��������1124��Pending��1121
        tx.Commit();
    }
}

TEST(PagerTest, Clear) {

}

} // namespace yudb