#include "venus/lsm_tree.h"

#include <algorithm>
#include <filesystem>

#include "venus/compaction.h"
#include "venus/merge_iterator.h"
#include "venus/sstable_builder.h"

namespace venus {

static const std::string kTombstoneValue = std::string("\x00TOMBSTONE", 10);

static bool IsTombstone(const std::string& value) {
    return value == kTombstoneValue;
}

LSMTree::LSMTree(const Options& opts)
    : opts_(opts), db_path_(opts.db_path) {}

LSMTree::~LSMTree() { Close(); }

std::string LSMTree::MakeSSTablePath(uint64_t file_number) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%06llu.sst", (unsigned long long)file_number);
    return db_path_ + "/" + buf;
}

std::string LSMTree::MakeWALPath(uint64_t file_number) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "wal_%06llu.log",
             (unsigned long long)file_number);
    return db_path_ + "/" + buf;
}

Status LSMTree::Open() {
    std::lock_guard<std::mutex> lock(mu_);

    // Create DB directory if needed
    std::filesystem::create_directories(db_path_);

    manifest_ = std::make_unique<Manifest>(db_path_);
    Status s = manifest_->Load();
    if (!s.ok()) return s;

    // Open existing SSTables
    for (auto& file_meta : manifest_->GetAllFiles()) {
        s = OpenSSTable(file_meta);
        if (!s.ok()) return s;
    }

    // Recover from WAL
    s = Recover();
    if (!s.ok()) return s;

    // Create fresh memtable + WAL if we don't have one yet
    if (!active_memtable_) {
        active_wal_number_ = manifest_->NextFileNumber();
        std::unique_ptr<WALWriter> wal;
        s = WALWriter::Open(MakeWALPath(active_wal_number_), &wal);
        if (!s.ok()) return s;
        active_wal_ = std::move(wal);
        active_memtable_ =
            std::make_unique<Memtable>(opts_.memtable_size_threshold);
    }

    return Status::OK();
}

Status LSMTree::Close() {
    std::lock_guard<std::mutex> lock(mu_);
    if (active_wal_) {
        active_wal_->Sync();
        active_wal_->Close();
        active_wal_.reset();
    }
    active_memtable_.reset();
    frozen_memtable_.reset();
    level_readers_.clear();
    return Status::OK();
}

Status LSMTree::Recover() {
    // Find WAL files
    std::vector<std::pair<uint64_t, std::string>> wal_files;
    for (auto& entry : std::filesystem::directory_iterator(db_path_)) {
        std::string name = entry.path().filename().string();
        if (name.substr(0, 4) == "wal_" &&
            name.size() > 8 && name.substr(name.size() - 4) == ".log") {
            std::string num_str = name.substr(4, name.size() - 8);
            uint64_t num = std::stoull(num_str);
            wal_files.emplace_back(num, entry.path().string());
        }
    }

    if (wal_files.empty()) return Status::OK();

    // Sort by number (oldest first)
    std::sort(wal_files.begin(), wal_files.end());

    // Replay WALs
    auto memtable =
        std::make_unique<Memtable>(opts_.memtable_size_threshold);
    uint64_t last_wal_num = 0;

    for (auto& [num, path] : wal_files) {
        Status s = WALReader::ReadAll(path, [&](const WALEntry& entry) {
            if (entry.type == WALRecordType::kPut) {
                memtable->Put(entry.key, entry.value);
            } else if (entry.type == WALRecordType::kDelete) {
                memtable->Delete(entry.key);
            }
        });
        if (!s.ok()) return s;
        last_wal_num = num;
    }

    // Set up recovered state
    active_memtable_ = std::move(memtable);
    active_wal_number_ = last_wal_num;

    // Re-open the WAL for appending
    std::unique_ptr<WALWriter> wal;
    Status s = WALWriter::Open(MakeWALPath(active_wal_number_), &wal);
    if (!s.ok()) return s;
    active_wal_ = std::move(wal);

    // Delete old WAL files (except the active one)
    for (auto& [num, path] : wal_files) {
        if (num != active_wal_number_) {
            std::filesystem::remove(path);
        }
    }

    return Status::OK();
}

Status LSMTree::OpenSSTable(const FileMetadata& meta) {
    std::string path = MakeSSTablePath(meta.file_number);
    std::unique_ptr<SSTableReader> reader;
    Status s = SSTableReader::Open(path, &reader);
    if (!s.ok()) return s;

    level_readers_[meta.level].push_back(std::move(reader));
    return Status::OK();
}

