/* Tests for D01 T01 s5: fixed-point TrueType/OpenType font parsing, outline rasterisation, and text layout. */
#include <impossible/base.h>
#include <impossible/file.h>
#include <impossible/text.h>
#include <windows.h>
#include "font.h"
#include "text.h"
#include "layer.h"
#include "transform.h"

/* ---- Allocator with optional OOM injection ---- */

typedef struct test_alloc_ctx {
    size_t live;
    int fail_after; /* < 0: never fail; >= 0: fail when alloc_calls >= fail_after */
    int alloc_calls;
} test_alloc_ctx;

static void *t_alloc(void *ctx, size_t n) {
    test_alloc_ctx *c = (test_alloc_ctx *)ctx;
    if (c && c->fail_after >= 0 && c->alloc_calls >= c->fail_after) return NULL;
    if (c) { c->alloc_calls++; c->live++; }
    return im_alloc(n ? n : 1u);
}

static void *t_realloc(void *ctx, void *p, size_t old_n, size_t n) {
    test_alloc_ctx *c = (test_alloc_ctx *)ctx;
    if (c && c->fail_after >= 0 && c->alloc_calls >= c->fail_after) return NULL;
    if (c) { c->alloc_calls++; if (!p) c->live++; }
    if (!p) return im_alloc(n ? n : 1u);
    void *np = im_alloc(n ? n : 1u);
    if (!np) return NULL;
    size_t cp = old_n < n ? old_n : n;
    for (size_t i = 0; i < cp; ++i) ((uint8_t *)np)[i] = ((const uint8_t *)p)[i];
    im_free(p);
    return np;
}

static void t_free(void *ctx, void *p, size_t old_n) {
    (void)old_n;
    if (!p) return;
    test_alloc_ctx *c = (test_alloc_ctx *)ctx;
    if (c && c->live > 0) c->live--;
    im_free(p);
}

static im_allocator make_alloc(test_alloc_ctx *c) {
    return (im_allocator){ t_alloc, t_realloc, t_free, c };
}

/* ---- Print helpers ---- */

static int g_fails = 0;

static void write_out(const char *s, size_t n) {
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)n, &written, NULL);
}

static void print_str(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    write_out(s, n);
}

static void print_i64(int64_t v) {
    char buf[32];
    int pos = 0;
    uint64_t u;
    if (v < 0) {
        buf[pos++] = '-';
        u = (uint64_t)(-(v + 1)) + 1u;
    } else {
        u = (uint64_t)v;
    }
    char rev[24];
    int r = 0;
    do { rev[r++] = (char)('0' + (u % 10u)); u /= 10u; } while (u);
    while (r > 0) buf[pos++] = rev[--r];
    write_out(buf, (size_t)pos);
}

static void check(bool cond, const char *msg) {
    if (!cond) {
        ++g_fails;
        print_str("FAIL: ");
        print_str(msg);
        print_str("\n");
    }
}

/* ---- Minimal SFNT builder for synthetic CFF, TTC, and composite TTF fixtures ---- */

typedef struct buf_w {
    uint8_t *p;
    size_t len;
    size_t cap;
} buf_w;

static void bw_u8(buf_w *b, uint8_t v) {
    if (b->len < b->cap) b->p[b->len++] = v;
}
static void bw_u16(buf_w *b, uint16_t v) {
    bw_u8(b, (uint8_t)(v >> 8));
    bw_u8(b, (uint8_t)(v & 0xFFu));
}
static void bw_u32(buf_w *b, uint32_t v) {
    bw_u16(b, (uint16_t)(v >> 16));
    bw_u16(b, (uint16_t)(v & 0xFFFFu));
}
static void bw_tag(buf_w *b, char c0, char c1, char c2, char c3) {
    bw_u8(b, (uint8_t)c0); bw_u8(b, (uint8_t)c1); bw_u8(b, (uint8_t)c2); bw_u8(b, (uint8_t)c3);
}
static void bw_bytes(buf_w *b, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; ++i) bw_u8(b, src[i]);
}
static void bw_pad4(buf_w *b) {
    while ((b->len & 3u) != 0) bw_u8(b, 0);
}

typedef struct tbl_spec {
    const char *tag;
    const uint8_t *data;
    size_t len;
} tbl_spec;

