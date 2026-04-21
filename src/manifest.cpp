#include "venus/manifest.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace venus {

const std::vector<FileMetadata> Manifest::empty_files_ = {};

Manifest::Manifest(const std::string& db_path)
    : db_path_(db_path),
      manifest_path_(db_path + "/MANIFEST") {}

uint64_t Manifest::NextFileNumber() { return next_file_number_++; }

void Manifest::AddFile(int level, const FileMetadata& meta) {
    levels_[level].push_back(meta);
}

void Manifest::RemoveFile(int level, uint64_t file_number) {
    auto& files = levels_[level];
    files.erase(std::remove_if(files.begin(), files.end(),
                                [file_number](const FileMetadata& f) {
                                    return f.file_number == file_number;
                                }),
                files.end());
}

const std::vector<FileMetadata>& Manifest::GetFiles(int level) const {
    auto it = levels_.find(level);
    if (it == levels_.end()) return empty_files_;
    return it->second;
}

std::vector<FileMetadata> Manifest::GetAllFiles() const {
    std::vector<FileMetadata> result;
    for (auto& [level, files] : levels_) {
        for (auto& f : files) result.push_back(f);
    }
    return result;
}

int Manifest::NumFiles(int level) const {
    auto it = levels_.find(level);
    if (it == levels_.end()) return 0;
    return static_cast<int>(it->second.size());
}

uint64_t Manifest::LevelSize(int level) const {
    auto it = levels_.find(level);
    if (it == levels_.end()) return 0;
    uint64_t total = 0;
    for (auto& f : it->second) total += f.file_size;
    return total;
}

// Format:
// VENUS_MANIFEST v1
// next_file_number: <N>
// level <L>: file_num=<N> size=<S> smallest=<K> largest=<K>
// ...

Status Manifest::Save() {
    std::string tmp_path = manifest_path_ + ".tmp";
    std::ofstream out(tmp_path);
    if (!out.is_open()) {
        return Status::IOError("Failed to open manifest for writing: " +
                               tmp_path);
    }

    out << "VENUS_MANIFEST v1\n";
    out << "next_file_number: " << next_file_number_ << "\n";

    for (auto& [level, files] : levels_) {
        for (auto& f : files) {
            out << "level " << level << ": "
                << "file_num=" << f.file_number << " "
                << "size=" << f.file_size << " "
                << "smallest=" << f.smallest_key << " "
                << "largest=" << f.largest_key << "\n";
        }
    }

    out.close();
    if (!out.good()) {
        return Status::IOError("Failed to write manifest");
    }

    // Atomic rename
    if (std::rename(tmp_path.c_str(), manifest_path_.c_str()) != 0) {
        return Status::IOError("Failed to rename manifest");
    }

    return Status::OK();
}

Status Manifest::Load() {
    std::ifstream in(manifest_path_);
    if (!in.is_open()) {
        // No manifest = fresh database
        return Status::OK();
    }

    levels_.clear();
    std::string line;

    // Header
    if (!std::getline(in, line) || line != "VENUS_MANIFEST v1") {
        return Status::Corruption("Bad manifest header");
    }

    // next_file_number
    if (!std::getline(in, line)) {
        return Status::Corruption("Missing next_file_number");
    }
    if (line.substr(0, 18) == "next_file_number: ") {
        next_file_number_ = std::stoull(line.substr(18));
    }

    // Level entries
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        // Parse: level <L>: file_num=<N> size=<S> smallest=<K> largest=<K>
        FileMetadata meta{};
        int level;

        // Find "level "
        if (line.substr(0, 6) != "level ") continue;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        level = std::stoi(line.substr(6, colon - 6));
        meta.level = level;

        std::string rest = line.substr(colon + 2);
        std::istringstream iss(rest);
        std::string token;
        while (iss >> token) {
            if (token.substr(0, 9) == "file_num=") {
                meta.file_number = std::stoull(token.substr(9));
            } else if (token.substr(0, 5) == "size=") {
                meta.file_size = std::stoull(token.substr(5));
            } else if (token.substr(0, 9) == "smallest=") {
                meta.smallest_key = token.substr(9);
            } else if (token.substr(0, 8) == "largest=") {
                meta.largest_key = token.substr(8);
            }
        }

        levels_[level].push_back(meta);
    }

    return Status::OK();
}

}  // namespace venus
