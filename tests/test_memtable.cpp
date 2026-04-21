#include <gtest/gtest.h>

#include "venus/memtable.h"

namespace venus {

TEST(MemtableTest, PutAndGet) {
    Memtable mt(1024 * 1024);

    ASSERT_TRUE(mt.Put("key1", "value1").ok());
    ASSERT_TRUE(mt.Put("key2", "value2").ok());

    std::string val;
    ASSERT_TRUE(mt.Get("key1", &val).ok());
    EXPECT_EQ(val, "value1");

    ASSERT_TRUE(mt.Get("key2", &val).ok());
    EXPECT_EQ(val, "value2");
}

TEST(MemtableTest, GetMissing) {
    Memtable mt(1024 * 1024);

    std::string val;
    auto s = mt.Get("nonexistent", &val);
    EXPECT_TRUE(s.IsNotFound());
}

TEST(MemtableTest, DeleteKey) {
    Memtable mt(1024 * 1024);

    mt.Put("key1", "value1");
    mt.Delete("key1");

    std::string val;
    auto s = mt.Get("key1", &val);
    EXPECT_TRUE(s.IsNotFound());
}

TEST(MemtableTest, OverwriteKey) {
    Memtable mt(1024 * 1024);

    mt.Put("key1", "v1");
    mt.Put("key1", "v2");

    std::string val;
    ASSERT_TRUE(mt.Get("key1", &val).ok());
    EXPECT_EQ(val, "v2");
}

TEST(MemtableTest, ShouldFlush) {
    // Very small threshold
    Memtable mt(100);

    EXPECT_FALSE(mt.ShouldFlush());

    // Insert enough data to exceed threshold
    for (int i = 0; i < 10; i++) {
        mt.Put("key_" + std::to_string(i),
               std::string(50, 'x'));
    }

    EXPECT_TRUE(mt.ShouldFlush());
}

TEST(MemtableTest, IteratorIncludesTombstones) {
    Memtable mt(1024 * 1024);

    mt.Put("a", "1");
    mt.Put("b", "2");
    mt.Delete("c");
    mt.Put("d", "4");

    auto it = mt.NewIterator();
    it->SeekToFirst();

    int count = 0;
    while (it->Valid()) {
        count++;
        it->Next();
    }
    // Should include the tombstone for "c"
    EXPECT_EQ(count, 4);
}

}  // namespace venus
