#pragma once
#include <memory>
#include <vector>

#include "venus/iterator.h"

namespace venus {

// Merges multiple sorted iterators. children[0] is newest (highest priority).
class MergeIterator : public Iterator {
public:
    explicit MergeIterator(std::vector<std::unique_ptr<Iterator>> children);
    ~MergeIterator() override;

    bool Valid() const override;
    void SeekToFirst() override;
    void Seek(const Slice& target) override;
    void Next() override;
    Slice key() const override;
    Slice value() const override;
    Status status() const override;

private:
    void FindSmallest();

    struct Child {
        std::unique_ptr<Iterator> iter;
        int index;
    };

    std::vector<Child> children_;
    Child* current_ = nullptr;
};

}  // namespace venus
