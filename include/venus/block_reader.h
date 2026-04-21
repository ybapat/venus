#pragma once
#include <cstddef>
#include <memory>
#include <string>

#include "venus/iterator.h"
#include "venus/status.h"

namespace venus {

class BlockReader {
public:
    // data must remain valid for the lifetime of the reader.
    BlockReader(const char* data, size_t size);

    Status Validate() const;
    std::unique_ptr<Iterator> NewIterator() const;

    const char* data() const { return data_; }
    size_t size() const { return size_; }

private:
    const char* data_;
    size_t size_;
    uint32_t num_restarts_;
    const char* restarts_offset_;
    const char* data_end_;  // end of entry data (start of restart array)

    class BlockIterator;
};

}  // namespace venus
