#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "venus/lsm_tree.h"

namespace venus {

class CompactionTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/venus_compact_test_" + std::to_string(getpid());
        std::filesystem::remove_all(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    Options MakeOptions() {
        Options opts;
        opts.db_path = test_dir_;
        opts.memtable_size_threshold = 512;
        opts.level0_compaction_trigger = 4;
        opts.sync_wal = false;
        opts.block_size = 256;
        return opts;
    }

    std::string MakeKey(int n) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%08d", n);
        return buf;
    }

    std::string test_dir_;
};

TEST_F(CompactionTest, L0ToL1Compaction) {
    Options opts = MakeOptions();
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    // Write enough to trigger multiple L0 flushes + compaction
    for (int i = 0; i < 500; i++) {
        ASSERT_TRUE(
            tree.Put(wo, MakeKey(i), std::string(50, 'v')).ok());
    }

    // After writing enough data, compaction should have run
    // L0 count should be reduced
    EXPECT_LT(tree.GetManifest().NumFiles(0), 4);

    // All data should still be readable
    ReadOptions ro;
    for (int i = 0; i < 500; i++) {
        std::string value;
        Status s = tree.Get(ro, MakeKey(i), &value);
        ASSERT_TRUE(s.ok()) << "Failed for " << MakeKey(i) << ": "
                            << s.ToString();
    }

    tree.Close();
}

TEST_F(CompactionTest, OverwritesSurviveCompaction) {
    Options opts = MakeOptions();
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    // Write initial values
    for (int i = 0; i < 100; i++) {
        tree.Put(wo, MakeKey(i), "old_" + std::to_string(i));
    }

    // Overwrite with new values (triggers more flushes)
    for (int i = 0; i < 100; i++) {
        tree.Put(wo, MakeKey(i), "new_" + std::to_string(i));
    }

    // Force enough writes to trigger compaction
    for (int i = 100; i < 500; i++) {
        tree.Put(wo, MakeKey(i), std::string(50, 'v'));
    }

    // Verify overwritten values are correct
    ReadOptions ro;
    for (int i = 0; i < 100; i++) {
        std::string value;
        ASSERT_TRUE(tree.Get(ro, MakeKey(i), &value).ok());
        EXPECT_EQ(value, "new_" + std::to_string(i));
    }

    tree.Close();
}

TEST_F(CompactionTest, DeletesSurviveCompaction) {
    Options opts = MakeOptions();
    LSMTree tree(opts);
    ASSERT_TRUE(tree.Open().ok());

    WriteOptions wo;
    // Write keys
    for (int i = 0; i < 50; i++) {
        tree.Put(wo, MakeKey(i), "val_" + std::to_string(i));
    }

    // Delete some
    for (int i = 0; i < 25; i++) {
        tree.Delete(wo, MakeKey(i));
    }

    // Write more to trigger compaction
    for (int i = 50; i < 500; i++) {
        tree.Put(wo, MakeKey(i), std::string(50, 'x'));
    }

    // Deleted keys should stay deleted
    ReadOptions ro;
    for (int i = 0; i < 25; i++) {
        std::string value;
        EXPECT_TRUE(tree.Get(ro, MakeKey(i), &value).IsNotFound())
            << "Key " << MakeKey(i) << " should be deleted but found: "
            << value;
    }

    // Non-deleted keys should still exist
    for (int i = 25; i < 50; i++) {
        std::string value;
        ASSERT_TRUE(tree.Get(ro, MakeKey(i), &value).ok())
            << "Key " << MakeKey(i) << " should exist";
    }

    tree.Close();
}

TEST_F(CompactionTest, DataIntegrityAcrossReopenWithCompaction) {
    Options opts = MakeOptions();

    // Write data with compaction
    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());
        WriteOptions wo;
        for (int i = 0; i < 500; i++) {
            tree.Put(wo, MakeKey(i), "val_" + std::to_string(i));
        }
        tree.Close();
    }

    // Reopen and verify
    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());
        ReadOptions ro;
        for (int i = 0; i < 500; i++) {
            std::string value;
            Status s = tree.Get(ro, MakeKey(i), &value);
            ASSERT_TRUE(s.ok()) << "Failed for " << MakeKey(i) << ": "
                                << s.ToString();
            EXPECT_EQ(value, "val_" + std::to_string(i));
        }
        tree.Close();
    }
}

}  // namespace venus
