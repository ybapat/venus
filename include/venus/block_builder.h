#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "venus/slice.h"

namespace venus {

class BlockBuilder {
public:
    explicit BlockBuilder(int restart_interval = 16);

    void Add(const Slice& key, const Slice& value);
    std::string Finish();
    void Reset();

    size_t EstimatedSize() const;
    bool Empty() const;

private:
    std::string buffer_;
    std::vector<uint32_t> restarts_;
    std::string last_key_;
    int entry_count_;
    int restart_interval_;
};

}  // namespace venus
