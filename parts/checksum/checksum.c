/* Impossible SDK: portable freestanding IEEE 802.3 CRC-32 and RFC 1950 Adler-32 checksums. */
#include "checksum.h"

/* IEEE 802.3 reflected polynomial 0xEDB88320, 16-entry nibble table (compact, zero-init, freestanding). */
static const uint32_t k_crc32_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

uint32_t im_crc32_init(void) {
    return 0u;
}

uint32_t im_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!data || len == 0u) return crc;
    uint32_t c = ~crc;
    for (size_t i = 0; i < len; ++i) {
        c ^= (uint32_t)data[i];
        c = (c >> 4) ^ k_crc32_nibble[c & 0x0Fu];
        c = (c >> 4) ^ k_crc32_nibble[c & 0x0Fu];
    }
    return ~c;
}

uint32_t im_crc32(const uint8_t *data, size_t len) {
    return im_crc32_update(im_crc32_init(), data, len);
}

uint32_t im_adler32_init(void) {
    return 1u;
}

uint32_t im_adler32_update(uint32_t adler, const uint8_t *data, size_t len) {
    if (!data || len == 0u) return adler;
    const uint32_t BASE = 65521u;
    const size_t NMAX = 5552u; /* largest n where 255*n*(n+1)/2 + (n+1)*(BASE-1) <= 2^32-1 */
    uint32_t s1 = adler & 0xFFFFu;
    uint32_t s2 = (adler >> 16) & 0xFFFFu;
    size_t i = 0;
    while (i < len) {
        size_t chunk = len - i;
        if (chunk > NMAX) chunk = NMAX;
        for (size_t k = 0; k < chunk; ++k) {
            s1 += (uint32_t)data[i + k];
            s2 += s1;
        }
        s1 %= BASE;
        s2 %= BASE;
        i += chunk;
    }
    return (s2 << 16) | s1;
}

uint32_t im_adler32(const uint8_t *data, size_t len) {
    return im_adler32_update(im_adler32_init(), data, len);
}
