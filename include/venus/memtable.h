#pragma once
#include <memory>
#include <string>

#include "venus/iterator.h"
#include "venus/skiplist.h"
#include "venus/slice.h"
#include "venus/status.h"

namespace venus {

class Memtable {
public:
    explicit Memtable(size_t size_threshold);
    ~Memtable();

    bool ShouldFlush() const;

    Status Put(const std::string& key, const std::string& value);
    Status Delete(const std::string& key);
    Status Get(const Slice& key, std::string* value);

    std::unique_ptr<Iterator> NewIterator() const;

    size_t ApproximateMemoryUsage() const;

private:
    SkipList skiplist_;
    size_t size_threshold_;
};

}  // namespace venus
