/* Impossible SDK: tests/fixtures/ico_tests.c - ICO and CUR reader/writer boundary, 40-malformed, OOM, and batch bridge. */
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ico.h"

static void write_str(const char *s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    while (s[n]) ++n;
    DWORD w = 0;
    WriteFile(h, s, n, &w, NULL);
}

static bool read_exact(HANDLE h, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        DWORD chunk = (n - got > 0x40000000u) ? 0x40000000u : (DWORD)(n - got);
        DWORD r = 0;
        if (!ReadFile(h, buf + got, chunk, &r, NULL) || r == 0) return false;
        got += (size_t)r;
    }
    return true;
}

static bool write_exact(HANDLE h, const uint8_t *buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        DWORD chunk = (n - sent > 0x40000000u) ? 0x40000000u : (DWORD)(n - sent);
        DWORD w = 0;
        if (!WriteFile(h, buf + sent, chunk, &w, NULL) || w == 0) return false;
        sent += (size_t)w;
    }
    return true;
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* Exact (ptr, size) tracking allocator with 0xA5 poison and ordinal OOM injection. */
#define MAX_TRACKED 2048u
typedef struct track_entry {
    void *ptr;
    size_t size;
} track_entry;

typedef struct track_ctx {
    uint32_t total_allocs;
    uint32_t live;
    uint32_t mismatch_or_double_free;
    bool fail_enabled;
    uint32_t fail_ordinal;
    track_entry slots[MAX_TRACKED];
} track_ctx;

static void track_init(track_ctx *tc, bool fail_en, uint32_t fail_ord) {
    tc->total_allocs = 0u;
    tc->live = 0u;
    tc->mismatch_or_double_free = 0u;
    tc->fail_enabled = fail_en;
    tc->fail_ordinal = fail_ord;
    for (uint32_t i = 0; i < MAX_TRACKED; ++i) {
        tc->slots[i].ptr = NULL;
        tc->slots[i].size = 0u;
    }
}

static void *track_alloc(void *ctx, size_t size) {
    track_ctx *tc = (track_ctx *)ctx;
    uint32_t ord = tc->total_allocs++;
    if (tc->fail_enabled && ord == tc->fail_ordinal) return NULL;
    void *p = HeapAlloc(GetProcessHeap(), 0, size ? size : 1u);
    if (!p) return NULL;
    uint8_t *bp = (uint8_t *)p;
    for (size_t i = 0; i < size; ++i) bp[i] = 0xA5u;
    bool stored = false;
    for (uint32_t i = 0; i < MAX_TRACKED; ++i) {
        if (!tc->slots[i].ptr) {
            tc->slots[i].ptr = p;
            tc->slots[i].size = size;
            stored = true;
            break;
        }
    }
    if (!stored) tc->mismatch_or_double_free++;
    tc->live++;
    return p;
}

static void track_free(void *ctx, void *ptr, size_t size) {
    track_ctx *tc = (track_ctx *)ctx;
    if (!ptr) {
        tc->mismatch_or_double_free++;
        return;
    }
    bool found = false;
    for (uint32_t i = 0; i < MAX_TRACKED; ++i) {
        if (tc->slots[i].ptr == ptr) {
            if (tc->slots[i].size != size) tc->mismatch_or_double_free++;
            tc->slots[i].ptr = NULL;
            tc->slots[i].size = 0u;
            found = true;
            break;
        }
    }
    if (!found) tc->mismatch_or_double_free++;
    if (tc->live > 0u) tc->live--;
    HeapFree(GetProcessHeap(), 0, ptr);
}

static int g_fails = 0;
static void check(bool ok, const char *msg) {
    if (!ok) {
        write_str("FAIL: ");
        write_str(msg);
        write_str("\n");
        g_fails++;
    }
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
}

/* Build a minimal valid 4x4 24-bpp DIB payload (40 + 48 XOR + 16 AND = 104 bytes). */
static size_t build_dib24_4x4(uint8_t *dst) {
    for (size_t i = 0; i < 104u; ++i) dst[i] = 0u;
    wr32(dst + 0u, 40u);
    wr32(dst + 4u, 4u);
    wr32(dst + 8u, 8u);
    wr16(dst + 12u, 1u);
    wr16(dst + 14u, 24u);
    wr32(dst + 16u, 0u);
    wr32(dst + 20u, 64u);
    dst[40u] = 10u; dst[41u] = 20u; dst[42u] = 30u;
    /* Leave pixel (3,3) transparent via AND bit */
    dst[40u + 48u] = 0x10u;
    return 104u;
}

/* Build a minimal valid 4x4 32-bpp DIB payload (40 + 64 XOR + 16 AND = 120 bytes). */
static size_t build_dib32_4x4(uint8_t *dst, uint8_t alpha_val, bool include_and) {
    size_t len = include_and ? 120u : 104u;
    for (size_t i = 0; i < len; ++i) dst[i] = 0u;
    wr32(dst + 0u, 40u);
    wr32(dst + 4u, 4u);
    wr32(dst + 8u, 8u);
    wr16(dst + 12u, 1u);
    wr16(dst + 14u, 32u);
    wr32(dst + 16u, 0u);
    wr32(dst + 20u, (uint32_t)(len - 40u));
    dst[40u] = 40u; dst[41u] = 80u; dst[42u] = 120u; dst[43u] = alpha_val;
    return len;
}

