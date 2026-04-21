#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "venus/lsm_tree.h"

namespace venus {

class CrashRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ =
            "/tmp/venus_crash_test_" + std::to_string(getpid());
        std::filesystem::remove_all(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    Options MakeOptions() {
        Options opts;
        opts.db_path = test_dir_;
        opts.memtable_size_threshold = 1024 * 1024;  // large, don't flush
        opts.sync_wal = false;
        return opts;
    }

    std::string MakeKey(int n) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%08d", n);
        return buf;
    }

    std::string test_dir_;
};

TEST_F(CrashRecoveryTest, WALReplayOnReopen) {
    Options opts = MakeOptions();

    // Write data, close without flushing
    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());
        WriteOptions wo;
        for (int i = 0; i < 100; i++) {
            tree.Put(wo, MakeKey(i), "val_" + std::to_string(i));
        }
        // Close writes WAL but doesn't flush memtable to SSTable
        tree.Close();
    }

    // Reopen — WAL replay should recover all data
    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());

        ReadOptions ro;
        for (int i = 0; i < 100; i++) {
            std::string value;
            Status s = tree.Get(ro, MakeKey(i), &value);
            ASSERT_TRUE(s.ok()) << "Key " << MakeKey(i)
                                << " not found after recovery: "
                                << s.ToString();
            EXPECT_EQ(value, "val_" + std::to_string(i));
        }
        tree.Close();
    }
}

TEST_F(CrashRecoveryTest, FlushedAndUnflushedData) {
    Options opts = MakeOptions();
    opts.memtable_size_threshold = 512;  // small to trigger flush

    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());
        WriteOptions wo;

        // Write enough to trigger flush (flushed data)
        for (int i = 0; i < 50; i++) {
            tree.Put(wo, MakeKey(i), std::string(50, 'a'));
        }

        // Write more that stays in memtable (unflushed data)
        for (int i = 50; i < 60; i++) {
            tree.Put(wo, MakeKey(i), "unflushed_" + std::to_string(i));
        }

        // Simulate crash: close
        tree.Close();
    }

    // Reopen and verify both flushed and unflushed data
    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());

        ReadOptions ro;
        // Flushed data
        for (int i = 0; i < 50; i++) {
            std::string value;
            Status s = tree.Get(ro, MakeKey(i), &value);
            ASSERT_TRUE(s.ok()) << "Flushed key " << MakeKey(i)
                                << " not found: " << s.ToString();
        }
        // Unflushed data (recovered from WAL)
        for (int i = 50; i < 60; i++) {
            std::string value;
            Status s = tree.Get(ro, MakeKey(i), &value);
            ASSERT_TRUE(s.ok()) << "Unflushed key " << MakeKey(i)
                                << " not found: " << s.ToString();
            EXPECT_EQ(value, "unflushed_" + std::to_string(i));
        }
        tree.Close();
    }
}

TEST_F(CrashRecoveryTest, CorruptWALTailRecovery) {
    Options opts = MakeOptions();

    // Write data
    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());
        WriteOptions wo;
        for (int i = 0; i < 10; i++) {
            tree.Put(wo, MakeKey(i), "val_" + std::to_string(i));
        }
        tree.Close();
    }

    // Corrupt the WAL tail
    for (auto& entry : std::filesystem::directory_iterator(test_dir_)) {
        std::string name = entry.path().filename().string();
        if (name.substr(0, 4) == "wal_") {
            // Truncate last few bytes
            auto size = std::filesystem::file_size(entry.path());
            if (size > 20) {
                std::filesystem::resize_file(entry.path(), size - 10);
            }
            break;
        }
    }

    // Reopen — should recover valid prefix of WAL
    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());

        ReadOptions ro;
        // At least some early keys should be recoverable
        int recovered = 0;
        for (int i = 0; i < 10; i++) {
            std::string value;
            if (tree.Get(ro, MakeKey(i), &value).ok()) {
                recovered++;
            }
        }
        EXPECT_GT(recovered, 0);
        tree.Close();
    }
}

TEST_F(CrashRecoveryTest, DeletesRecoveredFromWAL) {
    Options opts = MakeOptions();

    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());
        WriteOptions wo;
        tree.Put(wo, "key1", "value1");
        tree.Delete(wo, "key1");
        tree.Close();
    }

    {
        LSMTree tree(opts);
        ASSERT_TRUE(tree.Open().ok());

        ReadOptions ro;
        std::string value;
        EXPECT_TRUE(tree.Get(ro, "key1", &value).IsNotFound());
        tree.Close();
    }
}

}  // namespace venus
