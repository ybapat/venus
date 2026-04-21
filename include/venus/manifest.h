#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "venus/status.h"

namespace venus {

struct FileMetadata {
    uint64_t file_number;
    uint64_t file_size;
    std::string smallest_key;
    std::string largest_key;
    int level;
};

class Manifest {
public:
    explicit Manifest(const std::string& db_path);

    Status Load();
    Status Save();

    uint64_t NextFileNumber();

    void AddFile(int level, const FileMetadata& meta);
    void RemoveFile(int level, uint64_t file_number);

    const std::vector<FileMetadata>& GetFiles(int level) const;
    std::vector<FileMetadata> GetAllFiles() const;

    int NumFiles(int level) const;
    uint64_t LevelSize(int level) const;

private:
    std::string db_path_;
    std::string manifest_path_;
    uint64_t next_file_number_ = 1;
    std::map<int, std::vector<FileMetadata>> levels_;
    static const std::vector<FileMetadata> empty_files_;
};

}  // namespace venus
