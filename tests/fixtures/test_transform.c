/* Tests and oracle dump for D01 T01 s11: transforms, transformed drawing, pictures, nine-slice, and input hit-testing. */
#include <impossible/base.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "dirty.h"
#include "draw.h"
#include "input.h"
#include "layer.h"
#include "layout.h"
#include "picture.h"
#include "states.h"
#include "theme.h"
#include "transform.h"
#include "tree.h"

#define FX(x) ((im_fixed)((x) * IM_ONE))

static int g_failures = 0;

static void write_str(DWORD std_handle, const char *s) {
    DWORD len = 0;
    while (s[len]) len++;
    DWORD written = 0;
    WriteFile(GetStdHandle(std_handle), s, len, &written, NULL);
}

static void write_i64(DWORD h, int64_t v) {
    char buf[32];
    int pos = 0;
    uint64_t uv;
    if (v < 0) {
        buf[pos++] = '-';
        uv = (uint64_t)(-(v + 1)) + 1u;
    } else {
        uv = (uint64_t)v;
    }
    char rev[24];
    int r = 0;
    do {
        rev[r++] = (char)('0' + (uv % 10u));
        uv /= 10u;
    } while (uv > 0u);
    while (r > 0) buf[pos++] = rev[--r];
    buf[pos] = '\0';
    write_str(h, buf);
}

static void write_hex32(DWORD h, uint32_t v) {
    static const char hex[17] = "0123456789ABCDEF";
    char buf[9];
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[v & 0xFu];
        v >>= 4;
    }
    buf[8] = '\0';
    write_str(h, buf);
}

static void fail_msg(const char *msg) {
    write_str(STD_ERROR_HANDLE, "FAIL: ");
    write_str(STD_ERROR_HANDLE, msg);
    write_str(STD_ERROR_HANDLE, "\n");
    g_failures++;
}

#define CHECK(cond, msg) do { \
    if (!(cond)) fail_msg(msg); \
} while (0)

static bool bytes_eq(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return false;
    }
    return true;
}

static void *heap_alloc_zero(size_t size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

static void heap_free_ptr(void *ptr) {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

static void *test_alloc_fn(void *ctx, size_t size) {
    (void)ctx;
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

static void *test_realloc_fn(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    (void)ctx;
    (void)old_size;
    if (!ptr) return HeapAlloc(GetProcessHeap(), 0, new_size ? new_size : 1);
    return HeapReAlloc(GetProcessHeap(), 0, ptr, new_size ? new_size : 1);
}

static void test_free_fn(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    (void)size;
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

static uint32_t rng_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static im_palette make_dark_palette(void) {
    im_palette p;
    memset(&p, 0, sizeof(p));
    p.theme = IM_DARK;
    return p;
}

/* 1. Transform edge cases + 1,000 random transforms + mandatory (1920, 0) rotated 30 deg. */
static void test_transforms(void) {
    /* Exact 0/90/180/270 trig checks. */
    CHECK(im_sin_deg(0) == 0 && im_cos_deg(0) == IM_TR_ONE, "sin/cos 0 exact");
    CHECK(im_sin_deg(FX(90)) == IM_TR_ONE && im_cos_deg(FX(90)) == 0, "sin/cos 90 exact");
    CHECK(im_sin_deg(FX(180)) == 0 && im_cos_deg(FX(180)) == -IM_TR_ONE, "sin/cos 180 exact");
    CHECK(im_sin_deg(FX(270)) == -IM_TR_ONE && im_cos_deg(FX(270)) == 0, "sin/cos 270 exact");

    /* Singular skews refused without mutating output. */
    im_transform sentinel = {11, 22, 33, 44, 55, 66};
    CHECK(!im_transform_skew(FX(90), 0, &sentinel), "90 deg X skew refused");
    CHECK(sentinel.m11 == 11 && sentinel.dy == 66, "refused skew leaves output unchanged");
    CHECK(!im_transform_skew(0, FX(270), &sentinel), "270 deg Y skew refused");

    /* Finite-angle singular two-axis skew (45 deg, 45 deg => det = 1 - 1*1 = 0). */
    im_transform sing_skew;
    CHECK(im_transform_skew(FX(45), FX(45), &sing_skew), "45,45 skew constructs");
    CHECK(!im_transform_invert(&sing_skew, &sentinel), "singular 45,45 skew cannot be inverted");
    CHECK(sentinel.m11 == 11 && sentinel.dy == 66, "failed invert leaves output unchanged");

    /* Zero scale cannot be inverted. */
    im_transform zscale = im_transform_scale(0, FX(2));
    CHECK(!im_transform_invert(&zscale, &sentinel), "zero scale cannot be inverted");

    /* Near-singular invertible matrix whose inverse exceeds IM_TR_MAX_COEFF (2^58) is refused cleanly. */
    im_transform tiny = {1, 0, 0, 1, 0, 0}; /* scale = 2^-30 => inverse = 2^30 (Q30 = 2^60 > 2^58). */
    CHECK(!im_transform_invert(&tiny, &sentinel), "overflowing inverse refused cleanly");
    CHECK(sentinel.m11 == 11 && sentinel.dy == 66, "overflowing invert leaves output unchanged");

    /* Zoom 101x about X=1000 (dx = -100,000 in Q30) must succeed and invert cleanly. */
    im_transform zoom101 = im_transform_scale_at(FX(101), FX(101), FX(1000), FX(1000));
    im_transform zoom101_inv;
    CHECK(im_transform_invert(&zoom101, &zoom101_inv), "101x zoom about (1000,1000) inverts cleanly");
    im_fixed zx = 0, zy = 0;
    CHECK(im_transform_point(&zoom101, FX(1000), FX(1000), &zx, &zy) && zx == FX(1000) && zy == FX(1000),
          "101x zoom keeps pivot fixed");

    /* Fractional-angle sine precision check (including 1126805/65536 deg) dumped for Python < 2^-29 oracle. */
    const int32_t probe_angles[6] = {1126805, 32768, 98304, 1966080, 2949120, 5898200};
    for (int i = 0; i < 6; i++) {
        write_str(STD_OUTPUT_HANDLE, "SIN ");
        write_i64(STD_OUTPUT_HANDLE, probe_angles[i]);
        write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, im_sin_deg(probe_angles[i]));
        write_str(STD_OUTPUT_HANDLE, "\n");
    }

    /* Point overflow refused cleanly. */
    im_transform huge_tr = im_transform_scale(FX(20000), FX(20000));
    im_fixed ox = 123, oy = 456;
    CHECK(!im_transform_point(&huge_tr, FX(20000), FX(20000), &ox, &oy), "point overflow refused");
    CHECK(ox == 123 && oy == 456, "overflowing point leaves outputs unchanged");

    /* Mandatory oracle vector: (1920, 0) rotated 30 degrees. */
    im_transform rot30 = im_transform_rotate(FX(30));
    im_fixed r30x = 0, r30y = 0;
    CHECK(im_transform_point(&rot30, FX(1920), 0, &r30x, &r30y), "rotate (1920,0) by 30 deg");
    write_str(STD_OUTPUT_HANDLE, "ROT30 ");
    write_i64(STD_OUTPUT_HANDLE, r30x);
    write_str(STD_OUTPUT_HANDLE, " ");
    write_i64(STD_OUTPUT_HANDLE, r30y);
    write_str(STD_OUTPUT_HANDLE, "\n");

    /* 1,000 random composite transforms dumped for Python double-precision exact oracle. */
    uint32_t rng = 0xCAFE1101u;
    for (int i = 0; i < 1000; i++) {
        im_fixed cx = (im_fixed)((int32_t)(rng_next(&rng) % 401u) - 200) * IM_ONE;
        im_fixed cy = (im_fixed)((int32_t)(rng_next(&rng) % 401u) - 200) * IM_ONE;
        im_fixed sx = (im_fixed)((int32_t)(rng_next(&rng) % 300u) + 50) * (IM_ONE / 100);
        im_fixed sy = (im_fixed)((int32_t)(rng_next(&rng) % 300u) + 50) * (IM_ONE / 100);
        if (rng_next(&rng) & 1u) sx = -sx;
        im_fixed skx = (im_fixed)((int32_t)(rng_next(&rng) % 81u) - 40) * IM_ONE;
        im_fixed sky = (im_fixed)((int32_t)(rng_next(&rng) % 81u) - 40) * IM_ONE;
        im_fixed rot = (im_fixed)((int32_t)(rng_next(&rng) % 7200u) - 3600) * (IM_ONE / 10);
        im_fixed tx = (im_fixed)((int32_t)(rng_next(&rng) % 1001u) - 500) * IM_ONE;
        im_fixed ty = (im_fixed)((int32_t)(rng_next(&rng) % 1001u) - 500) * IM_ONE;
        im_fixed px = (im_fixed)((int32_t)(rng_next(&rng) % 3841u) - 1920) * IM_ONE;
        im_fixed py = (im_fixed)((int32_t)(rng_next(&rng) % 2161u) - 1080) * IM_ONE;

        im_transform tr;
        CHECK(im_transform_composite(cx, cy, sx, sy, skx, sky, rot, tx, ty, &tr), "composite builds");
        im_fixed out_x = 0, out_y = 0;
        CHECK(im_transform_point(&tr, px, py, &out_x, &out_y), "composite point transforms");

        write_str(STD_OUTPUT_HANDLE, "TR ");
        write_i64(STD_OUTPUT_HANDLE, cx); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, cy); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, sx); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, sy); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, skx); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, sky); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, rot); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, tx); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, ty); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, px); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, py); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, out_x); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, out_y); write_str(STD_OUTPUT_HANDLE, "\n");
    }
}