static size_t build_sfnt(uint8_t *out, size_t cap, uint32_t sfnt_ver, const tbl_spec *tbls, uint16_t ntbl, size_t base_off) {
    buf_w b = { out + base_off, 0, cap - base_off };
    bw_u32(&b, sfnt_ver);
    bw_u16(&b, ntbl);
    bw_u16(&b, 0); bw_u16(&b, 0); bw_u16(&b, 0);
    size_t dir_start = b.len;
    for (uint16_t i = 0; i < ntbl; ++i) {
        bw_tag(&b, tbls[i].tag[0], tbls[i].tag[1], tbls[i].tag[2], tbls[i].tag[3]);
        bw_u32(&b, 0); bw_u32(&b, 0); bw_u32(&b, (uint32_t)tbls[i].len);
    }
    for (uint16_t i = 0; i < ntbl; ++i) {
        bw_pad4(&b);
        uint32_t abs_off = (uint32_t)(base_off + b.len);
        size_t off_pos = base_off + dir_start + (size_t)i * 16u + 8u;
        out[off_pos + 0] = (uint8_t)(abs_off >> 24);
        out[off_pos + 1] = (uint8_t)(abs_off >> 16);
        out[off_pos + 2] = (uint8_t)(abs_off >> 8);
        out[off_pos + 3] = (uint8_t)(abs_off & 0xFFu);
        bw_bytes(&b, tbls[i].data, tbls[i].len);
    }
    bw_pad4(&b);
    return base_off + b.len;
}

static void make_common_tables(uint8_t head[54], uint8_t maxp[6], uint8_t hhea[36],
                               uint8_t hmtx[12], uint8_t cmap[64], size_t *cmap_len,
                               uint16_t upem, uint16_t nglyphs, int16_t loc_fmt) {
    for (int i = 0; i < 54; ++i) head[i] = 0;
    head[18] = (uint8_t)(upem >> 8); head[19] = (uint8_t)(upem & 0xFFu);
    head[40] = 0x03; head[41] = 0xE8; /* xMax=1000 */
    head[42] = 0x03; head[43] = 0xE8; /* yMax=1000 */
    head[50] = (uint8_t)(loc_fmt >> 8); head[51] = (uint8_t)(loc_fmt & 0xFFu);

    for (int i = 0; i < 6; ++i) maxp[i] = 0;
    maxp[4] = (uint8_t)(nglyphs >> 8); maxp[5] = (uint8_t)(nglyphs & 0xFFu);

    for (int i = 0; i < 36; ++i) hhea[i] = 0;
    hhea[4] = 0x03; hhea[5] = 0x20; /* ascent = 800 */
    hhea[6] = 0xFF; hhea[7] = 0x38; /* descent = -200 */
    hhea[34] = (uint8_t)(nglyphs >> 8); hhea[35] = (uint8_t)(nglyphs & 0xFFu);

    /* 3 glyphs: 0 (.notdef adv=500), 1 ('A' adv=600), 2 ('B' adv=700) */
    for (int i = 0; i < 12; ++i) hmtx[i] = 0;
    hmtx[0] = (uint8_t)(500 >> 8); hmtx[1] = (uint8_t)(500 & 0xFFu);
    hmtx[4] = (uint8_t)(600 >> 8); hmtx[5] = (uint8_t)(600 & 0xFFu);
    hmtx[8] = (uint8_t)(700 >> 8); hmtx[9] = (uint8_t)(700 & 0xFFu);

    /* cmap format 4 mapping 'A'(65)->1, 'B'(66)->2 */
    buf_w cb = { cmap, 0, 64 };
    bw_u16(&cb, 0); bw_u16(&cb, 1); /* ver 0, 1 subtable */
    bw_u16(&cb, 3); bw_u16(&cb, 1); bw_u32(&cb, 12); /* Windows Unicode BMP */
    bw_u16(&cb, 4); bw_u16(&cb, 32); bw_u16(&cb, 0); /* fmt 4, len 32 */
    bw_u16(&cb, 4); bw_u16(&cb, 4); bw_u16(&cb, 1); bw_u16(&cb, 0); /* segCountX2=4 (2 segs) */
    bw_u16(&cb, 'B'); bw_u16(&cb, 0xFFFFu); /* endCode */
    bw_u16(&cb, 0); /* reservedPad */
    bw_u16(&cb, 'A'); bw_u16(&cb, 0xFFFFu); /* startCode */
    bw_u16(&cb, (uint16_t)(int16_t)(1 - 'A')); bw_u16(&cb, 1); /* idDelta */
    bw_u16(&cb, 0); bw_u16(&cb, 0); /* idRangeOffset */
    *cmap_len = cb.len;
}

/* ---- Test 1: OpenType CFF v1 (Global Subr + Local Subr + hstem + hintmask + cubic) + CFF2 refusal ---- */

