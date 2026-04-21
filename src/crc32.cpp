#include "venus/crc32.h"

namespace venus {

// CRC32C (Castagnoli) lookup table
static uint32_t crc32c_table[256];
static bool table_initialized = false;

static void InitCRC32CTable() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0x82F63B78;  // CRC32C polynomial
            else
                crc >>= 1;
        }
        crc32c_table[i] = crc;
    }
    table_initialized = true;
}

uint32_t CRC32C_Extend(uint32_t crc, const char* data, size_t length) {
    if (!table_initialized) InitCRC32CTable();
    crc = ~crc;
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = static_cast<uint8_t>(data[i]);
        crc = crc32c_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

uint32_t CRC32C(const char* data, size_t length) {
    return CRC32C_Extend(0, data, length);
}

static constexpr uint32_t kMaskDelta = 0xa282ead8;

uint32_t MaskCRC(uint32_t crc) {
    return ((crc >> 15) | (crc << 17)) + kMaskDelta;
}

uint32_t UnmaskCRC(uint32_t masked) {
    uint32_t rot = masked - kMaskDelta;
    return (rot >> 17) | (rot << 15);
}

}  // namespace venus
