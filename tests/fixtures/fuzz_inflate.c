/* Native Linux libFuzzer target for parts/inflate/inflate.c + parts/checksum/checksum.c. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "checksum.h"
#include "inflate.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2u) return 0;
    im_inflate_format fmt = (im_inflate_format)(data[0] % 3u);
    uint8_t mode = data[1];
    const uint8_t *src = data + 2u;
    size_t src_len = size - 2u;

    /* Exercise checksums on raw input */
    (void)im_crc32(src, src_len);
    (void)im_adler32(src, src_len);

    static uint8_t out[65536];
    im_inflate st;
    bool has_lim = (mode & 1u) != 0u;
    size_t lim = has_lim ? (size_t)(mode * 64u) : 65536u;
    im_inflate_init(&st, fmt, true, lim < sizeof(out) ? lim : sizeof(out));

    if ((mode & 2u) == 0u) {
        size_t in_u = 0, out_u = 0;
        (void)im_inflate_step(&st, src, src_len, true, &in_u, out, sizeof(out), &out_u);
    } else {
        /* Chunked feeding (1..7 bytes at a time) */
        size_t pos = 0;
        size_t step_sz = (size_t)(mode & 7u) + 1u;
        size_t out_chunk = (size_t)((mode >> 3) & 31u) + 1u;
        for (int iter = 0; iter < 4096; ++iter) {
            size_t avail = src_len - pos;
            size_t take = avail < step_sz ? avail : step_sz;
            bool eof = (pos + take == src_len);
            size_t iu = 0, ou = 0;
            im_inflate_status r = im_inflate_step(&st, src + pos, take, eof, &iu, out, out_chunk, &ou);
            pos += iu;
            if (r != IM_INFLATE_NEED_INPUT && r != IM_INFLATE_OUT_FULL) break;
            if (r == IM_INFLATE_NEED_INPUT && eof && iu == 0) break;
        }
    }
    return 0;
}
