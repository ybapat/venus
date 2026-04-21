#include "venus/merge_iterator.h"

namespace venus {

MergeIterator::MergeIterator(
    std::vector<std::unique_ptr<Iterator>> children) {
    for (int i = 0; i < static_cast<int>(children.size()); i++) {
        children_.push_back(Child{std::move(children[i]), i});
    }
}

MergeIterator::~MergeIterator() = default;

bool MergeIterator::Valid() const {
    return current_ != nullptr;
}

void MergeIterator::SeekToFirst() {
    for (auto& child : children_) {
        child.iter->SeekToFirst();
    }
    FindSmallest();
}

void MergeIterator::Seek(const Slice& target) {
    for (auto& child : children_) {
        child.iter->Seek(target);
    }
    FindSmallest();
}

void MergeIterator::Next() {
    if (!Valid()) return;

    // Advance current, and also advance any other children that have the
    // same key (deduplication: only the newest version matters).
    Slice cur_key = current_->iter->key();
    for (auto& child : children_) {
        if (child.iter->Valid() && child.iter->key() == cur_key) {
            child.iter->Next();
        }
    }
    FindSmallest();
}

Slice MergeIterator::key() const { return current_->iter->key(); }
Slice MergeIterator::value() const { return current_->iter->value(); }

Status MergeIterator::status() const {
    for (auto& child : children_) {
        if (!child.iter->status().ok()) return child.iter->status();
    }
    return Status::OK();
}

void MergeIterator::FindSmallest() {
    current_ = nullptr;
    for (auto& child : children_) {
        if (!child.iter->Valid()) continue;
        if (current_ == nullptr) {
            current_ = &child;
        } else {
            int cmp = child.iter->key().Compare(current_->iter->key());
            if (cmp < 0 || (cmp == 0 && child.index < current_->index)) {
                current_ = &child;
            }
        }
    }
}

}  // namespace venus
