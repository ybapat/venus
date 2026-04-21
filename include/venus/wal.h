#pragma once
#include <fstream>
#include <functional>
#include <memory>
#include <string>

#include "venus/slice.h"
#include "venus/status.h"

namespace venus {

enum class WALRecordType : uint8_t {
    kPut = 0x01,
    kDelete = 0x02,
};

struct WALEntry {
    WALRecordType type;
    std::string key;
    std::string value;
};

class WALWriter {
public:
    static Status Open(const std::string& path,
                       std::unique_ptr<WALWriter>* writer);

    Status AddPut(const Slice& key, const Slice& value);
    Status AddDelete(const Slice& key);
    Status Sync();
    Status Close();

    ~WALWriter();

private:
    WALWriter() = default;
    Status AddRecord(WALRecordType type, const Slice& key, const Slice& value);

    int fd_ = -1;
    std::string path_;
};

class WALReader {
public:
    static Status ReadAll(const std::string& path,
                          std::function<void(const WALEntry&)> visitor,
                          bool strict = false);
};

}  // namespace venus
