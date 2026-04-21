#include "venus/block_reader.h"

#include "venus/coding.h"
#include "venus/crc32.h"

namespace venus {

BlockReader::BlockReader(const char* data, size_t size)
    : data_(data), size_(size), num_restarts_(0), restarts_offset_(nullptr),
      data_end_(nullptr) {
    if (size_ < 8) return;  // minimum: num_restarts(4) + crc(4)

    // Last 4 bytes: CRC (we validate separately)
    // Before that: num_restarts (4 bytes)
    // Before that: restart array (num_restarts * 4 bytes)
    size_t content_size = size_ - 4;  // exclude CRC
    num_restarts_ =
        DecodeFixed32(data_ + content_size - 4);
    restarts_offset_ = data_ + content_size - 4 - num_restarts_ * 4;
    data_end_ = restarts_offset_;
}

Status BlockReader::Validate() const {
    if (size_ < 8) return Status::Corruption("Block too small");

    // CRC is last 4 bytes, covers everything before it
    size_t content_size = size_ - 4;
    uint32_t stored_crc = DecodeFixed32(data_ + content_size);
    uint32_t computed_crc = CRC32C(data_, content_size);
    if (stored_crc != computed_crc) {
        return Status::Corruption("Block CRC mismatch");
    }
    return Status::OK();
}

// -- BlockIterator --
class BlockReader::BlockIterator : public Iterator {
public:
    BlockIterator(const char* data, const char* data_end,
                  const char* restarts_offset, uint32_t num_restarts)
        : data_(data),
          data_end_(data_end),
          restarts_offset_(restarts_offset),
          num_restarts_(num_restarts),
          current_(nullptr) {}

    bool Valid() const override { return current_ != nullptr && current_ < data_end_; }

    void SeekToFirst() override {
        current_ = data_;
        current_key_.clear();
        ParseEntry();
    }

    void Seek(const Slice& target) override {
        // Binary search over restart points
        uint32_t left = 0;
        uint32_t right = num_restarts_ - 1;

        while (left < right) {
            uint32_t mid = (left + right + 1) / 2;
            uint32_t offset = GetRestartPoint(mid);
            // Decode key at restart point (shared=0)
            const char* p = data_ + offset;
            uint32_t shared, non_shared, val_len;
            if (!DecodeEntry(p, &shared, &non_shared, &val_len)) {
                current_ = nullptr;
                return;
            }
            Slice mid_key(p, non_shared);
            if (mid_key < target) {
                left = mid;
            } else {
                right = mid - 1;
            }
        }

        // Seek to restart point, then linear scan
        SeekToRestartPoint(left);
        while (Valid() && Slice(current_key_) < target) {
            Next();
        }
    }

    void Next() override {
        if (!Valid()) return;
        current_ = next_entry_;
        if (current_ >= data_end_) {
            current_ = nullptr;
            return;
        }
        ParseEntry();
    }

    Slice key() const override { return current_key_; }
    Slice value() const override { return current_value_; }
    Status status() const override { return status_; }

private:
    uint32_t GetRestartPoint(uint32_t index) const {
        return DecodeFixed32(restarts_offset_ + index * 4);
    }

    void SeekToRestartPoint(uint32_t index) {
        current_ = data_ + GetRestartPoint(index);
        current_key_.clear();
        ParseEntry();
    }

    bool DecodeEntry(const char*& p, uint32_t* shared, uint32_t* non_shared,
                     uint32_t* value_len) {
        const char* limit = data_end_;
        if (!GetVarint32(&p, limit, shared)) return false;
        if (!GetVarint32(&p, limit, non_shared)) return false;
        if (!GetVarint32(&p, limit, value_len)) return false;
        if (p + *non_shared + *value_len > limit) return false;
        return true;
    }

    void ParseEntry() {
        if (!current_ || current_ >= data_end_) {
            current_ = nullptr;
            return;
        }

        const char* p = current_;
        uint32_t shared, non_shared, val_len;
        if (!DecodeEntry(p, &shared, &non_shared, &val_len)) {
            status_ = Status::Corruption("Bad block entry");
            current_ = nullptr;
            return;
        }

        // Reconstruct key from prefix compression
        current_key_.resize(shared);
        current_key_.append(p, non_shared);
        p += non_shared;

        current_value_ = std::string(p, val_len);
        p += val_len;
        next_entry_ = p;
    }

    const char* data_;
    const char* data_end_;
    const char* restarts_offset_;
    uint32_t num_restarts_;
    const char* current_;
    const char* next_entry_ = nullptr;
    std::string current_key_;
    std::string current_value_;
    Status status_;
};

std::unique_ptr<Iterator> BlockReader::NewIterator() const {
    return std::make_unique<BlockIterator>(data_, data_end_, restarts_offset_,
                                          num_restarts_);
}

}  // namespace venus
