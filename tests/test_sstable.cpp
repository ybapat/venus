#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "venus/merge_iterator.h"
#include "venus/options.h"
#include "venus/sstable_builder.h"
#include "venus/sstable_reader.h"

namespace venus {

class SSTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/venus_sst_test_" + std::to_string(getpid());
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string MakeKey(int n) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%08d", n);
        return buf;
    }

    std::string MakeValue(int n) {
        return "value_" + std::to_string(n);
    }

    std::string test_dir_;
};

TEST_F(SSTableTest, BuildAndPointLookup) {
    Options opts;
    opts.block_size = 256;
    std::string path = test_dir_ + "/test.sst";

    // Build
    {
        SSTableBuilder builder(path, opts, 100);
        for (int i = 0; i < 100; i++) {
            ASSERT_TRUE(builder.Add(MakeKey(i), MakeValue(i)).ok());
        }
        ASSERT_TRUE(builder.Finish().ok());
        EXPECT_EQ(builder.NumEntries(), 100u);
    }

    // Read
    std::unique_ptr<SSTableReader> reader;
    ASSERT_TRUE(SSTableReader::Open(path, &reader).ok());

    ReadOptions ro;
    for (int i = 0; i < 100; i++) {
        std::string value;
        Status s = reader->Get(ro, MakeKey(i), &value);
        ASSERT_TRUE(s.ok()) << "Failed for key " << MakeKey(i) << ": "
                            << s.ToString();
        EXPECT_EQ(value, MakeValue(i));
    }

    // Non-existent key
    std::string value;
    EXPECT_TRUE(reader->Get(ro, "nonexistent", &value).IsNotFound());
}

TEST_F(SSTableTest, BloomFilterRejects) {
    Options opts;
    std::string path = test_dir_ + "/bloom.sst";

    {
        SSTableBuilder builder(path, opts, 1000);
        for (int i = 0; i < 1000; i++) {
            ASSERT_TRUE(builder.Add(MakeKey(i), MakeValue(i)).ok());
        }
        ASSERT_TRUE(builder.Finish().ok());
    }

    std::unique_ptr<SSTableReader> reader;
    ASSERT_TRUE(SSTableReader::Open(path, &reader).ok());

    // These keys were never added
    int bloom_negatives = 0;
    for (int i = 1000; i < 2000; i++) {
        if (!reader->MayContain(MakeKey(i))) {
            bloom_negatives++;
        }
    }
    // Bloom should reject most non-existent keys
    EXPECT_GT(bloom_negatives, 900);
}

TEST_F(SSTableTest, FullIteration) {
    Options opts;
    opts.block_size = 128;
    std::string path = test_dir_ + "/iter.sst";

    {
        SSTableBuilder builder(path, opts, 50);
        for (int i = 0; i < 50; i++) {
            ASSERT_TRUE(builder.Add(MakeKey(i), MakeValue(i)).ok());
        }
        ASSERT_TRUE(builder.Finish().ok());
    }

    std::unique_ptr<SSTableReader> reader;
    ASSERT_TRUE(SSTableReader::Open(path, &reader).ok());

    ReadOptions ro;
    auto it = reader->NewIterator(ro);
    it->SeekToFirst();

    int count = 0;
    std::string prev;
    while (it->Valid()) {
        std::string k = it->key().ToString();
        if (!prev.empty()) {
            EXPECT_LT(prev, k);
        }
        prev = k;
        count++;
        it->Next();
    }
    EXPECT_EQ(count, 50);
}

TEST_F(SSTableTest, IteratorSeek) {
    Options opts;
    std::string path = test_dir_ + "/seek.sst";

    {
        SSTableBuilder builder(path, opts, 100);
        for (int i = 0; i < 100; i += 2) {  // only even numbers
            ASSERT_TRUE(builder.Add(MakeKey(i), MakeValue(i)).ok());
        }
        ASSERT_TRUE(builder.Finish().ok());
    }

    std::unique_ptr<SSTableReader> reader;
    ASSERT_TRUE(SSTableReader::Open(path, &reader).ok());

    ReadOptions ro;
    auto it = reader->NewIterator(ro);

    // Seek to existing key
    it->Seek(MakeKey(10));
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(it->key().ToString(), MakeKey(10));

    // Seek between keys (should land on next key)
    it->Seek(MakeKey(11));
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(it->key().ToString(), MakeKey(12));
}

TEST_F(SSTableTest, FooterMetadata) {
    Options opts;
    std::string path = test_dir_ + "/meta.sst";

    {
        SSTableBuilder builder(path, opts, 10);
        builder.Add("aaa", "1");
        builder.Add("mmm", "2");
        builder.Add("zzz", "3");
        ASSERT_TRUE(builder.Finish().ok());
    }

    std::unique_ptr<SSTableReader> reader;
    ASSERT_TRUE(SSTableReader::Open(path, &reader).ok());

    EXPECT_EQ(reader->GetFooter().num_entries, 3u);
    EXPECT_EQ(reader->GetFooter().smallest_key, "aaa");
    EXPECT_EQ(reader->GetFooter().largest_key, "zzz");
}

TEST_F(SSTableTest, MergeIteratorTwoTables) {
    Options opts;
    opts.block_size = 128;

    // Table 1: even keys (older)
    std::string path1 = test_dir_ + "/t1.sst";
    {
        SSTableBuilder builder(path1, opts, 10);
        for (int i = 0; i < 10; i += 2) {
            ASSERT_TRUE(builder.Add(MakeKey(i), "old_" + std::to_string(i)).ok());
        }
        ASSERT_TRUE(builder.Finish().ok());
    }

    // Table 2: odd keys + key_00000002 overwrite (newer)
    std::string path2 = test_dir_ + "/t2.sst";
    {
        SSTableBuilder builder(path2, opts, 10);
        builder.Add(MakeKey(1), "new_1");
        builder.Add(MakeKey(2), "new_2");  // overwrites old
        builder.Add(MakeKey(3), "new_3");
        ASSERT_TRUE(builder.Finish().ok());
    }

    std::unique_ptr<SSTableReader> r1, r2;
    ASSERT_TRUE(SSTableReader::Open(path1, &r1).ok());
    ASSERT_TRUE(SSTableReader::Open(path2, &r2).ok());

    ReadOptions ro;
    std::vector<std::unique_ptr<Iterator>> children;
    children.push_back(r2->NewIterator(ro));  // newer first (index 0)
    children.push_back(r1->NewIterator(ro));  // older second (index 1)

    MergeIterator merge(std::move(children));
    merge.SeekToFirst();

    std::vector<std::pair<std::string, std::string>> results;
    while (merge.Valid()) {
        results.emplace_back(merge.key().ToString(), merge.value().ToString());
        merge.Next();
    }

    // Should have: key_0(old), key_1(new), key_2(new), key_3(new),
    //              key_4(old), key_6(old), key_8(old)
    ASSERT_EQ(results.size(), 7u);
    EXPECT_EQ(results[0].second, "old_0");
    EXPECT_EQ(results[1].second, "new_1");
    EXPECT_EQ(results[2].second, "new_2");  // newer wins
    EXPECT_EQ(results[3].second, "new_3");
    EXPECT_EQ(results[4].second, "old_4");
}

}  // namespace venus
