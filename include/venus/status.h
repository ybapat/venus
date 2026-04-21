#pragma once
#include <memory>
#include <string>

namespace venus {

class Status {
public:
    Status() = default;

    static Status OK() { return Status(); }
    static Status NotFound(const std::string& msg) {
        return Status(Code::kNotFound, msg);
    }
    static Status Corruption(const std::string& msg) {
        return Status(Code::kCorruption, msg);
    }
    static Status IOError(const std::string& msg) {
        return Status(Code::kIOError, msg);
    }
    static Status InvalidArgument(const std::string& msg) {
        return Status(Code::kInvalidArgument, msg);
    }

    bool ok() const { return !state_; }
    bool IsNotFound() const {
        return state_ && state_->code == Code::kNotFound;
    }
    bool IsCorruption() const {
        return state_ && state_->code == Code::kCorruption;
    }
    bool IsIOError() const {
        return state_ && state_->code == Code::kIOError;
    }

    std::string ToString() const;

private:
    enum class Code { kOk, kNotFound, kCorruption, kIOError, kInvalidArgument };
    struct State {
        Code code;
        std::string message;
    };
    std::shared_ptr<State> state_;

    Status(Code code, const std::string& msg)
        : state_(std::make_shared<State>(State{code, msg})) {}
};

}  // namespace venus
