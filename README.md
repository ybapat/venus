# Venus

An LSM-tree key-value storage engine built from scratch in C++17. Every component — skip list, WAL, SSTable format, bloom filters, block encoding, compaction — is implemented from first principles. No external storage libraries, no embedded databases, no shortcuts.

**~5,200 lines of C++ | 64 tests | 147K writes/sec | Zero global dependencies**

```
git clone https://github.com/ybapat/venus.git && cd venus && make deps && make test
```

---

## Why Venus?

Most "database projects" wrap LevelDB or RocksDB behind an API. Venus doesn't. It implements the full LSM-tree storage engine stack:

- **Custom skip list** — probabilistic data structure for O(log n) ordered inserts and lookups
- **Binary block format** — prefix-compressed keys with restart points for binary search within blocks
- **CRC32C checksums** — Castagnoli polynomial with lookup table, protecting every WAL entry and data block
- **Bloom filters** — double-hashing scheme with MurmurHash3, reducing unnecessary disk reads by ~99%
- **Varint encoding** — variable-length integer encoding for compact on-disk representation
- **Write-ahead logging** — append-only log with per-entry checksums for crash recovery
- **SSTable file format** — sorted on-disk tables with data blocks, index blocks, bloom filter blocks, and a magic-number footer
- **Leveled compaction** — merge-sort across levels with tombstone garbage collection
- **Crash recovery** — WAL replay on startup that tolerates truncated and corrupted log tails

## Architecture

```
    Writes ──────────────────────────────────────── Reads
      │                                               │
      ▼                                               ▼
  ┌────────┐   append   ┌────────┐        ┌──────────────────────┐
  │  WAL   │◄──────────│ Engine │───────►│  Active Memtable     │ ◄── check first
  └────────┘            └───┬────┘        │  (Skip List)         │
                            │             └──────────────────────┘
                            │ flush                   │ miss
                            ▼                         ▼
                     ┌─────────────┐      ┌──────────────────────┐
                     │  L0 SSTs    │      │  Frozen Memtable     │ ◄── check second
                     │  (unsorted) │      └──────────────────────┘
                     └──────┬──────┘                  │ miss
                            │ compact                 ▼
                            ▼              ┌─────────────────────┐
                     ┌─────────────┐       │  L0 SSTables        │ ◄── bloom filter
                     │  L1 SSTs    │       │  (newest first)     │     → index seek
                     │  (sorted)   │       └─────────────────────┘     → block read
                     └──────┬──────┘                  │ miss
                            │ compact                 ▼
                            ▼              ┌─────────────────────┐
                     ┌─────────────┐       │  L1+ SSTables       │ ◄── non-overlapping
                     │  L2+ SSTs   │       │  (one per level)    │     key ranges
                     └─────────────┘       └─────────────────────┘
```

### Write Path

1. **WAL append** — serialize key/value with CRC32C, write to append-only log
2. **Memtable insert** — insert into skip list (O(log n), ~12 pointer levels)
3. **Flush trigger** — when memtable exceeds 4 MB:
   - Freeze current memtable, open new WAL
   - Build SSTable: encode prefix-compressed data blocks, construct index block, generate bloom filter, write footer with magic `0x56454E55`
   - Register new L0 file in manifest
4. **Compaction trigger** — when L0 reaches 4 files:
   - Multi-way merge-sort of overlapping files
   - Drop tombstones only at the deepest level (prevents deleted key resurrection)
   - Atomic manifest update via write-to-temp + rename

### Read Path

1. **Active memtable** — O(log n) skip list lookup
2. **Frozen memtable** — same, if a flush is in progress
3. **L0 SSTables** — search newest-first; for each: bloom filter check (99% of misses stop here) → binary search index block → read and scan data block
4. **L1+ SSTables** — single file per level with non-overlapping key ranges

## On-Disk Formats

### WAL Entry
```
┌──────────┬────────────┬──────────────┬─────┬────────────────┬───────┐
│ CRC32C   │ RecordType │ key_len      │ key │ value_len      │ value │
│ (4B)     │ (1B)       │ (varint)     │     │ (varint)       │       │
└──────────┴────────────┴──────────────┴─────┴────────────────┴───────┘
  CRC covers everything after itself. RecordType: 0x01=PUT, 0x02=DELETE.
```

