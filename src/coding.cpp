#include "venus/coding.h"

#include <cstring>

namespace venus {

void EncodeFixed32(char* buf, uint32_t value) { memcpy(buf, &value, 4); }

void EncodeFixed64(char* buf, uint64_t value) { memcpy(buf, &value, 8); }

uint32_t DecodeFixed32(const char* buf) {
    uint32_t result;
    memcpy(&result, buf, 4);
    return result;
}

uint64_t DecodeFixed64(const char* buf) {
    uint64_t result;
    memcpy(&result, buf, 8);
    return result;
}

void PutVarint32(std::string* dst, uint32_t value) {
    uint8_t buf[5];
    int len = 0;
    while (value >= 0x80) {
        buf[len++] = static_cast<uint8_t>(value | 0x80);
        value >>= 7;
    }
    buf[len++] = static_cast<uint8_t>(value);
    dst->append(reinterpret_cast<const char*>(buf), len);
}

void PutVarint64(std::string* dst, uint64_t value) {
    uint8_t buf[10];
    int len = 0;
    while (value >= 0x80) {
        buf[len++] = static_cast<uint8_t>(value | 0x80);
        value >>= 7;
    }
    buf[len++] = static_cast<uint8_t>(value);
    dst->append(reinterpret_cast<const char*>(buf), len);
}

bool GetVarint32(const char** input, const char* limit, uint32_t* value) {
    const char* p = *input;
    uint32_t result = 0;
    for (int shift = 0; shift <= 28 && p < limit; shift += 7) {
        uint32_t byte = static_cast<uint8_t>(*p);
        p++;
        if (byte & 0x80) {
            result |= ((byte & 0x7F) << shift);
        } else {
            result |= (byte << shift);
            *value = result;
            *input = p;
            return true;
        }
    }
    return false;
}

bool GetVarint64(const char** input, const char* limit, uint64_t* value) {
    const char* p = *input;
    uint64_t result = 0;
    for (int shift = 0; shift <= 63 && p < limit; shift += 7) {
        uint64_t byte = static_cast<uint8_t>(*p);
        p++;
        if (byte & 0x80) {
            result |= ((byte & 0x7F) << shift);
        } else {
            result |= (byte << shift);
            *value = result;
            *input = p;
            return true;
        }
    }
    return false;
}

void PutLengthPrefixedSlice(std::string* dst, const std::string& value) {
    PutVarint32(dst, static_cast<uint32_t>(value.size()));
    dst->append(value);
}

bool GetLengthPrefixedSlice(const char** input, const char* limit,
                            std::string* result) {
    uint32_t len;
    if (!GetVarint32(input, limit, &len)) return false;
    if (*input + len > limit) return false;
    result->assign(*input, len);
    *input += len;
    return true;
}

}  // namespace venus