static void test_synthetic_cff_and_ttc_and_composite(void) {
    test_alloc_ctx actx = { 0, -1, 0 };
    im_allocator alloc = make_alloc(&actx);

    uint8_t head[54], maxp[6], hhea[36], hmtx[12], cmap[64];
    size_t cmap_len = 0;
    make_common_tables(head, maxp, hhea, hmtx, cmap, &cmap_len, 1000, 3, 0);

    /* Build CFF v1 table with:
       - Header (4 bytes: 1, 0, 4, 1)
       - Name INDEX: 1 entry "T"
       - Top DICT INDEX: 1 entry pointing to CharStrings (op 17) and Private (op 18)
       - String INDEX: empty (count 0)
       - Global Subr INDEX: 1 subr drawing right+up edges: 400 hlineto, 500 vlineto, return
       - CharStrings INDEX: 3 glyphs:
         gid 0: endchar
         gid 1 ('A'): 100 200 hstem (1 stem -> 1 mask byte), hintmask 0x80, 100 100 rmoveto,
                      -107 callgsubr, -107 callsubr, endchar
         gid 2 ('B'): 100 100 rmoveto, 200 0 200 200 0 200 rrcurveto, -200 0 rlineto, endchar
       - Private DICT: Subrs offset (op 19) -> Local Subr INDEX
       - Local Subr INDEX: 1 subr: -400 hlineto, return
    */
    uint8_t cff[512];
    buf_w cw = { cff, 0, sizeof(cff) };
    bw_u8(&cw, 1); bw_u8(&cw, 0); bw_u8(&cw, 4); bw_u8(&cw, 1); /* Header */
    /* Name INDEX: count=1, offSize=1, off=[1,2], data="T" */
    bw_u16(&cw, 1); bw_u8(&cw, 1); bw_u8(&cw, 1); bw_u8(&cw, 2); bw_u8(&cw, 'T');
    /* Top DICT INDEX: placeholder 12 bytes (2-byte int + op 17, 2-byte int + 2-byte int + op 18 -> 3+1 + 3+3+1 = 11 bytes) */
    bw_u16(&cw, 1); bw_u8(&cw, 1); bw_u8(&cw, 1); bw_u8(&cw, 12);
    size_t td_pos = cw.len;
    for (int i = 0; i < 11; ++i) bw_u8(&cw, 0);
    /* String INDEX: empty */
    bw_u16(&cw, 0);
    /* Global Subr INDEX: 1 subr: [28, 0x01, 0x90 (400), 6 (hlineto), 28, 0x01, 0xF4 (500), 7 (vlineto), 11 (return)] = 9 bytes */
    bw_u16(&cw, 1); bw_u8(&cw, 1); bw_u8(&cw, 1); bw_u8(&cw, 10);
    bw_u8(&cw, 28); bw_u16(&cw, 400); bw_u8(&cw, 6);
    bw_u8(&cw, 28); bw_u16(&cw, 500); bw_u8(&cw, 7);
    bw_u8(&cw, 11);

    /* CharStrings INDEX */
    uint16_t cs_off = (uint16_t)cw.len;
    uint8_t cs0[] = { 14 }; /* endchar */
    uint8_t cs1[] = {
        /* 100 (239), 200 (247, 92), hstem (1), hintmask (19), mask (0x80) */
        239, 247, 92, 1, 19, 0x80,
        /* 100 (239), 100 (239), rmoveto (21) */
        239, 239, 21,
        /* -107 (32), callgsubr (29), -107 (32), callsubr (10), endchar (14) */
        32, 29, 32, 10, 14
    };
    uint8_t cs2[] = {
        /* 100, 100 rmoveto */
        239, 239, 21,
        /* 200 0 200 200 0 200 rrcurveto (8) */
        247, 92, 139, 247, 92, 247, 92, 139, 247, 92, 8,
        /* -200 0 rlineto (5), endchar (14) */
        251, 92, 139, 5, 14
    };
    bw_u16(&cw, 3); bw_u8(&cw, 1);
    bw_u8(&cw, 1);
    bw_u8(&cw, (uint8_t)(1 + sizeof(cs0)));
    bw_u8(&cw, (uint8_t)(1 + sizeof(cs0) + sizeof(cs1)));
    bw_u8(&cw, (uint8_t)(1 + sizeof(cs0) + sizeof(cs1) + sizeof(cs2)));
    bw_bytes(&cw, cs0, sizeof(cs0));
    bw_bytes(&cw, cs1, sizeof(cs1));
    bw_bytes(&cw, cs2, sizeof(cs2));

    /* Private DICT: Subrs offset = 4 (relative to Private DICT start): [143 (4), 19 (Subrs)] -> 2 bytes? Make it 4 bytes: 28, 0, 4, 19 */
    uint16_t priv_off = (uint16_t)cw.len;
    uint16_t priv_size = 4;
    bw_u8(&cw, 28); bw_u16(&cw, 4); bw_u8(&cw, 19);
    /* Local Subr INDEX right after Private DICT: 1 subr: -400 hlineto, return */
    bw_u16(&cw, 1); bw_u8(&cw, 1); bw_u8(&cw, 1); bw_u8(&cw, 6);
    bw_u8(&cw, 28); bw_u16(&cw, (uint16_t)(int16_t)-400); bw_u8(&cw, 6); bw_u8(&cw, 11);

    /* Patch Top DICT: cs_off (28, hi, lo, 17), priv_size (28, hi, lo), priv_off (28, hi, lo), 18 */
    cff[td_pos + 0] = 28; cff[td_pos + 1] = (uint8_t)(cs_off >> 8); cff[td_pos + 2] = (uint8_t)(cs_off & 0xFFu); cff[td_pos + 3] = 17;
    cff[td_pos + 4] = 28; cff[td_pos + 5] = (uint8_t)(priv_size >> 8); cff[td_pos + 6] = (uint8_t)(priv_size & 0xFFu);
    cff[td_pos + 7] = 28; cff[td_pos + 8] = (uint8_t)(priv_off >> 8); cff[td_pos + 9] = (uint8_t)(priv_off & 0xFFu); cff[td_pos + 10] = 18;

    tbl_spec otf_tbls[6] = {
        { "CFF ", cff, cw.len },
        { "cmap", cmap, cmap_len },
        { "head", head, 54 },
        { "hhea", hhea, 36 },
        { "hmtx", hmtx, 12 },
        { "maxp", maxp, 6 },
    };
    uint8_t otf_buf[2048];
    size_t otf_sz = build_sfnt(otf_buf, sizeof(otf_buf), 0x4F54544Fu /* 'OTTO' */, otf_tbls, 6, 0);

    im_font cff_font;
    check(im_font_init(&cff_font, otf_buf, otf_sz), "CFF v1 font initializes");
    check(cff_font.metrics.is_cff, "CFF v1 font reports is_cff=true");
    check(im_font_glyph_index(&cff_font, 'A') == 1u, "CFF cmap maps 'A' -> 1");

    uint32_t pixels[48 * 48];
    for (int i = 0; i < 48 * 48; ++i) pixels[i] = 0x00000000u;
    im_surface surf = { pixels, 48, 48, 48, { 0 }, IM_ONE, NULL, NULL };
    check(im_font_draw_glyph(&surf, alloc, &cff_font, 1u, 4 * IM_ONE, 40 * IM_ONE, 32 * IM_ONE, 0xFFFFFFFFu),
          "CFF glyph 1 (hstem+hintmask+gsubr+lsubr) draws");
    int lit = 0;
    for (int i = 0; i < 48 * 48; ++i) if (pixels[i] != 0) ++lit;
    check(lit > 50, "CFF glyph 1 produces visible filled rectangle ink");

    for (int i = 0; i < 48 * 48; ++i) pixels[i] = 0;
    check(im_font_draw_glyph(&surf, alloc, &cff_font, 2u, 4 * IM_ONE, 40 * IM_ONE, 32 * IM_ONE, 0xFFFFFFFFu),
          "CFF glyph 2 (rrcurveto) draws");
    int partial = 0;
    lit = 0;
    for (int i = 0; i < 48 * 48; ++i) {
        uint8_t r = (uint8_t)((pixels[i] >> 16) & 0xFFu);
        if (r > 0) ++lit;
        if (r > 0 && r < 255) ++partial;
    }
    check(lit > 10 && partial > 0, "CFF cubic curve produces antialiased partial coverage");

    /* CFF2 (major=2) must be explicitly refused */
    cff[0] = 2;
    size_t cff2_sz = build_sfnt(otf_buf, sizeof(otf_buf), 0x4F54544Fu, otf_tbls, 6, 0);
    im_font bad_cff2;
    check(!im_font_init(&bad_cff2, otf_buf, cff2_sz), "CFF2 (major=2) is explicitly refused");
    cff[0] = 1;

    /* ---- Synthetic TrueType font with simple (off-curve start!) + composite (point attachment!) + TTC ---- */
    /* Glyph 0: empty
       Glyph 1: simple square with off-curve start (4 points, first off-curve!) -> [100..300] x [0..200]
       Glyph 2: composite of Glyph 1 + Glyph 1 attached by point matching (parent pt 1 -> child pt 0)! */
    uint8_t glyf[256];
    buf_w gw = { glyf, 0, sizeof(glyf) };
    size_t g0_off = 0, g1_off = 0, g2_off = 0, g3_off = 0;
    /* Glyph 1: 1 contour, 4 points: (100,0) off, (300,0) on, (300,200) on, (100,200) on */
    g1_off = gw.len;
    bw_u16(&gw, 1); /* numContours */
    bw_u16(&gw, 100); bw_u16(&gw, 0); bw_u16(&gw, 300); bw_u16(&gw, 200); /* bbox */
    bw_u16(&gw, 3); /* endPtsOfContours[0] = 3 */
    bw_u16(&gw, 0); /* instructionLength */
    /* Flags: pt0 off-curve (0x00), pt1..3 on-curve (0x01), all 16-bit deltas */
    bw_u8(&gw, 0x00); bw_u8(&gw, 0x01); bw_u8(&gw, 0x01); bw_u8(&gw, 0x01);
    /* X deltas: +100, +200, 0, -200 */
    bw_u16(&gw, 100); bw_u16(&gw, 200); bw_u16(&gw, 0); bw_u16(&gw, (uint16_t)(int16_t)-200);
    /* Y deltas: 0, 0, +200, 0 */
    bw_u16(&gw, 0); bw_u16(&gw, 0); bw_u16(&gw, 200); bw_u16(&gw, 0);
    bw_pad4(&gw);

    /* Glyph 2: composite (numContours=-1):
       comp 0: gid 1, XY=(0,0), MORE_COMPONENTS (0x0022)
       comp 1: gid 1, point-match (!ARGS_ARE_XY_VALUES): parent pt 1 (300,0) <- child pt 0 (100,0) => dx=+200! */
    g2_off = gw.len;
    bw_u16(&gw, (uint16_t)(int16_t)-1);
    bw_u16(&gw, 100); bw_u16(&gw, 0); bw_u16(&gw, 500); bw_u16(&gw, 200);
    bw_u16(&gw, 0x0022u); bw_u16(&gw, 1); bw_u8(&gw, 0); bw_u8(&gw, 0); /* comp 0 */
    bw_u16(&gw, 0x0000u); bw_u16(&gw, 1); bw_u8(&gw, 1); bw_u8(&gw, 0); /* comp 1: parent pt 1, child pt 0 */
    bw_pad4(&gw);
    g3_off = gw.len;

    uint8_t loca[8];
    loca[0] = (uint8_t)((g0_off / 2) >> 8); loca[1] = (uint8_t)((g0_off / 2) & 0xFFu);
    loca[2] = (uint8_t)((g1_off / 2) >> 8); loca[3] = (uint8_t)((g1_off / 2) & 0xFFu);
    loca[4] = (uint8_t)((g2_off / 2) >> 8); loca[5] = (uint8_t)((g2_off / 2) & 0xFFu);
    loca[6] = (uint8_t)((g3_off / 2) >> 8); loca[7] = (uint8_t)((g3_off / 2) & 0xFFu);

    tbl_spec tt_tbls[7] = {
        { "cmap", cmap, cmap_len },
        { "glyf", glyf, gw.len },
        { "head", head, 54 },
        { "hhea", hhea, 36 },
        { "hmtx", hmtx, 12 },
        { "loca", loca, 8 },
        { "maxp", maxp, 6 },
    };

    /* Build a 2-face TTC where face 0 is at offset 20 and face 1 has upem=2000 */
    uint8_t ttc[4096];
    for (size_t i = 0; i < sizeof(ttc); ++i) ttc[i] = 0;
    ttc[0] = 't'; ttc[1] = 't'; ttc[2] = 'c'; ttc[3] = 'f';
    ttc[5] = 1; /* major 1 */
    ttc[11] = 2; /* numFonts = 2 */
    uint32_t f0_off = 20;
    ttc[12] = 0; ttc[13] = 0; ttc[14] = 0; ttc[15] = (uint8_t)f0_off;
    size_t f1_off = build_sfnt(ttc, sizeof(ttc), 0x00010000u, tt_tbls, 7, f0_off);
    ttc[16] = (uint8_t)(f1_off >> 24); ttc[17] = (uint8_t)(f1_off >> 16);
    ttc[18] = (uint8_t)(f1_off >> 8); ttc[19] = (uint8_t)(f1_off & 0xFFu);
    head[18] = (uint8_t)(2000 >> 8); head[19] = (uint8_t)(2000 & 0xFFu);
    size_t ttc_sz = build_sfnt(ttc, sizeof(ttc), 0x00010000u, tt_tbls, 7, f1_off);

    im_font f0, f1, f2;
    check(im_font_init_face(&f0, ttc, ttc_sz, 0) && f0.metrics.units_per_em == 1000u, "TTC face 0 loads with upem=1000");
    check(im_font_init_face(&f1, ttc, ttc_sz, 1) && f1.metrics.units_per_em == 2000u, "TTC face 1 loads with upem=2000");
    check(!im_font_init_face(&f2, ttc, ttc_sz, 2), "TTC out-of-range face 2 refused");

    /* Verify point-attached composite glyph 2 extends twice as wide as glyph 1! */
    im_path p1, p2;
    im_path_init(&p1, alloc, IM_FILL_NON_ZERO);
    im_path_init(&p2, alloc, IM_FILL_NON_ZERO);
    check(im_font_glyph_outline(&f0, 1u, 100 * IM_ONE, &p1), "TTF simple glyph with off-curve start extracts");
    check(im_font_glyph_outline(&f0, 2u, 100 * IM_ONE, &p2), "TTF point-attached composite glyph extracts");
    im_rect b1 = { 0 }, b2 = { 0 };
    im_path_data v1 = im_path_view(&p1), v2 = im_path_view(&p2);
    check(im_path_bounds(&v1, &b1) && im_path_bounds(&v2, &b2), "Both TTF outlines have valid bounds");
    check(b2.right > b1.right + 15 * IM_ONE, "Point-attached composite shifts second component right");
    im_path_destroy(&p1);
    im_path_destroy(&p2);

    /* OOM injection + retry on im_text_layout_build */
    im_text_style st = { &f0, 16 * IM_ONE, 0, 100 * IM_ONE, IM_TEXT_ALIGN_LEFT, IM_TEXT_WRAP, 0xFFFFFFFFu, true };
    im_text_layout lay;
    actx.fail_after = 0;
    actx.alloc_calls = 0;
    check(!im_text_layout_build(&lay, alloc, "AB", 2, &st), "Injected OOM fails cleanly");
    check(lay.lines == NULL && lay.glyphs == NULL && lay.line_count == 0, "Failed layout is zeroed");
    actx.fail_after = -1;
    check(im_text_layout_build(&lay, alloc, "AB", 2, &st) && lay.glyph_count == 2u, "Retry after OOM succeeds");
    im_text_layout_destroy(&lay);

    /* ---- UTF-8 maximal subpart + hard breaks + emergency grapheme break ---- */
    uint32_t cp = 0;
    /* Invalid 0xE2 0x28 0xA1: 0xE2 consumes 1 byte -> U+FFFD, leaving '(' (0x28) intact! */
    const char bad_u8[] = { (char)0xE2, '(', 'A' };
    check(im_utf8_decode(bad_u8, 3, &cp) == 1u && cp == 0xFFFDu, "Invalid UTF-8 consumes maximal subpart (1 byte)");
    check(im_utf8_decode(bad_u8 + 1, 2, &cp) == 1u && cp == '(', "Valid byte immediately after invalid UTF-8 survives");

    /* Empty string -> 1 empty line */
    check(im_text_layout_build(&lay, alloc, "", 0, &st) && lay.line_count == 1u && lay.glyph_count == 0u,
          "Empty string emits 1 empty line");
    im_text_layout_destroy(&lay);

    /* Terminal CRLF -> 2 lines (CRLF treated as one separator, trailing empty line emitted) */
    check(im_text_layout_build(&lay, alloc, "A\r\n", 3, &st) && lay.line_count == 2u && lay.lines[0].glyph_count == 1u &&
          lay.lines[1].glyph_count == 0u, "Terminal CRLF emits 2 lines as one separator");
    im_text_layout_destroy(&lay);

    /* Consecutive \n\n -> 3 lines */
    check(im_text_layout_build(&lay, alloc, "A\n\nB", 4, &st) && lay.line_count == 3u && lay.lines[1].glyph_count == 0u,
          "Consecutive newlines emit middle empty line");
    im_text_layout_destroy(&lay);

    /* Emergency grapheme break: 'A' + combining acute U+0301 (CC 81) must NOT split at max_width=1px */
    const char combo[] = { 'A', (char)0xCC, (char)0x81, 'B' };
    im_text_style narrow = st;
    narrow.max_width = 1 * IM_ONE;
    check(im_text_layout_build(&lay, alloc, combo, sizeof(combo), &narrow) && lay.line_count == 2u &&
          lay.lines[0].glyph_count == 2u && lay.lines[1].glyph_count == 1u,
          "Emergency wrap preserves combining grapheme cluster and makes forward progress");
    im_text_layout_destroy(&lay);

    /* Malformed buffers refused */
    im_font bad;
    check(!im_font_init(&bad, NULL, 0), "NULL font refused");
    check(!im_font_init(&bad, ttc, 8), "Truncated 8-byte font refused");
    uint8_t zeros[64];
    for (int i = 0; i < 64; ++i) zeros[i] = 0;
    check(!im_font_init(&bad, zeros, sizeof(zeros)), "Zero-filled font refused");

    check(actx.live == 0, "Zero leaked allocations across synthetic font & text tests");
}

