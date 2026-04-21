#include "venus/sstable_builder.h"

#include "venus/coding.h"
#include "venus/crc32.h"

namespace venus {

static constexpr uint32_t kMagicNumber = 0x56454E55;  // "VENU"
static constexpr size_t kFooterSize = 48;

SSTableBuilder::SSTableBuilder(const std::string& filepath, const Options& opts,
                               size_t expected_entries)
    : filepath_(filepath),
      opts_(opts),
      data_block_(opts.block_restart_interval),
      index_block_(1) {
    file_.open(filepath, std::ios::binary | std::ios::trunc);
    bloom_ = std::make_unique<BloomFilter>(expected_entries,
                                           opts.bloom_bits_per_key);
}

SSTableBuilder::~SSTableBuilder() {
    if (!finished_) Abandon();
}

Status SSTableBuilder::Add(const Slice& key, const Slice& value) {
    if (finished_) {
        return Status::InvalidArgument("SSTableBuilder already finished");
    }

    // Record smallest key
    if (num_entries_ == 0) {
        smallest_key_ = key.ToString();
    }

    // Add pending index entry for previous data block
    if (pending_index_entry_) {
        // Index entry: key = last key of previous block, value = offset+size
        std::string handle;
        handle.resize(12);
        EncodeFixed64(&handle[0], pending_block_offset_);
        EncodeFixed32(&handle[8], pending_block_size_);
        index_block_.Add(last_key_, handle);
        pending_index_entry_ = false;
    }

    bloom_->Add(key);
    data_block_.Add(key, value);
    last_key_ = key.ToString();
    num_entries_++;

    // Flush data block if it exceeds target size
    if (data_block_.EstimatedSize() >= opts_.block_size) {
        return FlushDataBlock();
    }
    return Status::OK();
}

Status SSTableBuilder::FlushDataBlock() {
    if (data_block_.Empty()) return Status::OK();

    std::string block_data = data_block_.Finish();
    Status s = WriteRawBlock(block_data, &pending_block_offset_,
                              &pending_block_size_);
    if (!s.ok()) return s;

    pending_index_entry_ = true;
    data_block_.Reset();
    return Status::OK();
}

Status SSTableBuilder::WriteRawBlock(const std::string& data,
                                      uint64_t* offset, uint32_t* size) {
    *offset = offset_;
    *size = static_cast<uint32_t>(data.size());
    file_.write(data.data(), data.size());
    if (!file_.good()) {
        return Status::IOError("Failed to write block to " + filepath_);
    }
    offset_ += data.size();
    return Status::OK();
}

Status SSTableBuilder::Finish() {
    if (finished_) {
        return Status::InvalidArgument("SSTableBuilder already finished");
    }
    finished_ = true;

    // Flush remaining data block
    Status s = FlushDataBlock();
    if (!s.ok()) return s;

    // Add final index entry
    if (pending_index_entry_) {
        std::string handle;
        handle.resize(12);
        EncodeFixed64(&handle[0], pending_block_offset_);
        EncodeFixed32(&handle[8], pending_block_size_);
        index_block_.Add(last_key_, handle);
    }

    // Write index block
    std::string index_data = index_block_.Finish();
    uint64_t index_offset;
    uint32_t index_size;
    s = WriteRawBlock(index_data, &index_offset, &index_size);
    if (!s.ok()) return s;

    // Write bloom filter block
    std::string bloom_data = bloom_->Serialize();
    // Add CRC to bloom block
    uint32_t bloom_crc = CRC32C(bloom_data.data(), bloom_data.size());
    char crc_buf[4];
    EncodeFixed32(crc_buf, bloom_crc);
    bloom_data.append(crc_buf, 4);

    uint64_t bloom_offset;
    uint32_t bloom_size;
    s = WriteRawBlock(bloom_data, &bloom_offset, &bloom_size);
    if (!s.ok()) return s;

    // Write footer (48 bytes)
    char footer[kFooterSize];
    memset(footer, 0, kFooterSize);
    EncodeFixed64(footer + 0, index_offset);
    EncodeFixed32(footer + 8, index_size);
    EncodeFixed64(footer + 12, bloom_offset);
    EncodeFixed32(footer + 20, bloom_size);
    EncodeFixed32(footer + 24, static_cast<uint32_t>(num_entries_));
    // smallest key offset and size (within the footer we just store the length;
    // the actual smallest key is in the first data block)
    EncodeFixed32(footer + 28,
                  static_cast<uint32_t>(smallest_key_.size()));
    // reserved
    EncodeFixed32(footer + 40, 0);
    // magic
    EncodeFixed32(footer + 44, kMagicNumber);

    file_.write(footer, kFooterSize);
    // Write smallest key after footer
    file_.write(smallest_key_.data(), smallest_key_.size());

    if (!file_.good()) {
        return Status::IOError("Failed to write footer to " + filepath_);
    }

    offset_ += kFooterSize + smallest_key_.size();
    file_.close();
    return Status::OK();
}

void SSTableBuilder::Abandon() {
    finished_ = true;
    file_.close();
    std::remove(filepath_.c_str());
}

}  // namespace venus
