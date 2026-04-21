#include <gtest/gtest.h>

#include <set>
#include <string>

#include "venus/skiplist.h"

namespace venus {

TEST(SkipListTest, InsertAndGet) {
    SkipList sl;
    sl.Insert("key1", "value1");
    sl.Insert("key2", "value2");
    sl.Insert("key3", "value3");

    auto v1 = sl.Get("key1");
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, "value1");

    auto v2 = sl.Get("key2");
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v2, "value2");

    auto missing = sl.Get("key4");
    EXPECT_FALSE(missing.has_value());
}

TEST(SkipListTest, UpdateExistingKey) {
    SkipList sl;
    sl.Insert("key1", "v1");
    sl.Insert("key1", "v2");

    auto v = sl.Get("key1");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "v2");
}

TEST(SkipListTest, Deletion) {
    SkipList sl;
    sl.Insert("key1", "value1");
    sl.InsertDeletion("key1");

    bool is_deleted = false;
    auto v = sl.Get("key1", &is_deleted);
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(is_deleted);
}

TEST(SkipListTest, DeletionOfNonExistent) {
    SkipList sl;
    sl.InsertDeletion("key1");

    bool is_deleted = false;
    auto v = sl.Get("key1", &is_deleted);
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(is_deleted);
}

TEST(SkipListTest, IteratorSorted) {
    SkipList sl;
    sl.Insert("cherry", "3");
    sl.Insert("apple", "1");
    sl.Insert("banana", "2");
    sl.Insert("date", "4");

    auto it = sl.NewIterator();
    it->SeekToFirst();

    std::vector<std::string> keys;
    while (it->Valid()) {
        keys.push_back(it->key().ToString());
        it->Next();
    }

    ASSERT_EQ(keys.size(), 4u);
    EXPECT_EQ(keys[0], "apple");
    EXPECT_EQ(keys[1], "banana");
    EXPECT_EQ(keys[2], "cherry");
    EXPECT_EQ(keys[3], "date");
}

TEST(SkipListTest, IteratorSeek) {
    SkipList sl;
    sl.Insert("a", "1");
    sl.Insert("c", "3");
    sl.Insert("e", "5");

    auto it = sl.NewIterator();
    it->Seek("b");
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(it->key().ToString(), "c");

    it->Seek("c");
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(it->key().ToString(), "c");

    it->Seek("f");
    EXPECT_FALSE(it->Valid());
}

TEST(SkipListTest, MemoryUsageGrows) {
    SkipList sl;
    EXPECT_EQ(sl.ApproximateMemoryUsage(), 0u);

    sl.Insert("key1", "value1");
    EXPECT_GT(sl.ApproximateMemoryUsage(), 0u);

    size_t usage1 = sl.ApproximateMemoryUsage();
    sl.Insert("key2", "value2");
    EXPECT_GT(sl.ApproximateMemoryUsage(), usage1);
}

TEST(SkipListTest, LargeNumberOfEntries) {
    SkipList sl;
    for (int i = 0; i < 1000; i++) {
        std::string key = "key_" + std::to_string(i);
        std::string val = "val_" + std::to_string(i);
        sl.Insert(key, val);
    }

    EXPECT_EQ(sl.Count(), 1000u);

    // Verify all are retrievable
    for (int i = 0; i < 1000; i++) {
        std::string key = "key_" + std::to_string(i);
        auto v = sl.Get(key);
        ASSERT_TRUE(v.has_value()) << "Missing key: " << key;
    }

    // Verify iteration is sorted
    auto it = sl.NewIterator();
    it->SeekToFirst();
    std::string prev;
    int count = 0;
    while (it->Valid()) {
        std::string k = it->key().ToString();
        if (!prev.empty()) {
            EXPECT_LT(prev, k);
        }
        prev = k;
        it->Next();
        count++;
    }
    EXPECT_EQ(count, 1000);
}

}  // namespace venus
