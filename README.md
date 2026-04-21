# Venus

A from-scratch LSM-tree key-value storage engine written in C++17. No dependencies beyond a C++17 compiler — everything else is vendored locally.

## Features

- **LSM-tree architecture** — write-optimized storage with background compaction
- **Skip list memtable** — lock-free ordered in-memory index (height 12, p=1/4)
- **Write-ahead log** — crash recovery with CRC32C checksums per entry
- **SSTable format** — sorted on-disk tables with:
  - Prefix-compressed data blocks with restart points
  - Binary-searchable index blocks
  - Bloom filters (10 bits/key, ~1% false positive rate)
  - CRC32C integrity checks on every block
- **Leveled compaction** — automatic L0 → L1 → L2+ merge-sort with tombstone garbage collection
- **Crash recovery** — WAL replay on startup, tolerates corrupt/partial WAL tails
- **Range scans** — efficient ordered iteration via multi-way merge iterator
- **HTTP REST API** — full CRUD + range scan over HTTP (cpp-httplib)
- **Interactive CLI** — REPL with PUT/GET/DELETE/SCAN commands

## Quick Start

```bash
git clone https://github.com/ybapat/venus.git
cd venus
make deps   # downloads GoogleTest + cpp-httplib (no global install)
make        # builds everything
make test   # runs 64 tests across 12 test suites
```

## Build Targets

| Command | Description |
|---------|-------------|
| `make deps` | Download vendored dependencies |
| `make` | Build CLI, HTTP server, and benchmark |
| `make test` | Build and run all tests |
| `make clean` | Remove build artifacts |
| `make distclean` | Remove build artifacts and vendored deps |

## Usage

### CLI

```bash
./build/venus-cli [db_path]
```

```
Venus DB CLI — type HELP for commands.
> PUT name venus
OK
> GET name
venus
> SCAN a z
name -> venus
(1 results)
> DELETE name
OK
> QUIT
```

### HTTP Server

```bash
./build/venus-server --db ./mydata --port 8080
```

```bash
# Store a value
curl -X PUT -d 'venus' http://localhost:8080/db/name

# Retrieve it
curl http://localhost:8080/db/name

# Range scan
curl 'http://localhost:8080/db?start=a&end=z'

# Delete
curl -X DELETE http://localhost:8080/db/name

# Health check
curl http://localhost:8080/health
```

### Benchmark

```bash
./build/venus-bench [num_ops]   # default: 100,000
```

## Benchmark Results

Measured on Apple M-series, 100K operations per test, WAL sync disabled:

| Workload | Ops/sec |
|----------|---------|
| Sequential Writes | 147,203 |
| Random Writes | 140,574 |
| Sequential Reads | 100,841 |
| Random Reads | 94,195 |
| Full Range Scan | 6,447,627 |

## Architecture

```
                    ┌──────────────────────┐
                    │     DB (public API)   │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │      LSM Tree        │
                    │  (orchestration)     │
                    └──┬───────┬───────┬──┘
                       │       │       │
              ┌────────▼──┐ ┌─▼────┐ ┌▼──────────┐
              │  Memtable  │ │ WAL  │ │ Compaction │
              │ (SkipList) │ │      │ │  Engine    │
              └────────────┘ └──────┘ └─────┬─────┘
                                            │
                    ┌───────────────────────┐│
                    │      SSTables         ◄┘
                    │  ┌─────────────────┐  │
                    │  │   Data Blocks   │  │
                    │  │  (prefix comp.) │  │
                    │  ├─────────────────┤  │
                    │  │  Index Block    │  │
                    │  ├─────────────────┤  │
                    │  │  Bloom Filter   │  │
                    │  ├─────────────────┤  │
                    │  │    Footer       │  │
                    │  └─────────────────┘  │
                    └───────────────────────┘
```

### Write Path

1. Append to WAL (CRC32C protected)
2. Insert into active memtable (skip list)
3. When memtable exceeds threshold (4 MB default):
   - Freeze current memtable
   - Create new active memtable + WAL
   - Flush frozen memtable to L0 SSTable
4. When L0 file count hits trigger (4 default):
   - Leveled compaction merges L0 into L1, L1 into L2, etc.

### Read Path

1. Check active memtable
2. Check frozen memtable (if any)
3. Search L0 SSTables newest-first (bloom filter → index → data block)
4. Search L1+ SSTables (one file per level due to non-overlapping key ranges)

### On-Disk Formats

**WAL Entry:**
```
[CRC32C 4B][record_type 1B][key_len varint][key][value_len varint][value]
```

**SSTable:**
```
[Data Block 0..N][Index Block][Bloom Filter Block][Footer 48B + smallest_key]
```

**Manifest** (human-readable text):
```
VENUS_MANIFEST v1
next_file_number: 42
level 0: file_num=37 size=4194304 smallest=aaa largest=mmm
level 1: file_num=35 size=10485760 smallest=aaa largest=fff
```

## Project Structure

```
venus/
├── Makefile
├── include/venus/       # Headers
│   ├── slice.h          # Non-owning byte view
│   ├── status.h         # Error handling (OK/NotFound/Corruption/IOError)
│   ├── options.h        # Configuration
│   ├── coding.h         # Varint + fixed-width encoding
│   ├── crc32.h          # CRC32C checksums
│   ├── skiplist.h       # Skip list with iterator
│   ├── memtable.h       # In-memory sorted store
│   ├── wal.h            # Write-ahead log
│   ├── bloom_filter.h   # Bloom filter (double hashing)
│   ├── block_builder.h  # Data/index block writer
│   ├── block_reader.h   # Block reader with iterator
│   ├── sstable_builder.h # SSTable writer
│   ├── sstable_reader.h  # SSTable reader (point + range)
│   ├── iterator.h       # Abstract iterator interface
│   ├── merge_iterator.h # Multi-way merge iterator
│   ├── manifest.h       # Level metadata tracking
│   ├── compaction.h     # Leveled compaction engine
│   ├── lsm_tree.h       # Core engine orchestration
│   ├── db.h             # Public API (pimpl)
│   └── http_server.h    # REST API
├── src/                 # Implementations
├── cmd/
│   ├── cli_main.cpp     # Interactive REPL
│   ├── server_main.cpp  # HTTP server
│   └── bench_main.cpp   # Benchmark tool
└── tests/               # 64 GoogleTest tests
```

## Tests

64 tests across 12 suites covering:

- Binary encoding (varint, fixed-width, CRC32C)
- Skip list (insert, update, delete, iteration, seek)
- Memtable (CRUD, flush threshold, tombstone iteration)
- WAL (write/read, truncation detection, bit-flip detection)
- Bloom filter (membership, false positive rate, serialization)
- Block format (build/read, seek, CRC validation, prefix compression)
- SSTable (point lookup, bloom rejects, iteration, merge)
- LSM tree (CRUD, flush triggers, range scan, delete propagation)
- Compaction (L0 to L1, overwrites, tombstone handling, reopen integrity)
- Crash recovery (WAL replay, partial WAL, corrupt WAL tail)
- DB integration (open/close, persistence across reopen)

## Requirements

- C++17 compiler (tested with Apple Clang 17)
- `make` and `curl` (for fetching dependencies)
- No global library installs needed
