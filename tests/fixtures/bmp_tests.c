/* Impossible SDK: BMP decoder unit tests + persistent --batch test runner (Tier 0, Windows). */
#include <impossible/base.h>
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bmp.h"

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

static void wr_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void build_v3_hdr(uint8_t *buf, uint32_t bf_size, uint32_t bf_off,
                         int32_t w, int32_t h, uint16_t bpp, uint32_t comp, uint32_t img_size) {
    for (int i = 0; i < 54; i++) buf[i] = 0u;
    buf[0] = 'B'; buf[1] = 'M';
    wr_le32(buf + 2, bf_size);
    wr_le32(buf + 10, bf_off);
    wr_le32(buf + 14, 40u);
    wr_le32(buf + 18, (uint32_t)w);
    wr_le32(buf + 22, (uint32_t)h);
    wr_le16(buf + 26, 1u);
    wr_le16(buf + 28, bpp);
    wr_le32(buf + 30, comp);
    wr_le32(buf + 34, img_size);
}

static bool image_is_zero(const im_image *img) {
    return img->width == 0u && img->height == 0u && img->stride == 0u &&
           img->format == IM_PIXEL_BGRA8 && img->pixels == NULL && img->byte_size == 0u;
}

static void run_unit_tests(void) {
    HANDLE heap = GetProcessHeap();
    track_alloc tr = {heap, 0, 0, 0, 0, -1};
    im_bmp_alloc alloc = {t_alloc, t_free, &tr};

    /* 1. Largest BMP header (0x7FFFFFFF x 0x7FFFFFFF) against 16 MiB limit -> IM_IMAGE_TOO_LARGE, 0 allocs. */
    uint8_t hdr[64];
    build_v3_hdr(hdr, 0u, 54u, 0x7FFFFFFF, 0x7FFFFFFF, 24u, 0u, 0u);
    im_image_limits lim16 = {0x7FFFFFFFu, 0x7FFFFFFFu, 16u * 1024u * 1024u};
    im_bmp_info info;
    info.width = 999u;
    check(im_bmp_probe(hdr, 54u, &lim16, &info) == IM_IMAGE_TOO_LARGE, "max header probe -> TOO_LARGE");
    check(info.width == 0u && info.byte_size == 0u, "probe zeroes out_info on failure");

    im_image img;
    img.width = 123u;
    img.pixels = (uint8_t *)(uintptr_t)0x1234u;
    check(im_bmp_decode(hdr, 54u, IM_PIXEL_RGBA8, &lim16, &alloc, &img) == IM_IMAGE_TOO_LARGE, "max header decode -> TOO_LARGE");
    check(tr.alloc_calls == 0u, "max header decode makes 0 allocations");
    check(image_is_zero(&img), "decode zeroes prefilled out_image on failure");

    /* 2. Independent width, height, and byte-budget limits (0 allocs on refusal). */
    build_v3_hdr(hdr, 0u, 54u, 100, 50, 24u, 0u, 0u);
    im_image_limits lim_w = {99u, 1000u, 16u * 1024u * 1024u};
    im_image_limits lim_h = {1000u, 49u, 16u * 1024u * 1024u};
    im_image_limits lim_b = {1000u, 1000u, 100u * 50u * 4u - 1u};
    im_image_limits lim_exact = {100u, 50u, 100u * 50u * 4u};
    check(im_bmp_decode(hdr, 54u, IM_PIXEL_RGBA8, &lim_w, &alloc, &img) == IM_IMAGE_TOO_LARGE, "width limit");
    check(im_bmp_decode(hdr, 54u, IM_PIXEL_RGBA8, &lim_h, &alloc, &img) == IM_IMAGE_TOO_LARGE, "height limit");
    check(im_bmp_decode(hdr, 54u, IM_PIXEL_RGBA8, &lim_b, &alloc, &img) == IM_IMAGE_TOO_LARGE, "byte-budget limit");
    check(tr.alloc_calls == 0u, "width/height/byte refusals make 0 allocations");

    /* 3. Probe exact 54-byte boundary + NULL limits + INT32_MIN + zero-length vs NULL. */
    check(im_bmp_probe(hdr, 53u, NULL, &info) == IM_IMAGE_TRUNCATED, "53-byte v3 probe -> TRUNCATED");
    check(im_bmp_probe(hdr, 54u, &lim_exact, &info) == IM_IMAGE_OK, "54-byte v3 probe -> OK at exact limit");
    check(info.width == 100u && info.height == 50u && info.bit_count == 24u &&
          info.compression == 0u && info.header_size == 40u && !info.top_down && info.byte_size == 20000u,
          "probe returns all 7 fields accurately");
    check(im_bmp_probe(hdr, 54u, NULL, &info) == IM_IMAGE_OK, "probe accepts NULL limits");
    check(im_bmp_decode(hdr, 54u, IM_PIXEL_RGBA8, NULL, &alloc, &img) == IM_IMAGE_TRUNCATED, "decode on same 54B -> TRUNCATED");

    /* Non-NULL len == 0 -> IM_IMAGE_TRUNCATED, distinct from NULL -> IM_IMAGE_BAD_ARG. */
    uint8_t dummy = 0u;
    check(im_bmp_probe(&dummy, 0u, NULL, &info) == IM_IMAGE_TRUNCATED, "probe non-NULL len 0 -> TRUNCATED");
    check(im_bmp_decode(&dummy, 0u, IM_PIXEL_RGBA8, NULL, &alloc, &img) == IM_IMAGE_TRUNCATED, "decode non-NULL len 0 -> TRUNCATED");
    check(im_bmp_probe(NULL, 0u, NULL, &info) == IM_IMAGE_BAD_ARG, "probe NULL data -> BAD_ARG");
    check(im_bmp_probe(hdr, 54u, NULL, NULL) == IM_IMAGE_BAD_ARG, "probe NULL out_info -> BAD_ARG");

    /* INT32_MIN height -> IM_IMAGE_INVALID. */
    build_v3_hdr(hdr, 0u, 54u, 1, (int32_t)0x80000000u, 24u, 0u, 0u);
    check(im_bmp_probe(hdr, 54u, NULL, &info) == IM_IMAGE_INVALID, "INT32_MIN height -> INVALID");

    /* Argument validation. */
    im_bmp_alloc bad_a1 = {NULL, t_free, &tr};
    im_bmp_alloc bad_a2 = {t_alloc, NULL, &tr};
    check(im_bmp_decode(NULL, 54u, IM_PIXEL_RGBA8, NULL, &alloc, &img) == IM_IMAGE_BAD_ARG, "decode NULL data -> BAD_ARG");
    check(im_bmp_decode(hdr, 54u, IM_PIXEL_RGBA8, NULL, &alloc, NULL) == IM_IMAGE_BAD_ARG, "decode NULL out -> BAD_ARG");
    check(im_bmp_decode(hdr, 54u, IM_PIXEL_RGBA8, NULL, NULL, &img) == IM_IMAGE_BAD_ARG, "decode NULL alloc -> BAD_ARG");
    check(im_bmp_decode(hdr, 54u, IM_PIXEL_RGBA8, NULL, &bad_a1, &img) == IM_IMAGE_BAD_ARG, "decode NULL alloc fn -> BAD_ARG");
    check(im_bmp_decode(hdr, 54u, IM_PIXEL_RGBA8, NULL, &bad_a2, &img) == IM_IMAGE_BAD_ARG, "decode NULL free fn -> BAD_ARG");
    check(im_bmp_decode(hdr, 54u, (im_pixel_format)99, NULL, &alloc, &img) == IM_IMAGE_BAD_ARG, "decode bad format -> BAD_ARG");

    /* 4. Valid 1x1 24-bpp BMP (R=0x12, G=0x34, B=0x56): RGBA8, BGRA8, exact limits, NULL limits, OOM. */
    uint8_t one[64];
    build_v3_hdr(one, 58u, 54u, 1, 1, 24u, 0u, 4u);
    one[54] = 0x56u; one[55] = 0x34u; one[56] = 0x12u; one[57] = 0x00u; /* BGR + pad */

    check(im_bmp_decode(one, 58u, IM_PIXEL_RGBA8, NULL, &alloc, &img) == IM_IMAGE_OK, "1x1 RGBA8 NULL limits OK");
    check(img.width == 1u && img.height == 1u && img.stride == 4u && img.format == IM_PIXEL_RGBA8 && img.byte_size == 4u, "RGBA8 fields");
    check(img.pixels && img.pixels[0] == 0x12u && img.pixels[1] == 0x34u && img.pixels[2] == 0x56u && img.pixels[3] == 0xFFu, "RGBA8 bytes");
    check(tr.live_blocks == 1u && tr.live_bytes == 4u, "only pixels live on success");
    alloc.free(alloc.ctx, img.pixels, img.byte_size);
    check(tr.live_blocks == 0u && tr.live_bytes == 0u, "caller free leaves 0 live allocations");

    im_image_limits lim1 = {1u, 1u, 4u};
    check(im_bmp_decode(one, 58u, IM_PIXEL_BGRA8, &lim1, &alloc, &img) == IM_IMAGE_OK, "1x1 BGRA8 exact limits OK");
    check(img.format == IM_PIXEL_BGRA8 && img.stride == 4u && img.byte_size == 4u, "BGRA8 fields");
    check(img.pixels && img.pixels[0] == 0x56u && img.pixels[1] == 0x34u && img.pixels[2] == 0x12u && img.pixels[3] == 0xFFu, "BGRA8 bytes");
    alloc.free(alloc.ctx, img.pixels, img.byte_size);

    /* OOM fault injection */
    tr.alloc_calls = 0u;
    tr.fail_on_ordinal = 0;
    img.width = 77u;
    img.pixels = (uint8_t *)(uintptr_t)0x9999u;
    check(im_bmp_decode(one, 58u, IM_PIXEL_RGBA8, NULL, &alloc, &img) == IM_IMAGE_NO_MEMORY, "OOM returns NO_MEMORY");
    check(image_is_zero(&img), "OOM zeroes prefilled out_image");
    check(tr.live_blocks == 0u && tr.live_bytes == 0u, "OOM leaks 0 blocks");
    tr.fail_on_ordinal = -1;
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
    im_bmp_alloc alloc = {t_alloc, t_free, &tr};

    uint8_t *in_buf = NULL;
    size_t in_cap = 0;

    for (;;) {
        uint32_t hdr[2]; /* [0]=op (0=quit, 1=RGBA8, 2=BGRA8, 3=probe, 4=check all prefixes), [1]=len */
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
            im_image_error err = im_bmp_decode(in_buf, len, fmt, NULL, &alloc, &img);
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
            im_bmp_info info;
            info.width = 0xDEADu;
            im_image_error err = im_bmp_probe(in_buf, len, NULL, &info);
            uint32_t resp[4] = {(uint32_t)err, info.width, info.height, (uint32_t)info.byte_size};
            write_exact(hout, resp, sizeof(resp));
        } else if (op == 4u) {
            /* Exhaustive proper-prefix check: returns [bad_count, first_bad_plen, ok_prefix_count].
               For every proper prefix 0..len-1:
               - If decode fails: must zero out_image and leak 0 blocks.
               - If decode succeeds (only valid if full buffer also succeeded with identical pixels):
                 must match full width, height, and every pixel byte! */
            im_image full;
            tr.live_blocks = 0u;
            tr.live_bytes = 0u;
            im_image_error full_err = im_bmp_decode(in_buf, len, IM_PIXEL_RGBA8, NULL, &alloc, &full);
            uint32_t bad = 0u;
            uint32_t first_bad = 0xFFFFFFFFu;
            uint32_t ok_prefixes = 0u;
            for (uint32_t plen = 0u; plen < len; plen++) {
                im_image img;
                img.width = 99u;
                img.pixels = (uint8_t *)(uintptr_t)0x1234u;
                tr.live_blocks = 0u;
                tr.live_bytes = 0u;
                im_image_error err = im_bmp_decode(in_buf, plen, IM_PIXEL_RGBA8, NULL, &alloc, &img);
                if (err == IM_IMAGE_OK) {
                    ok_prefixes++;
                    bool match = (full_err == IM_IMAGE_OK && img.width == full.width &&
                                  img.height == full.height && img.byte_size == full.byte_size &&
                                  tr.live_blocks == 1u && tr.live_bytes == img.byte_size);
                    if (match) {
                        for (size_t i = 0; i < img.byte_size; i++) {
                            if (img.pixels[i] != full.pixels[i]) { match = false; break; }
                        }
                    }
                    if (!match) {
                        if (bad == 0u) first_bad = plen;
                        bad++;
                    }
                    alloc.free(alloc.ctx, img.pixels, img.byte_size);
                } else {
                    if (!image_is_zero(&img) || tr.live_blocks != 0u || tr.live_bytes != 0u) {
                        if (bad == 0u) first_bad = plen;
                        bad++;
                    }
                }
            }
            if (full_err == IM_IMAGE_OK && full.pixels) alloc.free(alloc.ctx, full.pixels, full.byte_size);
            uint32_t resp[3] = {bad, first_bad, ok_prefixes};
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
        write_str("bmp_tests: ALL PASSED\n");
        return 0;
    }
    return 1;
}
