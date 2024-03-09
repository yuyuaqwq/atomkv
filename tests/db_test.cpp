﻿// yudbpp.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <thread>
#include <set>
#include <map>
#include <chrono>
#include <algorithm>
#include <unordered_set>
#include <span>
#include <filesystem>

#include <gtest/gtest.h>

#include "yudb/db.h"

namespace yudb {

class DBTest : public testing::Test {
public:
    std::unique_ptr<yudb::DB> db_;
    int seed_;

public:
    DBTest() {
        Open();
    }

    void Open() {
        yudb::Options options{
            .max_wal_size = 1024 * 1024 * 64,
        };
        //std::string path = testing::TempDir() + "db_test.ydb";
        std::string path = "Z:/db_test.ydb";
        std::filesystem::remove(path);
        std::filesystem::remove(path + "-shm");
        std::filesystem::remove(path + "-wal");
        db_ = yudb::DB::Open(options, path);
        ASSERT_FALSE(!db_);
    }

    auto Update() {
        return db_->Update();
    }

    auto View() {
        return db_->View();
    }

    std::string RandomString(size_t min_size, size_t max_size) {
        int size;
        if (min_size == max_size) {
            size = min_size;
        }
        else {
            size = (rand() % (max_size - min_size)) + min_size;
        }
        std::string str(size, ' ');
        for (auto i = 0; i < size; i++) {
            str[i] = rand() % 26 + 'a';
        }
        return str;
    }
};

TEST_F(DBTest, EmptyKey) {
    auto tx = Update();
    auto bucket = tx.UserBucket();

    bucket.Put("", "v");
    
    auto iter = bucket.Get("");
    ASSERT_EQ(iter.key(), "");
    ASSERT_EQ(iter.value(), "v");

    bucket.Delete("");

    iter = bucket.Get("");
    ASSERT_EQ(iter, bucket.end());

    tx.Commit();
}

TEST_F(DBTest, EmptyValue) {
    auto tx = Update();
    auto bucket = tx.UserBucket();

    bucket.Put("k", "");

    auto iter = bucket.Get("k");
    ASSERT_EQ(iter.key(), "k");
    ASSERT_EQ(iter.value(), "");

    bucket.Delete("k");

    iter = bucket.Get("k");
    ASSERT_EQ(iter, bucket.end());

    tx.Commit();
}

TEST_F(DBTest, EmptyKeyValue) {
    auto tx = Update();
    auto bucket = tx.UserBucket();

    bucket.Put("", "");

    auto iter = bucket.Get("");
    ASSERT_EQ(iter.key(), "");
    ASSERT_EQ(iter.value(), "");

    bucket.Delete("");

    iter = bucket.Get("");
    ASSERT_EQ(iter, bucket.end());

    tx.Commit();
}

TEST_F(DBTest, Put) {
    auto tx = Update();
    auto bucket = tx.UserBucket();

    bucket.Put("ABC", "123");
    bucket.Put("!@#$%^&*(", "999888777");
    auto iter = bucket.Get("ABC");
    ASSERT_EQ(iter.key(), "ABC");
    ASSERT_EQ(iter.value(), "123");
    
    iter = bucket.Get("!@#$%^&*(");
    ASSERT_EQ(iter.key(), "!@#$%^&*(");
    ASSERT_EQ(iter.value(), "999888777");

    bucket.Put("ABC", "0xCC");
    iter = bucket.Get("ABC");
    ASSERT_EQ(iter.key(), "ABC");
    ASSERT_EQ(iter.value(), "0xCC");

    tx.Commit();
}

TEST_F(DBTest, Delete) {
    auto tx = Update();
    auto bucket = tx.UserBucket();

    bucket.Put("k1", "v1");
    bucket.Put("k2", "v2");
    auto iter = bucket.Get("k1");
    ASSERT_EQ(iter.key(), "k1");
    ASSERT_EQ(iter.value(), "v1");

    bucket.Delete("k1");

    iter = bucket.Get("k1");
    ASSERT_EQ(iter, bucket.end());

    iter = bucket.Get("k2");
    ASSERT_EQ(iter.key(), "k2");
    ASSERT_EQ(iter.value(), "v2");

    tx.Commit();
}

TEST_F(DBTest, PutLongData) {
    auto long_key1 = RandomString(4096, 4096);
    auto long_value1 = RandomString(1024 * 1024, 1024 * 1024);
    auto long_value2 = RandomString(1024 * 1024, 1024 * 1024);

    auto tx = Update();
    auto bucket = tx.UserBucket();

    bucket.Put(long_key1, long_value1);
    auto iter = bucket.Get(long_key1);
    ASSERT_EQ(iter.key(), long_key1);
    ASSERT_EQ(iter.value(), long_value1);

    bucket.Put(long_key1, long_value2);
    iter = bucket.Get(long_key1);
    ASSERT_EQ(iter.key(), long_key1);
    ASSERT_EQ(iter.value(), long_value2);

    tx.Commit();
}

TEST_F(DBTest, DeleteLongData) {
    auto long_key1 = RandomString(4096, 4096);
    auto long_value1 = RandomString(1024 * 1024, 1024 * 1024);
    auto long_value2 = RandomString(1024 * 1024, 1024 * 1024);

    auto tx = Update();
    auto bucket = tx.UserBucket();

    bucket.Put(long_key1, long_value1);
    auto iter = bucket.Get(long_key1);
    ASSERT_EQ(iter.key(), long_key1);
    ASSERT_EQ(iter.value(), long_value1);

    bucket.Delete(long_key1);
    iter = bucket.Get(long_key1);
    ASSERT_EQ(iter, bucket.end());

    tx.Commit();
}

TEST_F(DBTest, SubBucket) {
    auto tx = Update();
    auto bucket = tx.UserBucket();

    auto sub_bucket1 = bucket.SubUpdateBucket("sub1");
    sub_bucket1.Put("sub1_key1", "sub1_value1");
    sub_bucket1.Put("sub1_key2", "sub1_value2");
    auto iter = sub_bucket1.Get("sub1_key1");
    ASSERT_EQ(iter.key(), "sub1_key1");
    ASSERT_EQ(iter.value(), "sub1_value1");

    auto sub_bucket2 = bucket.SubUpdateBucket("sub2");
    sub_bucket2.Put("sub2_key1", "sub2_value1");
    sub_bucket2.Put("sub2_key2", "sub2_value2");
    iter = sub_bucket2.Get("sub2_key1");
    ASSERT_EQ(iter.key(), "sub2_key1");
    ASSERT_EQ(iter.value(), "sub2_value1");

    iter = bucket.Get("sub1");
    ASSERT_NE(iter, bucket.end());
    iter = bucket.Get("sub2");
    ASSERT_NE(iter, bucket.end());

    tx.Commit();
}



TEST_F(DBTest, BatchOrderedPut) {
    auto count = 1000000;

    std::vector<int64_t> arr(count);
    for (auto i = 0; i < count; i++) {
        arr[i] = i;
    }

    auto tx = Update();
    auto bucket = tx.UserBucket(yudb::UInt64Comparator);

    auto j = 0;
    for (auto& iter : arr) {
        bucket.Put(&iter, sizeof(iter), &iter, sizeof(iter));
        ++j;
    }
    tx.Commit();
}

//TEST_F(DBTest, BatchOrderedGet) {
//    auto tx = View();
//    auto bucket = tx.UserBucket(yudb::UInt64Comparator);
//    auto j = 0;
//    for (auto& iter : arr) {
//        auto res = bucket.Get(&iter, sizeof(iter));
//        ASSERT_NE(res, bucket.end());
//        //assert(res.value() == iter);
//        ++j;
//        //bucket.Put(&arr[i], sizeof(arr[i]), &arr[i], sizeof(arr[i]));
//        //bucket.Print(); printf("\n\n\n\n\n");
//    }
//   
//    
//    auto tx = Update();
//    auto bucket = tx.UserBucket(yudb::UInt64Comparator);
//    auto j = 0;
//    for (auto& iter : arr) {
//        auto res = bucket.Delete(&iter, sizeof(iter));
//        ASSERT_TRUE(res);
//        //assert(res.value() == iter);
//        ++j;
//        //bucket.Put(&arr[i], sizeof(arr[i]), &arr[i], sizeof(arr[i]));
//        //bucket.Print(); printf("\n\n\n\n\n");
//    }
//    tx.Commit();
//    
//    std::reverse(arr.begin(), arr.end());
//}
//
//TEST_F(DBTest, BatchRandomString) {
//    srand(seed_);
//
//    auto count = 1000000;
//    std::vector<std::string> arr(count);
//
//    for (auto i = 0; i < count; i++) {
//        arr[i] = RandomString(16, 100);
//    }
//
//    auto start_time = std::chrono::high_resolution_clock::now();
//    {
//        auto tx = Update();
//        auto bucket = tx.UserBucket();
//
//        auto i = 0;
//        std::string_view value{ nullptr, 0 };
//        for (auto& iter : arr) {
//            //printf("%d\n", i);
//            bucket.Put(iter, value);
//            //bucket.Print(); printf("\n\n\n\n");
//            ++i;
//        }
//        //bucket.Print();
//        tx.Commit();
//    }
//    auto end_time = std::chrono::high_resolution_clock::now();
//    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//    std::cout << "insert: " << duration.count() << " ms" << std::endl;
//
//    start_time = std::chrono::high_resolution_clock::now();
//    {
//        auto tx = View();
//        auto bucket = tx.UserBucket();
//        auto i = 0;
//        for (auto& iter : arr) {
//            auto res = bucket.Get(iter.c_str(), iter.size());
//            ASSERT_NE(res, bucket.end());
//            //assert(res.value() == iter);
//            ++i;
//            //bucket.Put(&arr[i], sizeof(arr[i]), &arr[i], sizeof(arr[i]));
//            //bucket.Print(); printf("\n\n\n\n\n");
//        }
//    }
//    end_time = std::chrono::high_resolution_clock::now();
//    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//    std::cout << "get: " << duration.count() << " ms" << std::endl;
//
//    start_time = std::chrono::high_resolution_clock::now();
//    {
//        auto tx = Update();
//        auto bucket = tx.UserBucket();
//        auto i = 0;
//        for (auto& iter : arr) {
//            auto res = bucket.Delete(iter.c_str(), iter.size());
//            ASSERT_TRUE(res);
//            //assert(res.value() == iter);
//            ++i;
//            //bucket.Put(&arr[i], sizeof(arr[i]), &arr[i], sizeof(arr[i]));
//            //bucket.Print(); printf("\n\n\n\n\n");
//        }
//        tx.Commit();
//    }
//    end_time = std::chrono::high_resolution_clock::now();
//    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//    std::cout << "delete: " << duration.count() << " ms" << std::endl;
//}
//
//TEST_F(DBTest, Sequential) {
//    auto count = 1000000;
//
//    std::vector<int64_t> arr(count);
//    for (auto i = 0; i < count; i++) {
//        arr[i] = i;
//    }
//
//    for (auto i = 0; i < 2; i++) {
//        auto start_time = std::chrono::high_resolution_clock::now();
//        {
//            auto j = 0;
//            for (auto& iter : arr) {
//                auto tx = Update();
//                auto bucket = tx.UserBucket(yudb::UInt64Comparator);
//                //printf("%d\n", i);
//                bucket.Put(&iter, sizeof(iter), &iter, sizeof(iter));
//                //bucket.Print(); printf("\n\n\n\n");
//                ++j;
//                tx.Commit();
//            }
//            //bucket.Print();
//
//        }
//        auto end_time = std::chrono::high_resolution_clock::now();
//        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//        std::cout << "insert: " << duration.count() << " ms" << std::endl;
//
//        start_time = std::chrono::high_resolution_clock::now();
//        {
//            auto j = 0;
//            for (auto& iter : arr) {
//                auto tx = View();
//                auto bucket = tx.UserBucket(yudb::UInt64Comparator);
//                auto res = bucket.Get(&iter, sizeof(iter));
//                ASSERT_NE(res, bucket.end());
//                //assert(res.value() == iter);
//                ++j;
//                //bucket.Put(&arr[i], sizeof(arr[i]), &arr[i], sizeof(arr[i]));
//                //bucket.Print(); printf("\n\n\n\n\n");
//            }
//        }
//        end_time = std::chrono::high_resolution_clock::now();
//        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//        std::cout << "get: " << duration.count() << " ms" << std::endl;
//
//        start_time = std::chrono::high_resolution_clock::now();
//        {
//            auto j = 0;
//            for (auto& iter : arr) {
//                auto tx = Update();
//                auto bucket = tx.UserBucket(yudb::UInt64Comparator);
//                auto res = bucket.Delete(&iter, sizeof(iter));
//                ASSERT_TRUE(res);
//                //assert(res.value() == iter);
//                ++j;
//                //bucket.Put(&arr[i], sizeof(arr[i]), &arr[i], sizeof(arr[i]));
//                //bucket.Print(); printf("\n\n\n\n\n");
//                tx.Commit();
//            }
//
//        }
//        end_time = std::chrono::high_resolution_clock::now();
//        duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//        std::cout << "delete: " << duration.count() << " ms" << std::endl;
//
//        std::reverse(arr.begin(), arr.end());
//    }
//}
//
//TEST_F(DBTest, Recover) {
//    auto count = 100;
//
//    std::vector<int64_t> arr(count);
//    for (auto i = 0; i < count; i++) {
//        arr[i] = i;
//    }
//    auto start_time = std::chrono::high_resolution_clock::now();
//    {
//        auto tx = Update();
//        auto bucket = tx.UserBucket(yudb::UInt64Comparator);
//
//        auto j = 0;
//        for (auto& iter : arr) {
//            //printf("%d\n", i);
//            bucket.Put(&iter, sizeof(iter), &iter, sizeof(iter));
//            //bucket.Print(); printf("\n\n\n\n");
//            ++j;
//        }
//        //bucket.Print();
//        tx.Commit();
//    }
//    auto end_time = std::chrono::high_resolution_clock::now();
//    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//    std::cout << "insert: " << duration.count() << " ms" << std::endl;
//
//}

} // namespace yudb

