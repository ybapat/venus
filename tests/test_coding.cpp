#include <gtest/gtest.h>

#include "venus/coding.h"
#include "venus/crc32.h"

namespace venus {

TEST(CodingTest, Fixed32) {
    char buf[4];
    EncodeFixed32(buf, 0);
    EXPECT_EQ(DecodeFixed32(buf), 0u);

    EncodeFixed32(buf, 12345);
    EXPECT_EQ(DecodeFixed32(buf), 12345u);

    EncodeFixed32(buf, 0xFFFFFFFF);
    EXPECT_EQ(DecodeFixed32(buf), 0xFFFFFFFF);
}

TEST(CodingTest, Fixed64) {
    char buf[8];
    EncodeFixed64(buf, 0);
    EXPECT_EQ(DecodeFixed64(buf), 0u);

    EncodeFixed64(buf, 123456789012345ULL);
    EXPECT_EQ(DecodeFixed64(buf), 123456789012345ULL);

    EncodeFixed64(buf, 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(DecodeFixed64(buf), 0xFFFFFFFFFFFFFFFFULL);
}

TEST(CodingTest, Varint32RoundTrip) {
    std::vector<uint32_t> values = {0, 1, 127, 128, 255, 256, 16383, 16384,
                                    (1u << 21) - 1, (1u << 21),
                                    (1u << 28) - 1, (1u << 28),
                                    0xFFFFFFFF};
    for (uint32_t v : values) {
        std::string encoded;
        PutVarint32(&encoded, v);

        const char* p = encoded.data();
        uint32_t decoded;
        ASSERT_TRUE(GetVarint32(&p, encoded.data() + encoded.size(), &decoded));
        EXPECT_EQ(decoded, v);
        EXPECT_EQ(p, encoded.data() + encoded.size());
    }
}

TEST(CodingTest, Varint64RoundTrip) {
    std::vector<uint64_t> values = {0, 1, 127, 128, (1ULL << 32),
                                    (1ULL << 48), 0xFFFFFFFFFFFFFFFFULL};
    for (uint64_t v : values) {
        std::string encoded;
        PutVarint64(&encoded, v);

        const char* p = encoded.data();
        uint64_t decoded;
        ASSERT_TRUE(GetVarint64(&p, encoded.data() + encoded.size(), &decoded));
        EXPECT_EQ(decoded, v);
    }
}

TEST(CodingTest, MultipleVarintsInSequence) {
    std::string buf;
    PutVarint32(&buf, 100);
    PutVarint32(&buf, 200);
    PutVarint32(&buf, 300);

    const char* p = buf.data();
    const char* limit = buf.data() + buf.size();
    uint32_t v;

    ASSERT_TRUE(GetVarint32(&p, limit, &v));
    EXPECT_EQ(v, 100u);
    ASSERT_TRUE(GetVarint32(&p, limit, &v));
    EXPECT_EQ(v, 200u);
    ASSERT_TRUE(GetVarint32(&p, limit, &v));
    EXPECT_EQ(v, 300u);
}

TEST(CodingTest, LengthPrefixedSlice) {
    std::string buf;
    PutLengthPrefixedSlice(&buf, "hello");
    PutLengthPrefixedSlice(&buf, "world");
    PutLengthPrefixedSlice(&buf, "");

    const char* p = buf.data();
    const char* limit = buf.data() + buf.size();
    std::string result;

    ASSERT_TRUE(GetLengthPrefixedSlice(&p, limit, &result));
    EXPECT_EQ(result, "hello");
    ASSERT_TRUE(GetLengthPrefixedSlice(&p, limit, &result));
    EXPECT_EQ(result, "world");
    ASSERT_TRUE(GetLengthPrefixedSlice(&p, limit, &result));
    EXPECT_EQ(result, "");
}

TEST(CRC32Test, BasicChecksum) {
    // Empty data
    uint32_t crc_empty = CRC32C("", 0);
    EXPECT_EQ(crc_empty, CRC32C("", 0));  // deterministic

    // Known data
    std::string data = "hello world";
    uint32_t crc1 = CRC32C(data.data(), data.size());
    uint32_t crc2 = CRC32C(data.data(), data.size());
    EXPECT_EQ(crc1, crc2);

    // Different data produces different CRC
    std::string data2 = "hello worle";
    uint32_t crc3 = CRC32C(data2.data(), data2.size());
    EXPECT_NE(crc1, crc3);
}

TEST(CRC32Test, Extend) {
    std::string full = "hello world";
    uint32_t crc_full = CRC32C(full.data(), full.size());

    uint32_t crc_part = CRC32C("hello ", 6);
    uint32_t crc_ext = CRC32C_Extend(crc_part, "world", 5);
    EXPECT_EQ(crc_full, crc_ext);
}

TEST(CRC32Test, MaskUnmask) {
    std::string data = "test data for masking";
    uint32_t crc = CRC32C(data.data(), data.size());
    uint32_t masked = MaskCRC(crc);
    EXPECT_NE(crc, masked);
    uint32_t unmasked = UnmaskCRC(masked);
    EXPECT_EQ(crc, unmasked);
}

}  // namespace venus
