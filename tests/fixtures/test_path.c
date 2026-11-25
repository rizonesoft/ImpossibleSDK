/* Tier-0 freestanding verification fixture for ui/path.{h,c}, ui/gradient.{h,c}, and compiled Path templates. */
#include <impossible/base.h>
#include <windows.h>
#include "gradient.h"
#include "input.h"
#include "layer.h"
#include "layout.h"
#include "path.h"
#include "states.h"
#include "template_data.h"
#include "tree.h"

#define FX(n) ((im_fixed)((int64_t)(n) * IM_ONE))

static int g_failures = 0;

static void write_str(DWORD handle_id, const char *s) {
    HANDLE h = GetStdHandle(handle_id);
    DWORD len = 0;
    while (s[len]) len++;
    DWORD written = 0;
    WriteFile(h, s, len, &written, NULL);
}

static void write_i64(DWORD handle_id, int64_t v) {
    char buf[32];
    int n = 0;
    uint64_t u;
    if (v < 0) {
        write_str(handle_id, "-");
        u = (uint64_t)(-(v + 1)) + 1u;
    } else {
        u = (uint64_t)v;
    }
    if (u == 0) {
        buf[n++] = '0';
    } else {
        while (u > 0) {
            buf[n++] = (char)('0' + (u % 10u));
            u /= 10u;
        }
    }
    char out[32];
    for (int i = 0; i < n; i++) out[i] = buf[n - 1 - i];
    out[n] = 0;
    write_str(handle_id, out);
}

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        write_str(STD_ERROR_HANDLE, "FAIL: " msg "\n"); \
        g_failures++; \
    } \
} while (0)

typedef struct heap_ctx {
    HANDLE heap;
    bool fail_next;
    size_t limit;
    size_t used;
} heap_ctx;

static void *h_alloc(void *vctx, size_t size) {
    heap_ctx *c = (heap_ctx *)vctx;
    if (c->fail_next) return NULL;
    if (c->limit > 0 && c->used + size > c->limit) return NULL;
    void *p = HeapAlloc(c->heap, 0, size ? size : 1);
    if (p) c->used += size;
    return p;
}

static void *h_realloc(void *vctx, void *ptr, size_t old_size, size_t new_size) {
    heap_ctx *c = (heap_ctx *)vctx;
    if (c->fail_next) return NULL;
    if (c->limit > 0 && c->used - old_size + new_size > c->limit) return NULL;
    void *p = ptr ? HeapReAlloc(c->heap, 0, ptr, new_size ? new_size : 1)
                  : HeapAlloc(c->heap, 0, new_size ? new_size : 1);
    if (p) c->used = c->used - old_size + new_size;
    return p;
}

static void h_free(void *vctx, void *ptr, size_t size) {
    heap_ctx *c = (heap_ctx *)vctx;
    if (ptr) {
        HeapFree(c->heap, 0, ptr);
        if (c->used >= size) c->used -= size;
        else c->used = 0;
    }
}

