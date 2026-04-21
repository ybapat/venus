#include "venus/wal.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "venus/coding.h"
#include "venus/crc32.h"

namespace venus {

// WAL entry format:
// [CRC32C: 4 bytes fixed][payload...]
// payload = [record_type: 1 byte][key_len: varint][key][value_len: varint][value]
// CRC covers the entire payload.

WALWriter::~WALWriter() {
    if (fd_ >= 0) Close();
}

Status WALWriter::Open(const std::string& path,
                       std::unique_ptr<WALWriter>* writer) {
    auto w = std::unique_ptr<WALWriter>(new WALWriter());
    w->path_ = path;
    w->fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (w->fd_ < 0) {
        return Status::IOError("Failed to open WAL: " + path);
    }
    *writer = std::move(w);
    return Status::OK();
}

Status WALWriter::AddPut(const Slice& key, const Slice& value) {
    return AddRecord(WALRecordType::kPut, key, value);
}

Status WALWriter::AddDelete(const Slice& key) {
    return AddRecord(WALRecordType::kDelete, key, Slice());
}

Status WALWriter::AddRecord(WALRecordType type, const Slice& key,
                             const Slice& value) {
    // Build payload
    std::string payload;
    payload += static_cast<char>(type);
    PutVarint32(&payload, static_cast<uint32_t>(key.size()));
    payload.append(key.data(), key.size());
    PutVarint32(&payload, static_cast<uint32_t>(value.size()));
    payload.append(value.data(), value.size());

    // Compute CRC over payload
    uint32_t crc = CRC32C(payload.data(), payload.size());

    // Write: [CRC 4B][payload]
    std::string record;
    record.resize(4);
    EncodeFixed32(&record[0], crc);
    record.append(payload);

    ssize_t written = ::write(fd_, record.data(), record.size());
    if (written != static_cast<ssize_t>(record.size())) {
        return Status::IOError("WAL write failed");
    }
    return Status::OK();
}

Status WALWriter::Sync() {
    if (fd_ >= 0 && ::fsync(fd_) != 0) {
        return Status::IOError("WAL fsync failed");
    }
    return Status::OK();
}

Status WALWriter::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    return Status::OK();
}

// -- WALReader --

Status WALReader::ReadAll(const std::string& path,
                          std::function<void(const WALEntry&)> visitor,
                          bool strict) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return Status::IOError("Failed to open WAL for reading: " + path);
    }

    // Read entire file
    struct stat st;
    if (fstat(fd, &st) != 0) {
        ::close(fd);
        return Status::IOError("Failed to stat WAL: " + path);
    }

    std::string data(st.st_size, '\0');
    ssize_t bytes_read = ::read(fd, &data[0], data.size());
    ::close(fd);

    if (bytes_read != st.st_size) {
        return Status::IOError("Failed to read WAL: " + path);
    }

    const char* p = data.data();
    const char* end = data.data() + data.size();

    while (p + 4 <= end) {
        // Read CRC
        uint32_t stored_crc = DecodeFixed32(p);
        p += 4;

        const char* payload_start = p;

        // We need to parse the payload to know its length
        // Record type (1 byte)
        if (p >= end) break;
        uint8_t type_byte = static_cast<uint8_t>(*p);
        p++;

        // Key
        uint32_t key_len;
        if (!GetVarint32(&p, end, &key_len)) {
            if (strict)
                return Status::Corruption("WAL: truncated key length");
            break;
        }
        if (p + key_len > end) {
            if (strict)
                return Status::Corruption("WAL: truncated key data");
            break;
        }
        std::string key(p, key_len);
        p += key_len;

        // Value
        uint32_t value_len;
        if (!GetVarint32(&p, end, &value_len)) {
            if (strict)
                return Status::Corruption("WAL: truncated value length");
            break;
        }
        if (p + value_len > end) {
            if (strict)
                return Status::Corruption("WAL: truncated value data");
            break;
        }
        std::string value(p, value_len);
        p += value_len;

        // Verify CRC
        size_t payload_len =
            static_cast<size_t>(p - payload_start);
        uint32_t computed_crc =
            CRC32C(payload_start, payload_len);

        if (stored_crc != computed_crc) {
            if (strict)
                return Status::Corruption("WAL: CRC mismatch");
            break;  // stop at first corruption in non-strict mode
        }

        WALEntry entry;
        entry.type = static_cast<WALRecordType>(type_byte);
        entry.key = std::move(key);
        entry.value = std::move(value);
        visitor(entry);
    }

    return Status::OK();
}

}  // namespace venus
