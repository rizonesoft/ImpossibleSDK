/* Impossible SDK: PNG decoder unit tests + persistent --batch test runner (Tier 0, Windows). */
#include <impossible/base.h>
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "checksum.h"
#include "png.h"

static int g_fails = 0;

static void write_str(const char *s) {
    DWORD n = 0;
    while (s[n]) n++;
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &w, NULL);
}

static void check(bool cond, const char *msg) {
    if (!cond) {
        write_str("FAIL: ");
        write_str(msg);
        write_str("\n");
        g_fails++;
    }
}

typedef struct track_alloc {
    HANDLE heap;
    size_t alloc_calls;
    size_t free_calls;
    size_t live_blocks;
    size_t live_bytes;
    int fail_on_ordinal; /* -1 = never fail */
} track_alloc;

static void *t_alloc(void *ctx, size_t size) {
    track_alloc *t = (track_alloc *)ctx;
    int ord = (int)t->alloc_calls;
    t->alloc_calls++;
    if (t->fail_on_ordinal >= 0 && ord == t->fail_on_ordinal) return NULL;
    void *p = HeapAlloc(t->heap, 0, size ? size : 1u);
    if (p) {
        t->live_blocks++;
        t->live_bytes += size;
    }
    return p;
}

static void t_free(void *ctx, void *ptr, size_t size) {
    track_alloc *t = (track_alloc *)ctx;
    if (!ptr) return;
    t->free_calls++;
    t->live_blocks--;
    t->live_bytes -= size;
    HeapFree(t->heap, 0, ptr);
}

static void wr_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static size_t append_chunk(uint8_t *dst, const char type[4], const uint8_t *payload, uint32_t len) {
    wr_be32(dst, len);
    dst[4] = (uint8_t)type[0];
    dst[5] = (uint8_t)type[1];
    dst[6] = (uint8_t)type[2];
    dst[7] = (uint8_t)type[3];
    for (uint32_t i = 0; i < len; i++) dst[8u + i] = payload[i];
    uint32_t crc = im_crc32(dst + 4u, (size_t)len + 4u);
    wr_be32(dst + 8u + len, crc);
    return 12u + (size_t)len;
}

