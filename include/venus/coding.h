#pragma once
#include <cstdint>
#include <string>

namespace venus {

// Fixed-width encoding (little-endian)
void EncodeFixed32(char* buf, uint32_t value);
void EncodeFixed64(char* buf, uint64_t value);
uint32_t DecodeFixed32(const char* buf);
uint64_t DecodeFixed64(const char* buf);

// Varint encoding
void PutVarint32(std::string* dst, uint32_t value);
void PutVarint64(std::string* dst, uint64_t value);
bool GetVarint32(const char** input, const char* limit, uint32_t* value);
bool GetVarint64(const char** input, const char* limit, uint64_t* value);

// Length-prefixed string
void PutLengthPrefixedSlice(std::string* dst, const std::string& value);
bool GetLengthPrefixedSlice(const char** input, const char* limit,
                            std::string* result);

}  // namespace venus