Status LSMTree::Put(const WriteOptions& wo, const Slice& key,
                     const Slice& value) {
    std::lock_guard<std::mutex> lock(mu_);

    // Write to WAL
    Status s = active_wal_->AddPut(key, value);
    if (!s.ok()) return s;
    if (wo.sync || opts_.sync_wal) {
        s = active_wal_->Sync();
        if (!s.ok()) return s;
    }

    // Write to memtable
    s = active_memtable_->Put(key.ToString(), value.ToString());
    if (!s.ok()) return s;

    // Check if flush needed
    if (active_memtable_->ShouldFlush()) {
        s = RotateMemtable();
        if (!s.ok()) return s;
    }

    return Status::OK();
}

Status LSMTree::Delete(const WriteOptions& wo, const Slice& key) {
    std::lock_guard<std::mutex> lock(mu_);

    Status s = active_wal_->AddDelete(key);
    if (!s.ok()) return s;
    if (wo.sync || opts_.sync_wal) {
        s = active_wal_->Sync();
        if (!s.ok()) return s;
    }

    s = active_memtable_->Delete(key.ToString());
    if (!s.ok()) return s;

    if (active_memtable_->ShouldFlush()) {
        s = RotateMemtable();
        if (!s.ok()) return s;
    }

    return Status::OK();
}

Status LSMTree::Get(const ReadOptions& ro, const Slice& key,
                     std::string* value) {
    std::lock_guard<std::mutex> lock(mu_);

    // 1. Check active memtable
    Status s = active_memtable_->Get(key, value);
    if (!s.IsNotFound()) return s;

    // 2. Check frozen memtable
    if (frozen_memtable_) {
        s = frozen_memtable_->Get(key, value);
        if (!s.IsNotFound()) return s;
    }

    // 3. Check L0 SSTables (newest first)
    auto l0_it = level_readers_.find(0);
    if (l0_it != level_readers_.end()) {
        auto& l0_files = l0_it->second;
        for (auto it = l0_files.rbegin(); it != l0_files.rend(); ++it) {
            auto& reader = *it;
            if (!reader->MayContain(key)) continue;
            s = reader->Get(ro, key, value);
            if (s.ok()) {
                if (IsTombstone(*value))
                    return Status::NotFound("key deleted");
                return s;
            }
            if (!s.IsNotFound()) return s;
        }
    }

    // 4. Check L1+ (non-overlapping, binary search by key range)
    for (int level = 1; level < opts_.max_levels; level++) {
        auto level_it = level_readers_.find(level);
        if (level_it == level_readers_.end()) continue;
        auto& files = level_it->second;

        for (auto& reader : files) {
            auto& footer = reader->GetFooter();
            if (key < Slice(footer.smallest_key) ||
                key > Slice(footer.largest_key)) {
                continue;
            }
            if (!reader->MayContain(key)) continue;
            s = reader->Get(ro, key, value);
            if (s.ok()) {
                if (IsTombstone(*value))
                    return Status::NotFound("key deleted");
                return s;
            }
            if (!s.IsNotFound()) return s;
        }
    }

    return Status::NotFound("key not found");
}

Status LSMTree::Scan(const ReadOptions& ro, const Slice& start,
                      const Slice& end,
                      std::vector<std::pair<std::string, std::string>>* results) {
    std::lock_guard<std::mutex> lock(mu_);

    // Build iterators for all sources
    std::vector<std::unique_ptr<Iterator>> children;

    // Active memtable (newest)
    children.push_back(active_memtable_->NewIterator());

    // Frozen memtable
    if (frozen_memtable_) {
        children.push_back(frozen_memtable_->NewIterator());
    }

    // L0 SSTables (newest first)
    auto l0_it = level_readers_.find(0);
    if (l0_it != level_readers_.end()) {
        for (auto it = l0_it->second.rbegin();
             it != l0_it->second.rend(); ++it) {
            children.push_back((*it)->NewIterator(ro));
        }
    }

    // L1+ SSTables
    for (int level = 1; level < opts_.max_levels; level++) {
        auto level_it = level_readers_.find(level);
        if (level_it == level_readers_.end()) continue;
        for (auto& reader : level_it->second) {
            children.push_back(reader->NewIterator(ro));
        }
    }

    MergeIterator merge(std::move(children));
    merge.Seek(start);

    while (merge.Valid()) {
        Slice k = merge.key();
        if (k >= end) break;

        // The merge iterator already handles dedup (newest wins).
        // But we need to check for tombstones.
        // Tombstones in memtable have empty value + deletion flag.
        // In SSTables, tombstones are stored as empty values with a special
        // encoding. For the MVP, we check if the value is a tombstone marker.
        //
        // Actually, our skiplist iterator exposes is_deletion via its value
        // encoding. Let's use a convention: tombstones have a special value
        // prefix. But that's fragile. Let's use a simpler approach:
        //
        // In the memtable, tombstones are stored with empty value and
        // is_deletion=true. The skiplist iterator value() returns "" for these.
        // In SSTables, we store tombstones as keys with value = "\x00TOMBSTONE".
        //
        // For now, let's use a sentinel value for tombstones in SSTables.
        std::string val = merge.value().ToString();
        if (val != "\x00TOMBSTONE") {
            results->emplace_back(k.ToString(), val);
        }
        merge.Next();
    }

    return Status::OK();
}

