#pragma once
#include <cassert>
#include <cstring>
#include <string>
#include <string_view>

namespace venus {

class Slice {
public:
    Slice() : data_(nullptr), size_(0) {}
    Slice(const char* d, size_t n) : data_(d), size_(n) {}
    Slice(const std::string& s) : data_(s.data()), size_(s.size()) {}
    Slice(const char* s) : data_(s), size_(strlen(s)) {}
    Slice(std::string_view sv) : data_(sv.data()), size_(sv.size()) {}

    const char* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    char operator[](size_t n) const {
        assert(n < size_);
        return data_[n];
    }

    std::string ToString() const { return std::string(data_, size_); }
    std::string_view ToStringView() const { return {data_, size_}; }

    int Compare(const Slice& b) const {
        size_t min_len = std::min(size_, b.size_);
        int r = memcmp(data_, b.data_, min_len);
        if (r == 0) {
            if (size_ < b.size_)
                r = -1;
            else if (size_ > b.size_)
                r = +1;
        }
        return r;
    }

    bool operator==(const Slice& o) const { return Compare(o) == 0; }
    bool operator!=(const Slice& o) const { return Compare(o) != 0; }
    bool operator<(const Slice& o) const { return Compare(o) < 0; }
    bool operator<=(const Slice& o) const { return Compare(o) <= 0; }
    bool operator>(const Slice& o) const { return Compare(o) > 0; }
    bool operator>=(const Slice& o) const { return Compare(o) >= 0; }

private:
    const char* data_;
    size_t size_;
};

}  // namespace venus
