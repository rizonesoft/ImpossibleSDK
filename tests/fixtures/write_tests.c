/* Tier 0 tests for D06 T01 s10: parts/png/write.c and parts/bmp/write.c. */
#include <impossible/base.h>
#include <windows.h>
#include "bmp.h"
#include "png.h"

static int g_fails = 0;

static void write_str(const char *s) {
    DWORD n = 0;
    while (s[n]) ++n;
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &w, NULL);
}

static void check(bool cond, const char *msg) {
    if (!cond) {
        ++g_fails;
        write_str("FAIL: ");
        write_str(msg);
        write_str("\n");
    }
}

typedef struct alloc_slot {
    void *ptr;
    size_t size;
} alloc_slot;

typedef struct track_ctx {
    HANDLE heap;
    alloc_slot slots[1024];
    uint32_t live;
    uint32_t total_allocs;
    uint32_t mismatch_or_double_free;
    bool fail_enabled;
    uint32_t fail_at;
} track_ctx;

static void *track_alloc(void *vctx, size_t size) {
    track_ctx *c = (track_ctx *)vctx;
    uint32_t ord = c->total_allocs++;
    if (c->fail_enabled && ord == c->fail_at) return NULL;
    if (size == 0u) return NULL;
    void *p = HeapAlloc(c->heap, 0, size);
    if (!p) return NULL;
    for (uint32_t i = 0; i < 1024u; ++i) {
        if (c->slots[i].ptr == NULL) {
            c->slots[i].ptr = p;
            c->slots[i].size = size;
            c->live++;
            return p;
        }
    }
    HeapFree(c->heap, 0, p);
    c->mismatch_or_double_free++;
    return NULL;
}

static void track_free(void *vctx, void *ptr, size_t size) {
    track_ctx *c = (track_ctx *)vctx;
    if (!ptr) {
        c->mismatch_or_double_free++;
        return;
    }
    for (uint32_t i = 0; i < 1024u; ++i) {
        if (c->slots[i].ptr == ptr) {
            if (c->slots[i].size != size) c->mismatch_or_double_free++;
            c->slots[i].ptr = NULL;
            c->slots[i].size = 0;
            c->live--;
            HeapFree(c->heap, 0, ptr);
            return;
        }
    }
    c->mismatch_or_double_free++;
}

static void track_init(track_ctx *c, bool fail_en, uint32_t fail_at) {
    c->heap = GetProcessHeap();
    for (uint32_t i = 0; i < 1024u; ++i) { c->slots[i].ptr = NULL; c->slots[i].size = 0; }
    c->live = 0;
    c->total_allocs = 0;
    c->mismatch_or_double_free = 0;
    c->fail_enabled = fail_en;
    c->fail_at = fail_at;
}

static bool read_exact(HANDLE h, uint8_t *dst, size_t n) {
    size_t got = 0;
    while (got < n) {
        DWORD chunk = (n - got) > 0x100000u ? 0x100000u : (DWORD)(n - got);
        DWORD r = 0;
        if (!ReadFile(h, dst + got, chunk, &r, NULL) || r == 0) return false;
        got += r;
    }
    return true;
}

