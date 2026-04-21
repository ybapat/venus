#include "venus/sstable_reader.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "venus/block_reader.h"
#include "venus/coding.h"
#include "venus/crc32.h"

namespace venus {

static constexpr uint32_t kMagicNumber = 0x56454E55;
static constexpr size_t kFooterSize = 48;

SSTableReader::~SSTableReader() {
    if (fd_ >= 0) ::close(fd_);
}

Status SSTableReader::ReadBlock(uint64_t offset, uint32_t size,
                                 std::string* result) {
    result->resize(size);
    ssize_t n = ::pread(fd_, &(*result)[0], size, offset);
    if (n != static_cast<ssize_t>(size)) {
        return Status::IOError("Failed to read block from " + filepath_);
    }
    return Status::OK();
}

Status SSTableReader::Open(const std::string& filepath,
                            std::unique_ptr<SSTableReader>* reader) {
    auto r = std::unique_ptr<SSTableReader>(new SSTableReader());
    r->filepath_ = filepath;
    r->fd_ = ::open(filepath.c_str(), O_RDONLY);
    if (r->fd_ < 0) {
        return Status::IOError("Failed to open SSTable: " + filepath);
    }

    // Get file size
    struct stat st;
    if (fstat(r->fd_, &st) != 0) {
        return Status::IOError("Failed to stat SSTable: " + filepath);
    }
    size_t file_size = st.st_size;

    if (file_size < kFooterSize) {
        return Status::Corruption("SSTable too small: " + filepath);
    }

    // Read footer
    char footer_buf[kFooterSize];
    ssize_t n = ::pread(r->fd_, footer_buf, kFooterSize,
                        file_size - kFooterSize);
    // The footer may be followed by the smallest key. We need to figure out
    // the layout. Let's re-read: footer is at a position that depends on
    // smallest_key_size.
    //
    // Actually, the builder writes: [footer 48B][smallest_key]
    // So we need to read from the end. First, let's try reading the last
    // part of the file to find the footer.
    //
    // Strategy: read last 48 + 256 bytes (max reasonable key), scan for magic.
    // Simpler: read footer at known position. The builder appends smallest_key
    // after footer, so footer is at (file_size - 48 - smallest_key_size).
    // But we don't know smallest_key_size yet.
    //
    // Let's fix: read last 48 bytes as potential footer first, check magic.
    // If that doesn't work, we need to scan.
    //
    // Actually, the simplest approach: try to read the footer, then read
    // smallest_key_size from offset 28 in footer, and re-read if needed.

    // First attempt: assume smallest_key is at the very end
    // footer is at file_size - 48 - smallest_key_size
    // But we need to determine smallest_key_size...
    //
    // Let me re-think the format: let's read from the very end.
    // The last bytes of file are: [footer 48][smallest_key]
    // We need to find where footer starts.
    //
    // Approach: read last 256 bytes, find magic at offset 44 within a 48-byte
    // window.

    // Better approach: read the last 48 bytes to check if smallest_key_size=0
    // Then read smallest_key_size and recalculate.

    // Read from a position assuming we'll find the footer structure.
    // The file layout is: [data blocks][index block][bloom block][footer 48B][smallest_key bytes]
    // So the footer is at (file_size - 48 - smallest_key_len), but we don't know smallest_key_len.
    // However, smallest_key_len is stored at footer+28.
    // Two-pass: first read the last 300 bytes, then parse.

    size_t tail_size = std::min(file_size, size_t(512));
    std::string tail(tail_size, '\0');
    n = ::pread(r->fd_, &tail[0], tail_size, file_size - tail_size);
    if (n != static_cast<ssize_t>(tail_size)) {
        return Status::IOError("Failed to read SSTable tail: " + filepath);
    }

    // Scan for magic number from the end
    bool found = false;
    size_t footer_pos_in_tail = 0;
    for (int off = static_cast<int>(tail_size) - 4; off >= 44; off--) {
        uint32_t magic = DecodeFixed32(tail.data() + off);
        if (magic == kMagicNumber) {
            // footer starts at off - 44
            footer_pos_in_tail = off - 44;
            found = true;
            break;
        }
    }

    if (!found) {
        return Status::Corruption("SSTable missing magic number: " + filepath);
    }

    const char* fp = tail.data() + footer_pos_in_tail;
    r->footer_.index_block_offset = DecodeFixed64(fp + 0);
    r->footer_.index_block_size = DecodeFixed32(fp + 8);
    r->footer_.bloom_block_offset = DecodeFixed64(fp + 12);
    r->footer_.bloom_block_size = DecodeFixed32(fp + 20);
    r->footer_.num_entries = DecodeFixed32(fp + 24);
    uint32_t smallest_key_size = DecodeFixed32(fp + 28);

    // smallest key follows the footer
    size_t smallest_key_start = footer_pos_in_tail + kFooterSize;
    if (smallest_key_start + smallest_key_size <= tail_size) {
        r->footer_.smallest_key.assign(
            tail.data() + smallest_key_start, smallest_key_size);
    }

    // Read index block
    Status s = r->ReadBlock(r->footer_.index_block_offset,
                             r->footer_.index_block_size,
                             &r->index_block_data_);
    if (!s.ok()) return s;

    // Validate index block CRC
    BlockReader index_reader(r->index_block_data_.data(),
                              r->index_block_data_.size());
    s = index_reader.Validate();
    if (!s.ok()) return s;

    // Find largest key from last index entry
    auto idx_it = index_reader.NewIterator();
    idx_it->SeekToFirst();
    while (idx_it->Valid()) {
        r->footer_.largest_key = idx_it->key().ToString();
        idx_it->Next();
    }

    // Read bloom filter block
    std::string bloom_data;
    s = r->ReadBlock(r->footer_.bloom_block_offset,
                      r->footer_.bloom_block_size, &bloom_data);
    if (!s.ok()) return s;

    // Validate bloom CRC (last 4 bytes)
    if (bloom_data.size() >= 4) {
        size_t content_size = bloom_data.size() - 4;
        uint32_t stored_crc =
            DecodeFixed32(bloom_data.data() + content_size);
        uint32_t computed_crc =
            CRC32C(bloom_data.data(), content_size);
        if (stored_crc != computed_crc) {
            return Status::Corruption("Bloom filter CRC mismatch: " +
                                      filepath);
        }
        r->bloom_ = std::make_unique<BloomFilter>(
            BloomFilter::Deserialize(bloom_data.data(), content_size));
    }

    *reader = std::move(r);
    return Status::OK();
}

bool SSTableReader::MayContain(const Slice& key) const {
    if (!bloom_) return true;
    return bloom_->MayContain(key);
}

Status SSTableReader::Get(const ReadOptions& opts, const Slice& key,
                           std::string* value) {
    // Check bloom filter
    if (!MayContain(key)) {
        return Status::NotFound("bloom filter negative");
    }

    // Search index block for the data block that may contain the key
    BlockReader index_reader(index_block_data_.data(),
                              index_block_data_.size());
    auto idx_it = index_reader.NewIterator();
    idx_it->Seek(key);

    if (!idx_it->Valid()) {
        return Status::NotFound("key beyond range");
    }

    // Decode block handle from index entry value
    Slice handle = idx_it->value();
    if (handle.size() < 12) {
        return Status::Corruption("Bad index entry");
    }
    uint64_t block_offset = DecodeFixed64(handle.data());
    uint32_t block_size = DecodeFixed32(handle.data() + 8);

    // Read data block
    std::string block_data;
    Status s = ReadBlock(block_offset, block_size, &block_data);
    if (!s.ok()) return s;

    BlockReader data_reader(block_data.data(), block_data.size());
    if (opts.verify_checksums) {
        s = data_reader.Validate();
        if (!s.ok()) return s;
    }

    // Search within data block
    auto data_it = data_reader.NewIterator();
    data_it->Seek(key);

    if (data_it->Valid() && data_it->key() == key) {
        *value = data_it->value().ToString();
        return Status::OK();
    }

    return Status::NotFound("key not in SSTable");
}

// -- SSTableIterator: iterates over all entries across all data blocks --
class SSTableIterator : public Iterator {
public:
    SSTableIterator(SSTableReader* reader, const ReadOptions& opts)
        : reader_(reader), opts_(opts) {}