### SSTable
```
┌─────────────────────────────────────────────────────────────────┐
│ Data Block 0   │ Data Block 1   │ ... │ Data Block N            │
├─────────────────────────────────────────────────────────────────┤
│ Index Block (last_key → offset/size per data block)             │
├─────────────────────────────────────────────────────────────────┤
│ Bloom Filter Block (bit array + num_hashes + CRC32C)            │
├─────────────────────────────────────────────────────────────────┤
│ Footer: index_off(8) index_sz(4) bloom_off(8) bloom_sz(4)      │
│         num_entries(4) smallest_key_sz(4) reserved(4)           │
│         magic=0x56454E55(4) smallest_key(variable)              │
└─────────────────────────────────────────────────────────────────┘

Data Block internals:
┌────────────────┬──────────────────┬─────────────┬───────┐
│ shared_key_len │ unshared_key_len │ value_len   │ data  │  ← repeated per entry
│ (varint)       │ (varint)         │ (varint)    │       │
├────────────────┴──────────────────┴─────────────┴───────┤
│ restart_point_0 │ restart_point_1 │ ... │ num_restarts  │  ← fixed32 each
├─────────────────┴─────────────────┴─────┴───────────────┤
│ CRC32C (4B)                                             │
└─────────────────────────────────────────────────────────┘
```

### Manifest (human-readable)
```
VENUS_MANIFEST v1
next_file_number: 42
level 0: file_num=37 size=4194304 smallest=aaa largest=mmm
level 1: file_num=35 size=10485760 smallest=aaa largest=fff
```

## Benchmark

```
$ ./build/venus-bench 100000
Venus Benchmark — 100000 operations per test
----------------------------------------------------------------------
Sequential Writes               100000 ops      679.3 ms     147203 ops/sec
Random Writes                   100000 ops      711.4 ms     140574 ops/sec
Sequential Reads                100000 ops      991.7 ms     100841 ops/sec
Random Reads                    100000 ops     1061.6 ms      94195 ops/sec
Full Range Scan                 100000 ops       15.5 ms    6447627 ops/sec
----------------------------------------------------------------------
```

Measured on Apple Silicon, WAL sync disabled, 100-byte values.

## Quick Start

```bash
make deps      # vendors GoogleTest + cpp-httplib locally (no sudo, no brew)
make           # builds venus-cli, venus-server, venus-bench
make test      # 64 tests, 12 suites, <1 second
```

### CLI
```bash
$ ./build/venus-cli ./mydb
Venus DB CLI — type HELP for commands.
> PUT user:1001 {"name":"alice","role":"admin"}
OK
> GET user:1001
{"name":"alice","role":"admin"}
> SCAN user:1000 user:2000
user:1001 -> {"name":"alice","role":"admin"}
(1 results)
> DELETE user:1001
OK
> QUIT
```

### HTTP Server
```bash
$ ./build/venus-server --db ./mydb --port 8080
Venus HTTP server listening on 0.0.0.0:8080
```
```bash
curl -X PUT -d '{"name":"alice"}' localhost:8080/db/user:1001
curl localhost:8080/db/user:1001
curl 'localhost:8080/db?start=user:1000&end=user:2000'
curl -X DELETE localhost:8080/db/user:1001
curl localhost:8080/health
```

## Testing

64 tests across 12 suites:

| Suite | Tests | What's covered |
|-------|-------|----------------|
| CodingTest | 6 | Varint32/64, fixed-width, length-prefixed slices |
| CRC32Test | 3 | Checksums, extend, mask/unmask |
| SkipListTest | 8 | Insert, update, delete, sorted iteration, seek, memory tracking |
| MemtableTest | 6 | CRUD, flush threshold, tombstone iteration |
| WALTest | 5 | Write/read, truncation, bit-flip detection, empty file |
| BloomFilterTest | 4 | Membership, false positive rate <3%, serialization |
| BlockTest | 6 | Build/read, seek, CRC validation, prefix compression |
| SSTableTest | 6 | 10K-entry table, bloom rejects, iteration, merge |
| LSMTreeTest | 8 | CRUD, flush triggers, range scan, tombstone propagation |
| CompactionTest | 4 | L0->L1, overwrites, tombstone GC, reopen integrity |
| CrashRecoveryTest | 4 | WAL replay, partial WAL, corrupt tail, delete recovery |
| DBTest | 4 | Open/close, persistence across reopen |