static size_t build_ihdr_only(uint8_t *buf, uint32_t w, uint32_t h, uint8_t bd, uint8_t ct, uint8_t interlace) {
    static const uint8_t sig[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    for (int i = 0; i < 8; i++) buf[i] = sig[i];
    uint8_t ihdr[13];
    wr_be32(ihdr, w);
    wr_be32(ihdr + 4, h);
    ihdr[8] = bd;
    ihdr[9] = ct;
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = interlace;
    return 8u + append_chunk(buf + 8u, "IHDR", ihdr, 13u);
}

static bool image_is_zero(const im_image *img) {
    return img->width == 0u && img->height == 0u && img->stride == 0u &&
           img->format == IM_PIXEL_BGRA8 && img->pixels == NULL && img->byte_size == 0u;
}

static void run_unit_tests(void) {
    HANDLE heap = GetProcessHeap();
    track_alloc tr = {heap, 0, 0, 0, 0, -1};
    im_png_alloc alloc = {t_alloc, t_free, &tr};

    /* 1. Largest PNG header (0x7FFFFFFF x 0x7FFFFFFF) against 16 MiB limit -> IM_IMAGE_TOO_LARGE, 0 allocs. */
    uint8_t hdr[64];
    size_t hdr_len = build_ihdr_only(hdr, 0x7FFFFFFFu, 0x7FFFFFFFu, 8u, 6u, 0u);
    im_image_limits lim16 = {0x7FFFFFFFu, 0x7FFFFFFFu, 16u * 1024u * 1024u};
    im_png_info info;
    info.width = 999u;
    im_image_error e = im_png_probe(hdr, hdr_len, &lim16, &info);
    check(e == IM_IMAGE_TOO_LARGE, "max IHDR probe returns IM_IMAGE_TOO_LARGE");
    check(info.width == 0u && info.byte_size == 0u, "probe zeroes out_info on failure");

    im_image img;
    img.width = 123u;
    img.height = 456u;
    img.pixels = (uint8_t *)(uintptr_t)0x1234u;
    img.byte_size = 789u;
    e = im_png_decode(hdr, hdr_len, IM_PIXEL_RGBA8, &lim16, &alloc, &img);
    check(e == IM_IMAGE_TOO_LARGE, "max IHDR decode returns IM_IMAGE_TOO_LARGE");
    check(tr.alloc_calls == 0u, "max IHDR decode makes 0 allocations");
    check(image_is_zero(&img), "decode zeroes prefilled out_image on failure");

    /* 2. Independent width, height, and byte-budget limit checks with 0 allocations. */
    size_t h100 = build_ihdr_only(hdr, 100u, 50u, 8u, 6u, 0u);
    im_image_limits lim_w = {99u, 1000u, 16u * 1024u * 1024u};
    im_image_limits lim_h = {1000u, 49u, 16u * 1024u * 1024u};
    im_image_limits lim_b = {1000u, 1000u, 100u * 50u * 4u - 1u};
    im_image_limits lim_exact = {100u, 50u, 100u * 50u * 4u};
    check(im_png_decode(hdr, h100, IM_PIXEL_RGBA8, &lim_w, &alloc, &img) == IM_IMAGE_TOO_LARGE, "width limit enforced");
    check(im_png_decode(hdr, h100, IM_PIXEL_RGBA8, &lim_h, &alloc, &img) == IM_IMAGE_TOO_LARGE, "height limit enforced");
    check(im_png_decode(hdr, h100, IM_PIXEL_RGBA8, &lim_b, &alloc, &img) == IM_IMAGE_TOO_LARGE, "byte-budget limit enforced");
    check(tr.alloc_calls == 0u, "width/height/byte refusals make 0 allocations");

    /* 3. Exact 33-byte probe success + all 6 fields + NULL limits. */
    check(h100 == 33u, "IHDR-only buffer is 33 bytes");
    check(im_png_probe(hdr, 32u, NULL, &info) == IM_IMAGE_TRUNCATED, "32-byte probe is TRUNCATED");
    check(im_png_probe(hdr, 33u, &lim_exact, &info) == IM_IMAGE_OK, "33-byte probe succeeds at exact limit");
    check(info.width == 100u && info.height == 50u && info.bit_depth == 8u &&
          info.color_type == 6u && info.interlace == 0u && info.byte_size == 20000u,
          "probe returns all 6 fields accurately");
    check(im_png_probe(hdr, 33u, NULL, &info) == IM_IMAGE_OK, "probe accepts NULL limits");

    /* Bad IHDR CRC zeroes prefilled out_info. */
    hdr[32] ^= 0x01u;
    info.width = 42u;
    check(im_png_probe(hdr, 33u, NULL, &info) == IM_IMAGE_INVALID, "bad IHDR CRC fails probe");
    check(info.width == 0u && info.height == 0u && info.byte_size == 0u, "bad IHDR CRC zeroes out_info");

    /* 4. Public argument validation. */
    im_png_alloc bad_a1 = {NULL, t_free, &tr};
    im_png_alloc bad_a2 = {t_alloc, NULL, &tr};
    check(im_png_probe(NULL, 33u, NULL, &info) == IM_IMAGE_BAD_ARG, "probe NULL data -> BAD_ARG");
    check(im_png_probe(hdr, 33u, NULL, NULL) == IM_IMAGE_BAD_ARG, "probe NULL out_info -> BAD_ARG");
    check(im_png_decode(NULL, 33u, IM_PIXEL_RGBA8, NULL, &alloc, &img) == IM_IMAGE_BAD_ARG, "decode NULL data -> BAD_ARG");
    check(im_png_decode(hdr, 33u, IM_PIXEL_RGBA8, NULL, &alloc, NULL) == IM_IMAGE_BAD_ARG, "decode NULL out -> BAD_ARG");
    check(im_png_decode(hdr, 33u, IM_PIXEL_RGBA8, NULL, NULL, &img) == IM_IMAGE_BAD_ARG, "decode NULL alloc -> BAD_ARG");
    check(im_png_decode(hdr, 33u, IM_PIXEL_RGBA8, NULL, &bad_a1, &img) == IM_IMAGE_BAD_ARG, "decode NULL alloc fn -> BAD_ARG");
    check(im_png_decode(hdr, 33u, IM_PIXEL_RGBA8, NULL, &bad_a2, &img) == IM_IMAGE_BAD_ARG, "decode NULL free fn -> BAD_ARG");
    check(im_png_decode(hdr, 33u, (im_pixel_format)99, NULL, &alloc, &img) == IM_IMAGE_BAD_ARG, "decode bad format -> BAD_ARG");

    /* 5. Valid 1x1 RGBA8 pixel (0x12, 0x34, 0x56, 0x78): test RGBA8, BGRA8, NULL limits, OOM ordinals, late IEND CRC. */
    uint8_t one[128];
    size_t n = build_ihdr_only(one, 1u, 1u, 8u, 6u, 0u);
    /* Stored zlib block of 5 bytes: filter=0, R=0x12, G=0x34, B=0x56, A=0x78. */
    uint8_t raw5[5] = {0x00u, 0x12u, 0x34u, 0x56u, 0x78u};
    uint32_t adl = im_adler32_update(im_adler32_init(), raw5, 5u);
    uint8_t zbuf[16];
    zbuf[0] = 0x78u; zbuf[1] = 0x01u;
    zbuf[2] = 0x01u; /* BFINAL=1, BTYPE=0 */
    zbuf[3] = 0x05u; zbuf[4] = 0x00u;
    zbuf[5] = 0xFAu; zbuf[6] = 0xFFu;
    for (int i = 0; i < 5; i++) zbuf[7 + i] = raw5[i];
    wr_be32(zbuf + 12, adl);
    n += append_chunk(one + n, "IDAT", zbuf, 16u);
    n += append_chunk(one + n, "IEND", NULL, 0u);

    /* RGBA8 with NULL limits */
    check(im_png_decode(one, n, IM_PIXEL_RGBA8, NULL, &alloc, &img) == IM_IMAGE_OK, "1x1 RGBA8 NULL limits OK");
    check(img.width == 1u && img.height == 1u && img.stride == 4u && img.format == IM_PIXEL_RGBA8 && img.byte_size == 4u, "RGBA8 fields");
    check(img.pixels && img.pixels[0] == 0x12u && img.pixels[1] == 0x34u && img.pixels[2] == 0x56u && img.pixels[3] == 0x78u, "RGBA8 bytes");
    check(tr.live_blocks == 1u && tr.live_bytes == 4u, "only pixels remain live on success");
    alloc.free(alloc.ctx, img.pixels, img.byte_size);
    check(tr.live_blocks == 0u && tr.live_bytes == 0u, "caller free leaves 0 live allocations");

    /* BGRA8 with exact limits */
    im_image_limits lim1 = {1u, 1u, 4u};
    check(im_png_decode(one, n, IM_PIXEL_BGRA8, &lim1, &alloc, &img) == IM_IMAGE_OK, "1x1 BGRA8 exact limits OK");
    check(img.format == IM_PIXEL_BGRA8 && img.stride == 4u && img.byte_size == 4u, "BGRA8 fields");
    check(img.pixels && img.pixels[0] == 0x56u && img.pixels[1] == 0x34u && img.pixels[2] == 0x12u && img.pixels[3] == 0x78u, "BGRA8 bytes");
    alloc.free(alloc.ctx, img.pixels, img.byte_size);

    /* Allocation-ordinal OOM fault injection (0, 1, 2) */
    for (int ord = 0; ord < 3; ord++) {
        tr.alloc_calls = 0u;
        tr.fail_on_ordinal = ord;
        img.width = 77u;
        img.pixels = (uint8_t *)(uintptr_t)0x9999u;
        e = im_png_decode(one, n, IM_PIXEL_RGBA8, NULL, &alloc, &img);
        check(e == IM_IMAGE_NO_MEMORY, "OOM ordinal returns IM_IMAGE_NO_MEMORY");
        check(image_is_zero(&img), "OOM zeroes prefilled out_image");
        check(tr.live_blocks == 0u && tr.live_bytes == 0u, "OOM leaks 0 blocks");
    }
    tr.fail_on_ordinal = -1;

    /* Late IEND CRC failure leaves 0 live allocations */
    one[n - 1u] ^= 0x80u;
    img.width = 88u;
    e = im_png_decode(one, n, IM_PIXEL_RGBA8, NULL, &alloc, &img);
    check(e == IM_IMAGE_INVALID, "damaged IEND CRC -> IM_IMAGE_INVALID");
    check(image_is_zero(&img), "damaged IEND CRC zeroes out_image");
    check(tr.live_blocks == 0u && tr.live_bytes == 0u, "damaged IEND CRC leaks 0 blocks");
}

static bool read_exact(HANDLE h, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < n) {
        DWORD rd = 0;
        if (!ReadFile(h, p + got, (DWORD)(n - got), &rd, NULL) || rd == 0) return false;
        got += rd;
    }
    return true;
}

