#include <iostream>
#include <sstream>
#include <string>

#include "venus/db.h"

int main(int argc, char* argv[]) {
    venus::Options opts;
    if (argc > 1) opts.db_path = argv[1];

    std::unique_ptr<venus::DB> db;
    auto status = venus::DB::Open(opts, &db);
    if (!status.ok()) {
        std::cerr << "Failed to open DB: " << status.ToString() << "\n";
        return 1;
    }

    std::cout << "Venus DB CLI — type HELP for commands.\n> " << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        for (auto& c : cmd) c = static_cast<char>(toupper(c));

        if (cmd == "PUT") {
            std::string key, value;
            iss >> key;
            std::getline(iss >> std::ws, value);
            if (key.empty()) {
                std::cout << "Usage: PUT <key> <value>\n";
            } else {
                auto s = db->Put(key, value);
                std::cout << (s.ok() ? "OK" : s.ToString()) << "\n";
            }
        } else if (cmd == "GET") {
            std::string key, value;
            iss >> key;
            if (key.empty()) {
                std::cout << "Usage: GET <key>\n";
            } else {
                auto s = db->Get(key, &value);
                if (s.ok())
                    std::cout << value << "\n";
                else
                    std::cout << s.ToString() << "\n";
            }
        } else if (cmd == "DELETE" || cmd == "DEL") {
            std::string key;
            iss >> key;
            if (key.empty()) {
                std::cout << "Usage: DELETE <key>\n";
            } else {
                auto s = db->Delete(key);
                std::cout << (s.ok() ? "OK" : s.ToString()) << "\n";
            }
        } else if (cmd == "SCAN") {
            std::string start, end;
            iss >> start >> end;
            if (start.empty() || end.empty()) {
                std::cout << "Usage: SCAN <start> <end>\n";
            } else {
                std::vector<std::pair<std::string, std::string>> results;
                auto s = db->Scan(start, end, &results);
                if (s.ok()) {
                    for (auto& [k, v] : results) {
                        std::cout << k << " -> " << v << "\n";
                    }
                    std::cout << "(" << results.size() << " results)\n";
                } else {
                    std::cout << s.ToString() << "\n";
                }
            }
        } else if (cmd == "QUIT" || cmd == "EXIT") {
            break;
        } else if (cmd == "HELP") {
            std::cout << "Commands:\n"
                      << "  PUT <key> <value>\n"
                      << "  GET <key>\n"
                      << "  DELETE <key>\n"
                      << "  SCAN <start> <end>\n"
                      << "  QUIT\n";
        } else if (!cmd.empty()) {
            std::cout << "Unknown command: " << cmd
                      << ". Type HELP for usage.\n";
        }
        std::cout << "> " << std::flush;
    }

    db->Close();
    return 0;
}
