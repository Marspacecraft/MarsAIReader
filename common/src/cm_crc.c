#include "cm_crc.h"

uint16_t cm_crc16(uint16_t seed, const void *data, cm_size n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = seed;

    for (cm_size i = 0; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint32_t cm_crc32(uint32_t seed, const void *data, cm_size n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = ~seed;

    for (cm_size i = 0; i < n; i++) {
        crc ^= (uint32_t)p[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}
