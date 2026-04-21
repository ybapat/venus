#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unistd.h>

#include "venus/db.h"

static std::string MakeKey(int n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%010d", n);
    return buf;
}

static std::string MakeValue(int n) {
    return std::string(100, 'v') + std::to_string(n);
}

struct BenchResult {
    std::string name;
    int ops;
    double elapsed_ms;
    double ops_per_sec() const { return ops / (elapsed_ms / 1000.0); }
};

static void PrintResult(const BenchResult& r) {
    std::cout << std::left << std::setw(30) << r.name << std::right
              << std::setw(8) << r.ops << " ops   " << std::fixed
              << std::setprecision(1) << std::setw(10) << r.elapsed_ms
              << " ms   " << std::setprecision(0) << std::setw(10)
              << r.ops_per_sec() << " ops/sec\n";
}

int main(int argc, char* argv[]) {
    int num_ops = 100000;
    std::string db_path = "/tmp/venus_bench_" + std::to_string(getpid());

    if (argc > 1) num_ops = std::stoi(argv[1]);

    std::cout << "Venus Benchmark — " << num_ops << " operations per test\n";
    std::cout << std::string(70, '-') << "\n";

    // Sequential writes
    {
        venus::Options opts;
        opts.db_path = db_path + "_seq_write";
        opts.sync_wal = false;
        std::unique_ptr<venus::DB> db;
        venus::DB::Open(opts, &db);

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < num_ops; i++) {
            db->Put(MakeKey(i), MakeValue(i));
        }
        auto end = std::chrono::high_resolution_clock::now();
        double ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        PrintResult({"Sequential Writes", num_ops, ms});
        db->Close();
        std::filesystem::remove_all(opts.db_path);
    }

    // Random writes
    {
        venus::Options opts;
        opts.db_path = db_path + "_rnd_write";
        opts.sync_wal = false;
        std::unique_ptr<venus::DB> db;
        venus::DB::Open(opts, &db);

        std::mt19937 rng(42);
        std::vector<int> keys(num_ops);
        for (int i = 0; i < num_ops; i++) keys[i] = i;
        std::shuffle(keys.begin(), keys.end(), rng);

        auto start = std::chrono::high_resolution_clock::now();
        for (int k : keys) {
            db->Put(MakeKey(k), MakeValue(k));
        }
        auto end = std::chrono::high_resolution_clock::now();
        double ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        PrintResult({"Random Writes", num_ops, ms});
        db->Close();
        std::filesystem::remove_all(opts.db_path);
    }

    // Sequential reads (populate then read)
    {
        venus::Options opts;
        opts.db_path = db_path + "_seq_read";
        opts.sync_wal = false;
        std::unique_ptr<venus::DB> db;
        venus::DB::Open(opts, &db);

        for (int i = 0; i < num_ops; i++) {
            db->Put(MakeKey(i), MakeValue(i));
        }

        auto start = std::chrono::high_resolution_clock::now();
        std::string value;
        for (int i = 0; i < num_ops; i++) {
            db->Get(MakeKey(i), &value);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        PrintResult({"Sequential Reads", num_ops, ms});
        db->Close();
        std::filesystem::remove_all(opts.db_path);
    }

    // Random reads
    {
        venus::Options opts;
        opts.db_path = db_path + "_rnd_read";
        opts.sync_wal = false;
        std::unique_ptr<venus::DB> db;
        venus::DB::Open(opts, &db);

        for (int i = 0; i < num_ops; i++) {
            db->Put(MakeKey(i), MakeValue(i));
        }

        std::mt19937 rng(42);
        std::vector<int> keys(num_ops);
        for (int i = 0; i < num_ops; i++) keys[i] = i;
        std::shuffle(keys.begin(), keys.end(), rng);

        auto start = std::chrono::high_resolution_clock::now();
        std::string value;
        for (int k : keys) {
            db->Get(MakeKey(k), &value);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        PrintResult({"Random Reads", num_ops, ms});
        db->Close();
        std::filesystem::remove_all(opts.db_path);
    }

    // Range scan
    {
        venus::Options opts;
        opts.db_path = db_path + "_scan";
        opts.sync_wal = false;
        std::unique_ptr<venus::DB> db;
        venus::DB::Open(opts, &db);

        for (int i = 0; i < num_ops; i++) {
            db->Put(MakeKey(i), MakeValue(i));
        }

        auto start = std::chrono::high_resolution_clock::now();
        std::vector<std::pair<std::string, std::string>> results;
        db->Scan(MakeKey(0), MakeKey(num_ops), &results);
        auto end = std::chrono::high_resolution_clock::now();
        double ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        PrintResult({"Full Range Scan", static_cast<int>(results.size()), ms});
        db->Close();
        std::filesystem::remove_all(opts.db_path);
    }

    std::cout << std::string(70, '-') << "\n";
    std::cout << "Done.\n";
    return 0;
}
