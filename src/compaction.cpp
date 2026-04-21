#include "venus/compaction.h"

#include <algorithm>
#include <filesystem>

#include "venus/merge_iterator.h"
#include "venus/sstable_builder.h"

namespace venus {

static const std::string kTombstoneValue = std::string("\x00TOMBSTONE", 10);

static bool IsTombstone(const Slice& value) {
    return value.size() == 10 &&
           std::string(value.data(), value.size()) == kTombstoneValue;
}

CompactionEngine::CompactionEngine(const Options& opts,
                                     const std::string& db_path)
    : opts_(opts), db_path_(db_path) {}

std::string CompactionEngine::MakeSSTablePath(uint64_t file_number) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%06llu.sst", (unsigned long long)file_number);
    return db_path_ + "/" + buf;
}

std::optional<CompactionTask> CompactionEngine::PickCompaction(
    const Manifest& manifest) {
    // Check L0 trigger
    if (manifest.NumFiles(0) >= opts_.level0_compaction_trigger) {
        CompactionTask task;
        task.source_level = 0;
        task.target_level = 1;
        task.source_files = manifest.GetFiles(0);

        // Find all overlapping L1 files
        std::string smallest, largest;
        for (auto& f : task.source_files) {
            if (smallest.empty() || f.smallest_key < smallest)
                smallest = f.smallest_key;
            if (largest.empty() || f.largest_key > largest)
                largest = f.largest_key;
        }

        for (auto& f : manifest.GetFiles(1)) {
            if (f.smallest_key <= largest && f.largest_key >= smallest) {
                task.target_files.push_back(f);
            }
        }

        return task;
    }

    // Check L1+ triggers
    for (int level = 1; level < opts_.max_levels - 1; level++) {
        uint64_t max_bytes = opts_.level1_max_bytes;
        for (int l = 1; l < level; l++) {
            max_bytes *= opts_.level_size_multiplier;
        }

        if (manifest.LevelSize(level) > max_bytes) {
            CompactionTask task;
            task.source_level = level;
            task.target_level = level + 1;

            // Pick the first file from source level
            auto& files = manifest.GetFiles(level);
            if (files.empty()) continue;
            task.source_files.push_back(files[0]);

            // Find overlapping files in target level
            std::string smallest = files[0].smallest_key;
            std::string largest = files[0].largest_key;

            for (auto& f : manifest.GetFiles(level + 1)) {
                if (f.smallest_key <= largest && f.largest_key >= smallest) {
                    task.target_files.push_back(f);
                }
            }

            return task;
        }
    }

    return std::nullopt;
}

Status CompactionEngine::Execute(
    const CompactionTask& task,
    const std::map<int, std::vector<std::shared_ptr<SSTableReader>>>& readers,
    Manifest* manifest, std::vector<FileMetadata>* new_files,
    std::vector<uint64_t>* deleted_files) {

    // Build iterators for all input files
    ReadOptions ro;
    std::vector<std::unique_ptr<Iterator>> children;

    // Source files (newer)
    auto src_it = readers.find(task.source_level);
    if (src_it != readers.end()) {
        for (auto& reader : src_it->second) {
            for (auto& meta : task.source_files) {
                if (reader->FilePath() == MakeSSTablePath(meta.file_number)) {
                    children.push_back(reader->NewIterator(ro));
                    break;
                }
            }
        }
    }

    // Target files (older)
    auto tgt_it = readers.find(task.target_level);
    if (tgt_it != readers.end()) {
        for (auto& reader : tgt_it->second) {
            for (auto& meta : task.target_files) {
                if (reader->FilePath() == MakeSSTablePath(meta.file_number)) {
                    children.push_back(reader->NewIterator(ro));
                    break;
                }
            }
        }
    }

    if (children.empty()) return Status::OK();

    MergeIterator merge(std::move(children));
    merge.SeekToFirst();

    // Only drop tombstones if target is the deepest level with any data
    // AND there are no files in deeper levels. Conservative approach to
    // avoid correctness issues.
    bool is_bottom_level = (task.target_level == opts_.max_levels - 1);
    if (is_bottom_level) {
        for (int l = task.target_level + 1; l < opts_.max_levels; l++) {
            if (manifest->NumFiles(l) > 0) {
                is_bottom_level = false;
                break;
            }
        }
    }

    // Write merged output, splitting into files of bounded size
    uint64_t file_number = 0;
    std::unique_ptr<SSTableBuilder> builder;
    size_t current_file_size = 0;
    size_t max_file_size = opts_.level1_max_bytes;
    // Target level files should be roughly level1_max_bytes each
    for (int l = 1; l < task.target_level; l++) {
        max_file_size *= opts_.level_size_multiplier;
    }
    // But cap per-file at a reasonable size
    max_file_size = std::min(max_file_size,
                              opts_.level1_max_bytes);

    auto finishCurrentFile = [&]() -> Status {
        if (!builder) return Status::OK();
        Status s = builder->Finish();
        if (!s.ok()) return s;

        FileMetadata meta;
        meta.file_number = file_number;
        meta.file_size = builder->FileSize();
        meta.level = task.target_level;

        std::unique_ptr<SSTableReader> reader;
        s = SSTableReader::Open(MakeSSTablePath(file_number), &reader);
        if (!s.ok()) return s;
        meta.smallest_key = reader->GetFooter().smallest_key;
        meta.largest_key = reader->GetFooter().largest_key;

        new_files->push_back(meta);
        builder.reset();
        return Status::OK();
    };

    while (merge.Valid()) {
        Slice key = merge.key();
        Slice value = merge.value();

        // Drop tombstones at bottom level
        if (IsTombstone(value) && is_bottom_level) {
            merge.Next();
            continue;
        }

        // Start new file if needed
        if (!builder || current_file_size >= max_file_size) {
            Status s = finishCurrentFile();
            if (!s.ok()) return s;

            file_number = manifest->NextFileNumber();
            builder = std::make_unique<SSTableBuilder>(
                MakeSSTablePath(file_number), opts_);
            current_file_size = 0;
        }

        Status s = builder->Add(key, value);
        if (!s.ok()) return s;
        current_file_size += key.size() + value.size();

        merge.Next();
    }

    Status s = finishCurrentFile();
    if (!s.ok()) return s;

    // Update manifest: remove old files, add new files
    for (auto& meta : task.source_files) {
        manifest->RemoveFile(task.source_level, meta.file_number);
        deleted_files->push_back(meta.file_number);
    }
    for (auto& meta : task.target_files) {
        manifest->RemoveFile(task.target_level, meta.file_number);
        deleted_files->push_back(meta.file_number);
    }
    for (auto& meta : *new_files) {
        manifest->AddFile(task.target_level, meta);
    }

    return manifest->Save();
}

}  // namespace venus
