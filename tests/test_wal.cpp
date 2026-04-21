#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "venus/wal.h"

namespace venus {

class WALTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/venus_wal_test_" + std::to_string(getpid());
        std::filesystem::create_directories(test_dir_);
        wal_path_ = test_dir_ + "/test.wal";
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
    std::string wal_path_;
};

TEST_F(WALTest, WriteAndReadBack) {
    {
        std::unique_ptr<WALWriter> writer;
        ASSERT_TRUE(WALWriter::Open(wal_path_, &writer).ok());
        ASSERT_TRUE(writer->AddPut("key1", "value1").ok());
        ASSERT_TRUE(writer->AddPut("key2", "value2").ok());
        ASSERT_TRUE(writer->AddDelete("key3").ok());
        ASSERT_TRUE(writer->Sync().ok());
        ASSERT_TRUE(writer->Close().ok());
    }

    std::vector<WALEntry> entries;
    auto s = WALReader::ReadAll(wal_path_, [&](const WALEntry& e) {
        entries.push_back(e);
    });
    ASSERT_TRUE(s.ok()) << s.ToString();

    ASSERT_EQ(entries.size(), 3u);

    EXPECT_EQ(entries[0].type, WALRecordType::kPut);
    EXPECT_EQ(entries[0].key, "key1");
    EXPECT_EQ(entries[0].value, "value1");

    EXPECT_EQ(entries[1].type, WALRecordType::kPut);
    EXPECT_EQ(entries[1].key, "key2");
    EXPECT_EQ(entries[1].value, "value2");

    EXPECT_EQ(entries[2].type, WALRecordType::kDelete);
    EXPECT_EQ(entries[2].key, "key3");
    EXPECT_EQ(entries[2].value, "");
}

TEST_F(WALTest, ManyEntries) {
    {
        std::unique_ptr<WALWriter> writer;
        ASSERT_TRUE(WALWriter::Open(wal_path_, &writer).ok());
        for (int i = 0; i < 100; i++) {
            auto k = "key_" + std::to_string(i);
            auto v = "val_" + std::to_string(i);
            ASSERT_TRUE(writer->AddPut(k, v).ok());
        }
        ASSERT_TRUE(writer->Close().ok());
    }

    int count = 0;
    auto s = WALReader::ReadAll(wal_path_, [&](const WALEntry& e) {
        EXPECT_EQ(e.key, "key_" + std::to_string(count));
        EXPECT_EQ(e.value, "val_" + std::to_string(count));
        count++;
    });
    ASSERT_TRUE(s.ok());
    EXPECT_EQ(count, 100);
}

TEST_F(WALTest, TruncatedFile) {
    {
        std::unique_ptr<WALWriter> writer;
        ASSERT_TRUE(WALWriter::Open(wal_path_, &writer).ok());
        ASSERT_TRUE(writer->AddPut("key1", "value1").ok());
        ASSERT_TRUE(writer->AddPut("key2", "value2").ok());
        ASSERT_TRUE(writer->Close().ok());
    }

    // Truncate the file to cut off the second entry
    {
        std::ifstream in(wal_path_, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        in.close();

        // Write back only 2/3 of the data
        size_t trunc_size = data.size() * 2 / 3;
        std::ofstream out(wal_path_, std::ios::binary | std::ios::trunc);
        out.write(data.data(), trunc_size);
        out.close();
    }

    // Non-strict: should recover first entry
    std::vector<WALEntry> entries;
    auto s = WALReader::ReadAll(wal_path_, [&](const WALEntry& e) {
        entries.push_back(e);
    }, false);
    ASSERT_TRUE(s.ok());
    EXPECT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].key, "key1");
}

TEST_F(WALTest, BitFlipDetection) {
    {
        std::unique_ptr<WALWriter> writer;
        ASSERT_TRUE(WALWriter::Open(wal_path_, &writer).ok());
        ASSERT_TRUE(writer->AddPut("key1", "value1").ok());
        ASSERT_TRUE(writer->Close().ok());
    }

    // Flip a bit in the payload (not the CRC)
    {
        std::string data;
        {
            std::ifstream in(wal_path_, std::ios::binary);
            data.assign((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        }

        // Flip a bit at offset 5 (inside the payload, after the 4-byte CRC)
        if (data.size() > 5) {
            data[5] ^= 0x01;
        }

        std::ofstream out(wal_path_, std::ios::binary | std::ios::trunc);
        out.write(data.data(), data.size());
    }

    // Strict mode should return corruption
    std::vector<WALEntry> entries;
    auto s = WALReader::ReadAll(wal_path_, [&](const WALEntry& e) {
        entries.push_back(e);
    }, true);
    EXPECT_TRUE(s.IsCorruption());
    EXPECT_EQ(entries.size(), 0u);
}

TEST_F(WALTest, EmptyFile) {
    // Create empty WAL
    { std::ofstream out(wal_path_); }

    int count = 0;
    auto s = WALReader::ReadAll(wal_path_, [&](const WALEntry&) {
        count++;
    });
    ASSERT_TRUE(s.ok());
    EXPECT_EQ(count, 0);
}

}  // namespace venus
