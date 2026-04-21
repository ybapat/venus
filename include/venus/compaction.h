#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "venus/manifest.h"
#include "venus/options.h"
#include "venus/sstable_reader.h"
#include "venus/status.h"

namespace venus {

struct CompactionTask {
    int source_level;
    int target_level;
    std::vector<FileMetadata> source_files;
    std::vector<FileMetadata> target_files;
};

class CompactionEngine {
public:
    CompactionEngine(const Options& opts, const std::string& db_path);

    std::optional<CompactionTask> PickCompaction(const Manifest& manifest);

    Status Execute(
        const CompactionTask& task,
        const std::map<int, std::vector<std::shared_ptr<SSTableReader>>>&
            readers,
        Manifest* manifest,
        std::vector<FileMetadata>* new_files,
        std::vector<uint64_t>* deleted_files);

private:
    std::string MakeSSTablePath(uint64_t file_number) const;

    Options opts_;
    std::string db_path_;
};

}  // namespace venus