/* ---- CLI helpers (Tier 0 wide command line parsing) ---- */

static int parse_args(char argv_buf[8][512]) {
    const wchar_t *cmd = GetCommandLineW();
    int argc = 0;
    while (*cmd && argc < 8) {
        while (*cmd == L' ' || *cmd == L'\t') ++cmd;
        if (!*cmd) break;
        bool q = (*cmd == L'"');
        if (q) ++cmd;
        wchar_t wtmp[512];
        int wn = 0;
        while (*cmd && wn < 511) {
            if (q && *cmd == L'"') { ++cmd; break; }
            if (!q && (*cmd == L' ' || *cmd == L'\t')) break;
            wtmp[wn++] = *cmd++;
        }
        wtmp[wn] = 0;
        int n = 0;
        for (int i = 0; i < wn && n + 4 < 511; ++i) {
            uint32_t c = (uint32_t)wtmp[i];
            if (c < 0x80u) { argv_buf[argc][n++] = (char)c; }
            else if (c < 0x800u) {
                argv_buf[argc][n++] = (char)(0xC0u | (c >> 6));
                argv_buf[argc][n++] = (char)(0x80u | (c & 0x3Fu));
            } else {
                argv_buf[argc][n++] = (char)(0xE0u | (c >> 12));
                argv_buf[argc][n++] = (char)(0x80u | ((c >> 6) & 0x3Fu));
                argv_buf[argc][n++] = (char)(0x80u | (c & 0x3Fu));
            }
        }
        argv_buf[argc][n] = 0;
        ++argc;
    }
    return argc;
}