/* 2. Transformed shapes (90-deg chevron, 30-deg gradient stroke, skewed fill, A8 mask, singular no-op). */
static void draw_asymmetric_chevron(im_surface *s) {
    /* Asymmetric L/chevron arms so 90-degree rotation is unambiguous. */
    im_radii r = {FX(2), FX(2), FX(2), FX(2)};
    im_fill_argb(s, (im_rect){FX(10), FX(8), FX(26), FX(14)}, r, 0xFFFF4422u);
    im_fill_argb(s, (im_rect){FX(20), FX(8), FX(26), FX(28)}, r, 0xFF22AAFFu);
}

static void test_transformed_drawing(void) {
    const int S = 36;
    uint32_t *base = (uint32_t *)heap_alloc_zero((size_t)(S * S) * sizeof(uint32_t));
    uint32_t *rot90 = (uint32_t *)heap_alloc_zero((size_t)(S * S) * sizeof(uint32_t));
    uint32_t *sing_buf = (uint32_t *)heap_alloc_zero((size_t)(S * S) * sizeof(uint32_t));
    CHECK(base && rot90 && sing_buf, "alloc shape buffers");
    if (!base || !rot90 || !sing_buf) return;

    im_palette pal = make_dark_palette();
    im_transform r90 = im_transform_rotate_at(FX(90), FX(S / 2), FX(S / 2));

    /* Compare transformed 90-deg chevron against an UNTRANSFORMED (transform == NULL) reference. */
    im_surface s_base = {base, S, S, S, pal, IM_ONE, NULL, NULL};
    im_surface s_rot = {rot90, S, S, S, pal, IM_ONE, NULL, &r90};
    draw_asymmetric_chevron(&s_base);
    draw_asymmetric_chevron(&s_rot);

    /* Exact 90-degree clockwise permutation around (S/2, S/2): (x, y) -> (S - 1 - y, x).
       Straight edges and interiors match; rounded corners agree within corner tolerance (<= 24). */
    int worst_perm = 0;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            uint32_t a = rot90[x * S + (S - 1 - y)];
            uint32_t b = base[y * S + x];
            for (int sh = 0; sh < 24; sh += 8) {
                int d = (int)((a >> sh) & 255u) - (int)((b >> sh) & 255u);
                if (d < 0) d = -d;
                if (d > worst_perm) worst_perm = d;
            }
        }
    }
    CHECK(worst_perm <= 24, "90-degree rotated chevron matches untransformed reference within corner limit");

    /* Asymmetric A8 glyph mask rotated 90 degrees matches exact permutation, and half-pixel shift blurs cleanly. */
    uint8_t glyph[8 * 8];
    memset(glyph, 0, sizeof(glyph));
    for (int i = 0; i < 8; i++) glyph[i * 8 + 1] = 255;
    for (int i = 1; i < 6; i++) glyph[1 * 8 + i] = 255;
    for (int i = 1; i < 5; i++) glyph[4 * 8 + i] = 200;

    memset(base, 0, (size_t)(S * S) * sizeof(uint32_t));
    memset(rot90, 0, (size_t)(S * S) * sizeof(uint32_t));
    im_draw_alpha_mask(&s_base, (im_rect){FX(10), FX(10), FX(18), FX(18)}, glyph, 8, 8, 8, 0xFFEEEEEEu);
    im_draw_alpha_mask(&s_rot, (im_rect){FX(10), FX(10), FX(18), FX(18)}, glyph, 8, 8, 8, 0xFFEEEEEEu);
    bool mask_perm = true;
    for (int y = 0; y < S && mask_perm; y++) {
        for (int x = 0; x < S; x++) {
            if (rot90[x * S + (S - 1 - y)] != base[y * S + x]) {
                mask_perm = false;
                break;
            }
        }
    }
    CHECK(mask_perm, "90-degree rotated A8 glyph mask matches exact pixel permutation");

    /* Half-pixel translated 1x1 opaque A8 glyph over black must split 50/50 (~0x808080) across both outer pixels. */
    memset(sing_buf, 0, (size_t)(S * S) * sizeof(uint32_t));
    uint8_t one_px[1] = {255};
    im_transform half_tr = im_transform_translate(IM_ONE / 2, 0);
    im_surface s_half = {sing_buf, S, S, S, pal, IM_ONE, NULL, &half_tr};
    im_draw_alpha_mask(&s_half, (im_rect){FX(4), FX(4), FX(5), FX(5)}, one_px, 1, 1, 1, 0xFFFFFFFFu);
    int p0 = (int)(sing_buf[4 * S + 4] & 255u);
    int p1 = (int)(sing_buf[4 * S + 5] & 255u);
    CHECK(p0 >= 126 && p0 <= 130 && p1 >= 126 && p1 <= 130,
          "half-pixel translated 1x1 A8 glyph splits coverage ~128/128 across outer pixels");

    /* Magnified 1x1 opaque A8 glyph into [8,16)x[8,16) through 8x scale:
       at (x=6, y=12), u=-0.6875 (0.3125) and v=+0.0625 (0.9375) => round(0.3125 * 0.9375 * 255) = 75. */
    memset(sing_buf, 0, (size_t)(S * S) * sizeof(uint32_t));
    im_transform mag8 = im_transform_scale(FX(8), FX(8));
    im_surface s_mag = {sing_buf, S, S, S, pal, IM_ONE, NULL, &mag8};
    im_draw_alpha_mask(&s_mag, (im_rect){FX(1), FX(1), FX(2), FX(2)}, one_px, 1, 1, 1, 0xFFFFFFFFu);
    int mag_edge = (int)(sing_buf[12 * S + 6] & 255u);
    CHECK(mag_edge >= 73 && mag_edge <= 77,
          "magnified 1x1 A8 glyph preserves half-texel outer-edge coverage at x=6 (~75/255)");

    /* Wide magnified 1x1 A8 mask into [0,3000)x[0,1) with 8x horizontal scale (padded right = 36000 > 32767):
       must clip wide Q30 bounds to viewport without disappearing, giving pixel (0,0) ~half coverage (~128). */
    memset(sing_buf, 0, (size_t)(S * S) * sizeof(uint32_t));
    im_transform mag8x = im_transform_scale(FX(8), IM_ONE);
    im_surface s_wide = {sing_buf, S, S, S, pal, IM_ONE, NULL, &mag8x};
    im_draw_alpha_mask(&s_wide, (im_rect){0, 0, FX(3000), IM_ONE}, one_px, 1, 1, 1, 0xFFFFFFFFu);
    int wide_px = (int)(sing_buf[0] & 255u);
    CHECK(wide_px >= 125 && wide_px <= 131,
          "wide magnified A8 mask (padded right=36000 > 32767) clips in Q30 and renders pixel (0,0) ~128/255");

    /* Extreme wide A8 mask [0, 16384) x [0, 1) with 16384x horizontal scale (padded right = 402,653,184 > 2^28):
       __int128 forward bounds clipping keeps visible pixel (0,0) at ~128/255 without overflow drop. */
    memset(sing_buf, 0, (size_t)(S * S) * sizeof(uint32_t));
    im_transform mag16k = im_transform_scale(FX(16384), IM_ONE);
    im_surface s_huge = {sing_buf, S, S, S, pal, IM_ONE, NULL, &mag16k};
    im_draw_alpha_mask(&s_huge, (im_rect){0, 0, FX(16384), IM_ONE}, one_px, 1, 1, 1, 0xFFFFFFFFu);
    int huge_px = (int)(sing_buf[0] & 255u);
    CHECK(huge_px >= 125 && huge_px <= 131,
          "extreme magnified A8 mask (padded right=402M > IM_TR_MAX_COEFF) clips in __int128 and renders (0,0) ~128/255");

    /* Wide zoomed shape AND picture: [0, 4096) x [0, 1) at 8x horizontal zoom (right = 32768 > 16.16 max)
       must render pixel (0, 0) fully opaque through both im_fill_argb and im_picture_draw. */
    memset(sing_buf, 0, (size_t)(S * S) * sizeof(uint32_t));
    im_surface s_zoom8 = {sing_buf, S, S, S, pal, IM_ONE, NULL, &mag8x};
    im_fill_argb(&s_zoom8, (im_rect){0, 0, FX(4096), IM_ONE}, (im_radii){0, 0, 0, 0}, 0xFF336699u);
    CHECK(sing_buf[0] == 0x00336699u, "wide 8x zoomed shape (right=32768) renders pixel (0,0) opaque");

    memset(sing_buf, 0, (size_t)(S * S) * sizeof(uint32_t));
    uint32_t opq_px[1] = {0xFF44AA22u};
    im_picture opq_pic = {opq_px, 1, 1, 1};
    CHECK(im_picture_draw(&s_zoom8, &opq_pic, (im_rect){0, 0, FX(4096), IM_ONE}, IM_STRETCH_FILL),
          "wide 8x zoomed picture (right=32768) succeeds");
    CHECK(sing_buf[0] == 0x0044AA22u, "wide 8x zoomed picture (right=32768) renders pixel (0,0) opaque");

    /* 30-degree rotated gradient STROKE: compare two stroke points at the SAME device Y (y=32)
       so a device-space gradient (same device Y => identical color) cannot pass! */
    const int GS = 64;
    uint32_t *gbuf = (uint32_t *)heap_alloc_zero((size_t)(GS * GS) * sizeof(uint32_t));
    CHECK(gbuf != NULL, "alloc gradient buffer");
    if (gbuf) {
        im_transform r30 = im_transform_rotate_at(FX(90), FX(32), FX(32));
        im_surface s_grad = {gbuf, GS, GS, GS, pal, IM_ONE, NULL, &r30};
        im_stroke(&s_grad, (im_rect){FX(16), FX(16), FX(48), FX(48)}, (im_radii){FX(4), FX(4), FX(4), FX(4)},
                  FX(4), IM_BRUSH_ButtonBorderBrush);
        /* Local top edge (y=18) rotates 90 deg CW around (32,32) to device (46, 32).
           Local bottom edge (y=46) rotates 90 deg CW around (32,32) to device (18, 32).
           Both share the exact same device Y = 32, so only a local-space gradient differs! */
        uint32_t right_px = gbuf[32 * GS + 46] & 0xFFu;
        uint32_t left_px = gbuf[32 * GS + 18] & 0xFFu;
        CHECK(right_px > left_px, "rotated gradient stroke follows local shape axis at equal device Y");

        /* Skewed rounded fill renders non-empty interior and leaves far outside untouched. */
        memset(gbuf, 0, (size_t)(GS * GS) * sizeof(uint32_t));
        im_transform sk;
        CHECK(im_transform_skew(FX(25), 0, &sk), "25-deg skew builds");
        im_surface s_skew = {gbuf, GS, GS, GS, pal, IM_ONE, NULL, &sk};
        im_fill_argb(&s_skew, (im_rect){FX(10), FX(10), FX(30), FX(30)}, (im_radii){FX(4), FX(4), FX(4), FX(4)}, 0xFF33AA55u);
        /* At y=25, x_dev = x_local + 25 * tan(25 deg) ~= x_local + 11.66 => x_local=20 maps to x_dev~=32. */
        CHECK(gbuf[25 * GS + 32] == 0x0033AA55u && gbuf[25 * GS + 12] == 0x00000000u,
              "skewed rounded fill shifts interior along skew axis");
        heap_free_ptr(gbuf);
    }

    /* Singular transform leaves destination byte-for-byte identical. */
    for (int i = 0; i < S * S; i++) sing_buf[i] = 0x00123456u;
    uint32_t *sing_ref = (uint32_t *)heap_alloc_zero((size_t)(S * S) * sizeof(uint32_t));
    for (int i = 0; i < S * S; i++) sing_ref[i] = 0x00123456u;
    im_transform ztr = im_transform_scale(0, 0);
    im_surface s_sing = {sing_buf, S, S, S, pal, IM_ONE, NULL, &ztr};
    im_fill_argb(&s_sing, (im_rect){0, 0, FX(S), FX(S)}, (im_radii){0, 0, 0, 0}, 0xFFFF0000u);
    im_draw_alpha_mask(&s_sing, (im_rect){0, 0, FX(8), FX(8)}, glyph, 8, 8, 8, 0xFFFF0000u);
    CHECK(bytes_eq(sing_buf, sing_ref, (size_t)(S * S) * sizeof(uint32_t)),
          "singular transform leaves destination byte-identical");

    heap_free_ptr(sing_ref);
    heap_free_ptr(base);
    heap_free_ptr(rot90);
    heap_free_ptr(sing_buf);
}

