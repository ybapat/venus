#include "venus/bloom_filter.h"

#include <algorithm>
#include <cstring>

namespace venus {

// MurmurHash3 32-bit finalizer mix
static uint32_t MurmurHash(const char* data, size_t len, uint32_t seed) {
    uint32_t h = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    const int nblocks = static_cast<int>(len / 4);
    const uint32_t* blocks =
        reinterpret_cast<const uint32_t*>(data);

    for (int i = 0; i < nblocks; i++) {
        uint32_t k;
        memcpy(&k, &blocks[i], sizeof(k));
        k *= c1;
        k = (k << 15) | (k >> 17);
        k *= c2;
        h ^= k;
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }

    const uint8_t* tail =
        reinterpret_cast<const uint8_t*>(data) + nblocks * 4;
    uint32_t k1 = 0;
    switch (len & 3) {
        case 3:
            k1 ^= static_cast<uint32_t>(tail[2]) << 16;
            [[fallthrough]];
        case 2:
            k1 ^= static_cast<uint32_t>(tail[1]) << 8;
            [[fallthrough]];
        case 1:
            k1 ^= tail[0];
            k1 *= c1;
            k1 = (k1 << 15) | (k1 >> 17);
            k1 *= c2;
            h ^= k1;
    }

    h ^= static_cast<uint32_t>(len);
    // fmix32
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

BloomFilter::BloomFilter(size_t expected_keys, size_t bits_per_key) {
    num_bits_ = std::max(expected_keys * bits_per_key, size_t(64));
    // Round up to multiple of 8
    num_bits_ = (num_bits_ + 7) / 8 * 8;
    bits_.resize(num_bits_ / 8, 0);

    // Optimal k = bits_per_key * ln(2) ≈ bits_per_key * 0.693
    num_hashes_ = static_cast<uint8_t>(
        std::max(1.0, std::min(30.0, bits_per_key * 0.693)));
}

BloomFilter::BloomFilter(std::vector<uint8_t> bits, uint8_t num_hashes)
    : bits_(std::move(bits)),
      num_hashes_(num_hashes),
      num_bits_(bits_.size() * 8) {}

uint32_t BloomFilter::Hash(const Slice& key) const {
    return MurmurHash(key.data(), key.size(), 0xbc9f1d34);
}

void BloomFilter::Add(const Slice& key) {
    uint32_t h = Hash(key);
    // Double hashing: h1 + i*h2
    uint32_t delta = (h >> 17) | (h << 15);
    for (uint8_t i = 0; i < num_hashes_; i++) {
        uint32_t bit_pos = h % num_bits_;
        bits_[bit_pos / 8] |= (1 << (bit_pos % 8));
        h += delta;
    }
}

bool BloomFilter::MayContain(const Slice& key) const {
    uint32_t h = Hash(key);
    uint32_t delta = (h >> 17) | (h << 15);
    for (uint8_t i = 0; i < num_hashes_; i++) {
        uint32_t bit_pos = h % num_bits_;
        if ((bits_[bit_pos / 8] & (1 << (bit_pos % 8))) == 0) {
            return false;
        }
        h += delta;
    }
    return true;
}

std::string BloomFilter::Serialize() const {
    std::string result;
    result.append(reinterpret_cast<const char*>(bits_.data()), bits_.size());
    result += static_cast<char>(num_hashes_);
    return result;
}

BloomFilter BloomFilter::Deserialize(const char* data, size_t size) {
    if (size < 1) {
        return BloomFilter(0, 10);
    }
    uint8_t num_hashes = static_cast<uint8_t>(data[size - 1]);
    size_t bits_size = size - 1;
    std::vector<uint8_t> bits(bits_size);
    memcpy(bits.data(), data, bits_size);
    return BloomFilter(std::move(bits), num_hashes);
}

}  // namespace venus
