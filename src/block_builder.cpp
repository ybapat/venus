#include "venus/block_builder.h"

#include "venus/coding.h"
#include "venus/crc32.h"

namespace venus {

BlockBuilder::BlockBuilder(int restart_interval)
    : entry_count_(0), restart_interval_(restart_interval) {
    restarts_.push_back(0);  // first restart at offset 0
}

void BlockBuilder::Add(const Slice& key, const Slice& value) {
    size_t shared = 0;
    if (entry_count_ % restart_interval_ != 0) {
        // Compute shared prefix with last key
        size_t min_len = std::min(last_key_.size(), key.size());
        while (shared < min_len && last_key_[shared] == key[shared]) {
            shared++;
        }
    } else {
        // Restart point: store full key
        restarts_.push_back(static_cast<uint32_t>(buffer_.size()));
    }

    size_t non_shared = key.size() - shared;

    PutVarint32(&buffer_, static_cast<uint32_t>(shared));
    PutVarint32(&buffer_, static_cast<uint32_t>(non_shared));
    PutVarint32(&buffer_, static_cast<uint32_t>(value.size()));
    buffer_.append(key.data() + shared, non_shared);
    buffer_.append(value.data(), value.size());

    last_key_ = key.ToString();
    entry_count_++;
}

std::string BlockBuilder::Finish() {
    // Append restart array
    for (uint32_t restart : restarts_) {
        char buf[4];
        EncodeFixed32(buf, restart);
        buffer_.append(buf, 4);
    }
    // Append num_restarts
    char buf[4];
    EncodeFixed32(buf, static_cast<uint32_t>(restarts_.size()));
    buffer_.append(buf, 4);

    // Append CRC32C of everything
    uint32_t crc = CRC32C(buffer_.data(), buffer_.size());
    EncodeFixed32(buf, crc);
    buffer_.append(buf, 4);

    return buffer_;
}

void BlockBuilder::Reset() {
    buffer_.clear();
    restarts_.clear();
    restarts_.push_back(0);
    last_key_.clear();
    entry_count_ = 0;
}

size_t BlockBuilder::EstimatedSize() const {
    return buffer_.size() + restarts_.size() * 4 + 4 + 4;
}

bool BlockBuilder::Empty() const { return entry_count_ == 0; }

}  // namespace venus