static bool str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return false; ++a; ++b; }
    return *a == *b;
}

static int32_t parse_i32(const char *s) {
    int64_t v = 0;
    bool neg = false;
    if (*s == '-') { neg = true; ++s; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return (int32_t)(neg ? -v : v);
}

static int run_all_glyphs(const char *font_path) {
    char *data = NULL;
    size_t len = 0;
    if (im_read(font_path, 32u * 1024u * 1024u, &data, &len) != IM_OK) {
        print_str("ERR: cannot read font\n");
        return 2;
    }
    im_font font;
    if (!im_font_init(&font, (const uint8_t *)data, len)) {
        print_str("ERR: im_font_init failed\n");
        im_free(data);
        return 3;
    }
    test_alloc_ctx actx = { 0, -1, 0 };
    im_allocator alloc = make_alloc(&actx);

    uint32_t pixels[48 * 48];
    im_surface surf = { pixels, 48, 48, 48, { 0 }, IM_ONE, NULL, NULL };

    uint32_t ok_count = 0, empty_count = 0, inked_count = 0, partial_count = 0;
    for (uint32_t g = 0; g < font.metrics.num_glyphs; ++g) {
        im_glyph_metrics gm;
        if (!im_font_glyph_metrics(&font, (uint16_t)g, &gm)) {
            print_str("ERR: glyph_metrics failed\n");
            im_free(data);
            return 4;
        }
        for (int i = 0; i < 48 * 48; ++i) pixels[i] = 0;
        if (!im_font_draw_glyph(&surf, alloc, &font, (uint16_t)g, 8 * IM_ONE, 36 * IM_ONE, 24 * IM_ONE, 0xFFFFFFFFu)) {
            print_str("ERR: draw_glyph failed at gid=");
            print_i64(g);
            print_str("\n");
            im_free(data);
            return 5;
        }
        ++ok_count;
        if (gm.is_empty) {
            ++empty_count;
        } else {
            int lit = 0, part = 0;
            for (int i = 0; i < 48 * 48; ++i) {
                uint8_t r = (uint8_t)((pixels[i] >> 16) & 0xFFu);
                if (r > 0) ++lit;
                if (r > 0 && r < 255) ++part;
            }
            if (lit > 0) ++inked_count;
            if (part > 0) ++partial_count;
        }
    }

    /* Also verify transform + rounded clip on 'O' */
    uint16_t gid_o = im_font_glyph_index(&font, 'O');
    im_layer_ctx lctx;
    im_layer_init(&lctx, &surf, alloc, 64u * 1024u);
    im_layer_push_clip_rect(&lctx, (im_rect){ 0, 0, 24 * IM_ONE, 48 * IM_ONE });
    im_transform tr = im_transform_scale(2 * IM_ONE, 2 * IM_ONE);
    surf.layers = &lctx;
    surf.transform = &tr;
    for (int i = 0; i < 48 * 48; ++i) pixels[i] = 0;
    bool clip_ok = im_font_draw_glyph(&surf, alloc, &font, gid_o, 4 * IM_ONE, 20 * IM_ONE, 16 * IM_ONE, 0xFFFFFFFFu);
    int left_lit = 0, right_lit = 0;
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 24; ++x) if (pixels[y * 48 + x]) ++left_lit;
        for (int x = 24; x < 48; ++x) if (pixels[y * 48 + x]) ++right_lit;
    }
    im_layer_pop_clip(&lctx);
    im_layer_destroy(&lctx);

    print_str("ALL_GLYPHS total="); print_i64(font.metrics.num_glyphs);
    print_str(" ok="); print_i64(ok_count);
    print_str(" empty="); print_i64(empty_count);
    print_str(" inked="); print_i64(inked_count);
    print_str(" partial="); print_i64(partial_count);
    print_str(" clip_ok="); print_i64(clip_ok && left_lit > 0 && right_lit == 0 ? 1 : 0);
    print_str(" leaked="); print_i64((int64_t)actx.live);
    print_str("\n");

    im_free(data);
    return (ok_count == font.metrics.num_glyphs && clip_ok && left_lit > 0 && right_lit == 0 && actx.live == 0) ? 0 : 6;
}