    bool Valid() const override {
        return data_iter_ && data_iter_->Valid();
    }

    void SeekToFirst() override {
        BlockReader index_reader(reader_->index_block_data_.data(),
                                  reader_->index_block_data_.size());
        index_iter_ = index_reader.NewIterator();
        index_iter_->SeekToFirst();
        // We need to keep the index block data alive, so store it
        InitDataBlock();
    }

    void Seek(const Slice& target) override {
        BlockReader index_reader(reader_->index_block_data_.data(),
                                  reader_->index_block_data_.size());
        index_iter_ = index_reader.NewIterator();
        index_iter_->Seek(target);
        InitDataBlock();
        if (data_iter_ && data_iter_->Valid()) {
            data_iter_->Seek(target);
            SkipToValid();
        }
    }

    void Next() override {
        if (!Valid()) return;
        data_iter_->Next();
        SkipToValid();
    }

    Slice key() const override { return data_iter_->key(); }
    Slice value() const override { return data_iter_->value(); }
    Status status() const override { return status_; }

private:
    void InitDataBlock() {
        data_iter_.reset();
        if (!index_iter_ || !index_iter_->Valid()) return;

        Slice handle = index_iter_->value();
        if (handle.size() < 12) {
            status_ = Status::Corruption("Bad index handle");
            return;
        }
        uint64_t offset = DecodeFixed64(handle.data());
        uint32_t size = DecodeFixed32(handle.data() + 8);

        Status s = reader_->ReadBlock(offset, size, &current_block_data_);
        if (!s.ok()) {
            status_ = s;
            return;
        }

        current_block_reader_ = std::make_unique<BlockReader>(
            current_block_data_.data(), current_block_data_.size());
        data_iter_ = current_block_reader_->NewIterator();
        data_iter_->SeekToFirst();
    }

    void SkipToValid() {
        while (data_iter_ && !data_iter_->Valid()) {
            // Move to next data block
            index_iter_->Next();
            if (!index_iter_->Valid()) {
                data_iter_.reset();
                return;
            }
            InitDataBlock();
        }
    }

    SSTableReader* reader_;
    ReadOptions opts_;
    std::unique_ptr<Iterator> index_iter_;
    std::unique_ptr<Iterator> data_iter_;
    std::unique_ptr<BlockReader> current_block_reader_;
    std::string current_block_data_;
    Status status_;
};

std::unique_ptr<Iterator> SSTableReader::NewIterator(const ReadOptions& opts) {
    return std::make_unique<SSTableIterator>(this, opts);
}

}  // namespace venus
