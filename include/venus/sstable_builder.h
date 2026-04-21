#pragma once
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

#include "venus/block_builder.h"
#include "venus/bloom_filter.h"
#include "venus/options.h"
#include "venus/slice.h"
#include "venus/status.h"

namespace venus {

class SSTableBuilder {
public:
    SSTableBuilder(const std::string& filepath, const Options& opts,
                   size_t expected_entries = 1024);
    ~SSTableBuilder();

    // Keys MUST be added in sorted order.
    Status Add(const Slice& key, const Slice& value);
    Status Finish();
    void Abandon();

    size_t NumEntries() const { return num_entries_; }
    uint64_t FileSize() const { return offset_; }

private:
    Status FlushDataBlock();
    Status WriteRawBlock(const std::string& data, uint64_t* offset,
                         uint32_t* size);

    std::ofstream file_;
    std::string filepath_;
    Options opts_;

    BlockBuilder data_block_;
    BlockBuilder index_block_;
    std::unique_ptr<BloomFilter> bloom_;

    std::string last_key_;
    std::string smallest_key_;
    uint64_t pending_block_offset_ = 0;
    uint32_t pending_block_size_ = 0;
    bool pending_index_entry_ = false;
    uint64_t offset_ = 0;
    size_t num_entries_ = 0;
    bool finished_ = false;
};

}  // namespace venus