static int run_metrics(const char *font_path, im_fixed size_px) {
    char *data = NULL;
    size_t len = 0;
    if (im_read(font_path, 32u * 1024u * 1024u, &data, &len) != IM_OK) return 2;
    im_font font;
    if (!im_font_init(&font, (const uint8_t *)data, len)) { im_free(data); return 3; }

    im_font_metrics fm = im_font_get_metrics(&font);
    print_str("FONT upem="); print_i64(fm.units_per_em);
    print_str(" glyphs="); print_i64(fm.num_glyphs);
    print_str(" asc="); print_i64(fm.ascent);
    print_str(" desc="); print_i64(fm.descent);
    print_str(" gap="); print_i64(fm.line_gap);
    print_str(" nhm="); print_i64(font.num_hmetrics);
    print_str(" gpos_kern="); print_i64(font.has_gpos_kern ? 1 : 0);
    print_str("\n");

    for (uint32_t cp = 32; cp <= 126; ++cp) {
        uint16_t gid = im_font_glyph_index(&font, cp);
        im_glyph_metrics gm;
        if (im_font_glyph_metrics(&font, gid, &gm)) {
            print_str("CP "); print_i64(cp);
            print_str(" gid="); print_i64(gid);
            print_str(" adv_u="); print_i64(gm.advance_width);
            print_str(" adv_fx="); print_i64(im_font_scale_units(&font, gm.advance_width, size_px));
            print_str(" box="); print_i64(gm.x_min); print_str(","); print_i64(gm.y_min);
            print_str(","); print_i64(gm.x_max); print_str(","); print_i64(gm.y_max);
            print_str("\n");
        }
    }
    const char *pairs[] = { "AV", "To", "Wa", "LT", "Va", "Ye" };
    for (int i = 0; i < 6; ++i) {
        uint16_t g0 = im_font_glyph_index(&font, (uint32_t)(uint8_t)pairs[i][0]);
        uint16_t g1 = im_font_glyph_index(&font, (uint32_t)(uint8_t)pairs[i][1]);
        print_str("KERN "); print_str(pairs[i]);
        print_str("="); print_i64(im_font_kerning(&font, g0, g1));
        print_str("\n");
    }
    /* Last 3 glyphs (verifying shared hmtx final advance) */
    for (uint32_t g = fm.num_glyphs > 3 ? fm.num_glyphs - 3u : 0u; g < fm.num_glyphs; ++g) {
        im_glyph_metrics gm;
        if (im_font_glyph_metrics(&font, (uint16_t)g, &gm)) {
            print_str("TAIL "); print_i64(g);
            print_str(" adv_u="); print_i64(gm.advance_width);
            print_str("\n");
        }
    }
    im_free(data);
    return 0;
}

