#include "venus/skiplist.h"

namespace venus {

// -- SkipListIterator --
class SkipList::SkipListIterator : public Iterator {
public:
    explicit SkipListIterator(const SkipList* list)
        : list_(list), current_(nullptr) {}

    bool Valid() const override { return current_ != nullptr; }

    void SeekToFirst() override { current_ = list_->head_->forward[0]; }

    void Seek(const Slice& target) override {
        current_ = list_->FindGreaterOrEqual(target, nullptr);
    }

    void Next() override {
        if (current_) current_ = current_->forward[0];
    }

    Slice key() const override { return current_->key; }

    Slice value() const override { return current_->value; }

    Status status() const override { return Status::OK(); }

    bool is_deletion() const { return current_->is_deletion; }

private:
    const SkipList* list_;
    Node* current_;
};

// -- SkipList --
SkipList::SkipList()
    : head_(new Node("", "", kMaxHeight)),
      current_height_(1),
      memory_usage_(0),
      count_(0),
      rng_(std::random_device{}()) {}

SkipList::~SkipList() {
    Node* node = head_;
    while (node) {
        Node* next = node->forward[0];
        delete node;
        node = next;
    }
}

int SkipList::RandomHeight() {
    int height = 1;
    // p = 1/4 branching factor
    while (height < kMaxHeight &&
           (rng_() % 4) == 0) {
        height++;
    }
    return height;
}

SkipList::Node* SkipList::FindGreaterOrEqual(const Slice& key,
                                              Node** prev) const {
    Node* x = head_;
    int level = current_height_ - 1;
    while (level >= 0) {
        Node* next = x->forward[level];
        if (next && Slice(next->key) < key) {
            x = next;
        } else {
            if (prev) prev[level] = x;
            level--;
        }
    }
    return x->forward[0];
}

size_t SkipList::Insert(const std::string& key, const std::string& value) {
    Node* prev[kMaxHeight];
    Node* x = FindGreaterOrEqual(key, prev);

    // If key exists, update in place
    if (x && Slice(x->key) == Slice(key)) {
        size_t old_size = x->value.size() + (x->is_deletion ? 0 : 0);
        size_t delta = value.size() - old_size;
        x->value = value;
        x->is_deletion = false;
        memory_usage_ += delta;
        return delta;
    }

    int height = RandomHeight();
    if (height > current_height_) {
        for (int i = current_height_; i < height; i++) {
            prev[i] = head_;
        }
        current_height_ = height;
    }

    Node* new_node = new Node(key, value, height);
    for (int i = 0; i < height; i++) {
        new_node->forward[i] = prev[i]->forward[i];
        prev[i]->forward[i] = new_node;
    }

    size_t added = key.size() + value.size() + sizeof(Node) +
                   height * sizeof(Node*);
    memory_usage_ += added;
    count_++;
    return added;
}

size_t SkipList::InsertDeletion(const std::string& key) {
    Node* prev[kMaxHeight];
    Node* x = FindGreaterOrEqual(key, prev);

    // If key exists, mark as deletion
    if (x && Slice(x->key) == Slice(key)) {
        size_t freed = x->value.size();
        x->value.clear();
        x->is_deletion = true;
        memory_usage_ -= freed;
        return 0;
    }

    int height = RandomHeight();
    if (height > current_height_) {
        for (int i = current_height_; i < height; i++) {
            prev[i] = head_;
        }
        current_height_ = height;
    }

    Node* new_node = new Node(key, "", height, true);
    for (int i = 0; i < height; i++) {
        new_node->forward[i] = prev[i]->forward[i];
        prev[i]->forward[i] = new_node;
    }

    size_t added = key.size() + sizeof(Node) + height * sizeof(Node*);
    memory_usage_ += added;
    count_++;
    return added;
}

std::optional<std::string> SkipList::Get(const Slice& key,
                                          bool* is_deleted) const {
    Node* x = FindGreaterOrEqual(key, nullptr);
    if (x && Slice(x->key) == key) {
        if (is_deleted) *is_deleted = x->is_deletion;
        if (x->is_deletion) return "";
        return x->value;
    }
    return std::nullopt;
}

std::unique_ptr<Iterator> SkipList::NewIterator() const {
    return std::make_unique<SkipListIterator>(this);
}

}  // namespace venus
