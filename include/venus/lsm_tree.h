#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "venus/iterator.h"
#include "venus/manifest.h"
#include "venus/memtable.h"
#include "venus/options.h"
#include "venus/slice.h"
#include "venus/sstable_reader.h"
#include "venus/status.h"
#include "venus/wal.h"

namespace venus {

class LSMTree {
public:
    explicit LSMTree(const Options& opts);
    ~LSMTree();

    Status Open();
    Status Close();

    Status Put(const WriteOptions& wo, const Slice& key, const Slice& value);
    Status Delete(const WriteOptions& wo, const Slice& key);
    Status Get(const ReadOptions& ro, const Slice& key, std::string* value);

    // Range scan: returns all KV pairs in [start, end).
    Status Scan(const ReadOptions& ro, const Slice& start, const Slice& end,
                std::vector<std::pair<std::string, std::string>>* results);

    Status FlushMemtable();
    Status MaybeCompact();

    const Manifest& GetManifest() const { return *manifest_; }

private:
    Status Recover();
    Status RotateMemtable();
    Status FlushMemtableToL0(Memtable* mem);
    Status OpenSSTable(const FileMetadata& meta);
    std::string MakeSSTablePath(uint64_t file_number) const;
    std::string MakeWALPath(uint64_t file_number) const;

    Options opts_;
    std::string db_path_;

    std::unique_ptr<Memtable> active_memtable_;
    std::unique_ptr<WALWriter> active_wal_;
    uint64_t active_wal_number_ = 0;

    std::unique_ptr<Memtable> frozen_memtable_;

    // Level -> list of SSTable readers (for L0: ordered newest first)
    std::map<int, std::vector<std::shared_ptr<SSTableReader>>> level_readers_;

    std::unique_ptr<Manifest> manifest_;
    std::mutex mu_;
};

}  // namespace venus
