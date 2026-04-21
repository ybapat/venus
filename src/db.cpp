#include "venus/db.h"

#include "venus/lsm_tree.h"

namespace venus {

struct DB::Impl {
    LSMTree tree;
    explicit Impl(const Options& opts) : tree(opts) {}
};

DB::~DB() { Close(); }

Status DB::Open(const Options& options, std::unique_ptr<DB>* db) {
    auto d = std::unique_ptr<DB>(new DB());
    d->impl_ = std::make_unique<Impl>(options);
    Status s = d->impl_->tree.Open();
    if (!s.ok()) return s;
    *db = std::move(d);
    return Status::OK();
}

Status DB::Put(const std::string& key, const std::string& value) {
    WriteOptions wo;
    return impl_->tree.Put(wo, key, value);
}

Status DB::Get(const std::string& key, std::string* value) {
    ReadOptions ro;
    return impl_->tree.Get(ro, key, value);
}

Status DB::Delete(const std::string& key) {
    WriteOptions wo;
    return impl_->tree.Delete(wo, key);
}

Status DB::Scan(const std::string& start, const std::string& end,
                std::vector<std::pair<std::string, std::string>>* results) {
    ReadOptions ro;
    return impl_->tree.Scan(ro, start, end, results);
}

Status DB::Close() {
    if (impl_) {
        impl_->tree.Close();
    }
    return Status::OK();
}

}  // namespace venus
