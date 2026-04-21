#include <gtest/gtest.h>

#include <string>

#include "venus/bloom_filter.h"

namespace venus {

TEST(BloomFilterTest, BasicAddAndContains) {
    BloomFilter bf(100);
    bf.Add("hello");
    bf.Add("world");
    bf.Add("foo");

    EXPECT_TRUE(bf.MayContain("hello"));
    EXPECT_TRUE(bf.MayContain("world"));
    EXPECT_TRUE(bf.MayContain("foo"));
}

TEST(BloomFilterTest, DefinitelyNotPresent) {
    BloomFilter bf(100);
    for (int i = 0; i < 100; i++) {
        bf.Add("key_" + std::to_string(i));
    }

    // Check that keys we didn't add mostly return false
    int false_positives = 0;
    int total_checks = 10000;
    for (int i = 100; i < 100 + total_checks; i++) {
        if (bf.MayContain("key_" + std::to_string(i))) {
            false_positives++;
        }
    }

    double fp_rate = static_cast<double>(false_positives) / total_checks;
    // With 10 bits per key, expect ~1% FP rate. Allow up to 3%.
    EXPECT_LT(fp_rate, 0.03) << "False positive rate: " << fp_rate;
}

TEST(BloomFilterTest, SerializeDeserialize) {
    BloomFilter bf(100);
    for (int i = 0; i < 100; i++) {
        bf.Add("key_" + std::to_string(i));
    }

    std::string serialized = bf.Serialize();
    BloomFilter bf2 = BloomFilter::Deserialize(serialized.data(),
                                                serialized.size());

    // All original keys should still be found
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(bf2.MayContain("key_" + std::to_string(i)));
    }
}

TEST(BloomFilterTest, EmptyFilter) {
    BloomFilter bf(0);
    // Empty filter should not crash
    EXPECT_FALSE(bf.MayContain("anything"));
}

}  // namespace venus
