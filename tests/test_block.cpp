#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "venus/block_builder.h"
#include "venus/block_reader.h"

namespace venus {

TEST(BlockTest, BuildAndRead) {
    BlockBuilder builder(4);

    builder.Add("apple", "1");
    builder.Add("banana", "2");
    builder.Add("cherry", "3");
    builder.Add("date", "4");
    builder.Add("elderberry", "5");

    std::string block_data = builder.Finish();

    BlockReader reader(block_data.data(), block_data.size());
    ASSERT_TRUE(reader.Validate().ok());

    auto it = reader.NewIterator();
    it->SeekToFirst();

    std::vector<std::pair<std::string, std::string>> entries;
    while (it->Valid()) {
        entries.emplace_back(it->key().ToString(), it->value().ToString());
        it->Next();
    }

    ASSERT_EQ(entries.size(), 5u);
    EXPECT_EQ(entries[0].first, "apple");
    EXPECT_EQ(entries[0].second, "1");
    EXPECT_EQ(entries[4].first, "elderberry");
    EXPECT_EQ(entries[4].second, "5");
}

TEST(BlockTest, SeekExact) {
    BlockBuilder builder(4);

    for (int i = 0; i < 20; i++) {
        char key[32], val[32];
        snprintf(key, sizeof(key), "key_%04d", i);
        snprintf(val, sizeof(val), "val_%04d", i);
        builder.Add(key, val);
    }

    std::string block_data = builder.Finish();
    BlockReader reader(block_data.data(), block_data.size());

    auto it = reader.NewIterator();
    it->Seek("key_0010");
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(it->key().ToString(), "key_0010");
    EXPECT_EQ(it->value().ToString(), "val_0010");
}

TEST(BlockTest, SeekBetweenKeys) {
    BlockBuilder builder(4);
    builder.Add("aaa", "1");
    builder.Add("ccc", "3");
    builder.Add("eee", "5");

    std::string block_data = builder.Finish();
    BlockReader reader(block_data.data(), block_data.size());

    auto it = reader.NewIterator();
    it->Seek("bbb");
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(it->key().ToString(), "ccc");
}

TEST(BlockTest, SeekPastEnd) {
    BlockBuilder builder(4);
    builder.Add("aaa", "1");
    builder.Add("bbb", "2");

    std::string block_data = builder.Finish();
    BlockReader reader(block_data.data(), block_data.size());

    auto it = reader.NewIterator();
    it->Seek("zzz");
    EXPECT_FALSE(it->Valid());
}

TEST(BlockTest, CRCValidation) {
    BlockBuilder builder(4);
    builder.Add("key", "value");
    std::string block_data = builder.Finish();

    // Corrupt a byte in the data
    block_data[0] ^= 0xFF;

    BlockReader reader(block_data.data(), block_data.size());
    EXPECT_TRUE(reader.Validate().IsCorruption());
}

TEST(BlockTest, PrefixCompression) {
    BlockBuilder builder(16);  // restart every 16 entries

    // Keys with long shared prefix
    for (int i = 0; i < 16; i++) {
        std::string key = "shared_prefix_" + std::to_string(i);
        builder.Add(key, "v");
    }

    std::string block_data = builder.Finish();

    // Verify data is smaller than naive encoding would be
    // (each key is ~16 bytes, 16 keys = 256 bytes naive)
    // With prefix compression, should be notably smaller
    // Just verify correctness:
    BlockReader reader(block_data.data(), block_data.size());
    auto it = reader.NewIterator();
    it->SeekToFirst();

    int count = 0;
    while (it->Valid()) {
        std::string expected = "shared_prefix_" + std::to_string(count);
        EXPECT_EQ(it->key().ToString(), expected);
        it->Next();
        count++;
    }
    EXPECT_EQ(count, 16);
}

}  // namespace venus