## Project Layout

```
venus/
├── Makefile                    # build system — make deps/test/clean
├── include/venus/
│   ├── slice.h                 # non-owning byte view (zero-copy)
│   ├── status.h                # error type (OK/NotFound/Corruption/IOError)
│   ├── options.h               # tunable engine parameters
│   ├── coding.h                # varint + fixed-width binary encoding
│   ├── crc32.h                 # CRC32C with Castagnoli lookup table
│   ├── iterator.h              # abstract sorted iterator interface
│   ├── skiplist.h              # skip list (height=12, p=0.25)
│   ├── memtable.h              # in-memory store with flush threshold
│   ├── wal.h                   # write-ahead log writer + reader
│   ├── bloom_filter.h          # bloom filter (MurmurHash3, double hashing)
│   ├── block_builder.h         # prefix-compressed block encoding
│   ├── block_reader.h          # block decoding with binary search
│   ├── sstable_builder.h       # SSTable file writer
│   ├── sstable_reader.h        # SSTable file reader (point + range)
│   ├── merge_iterator.h        # N-way merge with deduplication
│   ├── manifest.h              # level metadata (atomic save via rename)
│   ├── compaction.h            # leveled compaction engine
│   ├── lsm_tree.h              # core orchestrator
│   ├── db.h                    # public API (pimpl)
│   └── http_server.h           # REST server
├── src/                        # all .cpp implementations
├── cmd/
│   ├── cli_main.cpp            # interactive REPL
│   ├── server_main.cpp         # HTTP server with signal handling
│   └── bench_main.cpp          # 5-workload benchmark suite
└── tests/                      # 64 GoogleTest tests across 12 suites
```

## Design Decisions

| Decision | Rationale |
|----------|-----------|
| Skip list over red-black tree | Simpler implementation, natural ordered iteration, same O(log n) complexity, standard in LevelDB/RocksDB |
| CRC32C over xxHash | Storage engine standard, good error detection properties, widely understood |
| Text manifest over binary log | Human-debuggable (`cat MANIFEST`), atomic update via rename, binary is an optimization for later |
| Pimpl on DB class | Clean public API, hides all engine internals, prevents header dependency leakage |
| Single mutex in MVP | Correct first, fast later. Concurrent readers and background compaction are natural extensions |
| Tombstone sentinel value | Simple propagation through memtable -> SSTable -> compaction pipeline without special-casing the write path |
| Vendored dependencies | `make deps` downloads everything. Clone and build on any machine with a C++17 compiler |

## Build Targets

| Command | What it does |
|---------|--------------|
| `make deps` | Downloads GoogleTest v1.16.0 + cpp-httplib v0.18.3 via curl |
| `make` / `make all` | Builds `venus-cli`, `venus-server`, `venus-bench` |
| `make test` | Compiles and runs all 64 tests |
| `make clean` | Removes `build/` |
| `make distclean` | Removes `build/` and `third_party/` |

## Requirements

- C++17 compiler (tested with Apple Clang 17, should work with GCC 9+ and MSVC 19.14+)
- `make` and `curl`
- No global library installs, no package managers, no cmake

---

## Summary

- Built a persistent key-value storage engine from scratch implementing the LSM-tree architecture used in LevelDB, RocksDB, and Cassandra
- Designed custom on-disk SSTable format with prefix-compressed data blocks, binary-searchable index blocks, bloom filters (MurmurHash3, ~1% false positive rate), and CRC32C integrity checks on every block
- Implemented a probabilistic skip list for the in-memory index, write-ahead logging with per-entry checksums for crash recovery, and leveled compaction with tombstone garbage collection
- Achieved 147K writes/sec and 6.4M range-scan ops/sec on 100K-operation benchmarks
- Exposed the engine via an interactive CLI and HTTP REST API; 64 unit/integration tests covering crash recovery, data corruption detection, and compaction correctness
- Zero external storage dependencies — every component (skip list, WAL, block encoding, bloom filter, compaction) implemented from first principles in ~5,200 lines of C++