static bool write_exact(HANDLE h, const uint8_t *src, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        DWORD chunk = (n - sent) > 0x100000u ? 0x100000u : (DWORD)(n - sent);
        DWORD w = 0;
        if (!WriteFile(h, src + sent, chunk, &w, NULL) || w == 0) return false;
        sent += w;
    }
    return true;
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void test_boundaries_and_oom(void) {
    track_ctx tc;
    track_init(&tc, false, 0);
    im_png_alloc pa = { track_alloc, track_free, &tc };
    im_bmp_alloc ba = { track_alloc, track_free, &tc };

    uint8_t px4[16] = {
        10, 20, 30, 255,   40, 50, 60, 127,
        70, 80, 90, 0,     100, 100, 100, 255
    };
    im_image img = { 2, 2, 8, IM_PIXEL_RGBA8, px4, sizeof(px4) };

    /* 1. Independent output clearing on missing required pointer. */
    uint8_t dummy_byte = 0;
    uint8_t *out_p = &dummy_byte;
    size_t out_n = 777u;
    check(im_png_encode(&img, NULL, &pa, &out_p, NULL) == IM_IMAGE_BAD_ARG && out_p == NULL,
          "png_encode NULL out_len clears *out_data independently");
    out_n = 777u;
    check(im_png_encode(&img, NULL, &pa, NULL, &out_n) == IM_IMAGE_BAD_ARG && out_n == 0u,
          "png_encode NULL out_data clears *out_len independently");
    out_p = &dummy_byte;
    check(im_bmp_encode(&img, IM_BMP_WRITE_AUTO, &ba, &out_p, NULL) == IM_IMAGE_BAD_ARG && out_p == NULL,
          "bmp_encode NULL out_len clears *out_data independently");
    out_n = 777u;
    check(im_bmp_encode(&img, IM_BMP_WRITE_AUTO, &ba, NULL, &out_n) == IM_IMAGE_BAD_ARG && out_n == 0u,
          "bmp_encode NULL out_data clears *out_len independently");

    /* 2. 32-bit width*4 wraparound (0x40000000) -> BAD_ARG; width > 0x7FFFFFFF -> TOO_LARGE. */
    im_image wrap_img = { 0x40000000u, 1u, 4u, IM_PIXEL_RGBA8, px4, sizeof(px4) };
    check(im_png_encode(&wrap_img, NULL, &pa, &out_p, &out_n) == IM_IMAGE_BAD_ARG,
          "png_encode width=0x40000000 (32-bit wrap) -> BAD_ARG");
    check(im_bmp_encode(&wrap_img, IM_BMP_WRITE_AUTO, &ba, &out_p, &out_n) == IM_IMAGE_BAD_ARG,
          "bmp_encode width=0x40000000 (32-bit wrap) -> BAD_ARG");

    im_image huge_img = { 0x80000000u, 1u, 4u, IM_PIXEL_RGBA8, px4, sizeof(px4) };
    check(im_png_encode(&huge_img, NULL, &pa, &out_p, &out_n) == IM_IMAGE_TOO_LARGE,
          "png_encode width=0x80000000 -> TOO_LARGE");
    check(im_bmp_encode(&huge_img, IM_BMP_WRITE_AUTO, &ba, &out_p, &out_n) == IM_IMAGE_TOO_LARGE,
          "bmp_encode width=0x80000000 -> TOO_LARGE");

    /* 3. Lossless mode refusals return IM_IMAGE_INVALID with 0 leaks. */
    im_png_write_options og = { IM_PNG_WRITE_GRAY, IM_DEFLATE_BALANCED };
    im_png_write_options orgb = { IM_PNG_WRITE_RGB, IM_DEFLATE_BALANCED };
    im_png_write_options oga = { IM_PNG_WRITE_GRAY_ALPHA, IM_DEFLATE_BALANCED };
    check(im_png_encode(&img, &og, &pa, &out_p, &out_n) == IM_IMAGE_INVALID && tc.live == 0u,
          "png_encode GRAY on color+alpha -> INVALID, 0 leaks");
    check(im_png_encode(&img, &orgb, &pa, &out_p, &out_n) == IM_IMAGE_INVALID && tc.live == 0u,
          "png_encode RGB on translucent -> INVALID, 0 leaks");
    check(im_png_encode(&img, &oga, &pa, &out_p, &out_n) == IM_IMAGE_INVALID && tc.live == 0u,
          "png_encode GRAY_ALPHA on color -> INVALID, 0 leaks");
    check(im_bmp_encode(&img, IM_BMP_WRITE_24, &ba, &out_p, &out_n) == IM_IMAGE_INVALID && tc.live == 0u,
          "bmp_encode 24-bpp on translucent -> INVALID, 0 leaks");

    /* >256 colors refused by IM_PNG_WRITE_INDEXED. */
    uint8_t px300[300u * 4u];
    for (uint32_t i = 0; i < 300u; ++i) {
        px300[i * 4u + 0u] = (uint8_t)(i & 0xFFu);
        px300[i * 4u + 1u] = (uint8_t)((i >> 8) & 0xFFu);
        px300[i * 4u + 2u] = 42u;
        px300[i * 4u + 3u] = 255u;
    }
    im_image img300 = { 300u, 1u, 300u * 4u, IM_PIXEL_RGBA8, px300, sizeof(px300) };
    im_png_write_options oidx = { IM_PNG_WRITE_INDEXED, IM_DEFLATE_BALANCED };
    check(im_png_encode(&img300, &oidx, &pa, &out_p, &out_n) == IM_IMAGE_INVALID && tc.live == 0u,
          "png_encode INDEXED on 300 unique colors -> INVALID, 0 leaks");

    /* 4. Non-packed stride (> width*4) with poison gap bytes encodes identically to packed. */
    uint8_t padded[2u * 24u];
    for (size_t i = 0; i < sizeof(padded); ++i) padded[i] = 0xAAu;
    for (size_t i = 0; i < 8u; ++i) padded[i] = px4[i];
    for (size_t i = 0; i < 8u; ++i) padded[24u + i] = px4[8u + i];
    im_image img_pad = { 2, 2, 24u, IM_PIXEL_RGBA8, padded, sizeof(padded) };
    uint8_t *p1 = NULL, *p2 = NULL;
    size_t n1 = 0, n2 = 0;
    check(im_png_encode(&img, NULL, &pa, &p1, &n1) == IM_IMAGE_OK, "png_encode packed OK");
    check(im_png_encode(&img_pad, NULL, &pa, &p2, &n2) == IM_IMAGE_OK, "png_encode padded stride OK");
    bool eq = (n1 == n2);
    for (size_t i = 0; eq && i < n1; ++i) if (p1[i] != p2[i]) eq = false;
    check(eq, "png_encode non-packed stride matches packed byte-for-byte");
    if (p1) pa.free(pa.ctx, p1, n1);
    if (p2) pa.free(pa.ctx, p2, n2);
    check(tc.live == 0u && tc.mismatch_or_double_free == 0u, "png_encode exact-size free verified");

    /* 5. Exhaustive allocation-ordinal OOM sweep on im_png_encode (including growth & final exact-size shrink)
       and im_bmp_encode: every injected failure returns NO_MEMORY with 0 leaks & 0 size mismatches. */
    uint32_t png_allocs = 0;
    {
        track_init(&tc, false, 0);
        check(im_png_encode(&img300, NULL, &pa, &p1, &n1) == IM_IMAGE_OK, "OOM baseline png_encode OK");
        png_allocs = tc.total_allocs;
        if (p1) pa.free(pa.ctx, p1, n1);
        check(tc.live == 0u && tc.mismatch_or_double_free == 0u, "OOM baseline exact-size free OK");
    }
    check(png_allocs >= 6u, "png_encode performs >= 6 tracked allocations");
    for (uint32_t ord = 0; ord < png_allocs; ++ord) {
        track_init(&tc, true, ord);
        out_p = &dummy_byte;
        out_n = 123u;
        im_image_error e = im_png_encode(&img300, NULL, &pa, &out_p, &out_n);
        check(e == IM_IMAGE_NO_MEMORY && out_p == NULL && out_n == 0u &&
              tc.live == 0u && tc.mismatch_or_double_free == 0u,
              "png_encode OOM ordinal returns NO_MEMORY with 0 leaks & exact free sizes");
    }
    {
        track_init(&tc, true, 0u);
        out_p = &dummy_byte;
        out_n = 123u;
        im_image_error e = im_bmp_encode(&img, IM_BMP_WRITE_AUTO, &ba, &out_p, &out_n);
        check(e == IM_IMAGE_NO_MEMORY && out_p == NULL && out_n == 0u &&
              tc.live == 0u && tc.mismatch_or_double_free == 0u,
              "bmp_encode OOM returns NO_MEMORY with 0 leaks");
    }
}

