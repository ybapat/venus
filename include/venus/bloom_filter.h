#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "venus/slice.h"

namespace venus {

class BloomFilter {
public:
    explicit BloomFilter(size_t expected_keys, size_t bits_per_key = 10);
    BloomFilter(std::vector<uint8_t> bits, uint8_t num_hashes);

    void Add(const Slice& key);
    bool MayContain(const Slice& key) const;

    std::string Serialize() const;
    static BloomFilter Deserialize(const char* data, size_t size);

    uint8_t NumHashes() const { return num_hashes_; }

private:
    uint32_t Hash(const Slice& key) const;

    std::vector<uint8_t> bits_;
    uint8_t num_hashes_;
    size_t num_bits_;
};

}  // namespace venus