/* Build a minimal valid 4x4 4-bpp DIB payload (40 + clr*4 + 16 XOR + 16 AND). */
static size_t build_dib4_4x4(uint8_t *dst, uint32_t clr_used, uint8_t px_nibble) {
    size_t pal_n = clr_used ? clr_used : 16u;
    size_t total = 40u + pal_n * 4u + 16u + 16u;
    for (size_t i = 0; i < total; ++i) dst[i] = 0u;
    wr32(dst + 0u, 40u);
    wr32(dst + 4u, 4u);
    wr32(dst + 8u, 8u);
    wr16(dst + 12u, 1u);
    wr16(dst + 14u, 4u);
    wr32(dst + 32u, clr_used);
    dst[40u + pal_n * 4u] = (uint8_t)((px_nibble << 4) | px_nibble);
    return total;
}

static size_t wrap_single_ico(uint8_t *out, uint16_t type, uint8_t w, uint8_t h,
                              uint16_t f4, uint16_t f6, const uint8_t *payload, size_t plen) {
    wr16(out + 0u, 0u);
    wr16(out + 2u, type);
    wr16(out + 4u, 1u);
    out[6u] = w;
    out[7u] = h;
    out[8u] = 0u;
    out[9u] = 0u;
    wr16(out + 10u, f4);
    wr16(out + 12u, f6);
    wr32(out + 14u, (uint32_t)plen);
    wr32(out + 18u, 22u);
    copy_bytes(out + 22u, payload, plen);
    return 22u + plen;
}

static void expect_bad(const uint8_t *buf, size_t len, im_image_error want_err, const char *label) {
    track_ctx tc;
    track_init(&tc, false, 0u);
    im_ico_alloc ia = { track_alloc, track_free, &tc };
    im_image img = { 1u, 1u, 4u, IM_PIXEL_RGBA8, (uint8_t *)1, 4u };
    uint8_t *inv = (uint8_t *)1;
    size_t inv_len = 77u;
    im_image_error err = im_ico_decode_entry(buf, len, 0u, IM_PIXEL_BGRA8, NULL, &ia, &img, &inv, &inv_len, NULL);
    check(err == want_err && img.pixels == NULL && img.width == 0u && inv == NULL && inv_len == 0u &&
          tc.live == 0u && tc.mismatch_or_double_free == 0u, label);
}

