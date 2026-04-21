#include "venus/memtable.h"

namespace venus {

Memtable::Memtable(size_t size_threshold)
    : size_threshold_(size_threshold) {}

Memtable::~Memtable() = default;

bool Memtable::ShouldFlush() const {
    return skiplist_.ApproximateMemoryUsage() >= size_threshold_;
}

Status Memtable::Put(const std::string& key, const std::string& value) {
    skiplist_.Insert(key, value);
    return Status::OK();
}

Status Memtable::Delete(const std::string& key) {
    skiplist_.InsertDeletion(key);
    return Status::OK();
}

Status Memtable::Get(const Slice& key, std::string* value) {
    bool is_deleted = false;
    auto result = skiplist_.Get(key, &is_deleted);
    if (!result.has_value()) {
        return Status::NotFound("key not in memtable");
    }
    if (is_deleted) {
        return Status::NotFound("key deleted");
    }
    *value = result.value();
    return Status::OK();
}

std::unique_ptr<Iterator> Memtable::NewIterator() const {
    return skiplist_.NewIterator();
}

size_t Memtable::ApproximateMemoryUsage() const {
    return skiplist_.ApproximateMemoryUsage();
}

}  // namespace venus