Status LSMTree::RotateMemtable() {
    // If there's still a frozen memtable, flush it first
    if (frozen_memtable_) {
        Status s = FlushMemtableToL0(frozen_memtable_.get());
        if (!s.ok()) return s;
        frozen_memtable_.reset();
    }

    // Freeze current memtable
    frozen_memtable_ = std::move(active_memtable_);
    uint64_t old_wal_number = active_wal_number_;

    // Create new memtable + WAL
    active_wal_number_ = manifest_->NextFileNumber();
    std::unique_ptr<WALWriter> wal;
    Status s = WALWriter::Open(MakeWALPath(active_wal_number_), &wal);
    if (!s.ok()) return s;
    active_wal_->Close();
    active_wal_ = std::move(wal);
    active_memtable_ =
        std::make_unique<Memtable>(opts_.memtable_size_threshold);

    // Flush frozen memtable
    s = FlushMemtableToL0(frozen_memtable_.get());
    if (!s.ok()) return s;
    frozen_memtable_.reset();

    // Delete old WAL
    std::filesystem::remove(MakeWALPath(old_wal_number));

    // Check if compaction needed
    return MaybeCompact();
}

Status LSMTree::FlushMemtable() {
    std::lock_guard<std::mutex> lock(mu_);
    if (active_memtable_->ApproximateMemoryUsage() == 0) {
        return Status::OK();
    }
    return RotateMemtable();
}

Status LSMTree::FlushMemtableToL0(Memtable* mem) {
    uint64_t file_number = manifest_->NextFileNumber();
    std::string path = MakeSSTablePath(file_number);

    SSTableBuilder builder(path, opts_);
    auto it = mem->NewIterator();
    it->SeekToFirst();

    while (it->Valid()) {
        std::string key = it->key().ToString();
        std::string value;

        // Check if this key is a tombstone
        auto result = mem->Get(key, &value);
        if (result.IsNotFound()) {
            value = kTombstoneValue;
        }

        builder.Add(key, value);
        it->Next();
    }

    Status s = builder.Finish();
    if (!s.ok()) return s;

    FileMetadata meta;
    meta.file_number = file_number;
    meta.file_size = builder.FileSize();
    meta.level = 0;
    // Get smallest/largest from the SSTable
    std::unique_ptr<SSTableReader> reader;
    s = SSTableReader::Open(path, &reader);
    if (!s.ok()) return s;
    meta.smallest_key = reader->GetFooter().smallest_key;
    meta.largest_key = reader->GetFooter().largest_key;

    manifest_->AddFile(0, meta);
    s = manifest_->Save();
    if (!s.ok()) return s;

    level_readers_[0].push_back(std::move(reader));
    return Status::OK();
}

Status LSMTree::MaybeCompact() {
    CompactionEngine engine(opts_, db_path_);
    auto task = engine.PickCompaction(*manifest_);
    if (!task.has_value()) return Status::OK();

    std::vector<FileMetadata> new_files;
    std::vector<uint64_t> deleted_files;
    Status s = engine.Execute(task.value(), level_readers_, manifest_.get(),
                               &new_files, &deleted_files);
    if (!s.ok()) return s;

    // Remove old readers
    for (uint64_t file_num : deleted_files) {
        std::string path = MakeSSTablePath(file_num);
        for (auto& [level, readers] : level_readers_) {
            readers.erase(
                std::remove_if(readers.begin(), readers.end(),
                               [&path](const std::shared_ptr<SSTableReader>& r) {
                                   return r->FilePath() == path;
                               }),
                readers.end());
        }
        std::filesystem::remove(path);
    }

    // Open new readers
    for (auto& meta : new_files) {
        s = OpenSSTable(meta);
        if (!s.ok()) return s;
    }

    return Status::OK();
}

}  // namespace venus