static int run_layout(const char *font_path, im_fixed size_px, im_fixed max_w, int align, int wrap, const char *text_path) {
    char *fdata = NULL, *tdata = NULL;
    size_t flen = 0, tlen = 0;
    if (im_read(font_path, 32u * 1024u * 1024u, &fdata, &flen) != IM_OK) return 2;
    if (im_read(text_path, 1024u * 1024u, &tdata, &tlen) != IM_OK) { im_free(fdata); return 3; }

    im_font font;
    if (!im_font_init(&font, (const uint8_t *)fdata, flen)) { im_free(fdata); im_free(tdata); return 4; }

    test_alloc_ctx actx = { 0, -1, 0 };
    im_allocator alloc = make_alloc(&actx);
    im_text_style st = {
        &font, size_px, 0, max_w, (im_text_align)align, (im_text_wrap)wrap, 0xFFFFFFFFu, true
    };
    im_text_layout lay;
    if (!im_text_layout_build(&lay, alloc, tdata, tlen, &st)) {
        im_free(fdata); im_free(tdata);
        return 5;
    }
    print_str("LAYOUT w="); print_i64(lay.width);
    print_str(" h="); print_i64(lay.height);
    print_str(" lines="); print_i64(lay.line_count);
    print_str("\n");
    for (uint32_t i = 0; i < lay.line_count; ++i) {
        const im_text_line *ln = &lay.lines[i];
        print_str("LINE "); print_i64(i);
        print_str(" bs="); print_i64(ln->byte_start);
        print_str(" be="); print_i64(ln->byte_end);
        print_str(" x="); print_i64(ln->x);
        print_str(" y="); print_i64(ln->y);
        print_str(" w="); print_i64(ln->width);
        print_str(" h="); print_i64(ln->height);
        print_str("\n");
    }
    im_text_layout_destroy(&lay);
    im_free(fdata);
    im_free(tdata);
    return actx.live == 0 ? 0 : 6;
}

int im_main(void) {
    char argv[8][512];
    int argc = parse_args(argv);
    if (argc >= 3 && str_eq(argv[1], "--all-glyphs")) {
        return run_all_glyphs(argv[2]);
    }
    if (argc >= 4 && str_eq(argv[1], "--metrics")) {
        return run_metrics(argv[2], (im_fixed)parse_i32(argv[3]));
    }
    if (argc >= 8 && str_eq(argv[1], "--layout")) {
        return run_layout(argv[2], (im_fixed)parse_i32(argv[3]), (im_fixed)parse_i32(argv[4]),
                          parse_i32(argv[5]), parse_i32(argv[6]), argv[7]);
    }

    test_synthetic_cff_and_ttc_and_composite();
    if (g_fails == 0) {
        print_str("test_text: ALL PASSED\n");
        return 0;
    }
    print_str("test_text: FAILED count=");
    print_i64(g_fails);
    print_str("\n");
    return 1;
}