static bool write_exact(HANDLE h, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < n) {
        DWORD wr = 0;
        if (!WriteFile(h, p + sent, (DWORD)(n - sent), &wr, NULL) || wr == 0) return false;
        sent += wr;
    }
    return true;
}

static void run_batch(void) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE heap = GetProcessHeap();
    track_alloc tr = {heap, 0, 0, 0, 0, -1};
    im_png_alloc alloc = {t_alloc, t_free, &tr};

    uint8_t *in_buf = NULL;
    size_t in_cap = 0;

    for (;;) {
        uint32_t hdr[2]; /* [0]=op (0=quit, 1=decode RGBA8, 2=decode BGRA8, 3=check all proper prefixes), [1]=len */
        if (!read_exact(hin, hdr, sizeof(hdr))) break;
        uint32_t op = hdr[0];
        uint32_t len = hdr[1];
        if (op == 0u) break;
        if (len > in_cap) {
            if (in_buf) HeapFree(heap, 0, in_buf);
            in_buf = (uint8_t *)HeapAlloc(heap, 0, len ? len : 1u);
            in_cap = len ? len : 1u;
        }
        if (len > 0u && !read_exact(hin, in_buf, len)) break;

        if (op == 1u || op == 2u) {
            im_pixel_format fmt = (op == 2u) ? IM_PIXEL_BGRA8 : IM_PIXEL_RGBA8;
            im_image img;
            img.width = 0xDEADu;
            img.pixels = (uint8_t *)(uintptr_t)0xBEEFu;
            tr.live_blocks = 0u;
            tr.live_bytes = 0u;
            im_image_error err = im_png_decode(in_buf, len, fmt, NULL, &alloc, &img);
            uint32_t resp[5];
            resp[0] = (uint32_t)err;
            resp[1] = img.width;
            resp[2] = img.height;
            resp[3] = (uint32_t)img.byte_size;
            if (err == IM_IMAGE_OK) {
                resp[4] = (tr.live_blocks == 1u && tr.live_bytes == img.byte_size) ? 0u : 1u;
                write_exact(hout, resp, sizeof(resp));
                if (img.byte_size > 0u) write_exact(hout, img.pixels, img.byte_size);
                alloc.free(alloc.ctx, img.pixels, img.byte_size);
            } else {
                resp[4] = (image_is_zero(&img) && tr.live_blocks == 0u && tr.live_bytes == 0u) ? 0u : 1u;
                write_exact(hout, resp, sizeof(resp));
            }
        } else if (op == 3u) {
            /* Test EVERY proper prefix 0 .. len-1 in-process for speed! Returns [bad_prefix_count, first_bad_prefix]. */
            uint32_t bad = 0u;
            uint32_t first_bad = 0xFFFFFFFFu;
            for (uint32_t plen = 0u; plen < len; plen++) {
                im_image img;
                img.width = 99u;
                img.pixels = (uint8_t *)(uintptr_t)0x1234u;
                tr.live_blocks = 0u;
                tr.live_bytes = 0u;
                im_image_error err = im_png_decode(in_buf, plen, IM_PIXEL_RGBA8, NULL, &alloc, &img);
                if (err == IM_IMAGE_OK || !image_is_zero(&img) || tr.live_blocks != 0u || tr.live_bytes != 0u) {
                    if (bad == 0u) first_bad = plen;
                    bad++;
                    if (err == IM_IMAGE_OK && img.pixels) alloc.free(alloc.ctx, img.pixels, img.byte_size);
                }
            }
            uint32_t resp[2] = {bad, first_bad};
            write_exact(hout, resp, sizeof(resp));
        }
    }
    if (in_buf) HeapFree(heap, 0, in_buf);
}

int im_main(void) {
    const wchar_t *cmd = GetCommandLineW();
    bool batch = false;
    if (cmd) {
        for (const wchar_t *p = cmd; *p; p++) {
            if (p[0] == L'-' && p[1] == L'-' && p[2] == L'b' && p[3] == L'a' &&
                p[4] == L't' && p[5] == L'c' && p[6] == L'h') {
                batch = true;
                break;
            }
        }
    }
    if (batch) {
        run_batch();
        return 0;
    }
    run_unit_tests();
    if (g_fails == 0) {
        write_str("png_tests: ALL PASSED\n");
        return 0;
    }
    return 1;
}
