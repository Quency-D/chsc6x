#pragma once

#include <stddef.h>
#include <stdint.h>

namespace chsc6x_bytes {

inline uint16_t le16(const uint8_t *p)
{
    return p[0] | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t le32(const uint8_t *p)
{
    return le16(p) | (static_cast<uint32_t>(le16(p + 2)) << 16);
}

inline uint16_t sum16(const uint8_t *data, size_t length)
{
    uint16_t sum = 0;
    for (size_t i = 0; i + 1 < length; i += 2) {
        sum += le16(data + i);
    }
    return sum;
}

} // namespace chsc6x_bytes