static uint32_t rng_next(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static int abs_i(int v) { return v < 0 ? -v : v; }

int im_main(void) {
    heap_ctx hctx = { GetProcessHeap(), false, 0u, 0u };
    im_allocator alloc = { h_alloc, h_realloc, h_free, &hctx };
    im_palette pal = { IM_DARK, { 0xFF0078D4u, 0xFF005A9Eu, 0xFF106EBEu, 0xFF4CC2FFu, 0xFF99EBFFu, 0xFF003E6Bu, 0xFF002050u } };

    /* 1. Known-answer analytic horizontal coverage test: x = 7/256 (0x0700 in 16.16) rounds to 7/255. */
    {
        uint32_t pix[4 * 2];
        for (int i = 0; i < 8; i++) pix[i] = 0x00000000u;
        im_surface s = { pix, 4, 2, 4, pal, IM_ONE, NULL, NULL };
        im_path p;
        im_path_init(&p, alloc, IM_FILL_NON_ZERO);
        im_fixed x_edge = (7 * IM_ONE) / 256; /* exact 7/256 */
        im_path_move_to(&p, 0, 0);
        im_path_cubic_to(&p, x_edge / 3, 0, (2 * x_edge) / 3, 0, x_edge, 0);
        im_path_line_to(&p, x_edge, FX(2));
        im_path_line_to(&p, 0, FX(2));
        im_path_close(&p);
        im_path_data pd = im_path_view(&p);
        CHECK(im_path_fill_argb(&s, alloc, &pd, 0xFFFFFFFFu), "fill 7/256 sliver");
        int r0 = (int)((pix[0] >> 16) & 0xFFu);
        CHECK(r0 == 7, "analytic horizontal coverage of 7/256 sliver equals 7 (not 0)");
        CHECK((pix[1] & 0x00FFFFFFu) == 0u, "pixel right of 7/256 sliver stays 0");
        im_path_destroy(&p);

        /* Subtle quadratic curve M0,0 Q0.0625,0.5 0,1 Z: chord has 0 coverage, curve has exact 5/255. */
        for (int i = 0; i < 8; i++) pix[i] = 0x00000000u;
        im_path pq;
        im_path_init(&pq, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&pq, 0, 0);
        im_path_quad_to(&pq, IM_ONE / 16, IM_ONE / 2, 0, IM_ONE);
        im_path_close(&pq);
        im_path_data pdq = im_path_view(&pq);
        CHECK(im_path_fill_argb(&s, alloc, &pdq, 0xFFFFFFFFu), "fill subtle quadratic M0,0 Q0.0625,0.5 0,1 Z");
        int rq0 = (int)((pix[0] >> 16) & 0xFFu);
        CHECK(rq0 == 5, "subtle quadratic M0,0 Q0.0625,0.5 0,1 Z covers exact 5/255");
        im_path_destroy(&pq);
    }

    /* 2. 1,000 random cubic and quadratic paths (500 EvenOdd, 500 NonZero) dumped on 16x16 for Python oracle. */
    {
        uint32_t rng = 0x13572468u;
        uint32_t pix[16 * 16];
        im_surface s = { pix, 16, 16, 16, pal, IM_ONE, NULL, NULL };
        for (int idx = 0; idx < 1000; idx++) {
            im_fill_rule rule = (idx & 1) ? IM_FILL_NON_ZERO : IM_FILL_EVEN_ODD;
            bool use_cubic = ((idx / 2) & 1) != 0;
            for (int i = 0; i < 256; i++) pix[i] = 0x00000000u;
            im_path p;
            im_path_init(&p, alloc, rule);
            /* Generate 3 curves forming a closed loop in [1..15] x [1..15] with 1/4-px coordinates. */
            im_fixed pts[10][2];
            int npts = use_cubic ? 9 : 6;
            for (int k = 0; k < npts; k++) {
                pts[k][0] = (im_fixed)(((rng_next(&rng) % 56u) + 4u) * (IM_ONE / 4));
                pts[k][1] = (im_fixed)(((rng_next(&rng) % 56u) + 4u) * (IM_ONE / 4));
            }
            im_path_move_to(&p, pts[0][0], pts[0][1]);
            if (use_cubic) {
                im_path_cubic_to(&p, pts[1][0], pts[1][1], pts[2][0], pts[2][1], pts[3][0], pts[3][1]);
                im_path_cubic_to(&p, pts[4][0], pts[4][1], pts[5][0], pts[5][1], pts[6][0], pts[6][1]);
                im_path_cubic_to(&p, pts[7][0], pts[7][1], pts[8][0], pts[8][1], pts[0][0], pts[0][1]);
            } else {
                im_path_quad_to(&p, pts[1][0], pts[1][1], pts[2][0], pts[2][1]);
                im_path_quad_to(&p, pts[3][0], pts[3][1], pts[4][0], pts[4][1]);
                im_path_quad_to(&p, pts[5][0], pts[5][1], pts[0][0], pts[0][1]);
            }
            im_path_close(&p);
            im_path_data pd = im_path_view(&p);
            CHECK(im_path_fill_argb(&s, alloc, &pd, 0xFFFFFFFFu), "fill random path");
            im_path_destroy(&p);

            /* Emit full 256-pixel row for ALL 1,000 random paths so every pixel of the corpus is oracle-checked. */
            write_str(STD_OUTPUT_HANDLE, "RPATH ");
            write_i64(STD_OUTPUT_HANDLE, (int64_t)rule); write_str(STD_OUTPUT_HANDLE, " ");
            write_i64(STD_OUTPUT_HANDLE, use_cubic ? 3 : 2); write_str(STD_OUTPUT_HANDLE, " ");
            write_i64(STD_OUTPUT_HANDLE, npts);
            for (int k = 0; k < npts; k++) {
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, pts[k][0]);
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, pts[k][1]);
            }
            write_str(STD_OUTPUT_HANDLE, " :");
            for (int i = 0; i < 256; i++) {
                write_str(STD_OUTPUT_HANDLE, " ");
                write_i64(STD_OUTPUT_HANDLE, (int64_t)((pix[i] >> 16) & 0xFFu));
            }
            write_str(STD_OUTPUT_HANDLE, "\n");
        }
    }

    /* 3. Quarter ring (90 degrees, 0 -> 90 deg, +X to +Y) at every size from 8 to 128 pixels. */
    {
        static const int sizes[8] = { 8, 16, 24, 32, 48, 64, 96, 128 };
        static uint32_t ring_pix[144 * 144];
        for (int si = 0; si < 8; si++) {
            int sz = sizes[si];
            int dim = sz + 16;
            for (int i = 0; i < dim * dim; i++) ring_pix[i] = 0x00000000u;
            im_surface s = { ring_pix, dim, dim, dim, pal, IM_ONE, NULL, NULL };
            im_fixed cx = FX(dim / 2);
            im_fixed cy = FX(dim / 2);
            im_fixed r = FX(sz / 2);
            im_fixed thick = (sz >= 16) ? FX(sz / 8) : FX(2);

            im_path p;
            im_path_init(&p, alloc, IM_FILL_NON_ZERO);
            CHECK(im_path_arc_angles(&p, cx, cy, r, r, 0, FX(90), true), "quarter arc angles");
            im_stroke_style st = { thick, IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_ROUND, FX(10), NULL, 0u, 0 };
            im_path_data pd = im_path_view(&p);
            CHECK(im_path_stroke_argb(&s, alloc, &pd, &st, 0xFFFFFFFFu), "stroke quarter ring");
            im_path_destroy(&p);

            /* Verify: zero pixels in the 3 excluded quadrants (x < dim/2 - 1 or y < dim/2 - 1),
               nonzero pixels on +X end (dim/2 + sz/2, dim/2 + 1), +Y end (dim/2 + 1, dim/2 + sz/2),
               and 45-degree diagonal, and empty center (dim/2, dim/2) for sz >= 16. */
            int outside_hits = 0;
            int q1_hits = 0;
            int half = dim / 2;
            for (int y = 0; y < dim; y++) {
                for (int x = 0; x < dim; x++) {
                    uint32_t c = ring_pix[y * dim + x] & 0xFFu;
                    if (x < half - 1 || y < half - 1) {
                        if (c > 0u) outside_hits++;
                    } else if (x >= half && y >= half) {
                        if (c > 128u) q1_hits++;
                    }
                }
            }
            CHECK(outside_hits == 0, "quarter ring has zero pixels outside 90-degree quadrant");
            CHECK(q1_hits > 0, "quarter ring paints inside 90-degree quadrant");
            if (sz >= 16) {
                CHECK((ring_pix[half * dim + half] & 0xFFu) == 0u, "quarter ring center is hollow");
            }
            /* Symmetry along y = x around center: continuous-X vs 16-row-Y differs by <= 8/255 (half subscanline). */
            int asym = 0;
            for (int dy = 0; dy < half; dy++) {
                for (int dx = dy + 1; dx < half; dx++) {
                    int a = (int)(ring_pix[(half + dy) * dim + (half + dx)] & 0xFFu);
                    int b = (int)(ring_pix[(half + dx) * dim + (half + dy)] & 0xFFu);
                    if (abs_i(a - b) > 8) asym++;
                }
            }
            CHECK(asym == 0, "quarter ring is symmetric across 45-degree diagonal");
        }
    }

    /* 4. Endpoint elliptical arcs (all 4 large_arc/sweep combos, rotated unequal radii, undersized, zero, coincident). */
    {
        uint32_t pix[48 * 48];
        im_surface s = { pix, 48, 48, 48, pal, IM_ONE, NULL, NULL };
        int counts[4] = { 0, 0, 0, 0 };
        for (int combo = 0; combo < 4; combo++) {
            bool la = (combo & 2) != 0;
            bool sw = (combo & 1) != 0;
            for (int i = 0; i < 48 * 48; i++) pix[i] = 0u;
            im_path p;
            im_path_init(&p, alloc, IM_FILL_NON_ZERO);
            im_path_move_to(&p, FX(14), FX(24));
            im_path_arc_to(&p, FX(12), FX(8), FX(25), la, sw, FX(34), FX(24));
            im_path_close(&p);
            im_path_data pd = im_path_view(&p);
            CHECK(im_path_fill_argb(&s, alloc, &pd, 0xFFFFFFFFu), "fill endpoint arc combo");
            for (int i = 0; i < 48 * 48; i++) if ((pix[i] & 0xFFu) > 128u) counts[combo]++;
            im_path_destroy(&p);
        }
        CHECK(counts[0] > 0 && counts[1] > 0 && counts[2] > counts[0] && counts[3] > counts[1],
              "large_arc=1 combos cover more area than large_arc=0 combos");

        /* Undersized radii (2, 2 for distance 20 -> auto-scaled to radius 10), zero radius (line), coincident (no-op). */
        for (int i = 0; i < 48 * 48; i++) pix[i] = 0u;
        im_path p;
        im_path_init(&p, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p, FX(14), FX(24));
        im_path_arc_to(&p, FX(2), FX(2), 0, false, true, FX(34), FX(24)); /* sweep=true goes clockwise UP through (24, 14) */
        im_path_arc_to(&p, 0, FX(5), 0, false, true, FX(14), FX(24)); /* zero rx -> straight chord back */
        im_path_arc_to(&p, FX(5), FX(5), 0, false, true, FX(14), FX(24)); /* coincident -> no-op */
        im_path_close(&p);
        im_path_data pd = im_path_view(&p);
        CHECK(im_path_fill_argb(&s, alloc, &pd, 0xFFFFFFFFu), "fill undersized + zero + coincident arc");
        CHECK((pix[20 * 48 + 24] & 0xFFu) > 200u, "undersized arc auto-scaled to semicircle covers (24, 20)");
        im_path_destroy(&p);
    }

    /* 5. Nested same- vs opposite-winding contours (NonZero vs EvenOdd) + implicit fill closure vs open stroke caps. */
    {
        uint32_t pix[32 * 32];
        im_surface s = { pix, 32, 32, 32, pal, IM_ONE, NULL, NULL };
        /* Two concentric CW squares: outer [4,28], inner [10,22]. */
        im_path p_same;
        im_path_init(&p_same, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p_same, FX(4), FX(4)); im_path_line_to(&p_same, FX(28), FX(4));
        im_path_line_to(&p_same, FX(28), FX(28)); im_path_line_to(&p_same, FX(4), FX(28)); im_path_close(&p_same);
        im_path_move_to(&p_same, FX(10), FX(10)); im_path_line_to(&p_same, FX(22), FX(10));
        im_path_line_to(&p_same, FX(22), FX(22)); im_path_line_to(&p_same, FX(10), FX(22)); im_path_close(&p_same);

        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        im_path_data pd_nz = im_path_view(&p_same);
        im_path_fill_argb(&s, alloc, &pd_nz, 0xFFFFFFFFu);
        CHECK((pix[16 * 32 + 16] & 0xFFu) == 255u, "same-winding nested contour is filled under NonZero");

        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        pd_nz.fill_rule = IM_FILL_EVEN_ODD;
        im_path_fill_argb(&s, alloc, &pd_nz, 0xFFFFFFFFu);
        CHECK((pix[16 * 32 + 16] & 0xFFu) == 0u, "same-winding nested contour is hollow under EvenOdd");
        im_path_destroy(&p_same);

        /* Opposite-winding nested contour is hollow under NonZero too. */
        im_path p_opp;
        im_path_init(&p_opp, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p_opp, FX(4), FX(4)); im_path_line_to(&p_opp, FX(28), FX(4));
        im_path_line_to(&p_opp, FX(28), FX(28)); im_path_line_to(&p_opp, FX(4), FX(28)); im_path_close(&p_opp);
        im_path_move_to(&p_opp, FX(10), FX(10)); im_path_line_to(&p_opp, FX(10), FX(22));
        im_path_line_to(&p_opp, FX(22), FX(22)); im_path_line_to(&p_opp, FX(22), FX(10)); im_path_close(&p_opp);
        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        im_path_data pd_opp = im_path_view(&p_opp);
        im_path_fill_argb(&s, alloc, &pd_opp, 0xFFFFFFFFu);
        CHECK((pix[16 * 32 + 16] & 0xFFu) == 0u && (pix[6 * 32 + 16] & 0xFFu) == 255u,
              "opposite-winding nested contour is hollow under NonZero");
        im_path_destroy(&p_opp);

        /* Open triangle: fill closes implicitly, stroke does NOT close the 3rd edge! */
        im_path p_open;
        im_path_init(&p_open, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p_open, FX(6), FX(6));
        im_path_line_to(&p_open, FX(26), FX(6));
        im_path_line_to(&p_open, FX(26), FX(26));
        im_path_data pd_open = im_path_view(&p_open);
        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        im_path_fill_argb(&s, alloc, &pd_open, 0xFFFFFFFFu);
        CHECK((pix[12 * 32 + 20] & 0xFFu) == 255u, "open path fill closes implicitly");
        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        im_stroke_style st_open = { FX(2), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), NULL, 0u, 0 };
        im_path_stroke_argb(&s, alloc, &pd_open, &st_open, 0xFFFFFFFFu);
        CHECK((pix[16 * 32 + 16] & 0xFFu) == 0u, "open path stroke does not draw closing chord");
        im_path_destroy(&p_open);
    }

    /* 6. Stroke caps (Flat/Square/Round/Triangle), joins (Miter + MiterLimit fallback, Bevel, Round), dashes. */
    {
        uint32_t pix[48 * 24];
        im_surface s = { pix, 48, 24, 48, pal, IM_ONE, NULL, NULL };
        im_path line;
        im_path_init(&line, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&line, FX(12), FX(12));
        im_path_line_to(&line, FX(36), FX(12));
        im_path_data pd_line = im_path_view(&line);

        /* Flat cap stops at x=12; Square extends to x=8; Triangle has apex at (8,12) and empty corners at (9,8). */
        for (int i = 0; i < 48 * 24; i++) pix[i] = 0u;
        im_stroke_style st_flat = { FX(8), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), NULL, 0u, 0 };
        im_path_stroke_argb(&s, alloc, &pd_line, &st_flat, 0xFFFFFFFFu);
        CHECK((pix[12 * 48 + 10] & 0xFFu) == 0u && (pix[12 * 48 + 14] & 0xFFu) == 255u, "Flat cap clips at endpoint");

        for (int i = 0; i < 48 * 24; i++) pix[i] = 0u;
        im_stroke_style st_sq = { FX(8), IM_CAP_SQUARE, IM_CAP_SQUARE, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), NULL, 0u, 0 };
        im_path_stroke_argb(&s, alloc, &pd_line, &st_sq, 0xFFFFFFFFu);
        CHECK((pix[9 * 48 + 9] & 0xFFu) == 255u, "Square cap fills corner at (9, 9)");

        for (int i = 0; i < 48 * 24; i++) pix[i] = 0u;
        im_stroke_style st_tri = { FX(8), IM_CAP_TRIANGLE, IM_CAP_TRIANGLE, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), NULL, 0u, 0 };
        im_path_stroke_argb(&s, alloc, &pd_line, &st_tri, 0xFFFFFFFFu);
        CHECK((pix[12 * 48 + 10] & 0xFFu) == 255u && (pix[8 * 48 + 9] & 0xFFu) == 0u, "Triangle cap tapers to apex");

        /* Odd dash array {2, 1, 1} -> doubled {2,1,1,2,1,1}, plus zero-length dot dash, plus all-zero fallback. */
        im_fixed odd_d[3] = { FX(2), FX(1), FX(1) };
        im_stroke_style st_dash = { FX(4), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), odd_d, 3u, FX(1) };
        for (int i = 0; i < 48 * 24; i++) pix[i] = 0u;
        CHECK(im_path_stroke_argb(&s, alloc, &pd_line, &st_dash, 0xFFFFFFFFu), "odd dash array with offset");
        int dash_on = 0, dash_off = 0;
        for (int x = 13; x < 35; x++) {
            if ((pix[12 * 48 + x] & 0xFFu) > 200u) dash_on++;
            else if ((pix[12 * 48 + x] & 0xFFu) == 0u) dash_off++;
        }
        CHECK(dash_on > 4 && dash_off > 2, "dashed stroke produces both drawn dashes and empty gaps");

        im_fixed zero_dot[2] = { 0, FX(2) };
        im_stroke_style st_dot = { FX(4), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_ROUND, IM_JOIN_MITER, FX(10), zero_dot, 2u, 0 };
        for (int i = 0; i < 48 * 24; i++) pix[i] = 0u;
        CHECK(im_path_stroke_argb(&s, alloc, &pd_line, &st_dot, 0xFFFFFFFFu), "zero-length round dash dots");
        CHECK((pix[12 * 48 + 12] & 0xFFu) > 200u && (pix[12 * 48 + 16] & 0xFFu) == 0u, "zero-length round dash draws dots");

        im_fixed all_zero[2] = { 0, 0 };
        im_stroke_style st_az = { FX(4), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), all_zero, 2u, 0 };
        for (int i = 0; i < 48 * 24; i++) pix[i] = 0u;
        CHECK(im_path_stroke_argb(&s, alloc, &pd_line, &st_az, 0xFFFFFFFFu), "all-zero dash cycle falls back cleanly");
        CHECK((pix[12 * 48 + 24] & 0xFFu) == 255u, "all-zero dash cycle draws solid fallback");
        im_path_destroy(&line);

        /* Acute V-join: Miter with limit=10 spikes far; Miter with limit=2 falls back to Bevel. */
        im_path vee;
        im_path_init(&vee, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&vee, FX(40), FX(16));
        im_path_line_to(&vee, FX(20), FX(12));
        im_path_line_to(&vee, FX(40), FX(8));
        im_path_data pd_vee = im_path_view(&vee);
        im_stroke_style st_m10 = { FX(4), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), NULL, 0u, 0 };
        im_stroke_style st_m2 = { FX(4), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_MITER, FX(2), NULL, 0u, 0 };
        for (int i = 0; i < 48 * 24; i++) pix[i] = 0u;
        im_path_stroke_argb(&s, alloc, &pd_vee, &st_m10, 0xFFFFFFFFu);
        uint32_t spike_m10 = pix[12 * 48 + 14] & 0xFFu;
        for (int i = 0; i < 48 * 24; i++) pix[i] = 0u;
        im_path_stroke_argb(&s, alloc, &pd_vee, &st_m2, 0xFFFFFFFFu);
        uint32_t spike_m2 = pix[12 * 48 + 14] & 0xFFu;
        CHECK(spike_m10 > 200u && spike_m2 == 0u, "MiterLimit=2 triggers bevel fallback where MiterLimit=10 spikes");
        im_path_destroy(&vee);
    }

    /* 7. Linear and Radial Gradients dumped for exact double-precision Python oracle (<= 2/255) + edge cases. */
    {
        im_gradient_stop stops3[3] = { { 0, 0xFFFF1020u }, { IM_ONE / 2, 0x8020E040u }, { IM_ONE, 0xFF1040F0u } };
        im_rect box = { FX(10), FX(20), FX(90), FX(60) }; /* 80x40 non-square translated */

        /* Dump 40 linear + 40 radial samples (Pad/Reflect/Repeat, Relative/Absolute, moved Center + default Origin). */
        im_linear_gradient lg = { 0, 0, IM_ONE, IM_ONE, IM_MAPPING_RELATIVE, IM_SPREAD_REFLECT, IM_GRAD_SPACE_SRGB, stops3, 3u };
        for (int i = 0; i < 20; i++) {
            int64_t lx = im_fixed_to_q30(FX(10 + i * 5));
            int64_t ly = im_fixed_to_q30(FX(20 + i * 3));
            uint32_t col = im_linear_gradient_eval(&lg, box, IM_ONE, lx, ly);
            write_str(STD_OUTPUT_HANDLE, "GLIN ");
            write_i64(STD_OUTPUT_HANDLE, 10 + i * 5); write_str(STD_OUTPUT_HANDLE, " ");
            write_i64(STD_OUTPUT_HANDLE, 20 + i * 3); write_str(STD_OUTPUT_HANDLE, " ");
            write_i64(STD_OUTPUT_HANDLE, (int64_t)col); write_str(STD_OUTPUT_HANDLE, "\n");
        }

        im_radial_gradient rg = im_radial_gradient_default(stops3, 3u);
        rg.center_x = (35 * IM_ONE + 50) / 100; /* moved Center, default Origin=(0.5, 0.5) */
        rg.center_y = (40 * IM_ONE + 50) / 100;
        rg.radius_x = (55 * IM_ONE + 50) / 100;
        rg.radius_y = (60 * IM_ONE + 50) / 100;
        rg.spread = IM_SPREAD_REPEAT;
        for (int i = 0; i < 20; i++) {
            int64_t lx = im_fixed_to_q30(FX(10 + i * 4));
            int64_t ly = im_fixed_to_q30(FX(20 + i * 2));
            uint32_t col = im_radial_gradient_eval(&rg, box, IM_ONE, lx, ly);
            write_str(STD_OUTPUT_HANDLE, "GRAD ");
            write_i64(STD_OUTPUT_HANDLE, 10 + i * 4); write_str(STD_OUTPUT_HANDLE, " ");
            write_i64(STD_OUTPUT_HANDLE, 20 + i * 2); write_str(STD_OUTPUT_HANDLE, " ");
            write_i64(STD_OUTPUT_HANDLE, (int64_t)col); write_str(STD_OUTPUT_HANDLE, "\n");
        }

        /* Edge cases: empty, single-stop, repeated stop offset, coincident linear, zero radial radius, outside focus. */
        CHECK(im_gradient_sample_stops(NULL, 0u, IM_SPREAD_PAD, IM_TR_ONE / 2) == 0u, "empty stops -> transparent");
        im_gradient_stop one_s[1] = { { IM_ONE / 2, 0xFF123456u } };
        CHECK(im_gradient_sample_stops(one_s, 1u, IM_SPREAD_PAD, 0) == 0xFF123456u, "single stop -> constant");
        im_gradient_stop hard_s[2] = { { IM_ONE / 2, 0xFFFF0000u }, { IM_ONE / 2, 0xFF0000FFu } };
        CHECK(im_gradient_sample_stops(hard_s, 2u, IM_SPREAD_PAD, IM_TR_ONE / 4) == 0xFFFF0000u &&
              im_gradient_sample_stops(hard_s, 2u, IM_SPREAD_PAD, (3 * IM_TR_ONE) / 4) == 0xFF0000FFu,
              "repeated stop offset produces hard step");
        im_linear_gradient lg_coinc = { FX(10), FX(10), FX(10), FX(10), IM_MAPPING_ABSOLUTE, IM_SPREAD_PAD, IM_GRAD_SPACE_SRGB, stops3, 3u };
        CHECK(im_linear_gradient_eval(&lg_coinc, box, IM_ONE, 0, 0) == stops3[2].argb, "coincident linear -> last stop");
        im_radial_gradient rg_zero = im_radial_gradient_default(stops3, 3u);
        rg_zero.radius_x = 0;
        CHECK(im_radial_gradient_eval(&rg_zero, box, IM_ONE, 0, 0) == stops3[2].argb, "zero radial radius -> last stop");
        im_radial_gradient rg_out = im_radial_gradient_default(stops3, 3u);
        rg_out.origin_x = FX(5); /* outside ellipse -> clamped cleanly without crash/NaN */
        CHECK((im_radial_gradient_eval(&rg_out, box, IM_ONE, im_fixed_to_q30(FX(50)), im_fixed_to_q30(FX(40))) >> 24) > 0u,
              "outside radial focus clamps cleanly");
        /* Near-limit 16.16 ellipse coordinates must not overflow __int128. */
        im_rect huge = { 0, 0, FX(30000), FX(30000) };
        CHECK((im_radial_gradient_eval(&rg, huge, IM_ONE, im_fixed_to_q30(FX(15000)), im_fixed_to_q30(FX(15000))) >> 24) > 0u,
              "near-limit 30000x30000 radial ellipse evaluates without overflow");
    }

    /* 8. Transform + clipped group-opacity layer + translucent gradient-stroked path + singular no-op + OOM no-partial-paint. */
    {
        uint32_t pix[64 * 64], saved[64 * 64];
        for (int i = 0; i < 64 * 64; i++) pix[i] = 0x00102030u;
        im_layer_ctx lctx;
        im_surface s = { pix, 64, 64, 64, pal, IM_ONE, &lctx, NULL };
        im_layer_init(&lctx, &s, alloc, 1024u * 1024u);

        im_path p;
        im_path_init(&p, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p, FX(4), FX(16));
        im_path_line_to(&p, FX(28), FX(16));
        im_path_data pd = im_path_view(&p);
        im_stroke_style st = { FX(6), IM_CAP_ROUND, IM_CAP_ROUND, IM_CAP_FLAT, IM_JOIN_ROUND, FX(10), NULL, 0u, 0 };
        im_gradient_stop gs[2] = { { 0, 0x80FF0000u }, { IM_ONE, 0x800000FFu } };
        im_linear_gradient lg = { 0, 0, IM_ONE, 0, IM_MAPPING_RELATIVE, IM_SPREAD_PAD, IM_GRAD_SPACE_SRGB, gs, 2u };

        im_transform tr;
        im_transform_composite(FX(16), FX(16), FX(2), IM_ONE, 0, 0, FX(15), FX(10), FX(8), &tr);
        s.transform = &tr;

        CHECK(im_layer_push_clip_rect(&lctx, (im_rect){FX(16), FX(8), FX(48), FX(48)}), "push layer clip");
        CHECK(im_layer_push(&lctx, (im_rect){FX(16), FX(8), FX(48), FX(48)}, 128u, false, (im_radii){0,0,0,0}) == IM_LAYER_OK, "push opacity layer");
        CHECK(im_gradient_stroke_path_linear(&s, alloc, &pd, &st, &lg), "gradient-stroked transformed path in clipped layer");
        im_layer_pop(&lctx);
        im_layer_pop_clip(&lctx);

        int changed_in = 0, changed_out = 0;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                bool eq = (pix[y * 64 + x] == 0x00102030u);
                if (x >= 16 && x < 48 && y >= 8 && y < 48) { if (!eq) changed_in++; }
                else { if (!eq) changed_out++; }
            }
        }
        CHECK(changed_in > 20 && changed_out == 0, "transformed gradient-stroked path respects clip and draws inside layer");

        /* Singular transform must leave destination untouched. */
        for (int i = 0; i < 64 * 64; i++) saved[i] = pix[i];
        im_transform sing = { 0, 0, 0, 0, 0, 0 };
        s.transform = &sing;
        CHECK(im_gradient_stroke_path_linear(&s, alloc, &pd, &st, &lg), "singular transform returns cleanly");
        int sing_diff = 0;
        for (int i = 0; i < 64 * 64; i++) if (pix[i] != saved[i]) sing_diff++;
        CHECK(sing_diff == 0, "singular transform leaves destination untouched");

        /* OOM / capacity exhaustion must return false with zero pixels modified! */
        s.transform = NULL;
        hctx.fail_next = true;
        bool oom_res = im_path_fill_argb(&s, alloc, &pd, 0xFFFFFFFFu);
        hctx.fail_next = false;
        int oom_diff = 0;
        for (int i = 0; i < 64 * 64; i++) if (pix[i] != saved[i]) oom_diff++;
        CHECK(!oom_res && oom_diff == 0, "OOM returns false without partially painting surface");

        im_path_destroy(&p);
        im_layer_destroy(&lctx);
    }

    /* 9. End-to-end compiled <Path> template-to-pixels test using SDK-owned IM_ICON_CHECKMARK + bound Data readback. */
    {
        static const im_template_element elems[3] = {
            { IM_ELEM_GRID, "Root", -1, 0, 0 },
            { IM_ELEM_PATH, "CheckGlyph", 0, 0, 6 },
            { IM_ELEM_PATH, "BoundPath", 0, 6, 1 }
        };
        static const im_template_prop props[7] = {
            { "Width", { IM_TVAL_FIXED, FX(18) + IM_ONE / 2, 0, 0, 0, NULL, NULL } },
            { "Height", { IM_TVAL_FIXED, FX(17), 0, 0, 0, NULL, NULL } },
            { "Stretch", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Fill", NULL } },
            { "Opacity", { IM_TVAL_FIXED, 0, 0, 0, 0, NULL, NULL } },
            { "Fill", { IM_TVAL_BRUSH, IM_BRUSH_TextFillColorPrimaryBrush, 0, 0, 0, NULL, NULL } },
            { "Data", { IM_TVAL_ICON, IM_ICON_CHECKMARK, 0, 0, 0, NULL, NULL } },
            { "Data", { IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, "Content", NULL } }
        };
        static const im_template_setter state_setters[2] = {
            { "CheckGlyph", "Opacity", { IM_TVAL_FIXED, 0, 0, 0, 0, NULL, NULL } },
            { "CheckGlyph", "Opacity", { IM_TVAL_FIXED, IM_ONE, 0, 0, 0, NULL, NULL } }
        };
        static const im_template_state states[2] = {
            { "Unselected", 0, 1, 0, 0 },
            { "Selected", 1, 1, 0, 0 }
        };
        static const im_template_group groups[1] = {
            { "SelectionStates", 0, 2, 0, 0 }
        };
        static const im_template tpl = { "PathTest", "Control", 0, 0, 0, 3, 0, 1 };
        im_template_tables tbl = { state_setters, elems, props, groups, states, NULL, NULL, NULL, NULL };

        im_tree tree;
        im_tree_init(&tree, alloc);
        im_node *root = im_tree_instantiate_custom_template(&tree, &pal, &tpl, NULL, elems, props, "root");
        CHECK(root != NULL, "instantiate compiled Path template");
        if (root) {
            im_layout_run(&tree, root, FX(32), FX(32), IM_ONE);
            im_node *chk = im_tree_find_by_name(root, "CheckGlyph");
            im_node *bnd = im_tree_find_by_name(root, "BoundPath");
            CHECK(chk != NULL && chk->path_icon_id == IM_ICON_CHECKMARK && chk->path_stretch == 1,
                  "compiled Path node binds SDK-owned checkmark and Stretch=Fill");
            CHECK(bnd != NULL && bnd->path_data_binding != NULL && bnd->path_data_binding[0] == 'C',
                  "compiled Path node preserves Data={TemplateBinding Content} for readback");

            im_state_machine sm;
            im_states_init_custom(&sm, &tpl, &tbl, &pal);
            uint32_t pix[32 * 32];
            im_surface s = { pix, 32, 32, 32, pal, IM_ONE, NULL, NULL };

            /* Initial state (Opacity=0): renders zero pixels. */
            im_states_go_to(&sm, "Unselected", 0);
            im_states_apply_to_tree(&sm, root, 0);
            for (int i = 0; i < 1024; i++) pix[i] = 0u;
            CHECK(im_states_draw_path_node(&sm, chk, &s, alloc, NULL, 0), "draw Unselected path");
            int lit0 = 0;
            for (int i = 0; i < 1024; i++) if ((pix[i] & 0x00FFFFFFu) != 0u) lit0++;
            CHECK(lit0 == 0, "compiled Path at Opacity=0 paints zero pixels");

            /* Selected state (Opacity=1): SDK checkmark renders inside arranged 18.5x17 bounds. */
            im_states_go_to(&sm, "Selected", 100);
            im_states_apply_to_tree(&sm, root, 100);
            for (int i = 0; i < 1024; i++) pix[i] = 0u;
            CHECK(im_states_draw_path_node(&sm, chk, &s, alloc, NULL, 100), "draw Selected path");
            int lit1 = 0;
            for (int i = 0; i < 1024; i++) if ((pix[i] & 0x00FFFFFFu) != 0u) lit1++;
            CHECK(lit1 > 20, "compiled Path at Opacity=1 renders visible SDK checkmark pixels");

            /* Return to Unselected: pixels clear back to 0. */
            im_states_go_to(&sm, "Unselected", 200);
            im_states_apply_to_tree(&sm, root, 200);
            for (int i = 0; i < 1024; i++) pix[i] = 0u;
            CHECK(im_states_draw_path_node(&sm, chk, &s, alloc, NULL, 200), "draw returned Unselected path");
            int lit2 = 0;
            for (int i = 0; i < 1024; i++) if ((pix[i] & 0x00FFFFFFu) != 0u) lit2++;
            CHECK(lit2 == 0, "compiled Path returned to Opacity=0 clears all pixels");

            im_tree_destroy_subtree(&tree, root);
        }
    }

    /* 10. Stroke-only compiled Path (WindowCaptionButton pattern): bound Data + visual-state Path.Data
       (WindowStateNormal <-> WindowStateMaximized) + Pressed/Normal Stroke color change + replace & clear. */
    {
        static const im_template_element elems[2] = {
            { IM_ELEM_GRID, "Root", -1, 0, 0 },
            { IM_ELEM_PATH, "CaptionPath", 0, 0, 7 }
        };
        static const im_template_prop props[7] = {
            { "Width", { IM_TVAL_FIXED, FX(16), 0, 0, 0, NULL, NULL } },
            { "Height", { IM_TVAL_FIXED, FX(16), 0, 0, 0, NULL, NULL } },
            { "Stretch", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Fill", NULL } },
            { "StrokeThickness", { IM_TVAL_FIXED, FX(2), 0, 0, 0, NULL, NULL } },
            { "StrokeStartLineCap", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Round", NULL } },
            { "StrokeEndLineCap", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Round", NULL } },
            { "Data", { IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, "Content", NULL } }
        };
        static const im_template_setter state_setters[4] = {
            /* 0: Normal stroke white */
            { "CaptionPath", "Stroke", { IM_TVAL_COLOR, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, -1, NULL, NULL } },
            /* 1: Pressed stroke red */
            { "CaptionPath", "Stroke", { IM_TVAL_COLOR, (int32_t)0xFFFF2020u, (int32_t)0xFFFF2020u, (int32_t)0xFFFF2020u, -1, NULL, NULL } },
            /* 2: WindowStateNormal -> IM_ICON_WINDOW_MAXIMIZE */
            { "CaptionPath", "Data", { IM_TVAL_ICON, IM_ICON_WINDOW_MAXIMIZE, 0, 0, 0, NULL, NULL } },
            /* 3: WindowStateMaximized -> IM_ICON_WINDOW_RESTORE */
            { "CaptionPath", "Data", { IM_TVAL_ICON, IM_ICON_WINDOW_RESTORE, 0, 0, 0, NULL, NULL } }
        };
        static const im_template_state states[4] = {
            { "Normal", 0, 1, 0, 0 },
            { "Pressed", 1, 1, 0, 0 },
            { "WindowStateNormal", 2, 1, 0, 0 },
            { "WindowStateMaximized", 3, 1, 0, 0 }
        };
        static const im_template_group groups[2] = {
            { "CommonStates", 0, 2, 0, 0 },
            { "MinMaxStates", 2, 2, 0, 0 }
        };
        static const im_template tpl = { "CaptionBtnTest", "Control", 0, 0, 0, 2, 0, 2 };
        im_template_tables tbl = { state_setters, elems, props, groups, states, NULL, NULL, NULL, NULL };

        im_tree tree;
        im_tree_init(&tree, alloc);
        im_node *root = im_tree_instantiate_custom_template(&tree, &pal, &tpl, NULL, elems, props, "root");
        im_layout_run(&tree, root, FX(24), FX(24), IM_ONE);
        im_node *cp = im_tree_find_by_name(root, "CaptionPath");
        im_state_machine sm;
        im_states_init_custom(&sm, &tpl, &tbl, &pal);

        uint32_t pix_n[24 * 24], pix_p[24 * 24], pix_m[24 * 24], pix_b[24 * 24];
        im_surface s_n = { pix_n, 24, 24, 24, pal, IM_ONE, NULL, NULL };
        im_surface s_p = { pix_p, 24, 24, 24, pal, IM_ONE, NULL, NULL };
        im_surface s_m = { pix_m, 24, 24, 24, pal, IM_ONE, NULL, NULL };
        im_surface s_b = { pix_b, 24, 24, 24, pal, IM_ONE, NULL, NULL };

        /* 10a: Stroke-only path bound via Content (before MinMaxStates overrides Data): Normal -> Pressed -> Normal -> Clear. */
        im_states_go_to(&sm, "Normal", 0);
        im_states_set_binding(&sm, "Content", (im_template_value){ IM_TVAL_ICON, IM_ICON_WINDOW_MAXIMIZE, 0, 0, 0, NULL, NULL });
        for (int i = 0; i < 576; i++) pix_n[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, cp, &s_n, alloc, NULL, 0), "draw stroke-only bound path Normal");
        int lit_n = 0;
        for (int i = 0; i < 576; i++) if ((pix_n[i] & 0x00FFFFFFu) != 0u) lit_n++;
        CHECK(lit_n > 20, "stroke-only bound path draws visible pixels in Normal");

        im_states_go_to(&sm, "Pressed", 10);
        for (int i = 0; i < 576; i++) pix_p[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, cp, &s_p, alloc, NULL, 10), "draw stroke-only bound path Pressed");
        int red_p = 0;
        for (int i = 0; i < 576; i++) {
            uint32_t r = (pix_p[i] >> 16) & 0xFFu, b = pix_p[i] & 0xFFu;
            if (r > 200u && b < 50u) red_p++;
        }
        CHECK(red_p > 20, "Pressed state changes stroke-only Path color to red");

        /* Release (Pressed -> Normal) with UNCHANGED Content must equal original Normal frame byte-for-byte. */
        im_states_go_to(&sm, "Normal", 15);
        for (int i = 0; i < 576; i++) pix_b[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, cp, &s_b, alloc, NULL, 15), "draw released Normal unchanged Content");
        int diff_rel = 0;
        for (int i = 0; i < 576; i++) if (pix_b[i] != pix_n[i]) diff_rel++;
        CHECK(diff_rel == 0, "releasing Pressed -> Normal with unchanged Content restores exact Normal pixels");

        /* Now replace bound Content with IM_ICON_WINDOW_RESTORE, then clear Content -> 0 pixels. */
        im_states_set_binding(&sm, "Content", (im_template_value){ IM_TVAL_ICON, IM_ICON_WINDOW_RESTORE, 0, 0, 0, NULL, NULL });
        for (int i = 0; i < 576; i++) pix_b[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, cp, &s_b, alloc, NULL, 20), "draw replaced bound Content");
        int diff_nb = 0;
        for (int i = 0; i < 576; i++) if (pix_b[i] != pix_n[i]) diff_nb++;
        CHECK(diff_nb > 10, "replacing bound Content changes rendered stroke pixels");

        im_states_clear_binding(&sm, "Content");
        for (int i = 0; i < 576; i++) pix_b[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, cp, &s_b, alloc, NULL, 30), "draw cleared bound Content");
        int lit_clr = 0;
        for (int i = 0; i < 576; i++) if ((pix_b[i] & 0x00FFFFFFu) != 0u) lit_clr++;
        CHECK(lit_clr == 0, "clearing bound Content clears all stroke pixels");

        /* 10b: Visual-state Path.Data setter transitions: WindowStateNormal -> WindowStateMaximized -> WindowStateNormal. */
        im_states_go_to(&sm, "WindowStateNormal", 40);
        for (int i = 0; i < 576; i++) pix_n[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, cp, &s_n, alloc, NULL, 40), "draw WindowStateNormal");
        im_states_go_to(&sm, "WindowStateMaximized", 50);
        for (int i = 0; i < 576; i++) pix_m[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, cp, &s_m, alloc, NULL, 50), "draw WindowStateMaximized");
        int diff_nm = 0;
        for (int i = 0; i < 576; i++) if (pix_n[i] != pix_m[i]) diff_nm++;
        CHECK(diff_nm > 10, "WindowStateMaximized Path.Data setter changes rendered glyph");
        im_states_go_to(&sm, "WindowStateNormal", 60);
        for (int i = 0; i < 576; i++) pix_b[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, cp, &s_b, alloc, NULL, 60), "draw returned WindowStateNormal");
        int diff_back = 0;
        for (int i = 0; i < 576; i++) if (pix_n[i] != pix_b[i]) diff_back++;
        CHECK(diff_back == 0, "returning to WindowStateNormal restores exact maximize pixels");

        im_tree_destroy_subtree(&tree, root);
    }

    /* 11. Unnamed static compiled Path + auto-sized literal & dynamically replaced/cleared bound Path intrinsic sizing. */
    {
        static const im_template_path_seg segs[10] = {
            /* 0..4: 14x10 rectangle M0,0 L14,0 L14,10 L0,10 Z */
            { 0, 0, 0, 0, 0, 0, 0, 0, false, false },
            { 1, FX(14), 0, 0, 0, 0, 0, 0, false, false },
            { 1, FX(14), FX(10), 0, 0, 0, 0, 0, false, false },
            { 1, 0, FX(10), 0, 0, 0, 0, 0, false, false },
            { 5, 0, 0, 0, 0, 0, 0, 0, false, false },
            /* 5..9: 20x6 rectangle M0,0 L20,0 L20,6 L0,6 Z */
            { 0, 0, 0, 0, 0, 0, 0, 0, false, false },
            { 1, FX(20), 0, 0, 0, 0, 0, 0, false, false },
            { 1, FX(20), FX(6), 0, 0, 0, 0, 0, false, false },
            { 1, 0, FX(6), 0, 0, 0, 0, 0, false, false },
            { 5, 0, 0, 0, 0, 0, 0, 0, false, false }
        };
        static const im_template_element elems[3] = {
            { IM_ELEM_STACK_PANEL, "Root", -1, 0, 0 },
            { IM_ELEM_PATH, NULL, 0, 0, 4 },          /* unnamed literal auto-sized 14x10 path */
            { IM_ELEM_PATH, "AutoBound", 0, 4, 4 }    /* auto-sized bound path */
        };
        static const im_template_prop props[8] = {
            { "HorizontalAlignment", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Left", NULL } },
            { "VerticalAlignment", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Top", NULL } },
            { "Fill", { IM_TVAL_BRUSH, IM_BRUSH_TextFillColorPrimaryBrush, 0, 0, 0, NULL, NULL } },
            { "Data", { IM_TVAL_PATH, 0, 5, 1, 0, NULL, NULL } },
            { "HorizontalAlignment", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Left", NULL } },
            { "VerticalAlignment", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Top", NULL } },
            { "Fill", { IM_TVAL_BRUSH, IM_BRUSH_TextFillColorPrimaryBrush, 0, 0, 0, NULL, NULL } },
            { "Data", { IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, "Content", NULL } }
        };
        static const im_template tpl = { "AutoPathTest", "Control", 0, 0, 0, 3, 0, 0 };
        im_template_tables tbl = { NULL, elems, props, NULL, NULL, NULL, NULL, NULL, segs };

        im_tree tree;
        im_tree_init(&tree, alloc);
        im_node *root = im_tree_instantiate_custom_template(&tree, &pal, &tpl, NULL, elems, props, "root");
        im_node *sp = im_tree_find_by_name(root, "Root");
        im_node *unnamed = (sp && sp->child_count >= 1u) ? sp->children[0] : NULL;
        im_node *ab = (sp && sp->child_count >= 2u) ? sp->children[1] : NULL;
        im_state_machine sm;
        im_states_init_custom(&sm, &tpl, &tbl, &pal);

        /* Step A: unnamed literal 14x10 path measures to 14x10 and paints at arranged location without x:Name. */
        im_states_set_binding(&sm, "Content", (im_template_value){ IM_TVAL_PATH, 0, 5, 1, 0, NULL, NULL });
        im_states_apply_to_tree(&sm, root, 0);
        im_layout_run(&tree, root, FX(32), FX(32), IM_ONE);
        CHECK(unnamed->name == NULL && unnamed->desired_size.width == FX(14) && unnamed->desired_size.height == FX(10),
              "unnamed auto-sized literal Path measures to exact 14x10");
        CHECK(ab->desired_size.width == FX(14) && ab->desired_size.height == FX(10),
              "auto-sized bound Path measures to 14x10 when bound to 14x10 geometry");
        uint32_t pix[32 * 32];
        im_surface s = { pix, 32, 32, 32, pal, IM_ONE, NULL, NULL };
        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, unnamed, &s, alloc, segs, 0), "draw unnamed literal Path");
        CHECK((pix[5 * 32 + 7] & 0x00FFFFFFu) != 0u && (pix[11 * 32 + 7] & 0x00FFFFFFu) == 0u,
              "unnamed static Path paints inside its arranged 14x10 rect");

        /* Step B: replace bound geometry with 20x6 -> desired_size, arranged bounds, and pixels update! */
        im_states_set_binding(&sm, "Content", (im_template_value){ IM_TVAL_PATH, 5, 5, 1, 0, NULL, NULL });
        im_states_apply_to_tree(&sm, root, 10);
        im_layout_run(&tree, root, FX(32), FX(32), IM_ONE);
        CHECK(ab->desired_size.width == FX(20) && ab->desired_size.height == FX(6) &&
              (ab->absolute_rect.right - ab->absolute_rect.left) == FX(20) &&
              (ab->absolute_rect.bottom - ab->absolute_rect.top) == FX(6),
              "replacing bound Path geometry updates desired_size and arranged bounds to 20x6");
        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, ab, &s, alloc, segs, 10), "draw resized bound Path");
        CHECK((pix[12 * 32 + 18] & 0x00FFFFFFu) != 0u, "resized 20x6 bound Path paints at x=18");

        /* Step C: clear bound geometry -> desired_size shrinks to 0x0 and 0 pixels painted! */
        im_states_clear_binding(&sm, "Content");
        im_states_apply_to_tree(&sm, root, 20);
        im_layout_run(&tree, root, FX(32), FX(32), IM_ONE);
        CHECK(ab->desired_size.width == 0 && ab->desired_size.height == 0,
              "clearing bound Path geometry shrinks desired_size to 0x0");
        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, ab, &s, alloc, segs, 20), "draw cleared bound Path");
        int lit_c = 0;
        for (int i = 0; i < 1024; i++) if ((pix[i] & 0x00FFFFFFu) != 0u) lit_c++;
        CHECK(lit_c == 0, "cleared bound Path paints 0 pixels");

        im_tree_destroy_subtree(&tree, root);
    }

    /* 12. Short endpoint arc M0,20 A20,20 0 0 1 10,20 (must NOT collapse to a 180-deg semicircle). */
    {
        uint32_t pix[32 * 32];
        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        im_surface s = { pix, 32, 32, 32, pal, IM_ONE, NULL, NULL };
        im_path p;
        im_path_init(&p, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p, FX(0), FX(20));
        im_path_arc_to(&p, FX(20), FX(20), 0, false, true, FX(10), FX(20));
        im_path_close(&p);
        im_path_data pd = im_path_view(&p);
        im_rect pb;
        CHECK(im_path_bounds(&pd, &pb) && (pb.bottom - pb.top) < FX(2),
              "short endpoint arc M0,20 A20,20 0 0 1 10,20 has shallow ~0.64 px height, not 5 px semicircle");
        CHECK(im_path_fill_argb(&s, alloc, &pd, 0xFFFFFFFFu), "fill short endpoint arc");
        CHECK((pix[19 * 32 + 5] & 0xFFu) > 20u && (pix[16 * 32 + 5] & 0xFFu) == 0u,
              "short endpoint arc paints y=19 and leaves y=16 untouched");
        im_path_destroy(&p);
    }

    /* 13. Compiled path default StrokeThickness=1.0 and literal #FF112233 Fill on unnamed path. */
    {
        static const im_template_path_seg segs[5] = {
            { IM_PATH_MOVE, FX(2), FX(2), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(14), FX(2), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(14), FX(14), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(2), FX(14), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_CLOSE, 0, 0, 0, 0, 0, 0, 0, false, false }
        };
        static const im_template_element elems[3] = {
            { IM_ELEM_GRID, "Root", -1, 0, 0 },
            { IM_ELEM_PATH, NULL, 0, 0, 4 },
            { IM_ELEM_PATH, NULL, 0, 4, 4 }
        };
        static const im_template_prop props[8] = {
            /* Unnamed stroke-only path with NO StrokeThickness attribute (must default to 1.0). */
            { "Width", { IM_TVAL_FIXED, FX(16), 0, 0, 0, NULL, NULL } },
            { "Height", { IM_TVAL_FIXED, FX(16), 0, 0, 0, NULL, NULL } },
            { "Stroke", { IM_TVAL_BRUSH, IM_BRUSH_TextFillColorPrimaryBrush, 0, 0, 0, NULL, NULL } },
            { "Data", { IM_TVAL_PATH, 0, 5, 1, 0, NULL, NULL } },
            /* Unnamed fill path with literal #FF112233 color. */
            { "Width", { IM_TVAL_FIXED, FX(16), 0, 0, 0, NULL, NULL } },
            { "Height", { IM_TVAL_FIXED, FX(16), 0, 0, 0, NULL, NULL } },
            { "Fill", { IM_TVAL_COLOR, (int32_t)0xFF112233u, (int32_t)0xFF112233u, (int32_t)0xFF112233u, -1, "#FF112233", NULL } },
            { "Data", { IM_TVAL_PATH, 0, 5, 1, 0, NULL, NULL } }
        };
        static const im_template tpl = { "DefaultStrokeAndHex", "Control", 0, 0, 0, 3, 0, 0 };
        im_template_tables tbl = { NULL, elems, props, NULL, NULL, NULL, NULL, NULL, segs };
        im_tree tree;
        im_tree_init(&tree, alloc);
        im_node *root = im_tree_instantiate_custom_template(&tree, &pal, &tpl, NULL, elems, props, "root");
        im_node *g = im_tree_find_by_name(root, "Root");
        im_node *p_stroke = g->children[0];
        im_node *p_hex = g->children[1];
        im_state_machine sm;
        im_states_init_custom(&sm, &tpl, &tbl, &pal);
        im_states_apply_to_tree(&sm, root, 0);
        im_layout_run(&tree, root, FX(16), FX(16), IM_ONE);
        uint32_t pix[16 * 16];
        im_surface s = { pix, 16, 16, 16, pal, IM_ONE, NULL, NULL };
        for (int i = 0; i < 256; i++) pix[i] = 0u;
        CHECK(p_stroke->path_stroke_thickness == IM_ONE, "Path default StrokeThickness is 1.0");
        CHECK(im_states_draw_path_node(&sm, p_stroke, &s, alloc, NULL, 0), "draw default-thickness stroked Path");
        int lit_s = 0;
        for (int i = 0; i < 256; i++) if ((pix[i] & 0x00FFFFFFu) != 0u) lit_s++;
        CHECK(lit_s > 20, "unnamed Path with default StrokeThickness=1.0 paints visible stroke");
        for (int i = 0; i < 256; i++) pix[i] = 0u;
        CHECK(im_states_draw_path_node(&sm, p_hex, &s, alloc, NULL, 0), "draw literal hex Fill Path");
        CHECK(pix[8 * 16 + 8] == 0x00112233u, "unnamed Path with Fill=#FF112233 paints exact #112233");
        im_tree_destroy_subtree(&tree, root);
    }

    /* 14. Rounded gradient under identity transform must stay rounded (corner (0,0) untouched). */
    {
        uint32_t pix[20 * 20];
        for (int i = 0; i < 400; i++) pix[i] = 0u;
        im_transform id = im_transform_identity();
        im_surface s = { pix, 20, 20, 20, pal, IM_ONE, NULL, &id };
        im_gradient_stop st[2] = { { 0, 0xFFFF0000u }, { IM_ONE, 0xFF0000FFu } };
        im_linear_gradient lg = { 0, 0, IM_ONE, IM_ONE, IM_MAPPING_RELATIVE, IM_SPREAD_PAD, IM_GRAD_SPACE_SRGB, st, 2u };
        im_radii r10 = { FX(10), FX(10), FX(10), FX(10) };
        im_gradient_fill_rect_linear(&s, (im_rect){ 0, 0, FX(20), FX(20) }, r10, &lg);
        CHECK(pix[0] == 0u && pix[10 * 20 + 10] != 0u,
              "rounded linear gradient under identity transform clips corner (0,0) and fills center");
        for (int i = 0; i < 400; i++) pix[i] = 0u;
        im_radial_gradient rg = im_radial_gradient_default(st, 2u);
        im_gradient_fill_rect_radial(&s, (im_rect){ 0, 0, FX(20), FX(20) }, r10, &rg);
        CHECK(pix[0] == 0u && pix[10 * 20 + 10] != 0u,
              "rounded radial gradient under identity transform clips corner (0,0) and fills center");
    }

    /* 15. Tight analytic bounds (quadratic, semicircle, stroked horizontal line). */
    {
        im_path pq;
        im_path_init(&pq, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&pq, FX(0), FX(0));
        im_path_quad_to(&pq, FX(10), FX(20), FX(20), FX(0));
        im_path_data pdq = im_path_view(&pq);
        im_rect bq;
        CHECK(im_path_bounds(&pdq, &bq) && bq.left == 0 && bq.right == FX(20) && bq.top == 0 && bq.bottom == FX(10),
              "tight quadratic bounds have height 10 (curve apex), not 20 (control point)");
        im_path_destroy(&pq);

        /* Auto-sized semicircle M0,10 A10,10 0 0 1 20,10 Z + auto-sized stroked horizontal line M0,5 L20,5. */
        static const im_template_path_seg segs[5] = {
            { IM_PATH_MOVE, FX(0), FX(10), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_ARC, FX(10), FX(10), FX(20), FX(10), 0, 0, 0, false, true },
            { IM_PATH_CLOSE, 0, 0, 0, 0, 0, 0, 0, false, false },
            { IM_PATH_MOVE, FX(0), FX(5), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(20), FX(5), 0, 0, 0, 0, 0, false, false }
        };
        static const im_template_element elems[3] = {
            { IM_ELEM_STACK_PANEL, "Root", -1, 0, 0 },
            { IM_ELEM_PATH, NULL, 0, 0, 4 },
            { IM_ELEM_PATH, NULL, 0, 4, 5 }
        };
        static const im_template_prop props[9] = {
            { "HorizontalAlignment", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Left", NULL } },
            { "VerticalAlignment", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Top", NULL } },
            { "Fill", { IM_TVAL_COLOR, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, -1, NULL, NULL } },
            { "Data", { IM_TVAL_PATH, 0, 3, 1, 0, NULL, NULL } },
            { "HorizontalAlignment", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Left", NULL } },
            { "VerticalAlignment", { IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Top", NULL } },
            { "Stroke", { IM_TVAL_COLOR, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, -1, NULL, NULL } },
            { "StrokeThickness", { IM_TVAL_FIXED, FX(2), 0, 0, 0, NULL, NULL } },
            { "Data", { IM_TVAL_PATH, 3, 2, 1, 0, NULL, NULL } }
        };
        static const im_template tpl = { "ArcAndLineAuto", "Control", 0, 0, 0, 3, 0, 0 };
        im_template_tables tbl = { NULL, elems, props, NULL, NULL, NULL, NULL, NULL, segs };
        im_tree tree;
        im_tree_init(&tree, alloc);
        im_node *root = im_tree_instantiate_custom_template(&tree, &pal, &tpl, NULL, elems, props, "root");
        im_node *sp = im_tree_find_by_name(root, "Root");
        im_state_machine sm;
        im_states_init_custom(&sm, &tpl, &tbl, &pal);
        im_states_apply_to_tree(&sm, root, 0);
        im_layout_run(&tree, root, FX(32), FX(32), IM_ONE);
        CHECK(sp->children[0]->desired_size.width == FX(20) && sp->children[0]->desired_size.height >= FX(9),
              "auto-sized semicircle measures to ~20x10 (non-zero height)");
        CHECK(sp->children[1]->desired_size.width == FX(22) && sp->children[1]->desired_size.height == FX(2),
              "auto-sized stroked horizontal line includes StrokeThickness=2 in height");
        im_tree_destroy_subtree(&tree, root);
    }

    /* 16. All-or-nothing OOM on combined fill+stroke (fail during stroke -> 0 fill pixels written). */
    {
        uint32_t pix[32 * 32];
        for (int i = 0; i < 1024; i++) pix[i] = 0x00123456u;
        im_surface s = { pix, 32, 32, 32, pal, IM_ONE, NULL, NULL };
        im_path p;
        im_path_init(&p, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p, FX(4), FX(4));
        im_path_line_to(&p, FX(28), FX(4));
        im_path_line_to(&p, FX(28), FX(28));
        im_path_line_to(&p, FX(4), FX(28));
        im_path_close(&p);
        im_path_data pd = im_path_view(&p);
        im_stroke_style st = { FX(2), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), NULL, 0u, 0 };
        /* Sweep memory limits so OOM hits during fill AND after fill succeeds while stroke allocates. */
        static const size_t limits[8] = { 64u, 128u, 256u, 512u, 768u, 1024u, 1536u, 2048u };
        bool saw_fail = false;
        int total_touched = 0;
        size_t total_leaked = 0u;
        for (int li = 0; li < 8; li++) {
            for (int i = 0; i < 1024; i++) pix[i] = 0x00123456u;
            heap_ctx oc = { GetProcessHeap(), false, limits[li], 0u };
            im_allocator oom_alloc = { h_alloc, h_realloc, h_free, &oc };
            bool ok = im_path_draw_stretched(&s, oom_alloc, &pd, (im_rect){ 0, 0, FX(32), FX(32) },
                                             0, 0x80FFFFFFu, &st, 0xFFFF0000u);
            if (!ok) {
                saw_fail = true;
                for (int i = 0; i < 1024; i++) if (pix[i] != 0x00123456u) total_touched++;
            }
            total_leaked += oc.used;
        }
        CHECK(saw_fail && total_touched == 0 && total_leaked == 0u,
              "combined fill+stroke OOM at any limit leaves 0 pixels modified and 0 bytes leaked");
        im_path_destroy(&p);
    }

    /* 17. Z terminates its contour: M0,0 L10,0 L10,10 Z L0,10 fills triangle only, not lower-left (2,8). */
    {
        uint32_t pix[16 * 16];
        for (int i = 0; i < 256; i++) pix[i] = 0u;
        im_surface s = { pix, 16, 16, 16, pal, IM_ONE, NULL, NULL };
        im_path p;
        im_path_init(&p, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p, FX(0), FX(0));
        im_path_line_to(&p, FX(10), FX(0));
        im_path_line_to(&p, FX(10), FX(10));
        im_path_close(&p);
        im_path_line_to(&p, FX(0), FX(10));
        im_path_data pd = im_path_view(&p);
        CHECK(im_path_fill_argb(&s, alloc, &pd, 0xFFFFFFFFu), "fill Z-terminated contour + trailing line");
        CHECK((pix[2 * 16 + 8] & 0xFFu) == 255u && (pix[8 * 16 + 2] & 0xFFu) == 0u,
              "Z closes triangle without absorbing subsequent L0,10 into a square");
        im_path_destroy(&p);
    }

    /* 18. Dash phase past zero entry, exact-vertex transition, and closed-seam join. */
    {
        uint32_t pix[32 * 32];
        im_surface s = { pix, 32, 32, 32, pal, IM_ONE, NULL, NULL };
        /* 18a: Exact-vertex gap end: (2,10)->(12,10)->(12,20), thickness=2, dash={2,3} (4px on, 6px gap = 10px!). */
        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        im_path p1;
        im_path_init(&p1, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p1, FX(2), FX(10));
        im_path_line_to(&p1, FX(12), FX(10));
        im_path_line_to(&p1, FX(12), FX(20));
        im_fixed da1[2] = { FX(2), FX(3) };
        im_stroke_style st1 = { FX(2), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), da1, 2u, 0 };
        im_path_data pd1 = im_path_view(&p1);
        CHECK(im_path_stroke_argb(&s, alloc, &pd1, &st1, 0xFFFFFFFFu), "stroke exact-vertex dash");
        CHECK((pix[12 * 32 + 12] & 0xFFu) > 200u, "dash resuming at exact vertex (12,10) paints vertical leg at (12,12)");
        im_path_destroy(&p1);

        /* 18b: Dotted {0, 2} with nonzero offset=1 (thickness=2, cycle=4): first dot at x=4+2=6, NOT x=4. */
        for (int i = 0; i < 1024; i++) pix[i] = 0u;
        im_path p2;
        im_path_init(&p2, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p2, FX(4), FX(16));
        im_path_line_to(&p2, FX(24), FX(16));
        im_fixed da2[2] = { 0, FX(2) };
        im_stroke_style st2 = { FX(2), IM_CAP_ROUND, IM_CAP_ROUND, IM_CAP_ROUND, IM_JOIN_ROUND, FX(10), da2, 2u, FX(1) };
        im_path_data pd2 = im_path_view(&p2);
        CHECK(im_path_stroke_argb(&s, alloc, &pd2, &st2, 0xFFFFFFFFu), "stroke dotted {0,2} with offset=1");
        CHECK((pix[16 * 32 + 4] & 0xFFu) == 0u && (pix[16 * 32 + 6] & 0xFFu) > 100u,
              "dotted {0,2} with offset=1 advances past zero entry so dot lands at x=6 not x=4");
        im_path_destroy(&p2);
    }

    /* 19. Shape stretch preserves unscaled stroke thickness and UniformToFill clips to dst_rect. */
    {
        uint32_t pix[48 * 48];
        for (int i = 0; i < 48 * 48; i++) pix[i] = 0u;
        im_surface s = { pix, 48, 48, 48, pal, IM_ONE, NULL, NULL };
        im_path p;
        im_path_init(&p, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p, FX(0), FX(0));
        im_path_line_to(&p, FX(10), FX(0));
        im_path_line_to(&p, FX(10), FX(10));
        im_path_line_to(&p, FX(0), FX(10));
        im_path_close(&p);
        im_path_data pd = im_path_view(&p);
        im_stroke_style st = { FX(2), IM_CAP_FLAT, IM_CAP_FLAT, IM_CAP_FLAT, IM_JOIN_MITER, FX(10), NULL, 0u, 0 };
        CHECK(im_path_draw_stretched(&s, alloc, &pd, (im_rect){ FX(4), FX(4), FX(44), FX(44) }, 1, 0u, &st, 0xFFFFFFFFu),
              "draw stretched 10x10->40x40 outlined box");
        /* Top stroke is y in [4, 6), so y=5 is lit and y=8 MUST be empty (stroke NOT scaled 4x to 8 px!). */
        CHECK((pix[5 * 48 + 24] & 0xFFu) > 200u && (pix[8 * 48 + 24] & 0xFFu) == 0u,
              "Stretch=Fill preserves unscaled 2px stroke thickness");
        im_path_destroy(&p);

        /* UniformToFill clips overflowing 20x10 rect strictly to [16, 28) x [16, 28). */
        for (int i = 0; i < 48 * 48; i++) pix[i] = 0u;
        im_path p2;
        im_path_init(&p2, alloc, IM_FILL_NON_ZERO);
        im_path_move_to(&p2, FX(0), FX(0));
        im_path_line_to(&p2, FX(20), FX(0));
        im_path_line_to(&p2, FX(20), FX(10));
        im_path_line_to(&p2, FX(0), FX(10));
        im_path_close(&p2);
        im_path_data pd2 = im_path_view(&p2);
        CHECK(im_path_draw_stretched(&s, alloc, &pd2, (im_rect){ FX(16), FX(16), FX(28), FX(28) }, 3, 0xFFFFFFFFu, NULL, 0u),
              "draw UniformToFill clipped rect");
        CHECK((pix[22 * 48 + 22] & 0xFFu) == 255u && (pix[22 * 48 + 14] & 0xFFu) == 0u && (pix[22 * 48 + 30] & 0xFFu) == 0u,
              "UniformToFill clips strictly to destination box");
        im_path_destroy(&p2);
    }

    /* 20. Group opacity for filled+stroked Path (overlap gets 50% opacity once, not 75% twice). */
    {
        static const im_template_path_seg segs[5] = {
            { IM_PATH_MOVE, FX(4), FX(4), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(20), FX(4), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(20), FX(20), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(4), FX(20), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_CLOSE, 0, 0, 0, 0, 0, 0, 0, false, false }
        };
        static const im_template_element elems[2] = {
            { IM_ELEM_GRID, "Root", -1, 0, 0 },
            { IM_ELEM_PATH, "BothOp", 0, 0, 7 }
        };
        static const im_template_prop props[7] = {
            { "Width", { IM_TVAL_FIXED, FX(24), 0, 0, 0, NULL, NULL } },
            { "Height", { IM_TVAL_FIXED, FX(24), 0, 0, 0, NULL, NULL } },
            { "Fill", { IM_TVAL_COLOR, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, -1, NULL, NULL } },
            { "Stroke", { IM_TVAL_COLOR, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, -1, NULL, NULL } },
            { "StrokeThickness", { IM_TVAL_FIXED, FX(4), 0, 0, 0, NULL, NULL } },
            { "Opacity", { IM_TVAL_FIXED, IM_ONE / 2, 0, 0, 0, NULL, NULL } },
            { "Data", { IM_TVAL_PATH, 0, 5, 1, 0, NULL, NULL } }
        };
        static const im_template tpl = { "GroupOpPath", "Control", 0, 0, 0, 2, 0, 0 };
        im_template_tables tbl = { NULL, elems, props, NULL, NULL, NULL, NULL, NULL, segs };
        im_tree tree;
        im_tree_init(&tree, alloc);
        im_node *root = im_tree_instantiate_custom_template(&tree, &pal, &tpl, NULL, elems, props, "root");
        im_node *bp = im_tree_find_by_name(root, "BothOp");
        im_state_machine sm;
        im_states_init_custom(&sm, &tpl, &tbl, &pal);
        im_states_apply_to_tree(&sm, root, 0);
        im_layout_run(&tree, root, FX(24), FX(24), IM_ONE);
        uint32_t pix[24 * 24];
        for (int i = 0; i < 576; i++) pix[i] = 0u;
        im_surface s = { pix, 24, 24, 24, pal, IM_ONE, NULL, NULL };
        CHECK(im_states_draw_path_node(&sm, bp, &s, alloc, NULL, 0), "draw filled+stroked Path at Opacity=0.5");
        uint32_t overlap_r = (pix[5 * 24 + 12] >> 16) & 0xFFu;
        uint32_t interior_r = (pix[12 * 24 + 12] >> 16) & 0xFFu;
        CHECK(overlap_r >= 120u && overlap_r <= 136u && interior_r >= 120u && interior_r <= 136u,
              "filled+stroked Path at Opacity=0.5 composites overlap once (~128), not twice (~191)");
        im_tree_destroy_subtree(&tree, root);
    }

    /* 21. Unnamed bound Path + equal-size geometry replacement marks paint_dirty. */
    {
        static const im_template_path_seg segs[8] = {
            /* 0..3: 10x10 upper-right triangle */
            { IM_PATH_MOVE, FX(0), FX(0), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(10), FX(0), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(10), FX(10), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_CLOSE, 0, 0, 0, 0, 0, 0, 0, false, false },
            /* 4..7: 10x10 lower-left triangle (EXACT SAME 10x10 size!) */
            { IM_PATH_MOVE, FX(0), FX(0), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(10), FX(10), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_LINE, FX(0), FX(10), 0, 0, 0, 0, 0, false, false },
            { IM_PATH_CLOSE, 0, 0, 0, 0, 0, 0, 0, false, false }
        };
        static const im_template_element elems[2] = {
            { IM_ELEM_GRID, "Root", -1, 0, 0 },
            { IM_ELEM_PATH, NULL, 0, 0, 2 } /* unnamed bound path */
        };
        static const im_template_prop props[2] = {
            { "Fill", { IM_TVAL_COLOR, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, (int32_t)0xFFFFFFFFu, -1, NULL, NULL } },
            { "Data", { IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, "Content", NULL } }
        };
        static const im_template tpl = { "UnnamedBound", "Control", 0, 0, 0, 2, 0, 0 };
        im_template_tables tbl = { NULL, elems, props, NULL, NULL, NULL, NULL, NULL, segs };
        im_tree tree;
        im_tree_init(&tree, alloc);
        im_node *root = im_tree_instantiate_custom_template(&tree, &pal, &tpl, NULL, elems, props, "root");
        im_node *g = im_tree_find_by_name(root, "Root");
        im_node *ub = g->children[0];
        im_state_machine sm;
        im_states_init_custom(&sm, &tpl, &tbl, &pal);
        im_states_set_binding(&sm, "Content", (im_template_value){ IM_TVAL_PATH, 0, 4, 1, 0, NULL, NULL });
        im_states_apply_to_tree(&sm, root, 0);
        im_layout_run(&tree, root, FX(16), FX(16), IM_ONE);
        CHECK(ub->name == NULL && ub->desired_size.width == FX(10) && ub->desired_size.height == FX(10),
              "unnamed bound Path resolves Content binding and measures 10x10");
        ub->paint_dirty = false;
        /* Swap to DIFFERENT 10x10 geometry -> must mark paint_dirty even though size did not change! */
        im_states_set_binding(&sm, "Content", (im_template_value){ IM_TVAL_PATH, 4, 4, 1, 0, NULL, NULL });
        im_states_apply_to_tree(&sm, root, 10);
        CHECK(ub->paint_dirty, "equal-size geometry swap on bound Path marks paint_dirty for incremental redraw");
        im_tree_destroy_subtree(&tree, root);
    }

    /* 22. Degenerate IM_SPREAD_REPEAT linear and radial gradients return last stop (not first stop). */
    {
        im_gradient_stop st[2] = { { 0, 0xFFFF0000u }, { IM_ONE, 0xFF0000FFu } };
        im_linear_gradient lg = { FX(10), FX(10), FX(10), FX(10), IM_MAPPING_ABSOLUTE, IM_SPREAD_REPEAT, IM_GRAD_SPACE_SRGB, st, 2u };
        im_radial_gradient rg = { FX(10), FX(10), 0, 0, FX(10), FX(10), IM_MAPPING_ABSOLUTE, IM_SPREAD_REPEAT, IM_GRAD_SPACE_SRGB, st, 2u };
        uint32_t cl = im_linear_gradient_eval(&lg, (im_rect){ 0, 0, FX(20), FX(20) }, IM_ONE, 0, 0);
        uint32_t cr = im_radial_gradient_eval(&rg, (im_rect){ 0, 0, FX(20), FX(20) }, IM_ONE, 0, 0);
        CHECK(cl == 0xFF0000FFu && cr == 0xFF0000FFu,
              "degenerate Repeat linear and radial gradients return last stop (blue), not wrapped first stop (red)");
    }

    if (g_failures > 0) {
        write_str(STD_ERROR_HANDLE, "test_path: FAILED\n");
        return 1;
    }
    write_str(STD_OUTPUT_HANDLE, "test_path: ALL PASSED\n");
    return 0;
}
