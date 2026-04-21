#include "venus/status.h"

namespace venus {

std::string Status::ToString() const {
    if (!state_) return "OK";
    std::string result;
    switch (state_->code) {
        case Code::kOk:
            result = "OK";
            break;
        case Code::kNotFound:
            result = "NotFound: ";
            break;
        case Code::kCorruption:
            result = "Corruption: ";
            break;
        case Code::kIOError:
            result = "IOError: ";
            break;
        case Code::kInvalidArgument:
            result = "InvalidArgument: ";
            break;
    }
    result += state_->message;
    return result;
}

}  // namespace venus