/* 3. Premultiplied pictures (all 4 stretch modes + padded stride + compositor group opacity & fallback). */
static void test_pictures_and_compositor(void) {
    /* Asymmetric 6x4 translucent straight BGRA image with padded byte stride (6*4 + 12 = 36 bytes). */
    const int SW = 6, SH = 4;
    const uint32_t SRC_STRIDE_BYTES = 36u;
    const int PIC_STRIDE_PX = SW + 5; /* padded pixel stride */
    uint8_t src_bytes[4 * 36];
    memset(src_bytes, 0xCC, sizeof(src_bytes));
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            uint8_t *p = src_bytes + y * (int)SRC_STRIDE_BYTES + x * 4;
            uint8_t b = (uint8_t)(20 + x * 35 + y * 15);
            uint8_t g = (uint8_t)(40 + x * 18 + y * 35);
            uint8_t r = (uint8_t)(180 - x * 20 + y * 20);
            uint8_t a = (uint8_t)(110 + x * 18 + y * 12);
            p[0] = b; p[1] = g; p[2] = r; p[3] = a;
        }
    }
    uint32_t pic_px[4 * 11];
    for (int i = 0; i < 4 * 11; i++) pic_px[i] = 0xDEADBEEFu;
    CHECK(im_picture_premultiply_straight(src_bytes, SW, SH, SRC_STRIDE_BYTES, false, pic_px, PIC_STRIDE_PX),
          "premultiply padded-stride BGRA8 succeeds");
    im_picture pic = {pic_px, SW, SH, PIC_STRIDE_PX};

    /* Invalid inputs refused cleanly. */
    im_palette pal = make_dark_palette();
    uint32_t dummy[16];
    im_surface s_dummy = {dummy, 4, 4, 4, pal, IM_ONE, NULL, NULL};
    im_picture bad_pic = {pic_px, SW, SH, SW - 1};
    CHECK(!im_picture_draw(&s_dummy, &bad_pic, (im_rect){0, 0, FX(4), FX(4)}, IM_STRETCH_FILL),
          "invalid stride picture refused");
    CHECK(!im_picture_draw(&s_dummy, NULL, (im_rect){0, 0, FX(4), FX(4)}, IM_STRETCH_FILL),
          "NULL picture refused");

    /* Draw all 4 stretch modes into 18x14 canvas with dst_rect [2, 16) x [2, 12) (14x10)
       and sentinel pixels outside dst_rect, dumping all pixels for Python's exact double-precision oracle. */
    const int CW = 18, CH = 14;
    uint32_t canvas[18 * 14];
    const uint32_t SENTINEL = 0x00192837u;
    im_rect dst = {FX(2), FX(2), FX(16), FX(12)};

    for (int mode = 0; mode < 4; mode++) {
        for (int i = 0; i < CW * CH; i++) canvas[i] = SENTINEL;
        im_surface s = {canvas, CW, CH, CW, pal, IM_ONE + (IM_ONE / 2), NULL, NULL}; /* 150% DPI */
        CHECK(im_picture_draw(&s, &pic, dst, (im_stretch)mode), "im_picture_draw succeeds");
        write_str(STD_OUTPUT_HANDLE, "PIC ");
        write_i64(STD_OUTPUT_HANDLE, mode);
        for (int i = 0; i < CW * CH; i++) {
            write_str(STD_OUTPUT_HANDLE, " ");
            write_hex32(STD_OUTPUT_HANDLE, canvas[i]);
        }
        write_str(STD_OUTPUT_HANDLE, "\n");
    }

    /* Regression: IM_STRETCH_NONE through a 2x node transform must NOT double-scale to 4px. */
    {
        uint32_t one_src[1] = {0xFF00FF00u};
        im_picture one_p = {one_src, 1, 1, 1};
        uint32_t out_2x[8 * 8];
        memset(out_2x, 0, sizeof(out_2x));
        im_transform tr2x = im_transform_scale(FX(2), FX(2));
        im_surface s2x = {out_2x, 8, 8, 8, pal, FX(2), NULL, &tr2x};
        CHECK(im_picture_draw(&s2x, &one_p, (im_rect){0, 0, FX(4), FX(4)}, IM_STRETCH_NONE), "2x transformed NONE draws");
        int lit = 0;
        for (int i = 0; i < 64; i++) if (out_2x[i] == 0x0000FF00u) lit++;
        CHECK(lit == 4, "1x1 picture with IM_STRETCH_NONE through 2x node transform occupies exactly 2x2=4 device pixels");
    }

    /* Regression: UniformToFill with wide 1000x1 strip into 100x100 thumbnail succeeds cleanly. */
    {
        uint32_t *strip = (uint32_t *)heap_alloc_zero(1000u * sizeof(uint32_t));
        uint32_t *thumb = (uint32_t *)heap_alloc_zero(100u * 100u * sizeof(uint32_t));
        if (strip && thumb) {
            for (int i = 0; i < 1000; i++) strip[i] = (i >= 450 && i < 550) ? 0xFF11AA22u : 0xFFAA1122u;
            im_picture sp = {strip, 1000, 1, 1000};
            im_surface st = {thumb, 100, 100, 100, pal, IM_ONE, NULL, NULL};
            CHECK(im_picture_draw(&st, &sp, (im_rect){0, 0, FX(100), FX(100)}, IM_STRETCH_UNIFORM_TO_FILL),
                  "UniformToFill 1000x1 strip into 100x100 succeeds");
            CHECK(thumb[50 * 100 + 50] == 0x0011AA22u, "UniformToFill samples centered crop of 1000x1 strip");
        }
        heap_free_ptr(strip);
        heap_free_ptr(thumb);
    }

    /* Premultiplied compositor test: transformed half-transparent red picture + overlapping A8 mask
       inside a fractional rounded clip in offscreen opacity group AND forced fallback over two backdrops. */
    uint32_t red_px[4] = {0x80800000u, 0x80800000u, 0x80800000u, 0x80800000u};
    im_picture red_pic = {red_px, 2, 2, 2};
    uint8_t tiny_mask[4] = {255, 255, 255, 255};
    im_allocator alloc = {test_alloc_fn, test_realloc_fn, test_free_fn, NULL};
    im_transform pic_tr = im_transform_translate(IM_ONE / 4, IM_ONE / 4);

    for (int backdrop_i = 0; backdrop_i < 2; backdrop_i++) {
        uint32_t bg = (backdrop_i == 0) ? 0x00000000u : 0x00FFFFFFu;
        uint32_t off_buf[16 * 16], fb_buf[16 * 16];
        for (int i = 0; i < 256; i++) { off_buf[i] = bg; fb_buf[i] = bg; }

        im_surface s_off = {off_buf, 16, 16, 16, pal, IM_ONE, NULL, &pic_tr};
        im_layer_ctx l_off;
        CHECK(im_layer_init(&l_off, &s_off, alloc, 1024 * 1024), "init offscreen layer");
        im_radii cr = {FX(3), FX(3), FX(3), FX(3)};
        CHECK(im_layer_push(&l_off, (im_rect){FX(2) + IM_ONE / 2, FX(2) + IM_ONE / 2, FX(14), FX(14)}, 128, true, cr) == IM_LAYER_OK,
              "push offscreen opacity group with fractional clip");
        CHECK(im_picture_draw(&s_off, &red_pic, (im_rect){FX(2), FX(2), FX(14), FX(14)}, IM_STRETCH_FILL),
              "draw transformed premul pic in offscreen group");
        im_draw_alpha_mask(&s_off, (im_rect){FX(2), FX(2), FX(6), FX(6)}, tiny_mask, 2, 2, 2, 0x8000FF00u);
        im_layer_pop(&l_off);
        im_layer_destroy(&l_off);

        im_surface s_fb = {fb_buf, 16, 16, 16, pal, IM_ONE, NULL, &pic_tr};
        im_layer_ctx l_fb;
        CHECK(im_layer_init(&l_fb, &s_fb, alloc, 0), "init fallback layer");
        CHECK(im_layer_push(&l_fb, (im_rect){FX(2) + IM_ONE / 2, FX(2) + IM_ONE / 2, FX(14), FX(14)}, 128, true, cr) == IM_LAYER_FALLBACK,
              "push forced fallback group with fractional clip");
        CHECK(im_picture_draw(&s_fb, &red_pic, (im_rect){FX(2), FX(2), FX(14), FX(14)}, IM_STRETCH_FILL),
              "draw transformed premul pic in fallback group");
        im_draw_alpha_mask(&s_fb, (im_rect){FX(2), FX(2), FX(6), FX(6)}, tiny_mask, 2, 2, 2, 0x8000FF00u);
        im_layer_pop(&l_fb);
        im_layer_destroy(&l_fb);

        /* Center pixel (8, 8) (picture only): avoids double-premultiplication.
           Fractional rounded-clip corner pixel (3, 3): clipped and attenuated by both modes. */
        uint32_t p_off = off_buf[8 * 16 + 8];
        uint32_t p_fb = fb_buf[8 * 16 + 8];
        int ro = (int)((p_off >> 16) & 255u), go = (int)((p_off >> 8) & 255u);
        int rf = (int)((p_fb >> 16) & 255u), gf = (int)((p_fb >> 8) & 255u);
        if (backdrop_i == 0) {
            CHECK(ro >= 63 && ro <= 65 && go == 0, "offscreen premul red over black avoids double-premul");
            CHECK(rf >= 63 && rf <= 65 && gf == 0, "fallback premul red over black avoids double-premul");
            CHECK(((off_buf[4 * 16 + 4] >> 8) & 255u) > 0u && ((fb_buf[4 * 16 + 4] >> 8) & 255u) > 0u,
                  "overlapping transformed A8 mask renders inside fractional clip in both modes");
        } else {
            CHECK(ro >= 253 && go >= 190 && go <= 192, "offscreen premul red over white matches exact alpha");
            CHECK(rf >= 253 && gf >= 190 && gf <= 192, "fallback premul red over white matches exact alpha");
        }
    }
}

