/* Impossible SDK: portable freestanding IEEE 802.3 CRC-32 and RFC 1950 Adler-32 checksums. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Return the initial CRC-32 state (0). */
uint32_t im_crc32_init(void);

/* Update a running IEEE 802.3 CRC-32 checksum with data[0..len-1]. */
uint32_t im_crc32_update(uint32_t crc, const uint8_t *data, size_t len);

/* Compute the IEEE 802.3 CRC-32 checksum of data[0..len-1]. */
uint32_t im_crc32(const uint8_t *data, size_t len);

/* Return the initial Adler-32 state (1). */
uint32_t im_adler32_init(void);

/* Update a running RFC 1950 Adler-32 checksum with data[0..len-1]. */
uint32_t im_adler32_update(uint32_t adler, const uint8_t *data, size_t len);

/* Compute the RFC 1950 Adler-32 checksum of data[0..len-1]. */
uint32_t im_adler32(const uint8_t *data, size_t len);
