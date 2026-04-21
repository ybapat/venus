#include <csignal>
#include <iostream>
#include <string>

#include "venus/db.h"
#include "venus/http_server.h"

static venus::HttpServer* g_server = nullptr;

static void SignalHandler(int) {
    if (g_server) g_server->Stop();
}

int main(int argc, char* argv[]) {
    std::string db_path = "./venus_data";
    std::string host = "0.0.0.0";
    int port = 8080;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: venus-server [options]\n"
                      << "  --db <path>    Database directory (default: "
                         "./venus_data)\n"
                      << "  --host <addr>  Bind address (default: 0.0.0.0)\n"
                      << "  --port <port>  Port (default: 8080)\n";
            return 0;
        }
    }

    venus::Options opts;
    opts.db_path = db_path;

    std::unique_ptr<venus::DB> db;
    auto status = venus::DB::Open(opts, &db);
    if (!status.ok()) {
        std::cerr << "Failed to open DB: " << status.ToString() << "\n";
        return 1;
    }

    venus::HttpServer server(db.get(), host, port);
    g_server = &server;

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Venus HTTP server listening on " << host << ":" << port
              << "\n";
    std::cout << "Endpoints:\n"
              << "  PUT    /db/:key    — store a value\n"
              << "  GET    /db/:key    — retrieve a value\n"
              << "  DELETE /db/:key    — delete a key\n"
              << "  GET    /db?start=X&end=Y — range scan\n"
              << "  GET    /health     — health check\n";

    server.Start();

    db->Close();
    std::cout << "\nServer shut down.\n";
    return 0;
}
