#pragma once

#include <cstddef>
#include <cstdint>

inline uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0)
{
    crc = ~crc;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