static uint8_t g_batch_px[4u * 1024u * 1024u];

static int run_batch(void) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    uint8_t hdr[12];
    while (read_exact(hin, hdr, 12u)) {
        uint8_t op = hdr[0];
        uint8_t mode = hdr[1];
        uint8_t eff = hdr[2];
        uint8_t fmt = hdr[3];
        uint32_t w = read_u32_le(hdr + 4u);
        uint32_t h = read_u32_le(hdr + 8u);
        uint64_t bsz = (uint64_t)w * (uint64_t)h * 4ULL;
        if (bsz > sizeof(g_batch_px)) return 2;
        if (bsz > 0u && !read_exact(hin, g_batch_px, (size_t)bsz)) return 3;

        track_ctx tc;
        track_init(&tc, false, 0);
        im_image img = { w, h, w * 4u, (im_pixel_format)fmt, g_batch_px, (size_t)bsz };
        uint8_t *out_p = NULL;
        size_t out_n = 0;
        im_image_error err = IM_IMAGE_BAD_ARG;

        if (op == 0u) {
            im_png_alloc pa = { track_alloc, track_free, &tc };
            im_png_write_options opts = { (im_png_write_mode)mode, (im_deflate_effort)eff };
            err = im_png_encode(&img, &opts, &pa, &out_p, &out_n);
            if (err == IM_IMAGE_OK) {
                /* Also decode back with our own im_png_decode to prove round-trip inside the fixture. */
                im_image dec = {0};
                im_image_error de = im_png_decode(out_p, out_n, (im_pixel_format)fmt, NULL, &pa, &dec);
                bool eq = (de == IM_IMAGE_OK && dec.width == w && dec.height == h && dec.byte_size == (size_t)bsz);
                for (size_t i = 0; eq && i < (size_t)bsz; ++i) if (dec.pixels[i] != g_batch_px[i]) eq = false;
                if (dec.pixels) pa.free(pa.ctx, dec.pixels, dec.byte_size);
                if (!eq) err = IM_IMAGE_INVALID;
            }
        } else if (op == 1u) {
            im_bmp_alloc ba = { track_alloc, track_free, &tc };
            err = im_bmp_encode(&img, (im_bmp_write_mode)mode, &ba, &out_p, &out_n);
            if (err == IM_IMAGE_OK) {
                im_image dec = {0};
                im_image_error de = im_bmp_decode(out_p, out_n, (im_pixel_format)fmt, NULL, &ba, &dec);
                bool eq = (de == IM_IMAGE_OK && dec.width == w && dec.height == h && dec.byte_size == (size_t)bsz);
                for (size_t i = 0; eq && i < (size_t)bsz; ++i) if (dec.pixels[i] != g_batch_px[i]) eq = false;
                if (dec.pixels) ba.free(ba.ctx, dec.pixels, dec.byte_size);
                if (!eq) err = IM_IMAGE_INVALID;
            }
        }

        uint32_t expected_live = (out_p != NULL) ? 1u : 0u;
        uint32_t leak_flag = (tc.live != expected_live || tc.mismatch_or_double_free != 0u) ? 1u : 0u;

        uint8_t resp[12];
        write_u32_le(resp, (uint32_t)err);
        write_u32_le(resp + 4u, leak_flag);
        write_u32_le(resp + 8u, (uint32_t)out_n);
        if (!write_exact(hout, resp, 12u)) return 4;
        if (out_p && out_n > 0u && !write_exact(hout, out_p, out_n)) return 5;
        if (out_p) track_free(&tc, out_p, out_n);
        if (tc.live != 0u || tc.mismatch_or_double_free != 0u) return 6;
    }
    return 0;
}

static bool arg_contains(const wchar_t *cmd, const wchar_t *needle) {
    if (!cmd || !needle) return false;
    for (size_t i = 0; cmd[i]; ++i) {
        size_t j = 0;
        while (needle[j] && cmd[i + j] == needle[j]) ++j;
        if (!needle[j]) return true;
    }
    return false;
}

int im_main(void) {
    if (arg_contains(GetCommandLineW(), L"--batch")) {
        return run_batch();
    }
    test_boundaries_and_oom();
    if (g_fails == 0) {
        write_str("write_tests: ALL PASSED\n");
        return 0;
    }
    write_str("write_tests: FAILED\n");
    return 1;
}
