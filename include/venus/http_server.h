#pragma once
#include <memory>
#include <string>

#include "venus/db.h"

namespace venus {

class HttpServer {
public:
    HttpServer(DB* db, const std::string& host, int port);
    ~HttpServer();

    void Start();
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace venus
