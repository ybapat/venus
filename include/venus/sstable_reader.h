#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "venus/bloom_filter.h"
#include "venus/iterator.h"
#include "venus/options.h"
#include "venus/slice.h"
#include "venus/status.h"

namespace venus {

struct SSTableFooter {
    uint64_t index_block_offset;
    uint32_t index_block_size;
    uint64_t bloom_block_offset;
    uint32_t bloom_block_size;
    uint32_t num_entries;
    std::string smallest_key;
    std::string largest_key;
};

class SSTableReader {
public:
    static Status Open(const std::string& filepath,
                       std::unique_ptr<SSTableReader>* reader);

    Status Get(const ReadOptions& opts, const Slice& key, std::string* value);
    std::unique_ptr<Iterator> NewIterator(const ReadOptions& opts);
    bool MayContain(const Slice& key) const;

    const SSTableFooter& GetFooter() const { return footer_; }
    const std::string& FilePath() const { return filepath_; }

    ~SSTableReader();

private:
    friend class SSTableIterator;

    SSTableReader() = default;
    Status ReadBlock(uint64_t offset, uint32_t size, std::string* result);

    std::string filepath_;
    int fd_ = -1;
    SSTableFooter footer_;
    std::string index_block_data_;
    std::unique_ptr<BloomFilter> bloom_;
};

}  // namespace venus
