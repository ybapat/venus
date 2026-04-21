#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "venus/options.h"
#include "venus/slice.h"
#include "venus/status.h"

namespace venus {

class DB {
public:
    static Status Open(const Options& options, std::unique_ptr<DB>* db);

    Status Put(const std::string& key, const std::string& value);
    Status Get(const std::string& key, std::string* value);
    Status Delete(const std::string& key);

    Status Scan(const std::string& start, const std::string& end,
                std::vector<std::pair<std::string, std::string>>* results);

    Status Close();

    ~DB();

private:
    DB() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace venus
