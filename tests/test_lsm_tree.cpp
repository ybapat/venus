#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "venus/lsm_tree.h"

namespace venus {

class LSMTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/venus_lsm_test_" + std::to_string(getpid());
        std::filesystem::remove_all(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    Options MakeOptions() {
        Options opts;
        opts.db_path = test_dir_;
        opts.memtable_size_threshold = 4096;  // small for testing
        opts.sync_wal = false;                 // speed up tests
        return opts;
    }

    std::string MakeKey(int n) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%08d", n);
        return buf;
    }

    std::string test_dir_;
};

TEST_F(LSMTreeTest, BasicPutGet) {
    Options opts = MakeOptions();
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    ASSERT_TRUE(tree.Put(wo, "hello", "world").ok());

    ReadOptions ro;
    std::string value;
    ASSERT_TRUE(tree.Get(ro, "hello", &value).ok());
    EXPECT_EQ(value, "world");

    tree.Close();
}

TEST_F(LSMTreeTest, GetMissing) {
    Options opts = MakeOptions();
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    ReadOptions ro;
    std::string value;
    EXPECT_TRUE(tree.Get(ro, "nonexistent", &value).IsNotFound());

    tree.Close();
}

TEST_F(LSMTreeTest, Overwrite) {
    Options opts = MakeOptions();
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    tree.Put(wo, "key", "v1");
    tree.Put(wo, "key", "v2");

    ReadOptions ro;
    std::string value;
    ASSERT_TRUE(tree.Get(ro, "key", &value).ok());
    EXPECT_EQ(value, "v2");

    tree.Close();
}

TEST_F(LSMTreeTest, DeleteKey) {
    Options opts = MakeOptions();
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    tree.Put(wo, "key", "value");
    tree.Delete(wo, "key");

    ReadOptions ro;
    std::string value;
    EXPECT_TRUE(tree.Get(ro, "key", &value).IsNotFound());

    tree.Close();
}

TEST_F(LSMTreeTest, FlushToSSTable) {
    Options opts = MakeOptions();
    opts.memtable_size_threshold = 512;  // very small to trigger flush
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    // Write enough data to trigger flush
    for (int i = 0; i < 50; i++) {
        ASSERT_TRUE(
            tree.Put(wo, MakeKey(i), std::string(100, 'v')).ok());
    }

    // Should have flushed to SSTable(s) — may be in L0 or L1 after compaction
    int total_files = 0;
    for (auto& f : tree.GetManifest().GetAllFiles()) {
        (void)f;
        total_files++;
    }
    EXPECT_GT(total_files, 0);

    // All data should still be readable
    ReadOptions ro;
    for (int i = 0; i < 50; i++) {
        std::string value;
        Status s = tree.Get(ro, MakeKey(i), &value);
        ASSERT_TRUE(s.ok()) << "Failed for " << MakeKey(i) << ": "
                            << s.ToString();
    }

    tree.Close();
}

TEST_F(LSMTreeTest, MultipleFlushes) {
    Options opts = MakeOptions();
    opts.memtable_size_threshold = 512;
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    for (int i = 0; i < 200; i++) {
        ASSERT_TRUE(
            tree.Put(wo, MakeKey(i), std::string(50, 'v')).ok());
    }

    EXPECT_GT(tree.GetManifest().NumFiles(0), 1);

    ReadOptions ro;
    for (int i = 0; i < 200; i++) {
        std::string value;
        Status s = tree.Get(ro, MakeKey(i), &value);
        ASSERT_TRUE(s.ok()) << "Failed for " << MakeKey(i) << ": "
                            << s.ToString();
    }

    tree.Close();
}

TEST_F(LSMTreeTest, ScanRange) {
    Options opts = MakeOptions();
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    for (int i = 0; i < 20; i++) {
        tree.Put(wo, MakeKey(i), "val_" + std::to_string(i));
    }

    ReadOptions ro;
    std::vector<std::pair<std::string, std::string>> results;
    ASSERT_TRUE(
        tree.Scan(ro, MakeKey(5), MakeKey(10), &results).ok());

    EXPECT_EQ(results.size(), 5u);
    EXPECT_EQ(results[0].first, MakeKey(5));
    EXPECT_EQ(results[4].first, MakeKey(9));

    tree.Close();
}

TEST_F(LSMTreeTest, DeleteThenFlush) {
    Options opts = MakeOptions();
    opts.memtable_size_threshold = 512;
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    // Write and delete, then flush
    tree.Put(wo, "key1", "value1");
    tree.Delete(wo, "key1");

    // Force flush
    ASSERT_TRUE(tree.FlushMemtable().ok());

    ReadOptions ro;
    std::string value;
    EXPECT_TRUE(tree.Get(ro, "key1", &value).IsNotFound());

    tree.Close();
}

}  // namespace venus
