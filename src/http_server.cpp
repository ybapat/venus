#include "venus/http_server.h"

#include <httplib.h>

#include <sstream>

namespace venus {

struct HttpServer::Impl {
    DB* db;
    httplib::Server svr;
    std::string host;
    int port;
};

HttpServer::HttpServer(DB* db, const std::string& host, int port)
    : impl_(std::make_unique<Impl>()) {
    impl_->db = db;
    impl_->host = host;
    impl_->port = port;

    // GET /health
    impl_->svr.Get("/health", [](const httplib::Request&,
                                  httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // PUT /db/:key
    impl_->svr.Put(R"(/db/(.+))",
                    [this](const httplib::Request& req,
                           httplib::Response& res) {
                        std::string key = req.matches[1];
                        auto s = impl_->db->Put(key, req.body);
                        if (s.ok()) {
                            res.status = 200;
                            res.set_content("OK", "text/plain");
                        } else {
                            res.status = 500;
                            res.set_content(s.ToString(), "text/plain");
                        }
                    });

    // GET /db?start=X&end=Y (range scan)
    impl_->svr.Get("/db", [this](const httplib::Request& req,
                                  httplib::Response& res) {
        if (req.has_param("start") && req.has_param("end")) {
            std::string start = req.get_param_value("start");
            std::string end = req.get_param_value("end");
            std::vector<std::pair<std::string, std::string>> results;
            auto s = impl_->db->Scan(start, end, &results);
            if (s.ok()) {
                std::ostringstream json;
                json << "[";
                for (size_t i = 0; i < results.size(); i++) {
                    if (i > 0) json << ",";
                    json << R"({"key":")" << results[i].first
                         << R"(","value":")" << results[i].second
                         << R"("})";
                }
                json << "]";
                res.set_content(json.str(), "application/json");
            } else {
                res.status = 500;
                res.set_content(s.ToString(), "text/plain");
            }
        } else {
            res.status = 400;
            res.set_content("Missing start/end params", "text/plain");
        }
    });

    // GET /db/:key
    impl_->svr.Get(R"(/db/(.+))",
                    [this](const httplib::Request& req,
                           httplib::Response& res) {
                        std::string key = req.matches[1];
                        std::string value;
                        auto s = impl_->db->Get(key, &value);
                        if (s.ok()) {
                            res.set_content(value,
                                            "application/octet-stream");
                        } else if (s.IsNotFound()) {
                            res.status = 404;
                            res.set_content("Not Found", "text/plain");
                        } else {
                            res.status = 500;
                            res.set_content(s.ToString(), "text/plain");
                        }
                    });

    // DELETE /db/:key
    impl_->svr.Delete(R"(/db/(.+))",
                       [this](const httplib::Request& req,
                              httplib::Response& res) {
                           std::string key = req.matches[1];
                           auto s = impl_->db->Delete(key);
                           if (s.ok()) {
                               res.set_content("OK", "text/plain");
                           } else {
                               res.status = 500;
                               res.set_content(s.ToString(), "text/plain");
                           }
                       });
}

HttpServer::~HttpServer() { Stop(); }

void HttpServer::Start() {
    impl_->svr.listen(impl_->host, impl_->port);
}

void HttpServer::Stop() { impl_->svr.stop(); }

}  // namespace venus
