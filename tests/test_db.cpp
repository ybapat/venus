#include <gtest/gtest.h>

#include <filesystem>

#include "venus/db.h"

namespace venus {

class DBTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/venus_db_test_" + std::to_string(getpid());
        std::filesystem::remove_all(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
};

TEST_F(DBTest, OpenAndClose) {
    Options opts;
    opts.db_path = test_dir_;
    std::unique_ptr<DB> db;
    ASSERT_TRUE(DB::Open(opts, &db).ok());
    ASSERT_TRUE(db->Close().ok());
}

TEST_F(DBTest, PutGetDelete) {
    Options opts;
    opts.db_path = test_dir_;
    std::unique_ptr<DB> db;
    ASSERT_TRUE(DB::Open(opts, &db).ok());

    ASSERT_TRUE(db->Put("name", "venus").ok());

    std::string val;
    ASSERT_TRUE(db->Get("name", &val).ok());
    EXPECT_EQ(val, "venus");

    ASSERT_TRUE(db->Delete("name").ok());
    EXPECT_TRUE(db->Get("name", &val).IsNotFound());

    db->Close();
}

TEST_F(DBTest, ScanRange) {
    Options opts;
    opts.db_path = test_dir_;
    std::unique_ptr<DB> db;
    ASSERT_TRUE(DB::Open(opts, &db).ok());

    db->Put("a", "1");
    db->Put("b", "2");
    db->Put("c", "3");
    db->Put("d", "4");

    std::vector<std::pair<std::string, std::string>> results;
    ASSERT_TRUE(db->Scan("b", "d", &results).ok());
    EXPECT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].first, "b");
    EXPECT_EQ(results[1].first, "c");

    db->Close();
}

TEST_F(DBTest, PersistenceAcrossReopen) {
    Options opts;
    opts.db_path = test_dir_;

    {
        std::unique_ptr<DB> db;
        ASSERT_TRUE(DB::Open(opts, &db).ok());
        db->Put("persistent", "data");
        db->Close();
    }

    {
        std::unique_ptr<DB> db;
        ASSERT_TRUE(DB::Open(opts, &db).ok());
        std::string val;
        ASSERT_TRUE(db->Get("persistent", &val).ok());
        EXPECT_EQ(val, "data");
        db->Close();
    }
}

}  // namespace venus