/* 4. Nine-slice picture corner preservation across 5 scales + compressed destination. */
static void test_nine_slice(void) {
    /* 12x12 source with insets (3, 3, 3, 3): 9 completely distinct solid colors. */
    const int SW = 12, SH = 12;
    uint32_t src[12 * 12];
    const uint32_t COL_TL = 0xFFFF0000u, COL_T = 0xFF00FF00u, COL_TR = 0xFF0000FFu;
    const uint32_t COL_L  = 0xFFFFFF00u, COL_C = 0xFF00FFFFu, COL_R  = 0xFFFF00FFu;
    const uint32_t COL_BL = 0xFFAA4400u, COL_B = 0xFF44AA00u, COL_BR = 0xFF0044AAu;
    for (int y = 0; y < SH; y++) {
        int gy = (y < 3) ? 0 : (y >= 9 ? 2 : 1);
        for (int x = 0; x < SW; x++) {
            int gx = (x < 3) ? 0 : (x >= 9 ? 2 : 1);
            uint32_t c = COL_C;
            if (gy == 0 && gx == 0) c = COL_TL;
            else if (gy == 0 && gx == 1) c = COL_T;
            else if (gy == 0 && gx == 2) c = COL_TR;
            else if (gy == 1 && gx == 0) c = COL_L;
            else if (gy == 1 && gx == 2) c = COL_R;
            else if (gy == 2 && gx == 0) c = COL_BL;
            else if (gy == 2 && gx == 1) c = COL_B;
            else if (gy == 2 && gx == 2) c = COL_BR;
            src[y * SW + x] = c;
        }
    }
    im_picture pic = {src, SW, SH, SW};
    im_nine_slice slice = {3, 3, 3, 3};
    im_palette pal = make_dark_palette();

    const im_fixed scales[5] = {
        IM_ONE,
        IM_ONE + IM_ONE / 4,
        IM_ONE + IM_ONE / 2,
        2 * IM_ONE,
        3 * IM_ONE
    };
    const int expected_corner[5] = {3, 4, 5, 6, 9}; /* round(3 * scale) */

    const int DW = 48, DH = 40;
    uint32_t *dst = (uint32_t *)heap_alloc_zero((size_t)(DW * DH) * sizeof(uint32_t));
    CHECK(dst != NULL, "alloc nine-slice dst");
    if (!dst) return;

    for (int si = 0; si < 5; si++) {
        memset(dst, 0, (size_t)(DW * DH) * sizeof(uint32_t));
        im_surface s = {dst, DW, DH, DW, pal, scales[si], NULL, NULL};
        CHECK(im_picture_draw_nine_slice(&s, &pic, (im_rect){0, 0, FX(DW), FX(DH)}, slice), "nine-slice draws");
        int csz = expected_corner[si];
        /* Check exact boundary pixels: inside TL corner vs immediately across into T and L edges,
           and zero bleed from adjacent slices! */
        CHECK(dst[(csz - 1) * DW + (csz - 1)] == (COL_TL & 0x00FFFFFFu), "nine-slice TL corner exact boundary");
        CHECK(dst[(csz - 1) * DW + csz] == (COL_T & 0x00FFFFFFu), "nine-slice T edge starts cleanly at csz");
        CHECK(dst[csz * DW + (csz - 1)] == (COL_L & 0x00FFFFFFu), "nine-slice L edge starts cleanly at csz");
        CHECK(dst[csz * DW + csz] == (COL_C & 0x00FFFFFFu), "nine-slice center starts cleanly at (csz, csz)");
        CHECK(dst[(DH - csz) * DW + (DW - csz)] == (COL_BR & 0x00FFFFFFu), "nine-slice BR corner exact boundary");
    }

    /* Asymmetric compressed destination (insets 2,2,6,4 in 8x6 box at 2x => dl=2, dr=6; dt=2, db=4, NOT equal split). */
    {
        uint32_t asrc[12 * 12];
        for (int y = 0; y < 12; y++) {
            int gy = (y < 2) ? 0 : (y >= 8 ? 2 : 1);
            for (int x = 0; x < 12; x++) {
                int gx = (x < 2) ? 0 : (x >= 6 ? 2 : 1);
                uint32_t c = COL_C;
                if (gy == 0 && gx == 0) c = COL_TL;
                else if (gy == 0 && gx == 2) c = COL_TR;
                else if (gy == 2 && gx == 0) c = COL_BL;
                else if (gy == 2 && gx == 2) c = COL_BR;
                asrc[y * 12 + x] = c;
            }
        }
        im_picture apic = {asrc, 12, 12, 12};
        memset(dst, 0, (size_t)(DW * DH) * sizeof(uint32_t));
        im_nine_slice asym_slice = {2, 2, 6, 4};
        im_surface s_small = {dst, DW, DH, DW, pal, 2 * IM_ONE, NULL, NULL};
        CHECK(im_picture_draw_nine_slice(&s_small, &apic, (im_rect){0, 0, FX(8), FX(6)}, asym_slice),
              "asymmetric compressed nine-slice draws");
        CHECK(dst[1 * DW + 1] == (COL_TL & 0x00FFFFFFu) && dst[1 * DW + 2] == (COL_TR & 0x00FFFFFFu) &&
              dst[2 * DW + 1] == (COL_BL & 0x00FFFFFFu) && dst[2 * DW + 2] == (COL_BR & 0x00FFFFFFu),
              "asymmetric compressed nine-slice shrinks corners proportionally (2:6 and 2:4), not equally");
    }

    /* Patterned corner texel check: non-solid 4x4 TL corner preserves exact pixel pattern at 100% scale. */
    {
        uint32_t pat[12 * 12];
        for (int y = 0; y < 12; y++)
            for (int x = 0; x < 12; x++)
                pat[y * 12 + x] = 0xFF000000u | ((uint32_t)(x * 20) << 16) | ((uint32_t)(y * 20) << 8) | 0x55u;
        im_picture ppic = {pat, 12, 12, 12};
        memset(dst, 0, (size_t)(DW * DH) * sizeof(uint32_t));
        im_surface sp = {dst, DW, DH, DW, pal, IM_ONE, NULL, NULL};
        CHECK(im_picture_draw_nine_slice(&sp, &ppic, (im_rect){0, 0, FX(30), FX(30)}, (im_nine_slice){4, 4, 4, 4}),
              "patterned nine-slice draws");
        CHECK(dst[0 * DW + 0] == (pat[0] & 0x00FFFFFFu) &&
              dst[2 * DW + 3] == (pat[2 * 12 + 3] & 0x00FFFFFFu) &&
              dst[29 * DW + 29] == (pat[11 * 12 + 11] & 0x00FFFFFFu),
              "patterned nine-slice preserves exact corner texels without distortion");
    }

    heap_free_ptr(dst);
}