static void test_all_self(void) {
    track_ctx tc;
    im_ico_alloc ia = { track_alloc, track_free, &tc };

    /* 1. Valid 2-entry adjacent and out-of-order controls + im_ico_list cap=0, cap=1, cap=2 with guard sentinel. */
    uint8_t dib_a[128], dib_b[128];
    size_t len_a = build_dib24_4x4(dib_a);
    size_t len_b = build_dib32_4x4(dib_b, 200u, true);
    uint8_t two_adj[512];
    wr16(two_adj + 0u, 0u); wr16(two_adj + 2u, 1u); wr16(two_adj + 4u, 2u);
    /* Entry 0 at offset 38 (len_a), Entry 1 at offset 38 + len_a (len_b) */
    two_adj[6u] = 4u; two_adj[7u] = 4u; two_adj[8u] = 0u; two_adj[9u] = 0u;
    wr16(two_adj + 10u, 1u); wr16(two_adj + 12u, 24u); wr32(two_adj + 14u, (uint32_t)len_a); wr32(two_adj + 18u, 38u);
    two_adj[22u] = 4u; two_adj[23u] = 4u; two_adj[24u] = 0u; two_adj[25u] = 0u;
    wr16(two_adj + 26u, 1u); wr16(two_adj + 28u, 32u); wr32(two_adj + 30u, (uint32_t)len_b); wr32(two_adj + 34u, (uint32_t)(38u + len_a));
    copy_bytes(two_adj + 38u, dib_a, len_a);
    copy_bytes(two_adj + 38u + len_a, dib_b, len_b);
    size_t two_adj_len = 38u + len_a + len_b;

    /* Valid out-of-order control: entry 0 points to second payload, entry 1 points to first payload. */
    uint8_t two_ooo[512];
    copy_bytes(two_ooo, two_adj, two_adj_len);
    wr32(two_ooo + 14u, (uint32_t)len_b); wr32(two_ooo + 18u, (uint32_t)(38u + len_a));
    wr32(two_ooo + 30u, (uint32_t)len_a); wr32(two_ooo + 34u, 38u);
    {
        im_ico_type t = 0; uint16_t cnt = 0;
        check(im_ico_probe(two_adj, two_adj_len, &t, &cnt) == IM_IMAGE_OK && t == IM_ICO_TYPE_ICON && cnt == 2u,
              "valid adjacent 2-entry probe OK");
        check(im_ico_probe(two_ooo, two_adj_len, &t, &cnt) == IM_IMAGE_OK && cnt == 2u,
              "valid out-of-order 2-entry probe OK");

        /* im_ico_list short-buffer contract with guard sentinel */
        im_ico_entry_info buf[3];
        buf[0].width = 0xDEADu; buf[1].width = 0xBEEFu; buf[2].width = 0xCAFEu;
        check(im_ico_list(two_adj, two_adj_len, &t, NULL, 0u, &cnt) == IM_IMAGE_TOO_LARGE && cnt == 2u,
              "im_ico_list cap=0 NULL returns TOO_LARGE with count=2");
        check(im_ico_list(two_adj, two_adj_len, &t, buf, 1u, &cnt) == IM_IMAGE_TOO_LARGE && cnt == 2u &&
              buf[0].width == 0xDEADu && buf[1].width == 0xBEEFu,
              "im_ico_list cap=count-1 writes 0 entries and returns TOO_LARGE");
        check(im_ico_list(two_adj, two_adj_len, &t, buf, 2u, &cnt) == IM_IMAGE_OK && cnt == 2u &&
              buf[0].width == 4u && buf[1].width == 4u && buf[2].width == 0xCAFEu,
              "im_ico_list cap=count fills exact count and preserves guard sentinel");
        check(im_ico_list(two_adj, two_adj_len, &t, NULL, 2u, &cnt) == IM_IMAGE_BAD_ARG,
              "im_ico_list NULL entries with cap>0 returns BAD_ARG");
    }

    /* 2. Build a valid 4x4 PNG entry via im_ico_encode for control and malformed tests. */
    uint8_t px4[4u * 4u * 4u];
    for (size_t i = 0; i < sizeof(px4); i += 4u) {
        px4[i] = 10u; px4[i + 1u] = 20u; px4[i + 2u] = 30u; px4[i + 3u] = 200u;
    }
    im_image img4 = { 4u, 4u, 16u, IM_PIXEL_RGBA8, px4, sizeof(px4) };
    uint8_t *png_ico = NULL;
    size_t png_ico_len = 0u;
    {
        track_init(&tc, false, 0u);
        im_ico_write_entry we = { &img4, NULL, 0u, 0u, 0u, IM_ICO_ENTRY_PNG };
        check(im_ico_encode(IM_ICO_TYPE_ICON, &we, 1u, &ia, &png_ico, &png_ico_len) == IM_IMAGE_OK && png_ico_len > 30u,
              "valid 4x4 PNG ICO control encode OK");
    }

    /* 3. 40 planted malformed ICO/CUR files (each built from a valid control). */
    uint8_t tmp[512];
    size_t base_len = wrap_single_ico(tmp, 1u, 4u, 4u, 1u, 24u, dib_a, len_a);
    /* Control passes first */
    {
        track_init(&tc, false, 0u);
        im_image img = {0};
        check(im_ico_decode_entry(tmp, base_len, 0u, IM_PIXEL_BGRA8, NULL, &ia, &img, NULL, NULL, NULL) == IM_IMAGE_OK,
              "malformed test control passes");
        if (img.pixels) ia.free(ia.ctx, img.pixels, img.byte_size);
    }

    /* #1..#7: ICONDIR header failures */
    expect_bad(tmp, 5u, IM_IMAGE_TRUNCATED, "M01: truncated < 6 bytes");
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr16(m, 1u); expect_bad(m, base_len, IM_IMAGE_INVALID, "M02: reserved != 0"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr16(m + 2u, 0u); expect_bad(m, base_len, IM_IMAGE_INVALID, "M03: type = 0"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr16(m + 2u, 3u); expect_bad(m, base_len, IM_IMAGE_INVALID, "M04: type = 3"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr16(m + 4u, 0u); expect_bad(m, base_len, IM_IMAGE_INVALID, "M05: count = 0"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr16(m + 4u, 257u); expect_bad(m, base_len, IM_IMAGE_TOO_LARGE, "M06: count = 257 > IM_ICO_MAX_ENTRIES"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr16(m + 4u, 2u); expect_bad(m, 30u, IM_IMAGE_TRUNCATED, "M07: truncated directory table"); }

    /* #8..#13: Single-entry offset/size bounds failures */
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr32(m + 14u, 0u); expect_bad(m, base_len, IM_IMAGE_INVALID, "M08: bytes_in_res = 0"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr32(m + 18u, 0u); expect_bad(m, base_len, IM_IMAGE_INVALID, "M09: offset = 0 inside ICONDIR"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr32(m + 18u, 10u); expect_bad(m, base_len, IM_IMAGE_INVALID, "M10: offset = 10 inside ICONDIRENTRY"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr32(m + 18u, (uint32_t)base_len); expect_bad(m, base_len, IM_IMAGE_TRUNCATED, "M11: offset at EOF"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr32(m + 14u, (uint32_t)(len_a + 1u)); expect_bad(m, base_len, IM_IMAGE_TRUNCATED, "M12: offset+size past EOF by 1"); }
    { uint8_t m[512]; copy_bytes(m, tmp, base_len); wr32(m + 18u, 0xFFFFFFF0u); wr32(m + 14u, 0x20u); expect_bad(m, base_len, IM_IMAGE_TRUNCATED, "M13: offset+size 32-bit wrap"); }

    /* #14..#20: Multi-entry overlap & cross-entry bleed failures */
    { uint8_t m[512]; copy_bytes(m, two_adj, two_adj_len); wr32(m + 34u, 38u); wr32(m + 30u, (uint32_t)len_a); expect_bad(m, two_adj_len, IM_IMAGE_INVALID, "M14: duplicate spans"); }
    { uint8_t m[512]; copy_bytes(m, two_adj, two_adj_len); wr32(m + 34u, (uint32_t)(38u + len_a - 1u)); expect_bad(m, two_adj_len, IM_IMAGE_INVALID, "M15: partial overlap by 1 byte"); }
    { uint8_t m[512]; copy_bytes(m, two_ooo, two_adj_len); wr32(m + 18u, (uint32_t)(38u + len_a - 4u)); expect_bad(m, two_adj_len, IM_IMAGE_INVALID, "M16: out-of-order overlap"); }
    { uint8_t m[512]; copy_bytes(m, two_adj, two_adj_len); wr32(m + 34u, 48u); wr32(m + 30u, 40u); expect_bad(m, two_adj_len, IM_IMAGE_INVALID, "M17: nested span inside entry 0"); }
    { uint8_t m[512]; copy_bytes(m, two_ooo, two_adj_len); wr32(m + 18u, 48u); wr32(m + 14u, 40u); expect_bad(m, two_adj_len, IM_IMAGE_INVALID, "M18: nested span reversed"); }
    {
        /* M19: 3 entries where 0 and 2 overlap while 1 is disjoint */
        uint8_t m[512];
        for (size_t i = 0; i < sizeof(m); ++i) m[i] = 0u;
        wr16(m + 0u, 0u); wr16(m + 2u, 1u); wr16(m + 4u, 3u);
        wr32(m + 14u, 104u); wr32(m + 18u, 54u);
        wr32(m + 30u, 104u); wr32(m + 34u, 200u);
        wr32(m + 46u, 50u);  wr32(m + 50u, 150u); /* overlaps entry 0 [54..158) */
        expect_bad(m, 350u, IM_IMAGE_INVALID, "M19: 3-entry non-adjacent overlap");
    }
    {
        /* M20: adjacent entry 0 shortened by 1 byte right before entry 1 -> must NOT bleed into entry 1! */
        uint8_t m[512]; copy_bytes(m, two_adj, two_adj_len);
        wr32(m + 14u, (uint32_t)(len_a - 1u));
        expect_bad(m, two_adj_len, IM_IMAGE_TRUNCATED, "M20: adjacent entry 0 short by 1 byte refuses bleed into entry 1");
    }

    /* #21..#39: DIB header, palette, and plane failures */
    { uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, dib_a, 39u); expect_bad(m, l, IM_IMAGE_TRUNCATED, "M21: DIB < 40 bytes"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d, 12u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M22: DIB biSize=12"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d, 44u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M23: DIB biSize=44"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d, 108u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, 80u); expect_bad(m, l, IM_IMAGE_TRUNCATED, "M24: DIB V4 truncated header"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d + 4u, 0u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M25: DIB biWidth=0"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d + 4u, (uint32_t)-4); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M26: DIB biWidth=-4"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d + 8u, 0u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M27: DIB biHeight=0"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d + 8u, 1u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M28: DIB biHeight=1"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d + 8u, 7u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M29: DIB biHeight=7 odd"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d + 8u, (uint32_t)-8); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M30: DIB biHeight=-8"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr16(d + 12u, 2u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M31: DIB biPlanes=2"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr16(d + 14u, 2u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 2u, d, len_a); expect_bad(m, l, IM_IMAGE_INVALID, "M32: DIB biBitCount=2"); }
    { uint8_t d[128]; copy_bytes(d, dib_a, len_a); wr32(d + 16u, 1u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, d, len_a); expect_bad(m, l, IM_IMAGE_UNSUPPORTED, "M33: DIB biCompression=BI_RLE8"); }
    { uint8_t d[256]; size_t dl = build_dib4_4x4(d, 16u, 0u); wr32(d + 32u, 17u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 4u, d, dl); expect_bad(m, l, IM_IMAGE_INVALID, "M34: DIB 4-bpp biClrUsed=17"); }
    { uint8_t d[256]; size_t dl = build_dib4_4x4(d, 2u, 3u); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 4u, d, dl); expect_bad(m, l, IM_IMAGE_INVALID, "M35: DIB 4-bpp palette index out of range"); }
    { uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, dib_a, 40u + 47u); expect_bad(m, l, IM_IMAGE_TRUNCATED, "M36: DIB 24-bpp truncated XOR"); }
    { uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 24u, dib_a, 40u + 48u); expect_bad(m, l, IM_IMAGE_TRUNCATED, "M37: DIB 24-bpp missing AND mask"); }
    { uint8_t d[128]; size_t dl = build_dib32_4x4(d, 0u, false); uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 32u, d, dl); expect_bad(m, l, IM_IMAGE_TRUNCATED, "M38: DIB 32-bpp all-zero alpha missing AND mask"); }
    { uint8_t m[512]; size_t l = wrap_single_ico(m, 1u, 4u, 4u, 1u, 32u, dib_b, len_b - 1u); expect_bad(m, l, IM_IMAGE_TRUNCATED, "M39: DIB 32-bpp partial AND mask"); }
    {
        /* M40: Corrupt embedded PNG (truncated payload) */
        uint8_t m[512];
        size_t clen = png_ico_len < 512u ? png_ico_len : 512u;
        copy_bytes(m, png_ico, clen);
        uint32_t old_sz = rd32(m + 14u);
        wr32(m + 14u, old_sz - 6u);
        expect_bad(m, clen - 6u, IM_IMAGE_TRUNCATED, "M40: corrupt truncated embedded PNG");
    }

    /* 4. CUR hotspot read/write boundary tests for both DIB and PNG: (0,0), (3,3) OK; (4,0), (0,4) refused. */
    {
        uint8_t m[512];
        size_t l = wrap_single_ico(m, 2u, 4u, 4u, 0u, 0u, dib_a, len_a);
        track_init(&tc, false, 0u);
        im_image img = {0}; im_ico_entry_info info;
        check(im_ico_decode_entry(m, l, 0u, IM_PIXEL_BGRA8, NULL, &ia, &img, NULL, NULL, &info) == IM_IMAGE_OK &&
              info.hotspot_x == 0u && info.hotspot_y == 0u, "CUR DIB hotspot (0,0) accepted");
        if (img.pixels) ia.free(ia.ctx, img.pixels, img.byte_size);

        wr16(m + 10u, 3u); wr16(m + 12u, 3u);
        check(im_ico_decode_entry(m, l, 0u, IM_PIXEL_BGRA8, NULL, &ia, &img, NULL, NULL, &info) == IM_IMAGE_OK &&
              info.hotspot_x == 3u && info.hotspot_y == 3u, "CUR DIB hotspot (w-1,h-1) accepted");
        if (img.pixels) ia.free(ia.ctx, img.pixels, img.byte_size);

        wr16(m + 10u, 4u); wr16(m + 12u, 0u);
        expect_bad(m, l, IM_IMAGE_INVALID, "CUR DIB hotspot (w,0) refused");
        wr16(m + 10u, 0u); wr16(m + 12u, 4u);
        expect_bad(m, l, IM_IMAGE_INVALID, "CUR DIB hotspot (0,h) refused");

        /* PNG CUR hotspot bounds */
        uint8_t pm[512];
        copy_bytes(pm, png_ico, png_ico_len);
        wr16(pm + 2u, 2u);
        wr16(pm + 10u, 3u); wr16(pm + 12u, 3u);
        check(im_ico_decode_entry(pm, png_ico_len, 0u, IM_PIXEL_BGRA8, NULL, &ia, &img, NULL, NULL, &info) == IM_IMAGE_OK &&
              info.hotspot_x == 3u && info.hotspot_y == 3u, "CUR PNG hotspot (w-1,h-1) accepted");
        if (img.pixels) ia.free(ia.ctx, img.pixels, img.byte_size);
        wr16(pm + 10u, 4u); wr16(pm + 12u, 0u);
        expect_bad(pm, png_ico_len, IM_IMAGE_INVALID, "CUR PNG hotspot (w,0) refused");
        wr16(pm + 10u, 0u); wr16(pm + 12u, 4u);
        expect_bad(pm, png_ico_len, IM_IMAGE_INVALID, "CUR PNG hotspot (0,h) refused");
    }

    /* 5. Writer validation: unknown encoding, PNG with invert_mask, 256-entry cap vs 257, all-transparent colored source. */
    {
        uint8_t *out_p = NULL; size_t out_n = 0u;
        track_init(&tc, false, 0u);
        im_ico_write_entry bad_enc = { &img4, NULL, 0u, 0u, 0u, (im_ico_encoding)99 };
        check(im_ico_encode(IM_ICO_TYPE_ICON, &bad_enc, 1u, &ia, &out_p, &out_n) == IM_IMAGE_BAD_ARG,
              "im_ico_encode refuses unknown encoding");

        uint8_t inv1[4] = { 0x80u, 0u, 0u, 0u };
        im_ico_write_entry png_inv = { &img4, inv1, sizeof(inv1), 0u, 0u, IM_ICO_ENTRY_PNG };
        check(im_ico_encode(IM_ICO_TYPE_ICON, &png_inv, 1u, &ia, &out_p, &out_n) == IM_IMAGE_UNSUPPORTED &&
              tc.live == 0u, "im_ico_encode refuses PNG with invert_mask");

        /* All-transparent colored source (RGBA 255,0,0,0) -> XOR RGB must be zeroed so has_invert_mask == false */
        uint8_t trans_px[4u * 4u * 4u];
        for (size_t i = 0; i < sizeof(trans_px); i += 4u) {
            trans_px[i] = 255u; trans_px[i + 1u] = 128u; trans_px[i + 2u] = 64u; trans_px[i + 3u] = 0u;
        }
        im_image img_trans = { 4u, 4u, 16u, IM_PIXEL_RGBA8, trans_px, sizeof(trans_px) };
        im_ico_write_entry we_trans = { &img_trans, NULL, 0u, 0u, 0u, IM_ICO_ENTRY_DIB };
        check(im_ico_encode(IM_ICO_TYPE_ICON, &we_trans, 1u, &ia, &out_p, &out_n) == IM_IMAGE_OK,
              "im_ico_encode all-transparent colored source OK");
        im_image dec = {0}; uint8_t *d_inv = NULL; size_t d_inv_len = 0u; im_ico_entry_info d_info;
        check(im_ico_decode_entry(out_p, out_n, 0u, IM_PIXEL_RGBA8, NULL, &ia, &dec, &d_inv, &d_inv_len, &d_info) == IM_IMAGE_OK &&
              !d_info.has_invert_mask && d_inv == NULL && dec.pixels[0] == 0u && dec.pixels[3] == 0u,
              "all-transparent colored source produces zeroed XOR RGB and no invert mask");
        if (dec.pixels) ia.free(ia.ctx, dec.pixels, dec.byte_size);
        if (out_p) ia.free(ia.ctx, out_p, out_n);
        check(tc.live == 0u && tc.mismatch_or_double_free == 0u, "all-transparent check 0 leaks");

        /* Exactly IM_ICO_MAX_ENTRIES (256) accepted in both writer and reader; 257 refused */
        static im_ico_write_entry we257[257];
        for (uint32_t i = 0; i < 257u; ++i) {
            we257[i].image = &img4;
            we257[i].invert_mask = NULL;
            we257[i].invert_mask_size = 0u;
            we257[i].hotspot_x = 0u;
            we257[i].hotspot_y = 0u;
            we257[i].encoding = IM_ICO_ENTRY_DIB;
        }
        check(im_ico_encode(IM_ICO_TYPE_ICON, we257, 257u, &ia, &out_p, &out_n) == IM_IMAGE_TOO_LARGE,
              "im_ico_encode refuses 257 entries");
        check(im_ico_encode(IM_ICO_TYPE_ICON, we257, 256u, &ia, &out_p, &out_n) == IM_IMAGE_OK,
              "im_ico_encode accepts 256 entries");
        im_ico_type t256 = 0; uint16_t c256 = 0;
        check(im_ico_probe(out_p, out_n, &t256, &c256) == IM_IMAGE_OK && c256 == 256u,
              "im_ico_probe accepts 256 entries");
        if (out_p) ia.free(ia.ctx, out_p, out_n);
        check(tc.live == 0u && tc.mismatch_or_double_free == 0u, "256-entry test 0 leaks");
    }

    /* 6. Exhaustive OOM allocation-ordinal sweeps: DIB+invert decode, PNG decode, mixed multi-entry encode. */
    {
        /* 6a. DIB with invert pixel (AND=1, XOR!=0) decode OOM sweep */
        uint8_t d_inv_dib[128];
        size_t dl = build_dib24_4x4(d_inv_dib);
        d_inv_dib[40u + 48u] = 0x80u; /* pixel (0,3) has XOR=(10,20,30) and AND=1 -> invert! */
        uint8_t inv_ico[256];
        size_t inv_ico_len = wrap_single_ico(inv_ico, 1u, 4u, 4u, 1u, 24u, d_inv_dib, dl);
        track_init(&tc, false, 0u);
        im_image img = {0}; uint8_t *inv = NULL; size_t inv_l = 0u; im_ico_entry_info info;
        check(im_ico_decode_entry(inv_ico, inv_ico_len, 0u, IM_PIXEL_BGRA8, NULL, &ia, &img, &inv, &inv_l, &info) == IM_IMAGE_OK &&
              info.has_invert_mask && inv != NULL, "DIB invert decode OOM baseline OK");
        uint32_t allocs = tc.total_allocs;
        if (img.pixels) ia.free(ia.ctx, img.pixels, img.byte_size);
        if (inv) ia.free(ia.ctx, inv, inv_l);
        check(allocs == 2u && tc.live == 0u && tc.mismatch_or_double_free == 0u, "DIB invert baseline 2 allocs");
        for (uint32_t ord = 0; ord < allocs; ++ord) {
            track_init(&tc, true, ord);
            im_image_error e = im_ico_decode_entry(inv_ico, inv_ico_len, 0u, IM_PIXEL_BGRA8, NULL, &ia, &img, &inv, &inv_l, NULL);
            check(e == IM_IMAGE_NO_MEMORY && img.pixels == NULL && inv == NULL && inv_l == 0u &&
                  tc.live == 0u && tc.mismatch_or_double_free == 0u, "DIB invert decode OOM ordinal clean");
        }

        /* 6b. Embedded PNG decode OOM sweep */
        track_init(&tc, false, 0u);
        check(im_ico_decode_entry(png_ico, png_ico_len, 0u, IM_PIXEL_BGRA8, NULL, &ia, &img, &inv, &inv_l, NULL) == IM_IMAGE_OK,
              "PNG entry decode OOM baseline OK");
        allocs = tc.total_allocs;
        if (img.pixels) ia.free(ia.ctx, img.pixels, img.byte_size);
        for (uint32_t ord = 0; ord < allocs; ++ord) {
            track_init(&tc, true, ord);
            im_image_error e = im_ico_decode_entry(png_ico, png_ico_len, 0u, IM_PIXEL_BGRA8, NULL, &ia, &img, &inv, &inv_l, NULL);
            check(e == IM_IMAGE_NO_MEMORY && img.pixels == NULL && tc.live == 0u && tc.mismatch_or_double_free == 0u,
                  "PNG entry decode OOM ordinal clean");
        }

        /* 6c. Mixed multi-entry encode (DIB + PNG) OOM sweep + succeeding run after last ordinal */
        im_ico_write_entry mix[2] = {
            { &img4, NULL, 0u, 0u, 0u, IM_ICO_ENTRY_DIB },
            { &img4, NULL, 0u, 0u, 0u, IM_ICO_ENTRY_PNG }
        };
        uint8_t *out_p = NULL; size_t out_n = 0u;
        track_init(&tc, false, 0u);
        check(im_ico_encode(IM_ICO_TYPE_ICON, mix, 2u, &ia, &out_p, &out_n) == IM_IMAGE_OK, "mixed encode OOM baseline OK");
        allocs = tc.total_allocs;
        if (out_p) ia.free(ia.ctx, out_p, out_n);
        check(allocs >= 6u && tc.live == 0u, "mixed encode performs >= 6 tracked allocs");
        for (uint32_t ord = 0; ord < allocs; ++ord) {
            track_init(&tc, true, ord);
            out_p = (uint8_t *)1; out_n = 99u;
            im_image_error e = im_ico_encode(IM_ICO_TYPE_ICON, mix, 2u, &ia, &out_p, &out_n);
            check(e == IM_IMAGE_NO_MEMORY && out_p == NULL && out_n == 0u &&
                  tc.live == 0u && tc.mismatch_or_double_free == 0u, "mixed encode OOM ordinal clean");
        }
        track_init(&tc, true, allocs);
        check(im_ico_encode(IM_ICO_TYPE_ICON, mix, 2u, &ia, &out_p, &out_n) == IM_IMAGE_OK,
              "mixed encode succeeds at ordinal == total_allocs");
        if (out_p) ia.free(ia.ctx, out_p, out_n);
        check(tc.live == 0u && tc.mismatch_or_double_free == 0u, "mixed encode post-OOM 0 leaks");
    }

    if (png_ico) {
        track_init(&tc, false, 0u);
        HeapFree(GetProcessHeap(), 0, png_ico);
    }
}

static uint8_t g_in_buf[4u * 1024u * 1024u];
static uint8_t g_comp_buf[256u * 256u * 4u];

static int run_batch(void) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    uint8_t op = 0u;
    while (read_exact(hin, &op, 1u)) {
        if (op == 0u) {
            /* List: <u32 len><bytes> */
            uint8_t lb[4];
            if (!read_exact(hin, lb, 4u)) return 2;
            uint32_t n = rd32(lb);
            if (n > sizeof(g_in_buf) || (n > 0u && !read_exact(hin, g_in_buf, n))) return 3;
            im_ico_entry_info entries[IM_ICO_MAX_ENTRIES];
            im_ico_type type = IM_ICO_TYPE_ICON;
            uint16_t cnt = 0u;
            im_image_error err = im_ico_list(g_in_buf, n, &type, entries, IM_ICO_MAX_ENTRIES, &cnt);
            uint8_t rh[8];
            wr32(rh, (uint32_t)err);
            wr16(rh + 4u, (uint16_t)type);
            wr16(rh + 6u, (err == IM_IMAGE_OK) ? cnt : 0u);
            if (!write_exact(hout, rh, 8u)) return 4;
            if (err == IM_IMAGE_OK) {
                for (uint16_t i = 0; i < cnt; ++i) {
                    uint8_t er[28];
                    wr32(er + 0u, entries[i].dir_width);
                    wr32(er + 4u, entries[i].dir_height);
                    wr16(er + 8u, entries[i].dir_color_count);
                    wr16(er + 10u, entries[i].dir_planes);
                    wr16(er + 12u, entries[i].dir_bit_count);
                    wr32(er + 14u, entries[i].width);
                    wr32(er + 18u, entries[i].height);
                    wr16(er + 22u, entries[i].bit_count);
                    wr16(er + 24u, entries[i].hotspot_x);
                    wr16(er + 26u, entries[i].hotspot_y);
                    if (!write_exact(hout, er, 28u)) return 5;
                }
            }
        } else if (op == 1u) {
            /* Decode entry: <u16 idx><u8 fmt><u32 len><bytes> */
            uint8_t hdr[7];
            if (!read_exact(hin, hdr, 7u)) return 2;
            uint16_t idx = rd16(hdr);
            uint8_t fmt = hdr[2];
            uint32_t n = rd32(hdr + 3u);
            if (n > sizeof(g_in_buf) || (n > 0u && !read_exact(hin, g_in_buf, n))) return 3;
            track_ctx tc; track_init(&tc, false, 0u);
            im_ico_alloc ia = { track_alloc, track_free, &tc };
            im_image img = {0}; uint8_t *inv = NULL; size_t inv_l = 0u; im_ico_entry_info info = {0};
            im_image_error err = im_ico_decode_entry(g_in_buf, n, idx, (im_pixel_format)fmt, NULL, &ia,
                                                     &img, &inv, &inv_l, &info);
            uint8_t rh[32];
            wr32(rh + 0u, (uint32_t)err);
            wr32(rh + 4u, img.width);
            wr32(rh + 8u, img.height);
            wr16(rh + 12u, info.hotspot_x);
            wr16(rh + 14u, info.hotspot_y);
            wr32(rh + 16u, info.has_invert_mask ? 1u : 0u);
            wr32(rh + 20u, (uint32_t)img.byte_size);
            wr32(rh + 24u, (uint32_t)inv_l);
            wr32(rh + 28u, tc.mismatch_or_double_free);
            if (!write_exact(hout, rh, 32u)) return 4;
            if (img.pixels && img.byte_size > 0u && !write_exact(hout, img.pixels, img.byte_size)) return 5;
            if (inv && inv_l > 0u && !write_exact(hout, inv, inv_l)) return 6;
            if (img.pixels) ia.free(ia.ctx, img.pixels, img.byte_size);
            if (inv) ia.free(ia.ctx, inv, inv_l);
            if (tc.live != 0u || tc.mismatch_or_double_free != 0u) return 7;
        } else if (op == 2u) {
            /* Composite entry onto solid RGB background: <u16 idx><u8 r><u8 g><u8 b><u32 len><bytes> */
            uint8_t hdr[9];
            if (!read_exact(hin, hdr, 9u)) return 2;
            uint16_t idx = rd16(hdr);
            uint8_t bgr = hdr[2], bgg = hdr[3], bgb = hdr[4];
            uint32_t n = rd32(hdr + 5u);
            if (n > sizeof(g_in_buf) || (n > 0u && !read_exact(hin, g_in_buf, n))) return 3;
            track_ctx tc; track_init(&tc, false, 0u);
            im_ico_alloc ia = { track_alloc, track_free, &tc };
            im_image img = {0}; uint8_t *inv = NULL; size_t inv_l = 0u; im_ico_entry_info info = {0};
            im_image_error err = im_ico_decode_entry(g_in_buf, n, idx, IM_PIXEL_BGRA8, NULL, &ia,
                                                     &img, &inv, &inv_l, &info);
            uint32_t out_bytes = 0u;
            if (err == IM_IMAGE_OK && img.width <= 256u && img.height <= 256u) {
                out_bytes = img.width * img.height * 4u;
                for (uint32_t i = 0; i < out_bytes; i += 4u) {
                    g_comp_buf[i + 0u] = bgb;
                    g_comp_buf[i + 1u] = bgg;
                    g_comp_buf[i + 2u] = bgr;
                    g_comp_buf[i + 3u] = 255u;
                }
                im_ico_composite_onto(g_comp_buf, img.width * 4u, img.width, img.height,
                                      IM_PIXEL_BGRA8, &img, inv, inv_l);
            }
            uint8_t rh[16];
            wr32(rh + 0u, (uint32_t)err);
            wr32(rh + 4u, img.width);
            wr32(rh + 8u, img.height);
            wr32(rh + 12u, out_bytes);
            if (!write_exact(hout, rh, 16u)) return 4;
            if (out_bytes > 0u && !write_exact(hout, g_comp_buf, out_bytes)) return 5;
            if (img.pixels) ia.free(ia.ctx, img.pixels, img.byte_size);
            if (inv) ia.free(ia.ctx, inv, inv_l);
            if (tc.live != 0u || tc.mismatch_or_double_free != 0u) return 7;
        } else if (op == 3u) {
            /* Choose scaled: <u16 count><u32 lw><u32 lh><u32 scale><u32 sbpp> + count * <u32 dw><u32 dh><u16 cc><u16 pl><u16 bpp> */
            uint8_t hdr[18];
            if (!read_exact(hin, hdr, 18u)) return 2;
            uint16_t cnt = rd16(hdr);
            uint32_t lw = rd32(hdr + 2u);
            uint32_t lh = rd32(hdr + 6u);
            uint32_t sc = rd32(hdr + 10u);
            uint32_t sb = rd32(hdr + 14u);
            if (cnt > IM_ICO_MAX_ENTRIES) return 3;
            im_ico_entry_info entries[IM_ICO_MAX_ENTRIES];
            for (uint16_t i = 0; i < cnt; ++i) {
                uint8_t er[14];
                if (!read_exact(hin, er, 14u)) return 3;
                entries[i].dir_width = rd32(er + 0u);
                entries[i].dir_height = rd32(er + 4u);
                entries[i].dir_color_count = rd16(er + 8u);
                entries[i].dir_planes = rd16(er + 10u);
                entries[i].dir_bit_count = rd16(er + 12u);
                entries[i].bit_count = entries[i].dir_bit_count;
            }
            int32_t chosen = im_ico_choose_entry_scaled(entries, cnt, lw, lh, sc, sb);
            uint8_t rh[4];
            wr32(rh, (uint32_t)chosen);
            if (!write_exact(hout, rh, 4u)) return 4;
        } else if (op == 4u) {
            /* Encode: <u8 type><u16 count> + count * (<u32 w><u32 h><u16 hx><u16 hy><u8 enc><u8 fmt><u32 inv_len><px><inv>) */
            uint8_t hdr[3];
            if (!read_exact(hin, hdr, 3u)) return 2;
            uint8_t type = hdr[0];
            uint16_t cnt = rd16(hdr + 1u);
            if (cnt == 0u || cnt > IM_ICO_MAX_ENTRIES) return 3;
            im_image imgs[IM_ICO_MAX_ENTRIES];
            im_ico_write_entry wes[IM_ICO_MAX_ENTRIES];
            size_t pos = 0u;
            for (uint16_t i = 0; i < cnt; ++i) {
                uint8_t eh[18];
                if (!read_exact(hin, eh, 18u)) return 3;
                uint32_t w = rd32(eh + 0u);
                uint32_t h = rd32(eh + 4u);
                uint16_t hx = rd16(eh + 8u);
                uint16_t hy = rd16(eh + 10u);
                uint8_t enc = eh[12];
                uint8_t fmt = eh[13];
                uint32_t inv_l = rd32(eh + 14u);
                size_t px_n = (size_t)w * (size_t)h * 4u;
                if (pos + px_n + inv_l > sizeof(g_in_buf)) return 3;
                if (px_n > 0u && !read_exact(hin, g_in_buf + pos, px_n)) return 3;
                imgs[i].width = w; imgs[i].height = h; imgs[i].stride = w * 4u;
                imgs[i].format = (im_pixel_format)fmt;
                imgs[i].pixels = g_in_buf + pos;
                imgs[i].byte_size = px_n;
                pos += px_n;
                const uint8_t *inv_p = NULL;
                if (inv_l > 0u) {
                    if (!read_exact(hin, g_in_buf + pos, inv_l)) return 3;
                    inv_p = g_in_buf + pos;
                    pos += inv_l;
                }
                wes[i].image = &imgs[i];
                wes[i].invert_mask = inv_p;
                wes[i].invert_mask_size = inv_l;
                wes[i].hotspot_x = hx;
                wes[i].hotspot_y = hy;
                wes[i].encoding = (im_ico_encoding)enc;
            }
            track_ctx tc; track_init(&tc, false, 0u);
            im_ico_alloc ia = { track_alloc, track_free, &tc };
            uint8_t *out_p = NULL; size_t out_n = 0u;
            im_image_error err = im_ico_encode((im_ico_type)type, wes, cnt, &ia, &out_p, &out_n);
            uint32_t expected_live = (out_p != NULL) ? 1u : 0u;
            uint32_t leak = (tc.live != expected_live || tc.mismatch_or_double_free != 0u) ? 1u : 0u;
            uint8_t rh[12];
            wr32(rh + 0u, (uint32_t)err);
            wr32(rh + 4u, leak);
            wr32(rh + 8u, (uint32_t)out_n);
            if (!write_exact(hout, rh, 12u)) return 4;
            if (out_p && out_n > 0u && !write_exact(hout, out_p, out_n)) return 5;
            if (out_p) ia.free(ia.ctx, out_p, out_n);
            if (tc.live != 0u || tc.mismatch_or_double_free != 0u) return 7;
        } else {
            return 9;
        }
    }
    return 0;
}

static bool has_cli_arg(const wchar_t *cmd, const wchar_t *want) {
    if (!cmd || !want) return false;
    const wchar_t *p = cmd;
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') ++p;
        if (*p == L'"') ++p;
    } else {
        while (*p && *p != L' ' && *p != L'\t') ++p;
    }
    while (*p) {
        while (*p == L' ' || *p == L'\t') ++p;
        if (!*p) break;
        const wchar_t *start = p;
        bool quoted = (*p == L'"');
        if (quoted) {
            ++p; start = p;
            while (*p && *p != L'"') ++p;
        } else {
            while (*p && *p != L' ' && *p != L'\t') ++p;
        }
        size_t len = (size_t)(p - start);
        if (quoted && *p == L'"') ++p;
        size_t j = 0;
        while (j < len && want[j] && start[j] == want[j]) ++j;
        if (j == len && want[j] == L'\0') return true;
    }
    return false;
}

int im_main(void) {
    if (has_cli_arg(GetCommandLineW(), L"--batch")) {
        return run_batch();
    }
    test_all_self();
    if (g_fails == 0) {
        write_str("ico_tests: ALL PASSED\n");
        return 0;
    }
    write_str("ico_tests: FAILED\n");
    return 1;
}
