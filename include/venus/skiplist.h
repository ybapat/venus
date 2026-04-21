#pragma once
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "venus/iterator.h"
#include "venus/slice.h"

namespace venus {

class SkipList {
public:
    static constexpr int kMaxHeight = 12;

    SkipList();
    ~SkipList();

    // Insert or update a key. Returns delta in memory usage.
    size_t Insert(const std::string& key, const std::string& value);

    // Insert a deletion tombstone. Returns delta in memory usage.
    size_t InsertDeletion(const std::string& key);

    // Lookup. Returns nullopt if not found. Sets is_deleted if tombstone.
    std::optional<std::string> Get(const Slice& key,
                                   bool* is_deleted = nullptr) const;

    size_t ApproximateMemoryUsage() const { return memory_usage_; }
    size_t Count() const { return count_; }
    bool Empty() const { return count_ == 0; }

    std::unique_ptr<Iterator> NewIterator() const;

private:
    struct Node {
        std::string key;
        std::string value;
        bool is_deletion;
        std::vector<Node*> forward;

        Node(std::string k, std::string v, int height, bool del = false)
            : key(std::move(k)),
              value(std::move(v)),
              is_deletion(del),
              forward(height, nullptr) {}
    };

    int RandomHeight();
    Node* FindGreaterOrEqual(const Slice& key, Node** prev) const;

    Node* head_;
    int current_height_;
    size_t memory_usage_;
    size_t count_;
    std::mt19937 rng_;

    class SkipListIterator;
};

}  // namespace venus