/* 5. Shared node-to-device transform contract: painting + dirty rects + pointer hit-testing. */
static void paint_node_transformed(im_surface *surf, const im_node *node, im_fixed scale, uint32_t color) {
    if (!node || node->visibility == IM_VIS_COLLAPSED) return;
    im_transform dev_tr;
    if (!im_node_device_transform(node, scale, &dev_tr)) return;
    im_fixed w = (im_fixed)((int64_t)node->absolute_rect.right - node->absolute_rect.left);
    im_fixed h = (im_fixed)((int64_t)node->absolute_rect.bottom - node->absolute_rect.top);
    if (w <= 0 || h <= 0) return;
    const im_transform *prev = surf->transform;
    surf->transform = &dev_tr;
    im_fill_argb(surf, (im_rect){0, 0, w, h}, (im_radii){0, 0, 0, 0}, color);
    surf->transform = prev;
}

static void test_shared_transform_and_input(void) {
    im_allocator alloc = {test_alloc_fn, test_realloc_fn, test_free_fn, NULL};
    im_tree tree;
    im_tree_init(&tree, alloc);

    const int W = 180, H = 120;
    const im_fixed SCALE = IM_ONE + IM_ONE / 2; /* 150% DPI */
    im_node *root = im_tree_create_node(&tree, IM_ELEM_GRID, "root");
    root->flow_direction = IM_FLOW_RTL;
    im_node *parent = im_tree_create_node(&tree, IM_ELEM_GRID, "parent");
    parent->h_align = IM_ALIGN_START;
    parent->v_align = IM_ALIGN_START;
    parent->width = FX(40);
    parent->height = FX(30);
    parent->margin = (im_thickness){FX(10), FX(10), 0, 0};

    im_node *child = im_tree_create_node(&tree, IM_ELEM_BORDER, "child");
    child->h_align = IM_ALIGN_START;
    child->v_align = IM_ALIGN_START;
    child->width = FX(30);
    child->height = FX(20);
    child->margin = (im_thickness){FX(5), FX(5), 0, 0};

    im_node *sibling = im_tree_create_node(&tree, IM_ELEM_BORDER, "sibling");
    sibling->h_align = IM_ALIGN_START;
    sibling->v_align = IM_ALIGN_START;
    sibling->width = FX(20);
    sibling->height = FX(20);
    sibling->margin = (im_thickness){FX(20), FX(10), 0, 0};

    im_node *collapsed = im_tree_create_node(&tree, IM_ELEM_BORDER, "collapsed");
    collapsed->visibility = IM_VIS_COLLAPSED;
    collapsed->width = FX(100);
    collapsed->height = FX(100);

    im_node *singular_n = im_tree_create_node(&tree, IM_ELEM_BORDER, "singular");
    singular_n->width = FX(100);
    singular_n->height = FX(100);
    im_transform zero_tr = im_transform_scale(0, 0);
    im_node_set_render_transform(singular_n, &zero_tr, 0, 0);

    im_tree_append_child(&tree, root, parent);
    im_tree_append_child(&tree, parent, child);
    im_tree_append_child(&tree, root, sibling);
    im_tree_append_child(&tree, root, collapsed);
    im_tree_append_child(&tree, root, singular_n);

    im_layout_run(&tree, root, FX(120), FX(80), SCALE);

    /* Rotate child 45 deg around center (0.5, 0.5) and translate it outside parent bounds! */
    im_transform tr_child;
    CHECK(im_transform_composite(0, 0, IM_ONE, IM_ONE, 0, 0, FX(45), FX(-20), FX(25), &tr_child), "child composite");
    im_node_set_render_transform(child, &tr_child, IM_ONE / 2, IM_ONE / 2);

    /* Paint shared scene into full and partial framebuffers at 150% DPI. */
    uint32_t *pix_part = (uint32_t *)heap_alloc_zero((size_t)(W * H) * sizeof(uint32_t));
    uint32_t *pix_full = (uint32_t *)heap_alloc_zero((size_t)(W * H) * sizeof(uint32_t));
    im_palette pal = make_dark_palette();
    im_surface s_part = {pix_part, W, H, W, pal, SCALE, NULL, NULL};
    im_surface s_full = {pix_full, W, H, W, pal, SCALE, NULL, NULL};
    im_layer_ctx l_part;
    CHECK(im_layer_init(&l_part, &s_part, alloc, 1024 * 1024), "init shared scene layer");

    im_dirty_list dirty;
    im_dirty_init(&dirty, W, H);
    im_dirty_collect_tree(&dirty, &tree, root, SCALE);
    im_fill_argb(&s_part, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFF101010u);
    paint_node_transformed(&s_part, child, SCALE, 0xFF22CC88u);
    paint_node_transformed(&s_part, sibling, SCALE, 0xFFCC4422u);
    memcpy(pix_full, pix_part, (size_t)(W * H) * sizeof(uint32_t));
    im_dirty_clear(&dirty);

    /* Hit-test center of transformed child: must hit child (extending outside parent) AND match painted pixel! */
    im_transform c_dev;
    CHECK(im_node_device_transform(child, SCALE, &c_dev), "child device transform");
    im_fixed dev_cx = 0, dev_cy = 0;
    CHECK(im_transform_point(&c_dev, FX(15), FX(10), &dev_cx, &dev_cy), "child center in device coords");
    im_fixed hit_lx = 0, hit_ly = 0;
    im_node *hit = im_input_hit_test(root, SCALE, dev_cx, dev_cy, &hit_lx, &hit_ly);
    CHECK(hit == child, "hit-test at transformed child center hits child (even outside parent)");
    CHECK(pix_part[(dev_cy >> 16) * W + (dev_cx >> 16)] == 0x0022CC88u,
          "visible painted pixel at hit-test coordinate matches child color");

    /* Replace, clear, and detach transformed child: partial redraw equals full redraw byte-for-byte. */
    im_transform tr_child2 = im_transform_translate(FX(15), FX(10));
    im_node_set_render_transform(child, &tr_child2, IM_ONE / 2, IM_ONE / 2);
    im_dirty_collect_tree(&dirty, &tree, root, SCALE);
    for (uint32_t i = 0; i < dirty.count; i++) {
        CHECK(im_dirty_push_clip(&l_part, dirty.rects[i]), "push replace clip");
        im_fill_argb(&s_part, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFF101010u);
        paint_node_transformed(&s_part, child, SCALE, 0xFF22CC88u);
        paint_node_transformed(&s_part, sibling, SCALE, 0xFFCC4422u);
        im_layer_pop_clip(&l_part);
    }
    im_fill_argb(&s_full, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFF101010u);
    paint_node_transformed(&s_full, child, SCALE, 0xFF22CC88u);
    paint_node_transformed(&s_full, sibling, SCALE, 0xFFCC4422u);
    CHECK(bytes_eq(pix_part, pix_full, (size_t)(W * H) * sizeof(uint32_t)),
          "partial redraw after replacing RenderTransform equals full redraw");
    im_dirty_clear(&dirty);

    im_node_clear_render_transform(child);
    im_dirty_collect_tree(&dirty, &tree, root, SCALE);
    for (uint32_t i = 0; i < dirty.count; i++) {
        CHECK(im_dirty_push_clip(&l_part, dirty.rects[i]), "push clear clip");
        im_fill_argb(&s_part, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFF101010u);
        paint_node_transformed(&s_part, child, SCALE, 0xFF22CC88u);
        paint_node_transformed(&s_part, sibling, SCALE, 0xFFCC4422u);
        im_layer_pop_clip(&l_part);
    }
    im_fill_argb(&s_full, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFF101010u);
    paint_node_transformed(&s_full, child, SCALE, 0xFF22CC88u);
    paint_node_transformed(&s_full, sibling, SCALE, 0xFFCC4422u);
    CHECK(bytes_eq(pix_part, pix_full, (size_t)(W * H) * sizeof(uint32_t)),
          "partial redraw after clearing RenderTransform equals full redraw");
    im_dirty_clear(&dirty);

    /* Detach transformed child and verify partial redraw equals full redraw byte-for-byte. */
    im_node_set_render_transform(child, &tr_child, IM_ONE / 2, IM_ONE / 2);
    im_dirty_collect_tree(&dirty, &tree, root, SCALE);
    im_fill_argb(&s_part, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFF101010u);
    paint_node_transformed(&s_part, child, SCALE, 0xFF22CC88u);
    paint_node_transformed(&s_part, sibling, SCALE, 0xFFCC4422u);
    memcpy(pix_full, pix_part, (size_t)(W * H) * sizeof(uint32_t));
    im_dirty_clear(&dirty);

    im_tree_remove_child(&tree, parent, child);
    im_dirty_collect_tree(&dirty, &tree, root, SCALE);
    for (uint32_t i = 0; i < dirty.count; i++) {
        CHECK(im_dirty_push_clip(&l_part, dirty.rects[i]), "push detach clip");
        im_fill_argb(&s_part, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFF101010u);
        paint_node_transformed(&s_part, sibling, SCALE, 0xFFCC4422u);
        im_layer_pop_clip(&l_part);
    }
    im_fill_argb(&s_full, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFF101010u);
    paint_node_transformed(&s_full, sibling, SCALE, 0xFFCC4422u);
    CHECK(bytes_eq(pix_part, pix_full, (size_t)(W * H) * sizeof(uint32_t)),
          "partial redraw after detaching transformed child equals full redraw");
    im_tree_append_child(&tree, parent, child);
    im_layout_run(&tree, root, FX(120), FX(80), SCALE);
    im_dirty_clear(&dirty);

    /* Q30 half-open boundary regression: 1-unit-wide node at 2x scale must hit at 2*IM_ONE - 1. */
    {
        im_node *tiny_n = im_tree_create_node(&tree, IM_ELEM_BORDER, "tiny");
        tiny_n->h_align = IM_ALIGN_START;
        tiny_n->v_align = IM_ALIGN_START;
        tiny_n->width = IM_ONE;
        tiny_n->height = IM_ONE;
        im_layout_run(&tree, tiny_n, FX(10), FX(10), FX(2));
        CHECK(im_input_hit_test(tiny_n, FX(2), 2 * IM_ONE - 1, IM_ONE, NULL, NULL) == tiny_n,
              "half-open Q30 hit-test hits 1-unit node at 2*IM_ONE - 1");
        CHECK(im_input_hit_test(tiny_n, FX(2), 2 * IM_ONE, IM_ONE, NULL, NULL) == NULL,
              "half-open Q30 hit-test misses 1-unit node at 2*IM_ONE");
        im_tree_destroy_subtree(&tree, tiny_n);
    }

    /* Compiled ToggleSwitch template: RenderTransformOrigin + KnobTranslateTransform.X feed into
       im_node_device_transform, im_dirty_collect_tree, and im_input_hit_test. */
    {
        im_tree ts_tree;
        im_tree_init(&ts_tree, alloc);
        im_node *ts_root = im_tree_instantiate_template(&ts_tree, &pal, IM_TEMPLATE_TOGGLE_SWITCH, "ts");
        CHECK(ts_root != NULL, "instantiate ToggleSwitch template");
        if (ts_root) {
            im_layout_run(&ts_tree, ts_root, FX(80), FX(40), IM_ONE);
            im_state_machine sm;
            im_states_init(&sm, IM_TEMPLATE_TOGGLE_SWITCH, &pal);
            im_states_go_to(&sm, "Off", 0);
            im_states_apply_to_tree(&sm, ts_root, 0);
            im_node *knob = im_tree_find_by_name(ts_root, "SwitchKnob");
            im_node *knob_on = im_tree_find_by_name(ts_root, "SwitchKnobOn");
            CHECK(knob_on != NULL && knob_on->render_transform_origin_x == IM_ONE / 2 &&
                  knob_on->render_transform_origin_y == IM_ONE / 2,
                  "ToggleSwitch SwitchKnobOn consumes compiled RenderTransformOrigin (0.5, 0.5)");
            im_dirty_list ts_dirty;
            im_dirty_init(&ts_dirty, 80, 40);
            im_dirty_collect_tree(&ts_dirty, &ts_tree, ts_root, IM_ONE);
            im_dirty_clear(&ts_dirty);

            im_states_go_to(&sm, "On", 0);
            im_states_apply_to_tree(&sm, ts_root, 500);
            CHECK(knob != NULL && knob->has_render_transform,
                  "ToggleSwitch On applies KnobTranslateTransform to SwitchKnob");
            im_dirty_collect_tree(&ts_dirty, &ts_tree, ts_root, IM_ONE);
            CHECK(ts_dirty.count > 0u, "ToggleSwitch animated transform emits dirty damage");

            /* Regression: clearing a RenderTransform on identity must preserve RenderTransformOrigin=(0.5,0.5)
               across replay of a scale animation. */
            if (knob_on) {
                im_fixed kw = knob_on->absolute_rect.right - knob_on->absolute_rect.left;
                im_fixed kh = knob_on->absolute_rect.bottom - knob_on->absolute_rect.top;
                im_transform dev_base;
                im_fixed base_cx = 0, base_cy = 0;
                CHECK(im_node_device_transform(knob_on, IM_ONE, &dev_base) &&
                      im_transform_point(&dev_base, kw / 2, kh / 2, &base_cx, &base_cy),
                      "baseline center maps before scale playback");

                im_transform sc2 = im_transform_scale(FX(2), FX(2));
                im_node_set_render_transform(knob_on, &sc2, knob_on->render_transform_origin_x, knob_on->render_transform_origin_y);
                im_node_clear_render_transform(knob_on);
                CHECK(knob_on->render_transform_origin_x == IM_ONE / 2 &&
                      knob_on->render_transform_origin_y == IM_ONE / 2,
                      "im_node_clear_render_transform preserves declared RenderTransformOrigin (0.5, 0.5)");
                im_node_set_render_transform(knob_on, &sc2, knob_on->render_transform_origin_x, knob_on->render_transform_origin_y);
                im_transform dev2;
                im_fixed px_c = 0, py_c = 0;
                CHECK(im_node_device_transform(knob_on, IM_ONE, &dev2) &&
                      im_transform_point(&dev2, kw / 2, kh / 2, &px_c, &py_c),
                      "center maps on second playback");
                CHECK(px_c == base_cx && py_c == base_cy,
                      "second scale playback pivots around preserved (0.5, 0.5) center");
            }
            im_tree_destroy_subtree(&ts_tree, ts_root);
        }
    }

    /* Half-open local boundary: (0,0) hits, (w, h) does not hit. */
    im_transform sib_dev;
    CHECK(im_node_device_transform(sibling, SCALE, &sib_dev), "sibling device transform");
    im_fixed sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
    im_transform_point(&sib_dev, 0, 0, &sx0, &sy0);
    im_transform_point(&sib_dev, FX(20), FX(20), &sx1, &sy1);
    CHECK(im_input_hit_test(root, SCALE, sx0, sy0, NULL, NULL) == sibling, "half-open (0,0) hits sibling");
    CHECK(im_input_hit_test(root, SCALE, sx1, sy1, NULL, NULL) != sibling, "half-open (w,h) misses sibling");

    /* 1,000 real im_input_hit_test calls on nested transformed nodes dumped for Python exact inverse oracle. */
    uint32_t rng = 0x5115CAFEu;
    im_node *ht_root = im_tree_create_node(&tree, IM_ELEM_GRID, "ht_root");
    im_node *ht_box = im_tree_create_node(&tree, IM_ELEM_BORDER, "ht_box");
    ht_box->use_layout_rounding = false;
    ht_box->h_align = IM_ALIGN_START;
    ht_box->v_align = IM_ALIGN_START;
    ht_box->width = FX(40);
    ht_box->height = FX(30);
    ht_box->margin = (im_thickness){FX(20), FX(15), 0, 0};
    im_tree_append_child(&tree, ht_root, ht_box);
    im_layout_run(&tree, ht_root, FX(120), FX(80), SCALE);

    for (int i = 0; i < 1000; i++) {
        im_fixed rot = (im_fixed)((int32_t)(rng_next(&rng) % 360u) - 180) * IM_ONE;
        im_fixed sc = (im_fixed)((int32_t)(rng_next(&rng) % 150u) + 50) * (IM_ONE / 100);
        im_fixed tx = (im_fixed)((int32_t)(rng_next(&rng) % 41u) - 20) * IM_ONE;
        im_fixed ty = (im_fixed)((int32_t)(rng_next(&rng) % 41u) - 20) * IM_ONE;
        im_transform tc;
        im_transform_composite(0, 0, sc, sc, 0, 0, rot, tx, ty, &tc);
        im_node_set_render_transform(ht_box, &tc, IM_ONE / 2, IM_ONE / 2);

        im_fixed px = (im_fixed)((int32_t)(rng_next(&rng) % 180u)) * IM_ONE + (IM_ONE / 2);
        im_fixed py = (im_fixed)((int32_t)(rng_next(&rng) % 120u)) * IM_ONE + (IM_ONE / 2);
        im_fixed lx = 0, ly = 0;
        im_node *got = im_input_hit_test(ht_root, SCALE, px, py, &lx, &ly);
        int hit_box = (got == ht_box) ? 1 : 0;
        write_str(STD_OUTPUT_HANDLE, "HIT ");
        write_i64(STD_OUTPUT_HANDLE, rot); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, sc); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, tx); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, ty); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, px); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, py); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, hit_box); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, lx); write_str(STD_OUTPUT_HANDLE, " ");
        write_i64(STD_OUTPUT_HANDLE, ly); write_str(STD_OUTPUT_HANDLE, "\n");
    }
    im_tree_destroy_subtree(&tree, ht_root);

    im_layer_destroy(&l_part);
    im_tree_destroy_subtree(&tree, root);
    heap_free_ptr(pix_part);
    heap_free_ptr(pix_full);
}

int im_main(void) {
    test_transforms();
    test_transformed_drawing();
    test_pictures_and_compositor();
    test_nine_slice();
    test_shared_transform_and_input();
    if (g_failures != 0) {
        write_str(STD_ERROR_HANDLE, "test_transform: FAILED\n");
        return 1;
    }
    write_str(STD_OUTPUT_HANDLE, "test_transform: ALL PASSED\n");
    return 0;
}
