#pragma once
#include <cstddef>
#include <cstdint>

namespace venus {

uint32_t CRC32C(const char* data, size_t length);
uint32_t CRC32C_Extend(uint32_t crc, const char* data, size_t length);

// Mask/unmask to avoid storing raw CRC values that could collide with data
uint32_t MaskCRC(uint32_t crc);
uint32_t UnmaskCRC(uint32_t masked);

}  // namespace venus
