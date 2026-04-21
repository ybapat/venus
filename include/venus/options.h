#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace venus {

struct Options {
    // Memtable
    size_t memtable_size_threshold = 4 * 1024 * 1024;  // 4 MB

    // WAL
    bool sync_wal = true;

    // SSTable
    size_t block_size = 4096;
    size_t bloom_bits_per_key = 10;
    int block_restart_interval = 16;

    // Compaction
    int level0_compaction_trigger = 4;
    size_t level_size_multiplier = 10;
    size_t level1_max_bytes = 10 * 1024 * 1024;  // 10 MB

    // General
    int max_levels = 7;
    std::string db_path = "./venus_data";
};

struct ReadOptions {
    bool verify_checksums = true;
};

struct WriteOptions {
    bool sync = false;
};

}  // namespace venus
