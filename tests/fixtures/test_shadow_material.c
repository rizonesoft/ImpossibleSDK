/* Tier-0 freestanding verification fixture for ui/shadow.{h,c} and ui/material.{h,c} (D01 T01 s12). */
#include <impossible/base.h>
#include <windows.h>
#include "accent.h"
#include "layer.h"
#include "material.h"
#include "resources.h"
#include "shadow.h"
#include "transform.h"
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

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        write_str(STD_ERROR_HANDLE, "FAIL: " msg "\n"); \
        g_failures++; \
    } \
} while (0)

typedef struct heap_ctx {
    HANDLE heap;
    size_t limit;
    size_t used;
} heap_ctx;

static void *h_alloc(void *vctx, size_t size) {
    heap_ctx *c = (heap_ctx *)vctx;
    if (c->limit > 0 && c->used + size > c->limit) return NULL;
    void *p = HeapAlloc(c->heap, 0, size ? size : 1);
    if (p) c->used += size;
    return p;
}

static void *h_realloc(void *vctx, void *ptr, size_t old_size, size_t new_size) {
    heap_ctx *c = (heap_ctx *)vctx;
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

static bool buf_eq(const uint32_t *a, const uint32_t *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}

int im_main(void) {
    heap_ctx hctx = { GetProcessHeap(), 0u, 0u };
    im_allocator alloc = { h_alloc, h_realloc, h_free, &hctx };

    /* 1. Pinned DropShadowRecipe.h verification at Translation.Z = 2, 4, 8, 16, 32, 128 in Light & Dark! */
    {
        im_shadow_recipe r2, r4l, r4d, r16l, r32l, r128l, r128d, r200;
        im_shadow_recipe_for(FX(2), IM_LIGHT, &r2);
        CHECK(r2.elevation == FX(1) && r2.ambient_alpha == 0u && r2.directional_alpha == 0u,
              "Translation.Z=2 (elevation=1 < 2) produces zero shadow opacity");
        im_shadow_recipe_for(FX(4), IM_LIGHT, &r4l);
        im_shadow_recipe_for(FX(4), IM_DARK, &r4d);
        CHECK(r4l.elevation == FX(2) && r4l.ambient_alpha == 0u && r4l.directional_alpha > 0u &&
              r4d.directional_alpha > r4l.directional_alpha,
              "Translation.Z=4 (elevation=2) is directional-only with stronger Dark opacity");
        im_shadow_recipe_for(FX(16), IM_LIGHT, &r16l);
        CHECK(r16l.elevation == FX(8) && r16l.ambient_alpha == 0u && r16l.directional_alpha == 35u,
              "Translation.Z=16 (elevation=8) Light directional opacity reaches 0.14 (35/255)");
        im_shadow_recipe_for(FX(32), IM_LIGHT, &r32l);
        CHECK(r32l.elevation == FX(16) && r32l.ambient_alpha == 0u && r32l.directional_alpha == 35u,
              "Translation.Z=32 (elevation=16) stays directional-only");
        im_shadow_recipe_for(FX(128), IM_LIGHT, &r128l);
        im_shadow_recipe_for(FX(128), IM_DARK, &r128d);
        im_shadow_recipe_for(FX(200), IM_DARK, &r200);
        CHECK(r128l.elevation == FX(64) && r128l.ambient_alpha == 38u && r128l.directional_alpha == 48u &&
              r128d.ambient_alpha == 94u && r128d.directional_alpha == 94u &&
              r200.elevation == FX(64) && r128l.insets.bottom > r128l.insets.top,
              "Translation.Z=128 clamps elevation to 64 with ambient+directional and asymmetric bottom > top inset");
    }

    /* 2. Hollow rounded shadow rendering, asymmetric extent, warm-cache 0-blur proof, theme reuse, LRU eviction, OOM retry! */
    {
        static uint32_t pix1[160 * 160], pix2[160 * 160], pix_dark[160 * 160];
        im_palette pal_light = { .theme = IM_LIGHT };
        im_palette pal_dark = { .theme = IM_DARK };
        im_shadow_cache cache;
        im_shadow_cache_init(&cache, alloc, 0u);

        im_rect caster = { FX(40), FX(40), FX(120), FX(100) };
        im_radii rad = { FX(8), FX(8), FX(8), FX(8) };

        /* Frame 1 (cold): computes 1 blur, leaves hollow center #FFFFFF, darkens bottom more than top! */
        for (int i = 0; i < 160 * 160; i++) { pix1[i] = 0x00FFFFFFu; pix2[i] = 0x00FFFFFFu; pix_dark[i] = 0x00FFFFFFu; }
        im_surface s1 = { pix1, 160, 160, 160, pal_light, IM_ONE, NULL, NULL };
        CHECK(im_shadow_draw(&s1, &cache, caster, rad, FX(32)) && cache.blur_computations == 1u && cache.cache_hits == 0u,
              "Cold shadow draw computes 1 blur");
        CHECK(pix1[70 * 160 + 80] == 0x00FFFFFFu, "Hollow caster interior receives zero shadow");
        uint32_t below = pix1[106 * 160 + 80] & 0xFFu;
        uint32_t above = pix1[34 * 160 + 80] & 0xFFu;
        CHECK(below < 0xFFu && below < above, "Directional Y offset darkens below caster more than above");

        /* Frame 2 (warm): exact same pixels with 0 new blur computations! */
        im_surface s2 = { pix2, 160, 160, 160, pal_light, IM_ONE, NULL, NULL };
        CHECK(im_shadow_draw(&s2, &cache, caster, rad, FX(32)) && cache.blur_computations == 1u && cache.cache_hits == 1u,
              "Warm shadow draw hits cache with 0 new blurs");
        CHECK(buf_eq(pix1, pix2, 160 * 160), "Warm cached shadow produces identical pixels to cold frame");

        /* Switching to Dark theme reuses the same cached mask (0 new blurs) and composites darker! */
        im_surface sd = { pix_dark, 160, 160, 160, pal_dark, IM_ONE, NULL, NULL };
        CHECK(im_shadow_draw(&sd, &cache, caster, rad, FX(32)) && cache.blur_computations == 1u && cache.cache_hits == 2u,
              "Theme switch reuses cached shadow mask without re-blurring");
        CHECK((pix_dark[106 * 160 + 80] & 0xFFu) < below, "Dark theme shadow composites stronger opacity than Light");

        /* All 6 required elevations (2, 4, 8, 16, 32, 128) draw cleanly without errors. */
        static const int elevs[6] = { 2, 4, 8, 16, 32, 128 };
        for (int ei = 0; ei < 6; ei++) {
            CHECK(im_shadow_draw(&s1, &cache, caster, rad, FX(elevs[ei])), "Elevation shadow draws cleanly");
        }

        /* Singular transform is a safe no-op; rounded clip restricts shadow pixels cleanly. */
        im_transform sing = { 0, 0, 0, 0, 0, 0 };
        s2.transform = &sing;
        CHECK(im_shadow_draw(&s2, &cache, caster, rad, FX(32)), "Singular transform shadow is a safe no-op");
        s2.transform = NULL;

        im_shadow_cache_destroy(&cache);

        /* OOM on bounded allocator refuses atomically and succeeds on retry when limit is lifted. */
        im_shadow_cache ocache;
        im_shadow_cache_init(&ocache, alloc, 0u);
        hctx.limit = hctx.used + 64u;
        CHECK(!im_shadow_draw(&s1, &ocache, caster, rad, FX(32)) && ocache.entry_count == 0u,
              "Shadow draw refuses cleanly on OOM without partial cache entry");
        hctx.limit = 0u;
        CHECK(im_shadow_draw(&s1, &ocache, caster, rad, FX(32)) && ocache.entry_count == 1u,
              "Shadow draw succeeds on retry after lifting memory cap");
        im_shadow_cache_destroy(&ocache);
    }

    /* 3. Materials: recording host list (Mica, Mica Alt, Acrylic), theme fallbacks, all 4 contrast presets, transparency-off! */
    {
        static uint32_t pix[160 * 100];
        im_palette pal_dark = { .theme = IM_DARK };
        im_accent_fill_palette(&pal_dark, 0xFF0078D7u);
        im_surface s = { pix, 160, 100, 160, pal_dark, IM_ONE, NULL, NULL };

        im_material_list list;
        im_material_list_init(&list, alloc);

        /* Record Mica window + Acrylic flyout + Mica Alt tab strip in one frame. */
        CHECK(im_material_draw(&s, &list, (im_rect){0, 0, FX(160), FX(100)}, (im_radii){0,0,0,0},
                               IM_MATERIAL_MICA, NULL, &pal_dark, true, true), "Record Mica");
        CHECK(im_material_draw(&s, &list, (im_rect){FX(10), FX(10), FX(150), FX(30)}, (im_radii){FX(4),FX(4),FX(4),FX(4)},
                               IM_MATERIAL_MICA_ALT, NULL, &pal_dark, true, true), "Record Mica Alt");
        CHECK(im_material_draw(&s, &list, (im_rect){FX(20), FX(40), FX(140), FX(90)}, (im_radii){FX(8),FX(8),FX(8),FX(8)},
                               IM_MATERIAL_ACRYLIC_BACKDROP, NULL, &pal_dark, true, true), "Record Acrylic");
        CHECK(list.count == 3u &&
              list.items[0].kind == IM_MATERIAL_MICA && !list.items[0].uses_fallback &&
              list.items[1].kind == IM_MATERIAL_MICA_ALT && !list.items[1].uses_fallback &&
              list.items[2].kind == IM_MATERIAL_ACRYLIC_BACKDROP && !list.items[2].uses_fallback &&
              list.items[0].tint_argb != 0u && list.items[0].fallback_argb != 0u &&
              list.items[2].tint_argb == 0xCC1C1C1Cu,
              "Recording host receives Mica, Mica Alt, and pinned #CC1C1C1C Dark Acrylic tint");

        /* Light Acrylic tint matches pinned SystemChromeAltHighColor@0.8 (#CCFFFFFF),
           unsupported host forces fallback even for in-app Acrylic, and NONE returns 0 in Contrast. */
        im_palette pal_light = { .theme = IM_LIGHT };
        im_material_region lr_ac, lr_inapp, lr_none;
        im_material_resolve(IM_MATERIAL_ACRYLIC_BACKDROP, NULL, &pal_light, true, true, &lr_ac);
        CHECK(lr_ac.tint_argb == 0xCCFFFFFFu && !lr_ac.uses_fallback, "Light Acrylic tint matches pinned #CCFFFFFF");
        im_material_resolve(IM_MATERIAL_ACRYLIC_IN_APP, NULL, &pal_dark, true, false, &lr_inapp);
        CHECK(lr_inapp.uses_fallback, "Unsupported host forces fallback for in-app Acrylic");
        im_palette phc0;
        im_palette_init_contrast(&phc0, IM_CONTRAST_AQUATIC, 0xFF0078D7u);
        im_material_resolve(IM_MATERIAL_NONE, NULL, &phc0, true, true, &lr_none);
        CHECK(lr_none.tint_argb == 0u && lr_none.fallback_argb == 0u, "IM_MATERIAL_NONE returns 0 even in Contrast");

        /* Exact Gaussian shadow check at Translation.Z=4 (sigma=1px, opacity=66):
           straight exterior pixels 0, 1, 2 must match double Gaussian (20, 4, 0) within <= 3/255,
           AND zero pixels outside im_shadow_outset may be darkened, AND translated shadow follows transform,
           AND cache==NULL works, AND bounded LRU evicts the oldest entry while keeping refreshed entry warm! */
        {
            im_surface sg = { pix, 160, 100, 160, pal_dark, IM_ONE, NULL, NULL };
            for (int i = 0; i < 160 * 100; i++) pix[i] = 0x00FFFFFFu;
            im_rect cb = { FX(40), FX(20), FX(80), FX(60) };
            im_radii cr = { 0, 0, 0, 0 };
            CHECK(im_shadow_draw(&sg, NULL, cb, cr, FX(4)), "Uncached (cache=NULL) shadow draw succeeds");
            int d0 = 255 - (int)(pix[40 * 160 + 80] & 0xFFu);
            int d1 = 255 - (int)(pix[40 * 160 + 81] & 0xFFu);
            int d2 = 255 - (int)(pix[40 * 160 + 82] & 0xFFu);
            CHECK(d0 >= 17 && d0 <= 23 && d1 >= 1 && d1 <= 7 && d2 <= 3,
                  "Translation.Z=4 exact Gaussian edge darkening matches double oracle (20, 4, 0) within <= 3/255");
            im_thickness out = im_shadow_outset(FX(4), IM_DARK);
            int max_x_out = 80 + (out.right >> 16);
            CHECK((pix[40 * 160 + max_x_out] & 0xFFFFFFu) == 0x00FFFFFFu,
                  "Zero pixels darkened outside im_shadow_outset");

            /* Translated shadow follows surface->transform */
            for (int i = 0; i < 160 * 100; i++) pix[i] = 0x00FFFFFFu;
            im_transform tr = im_transform_translate(FX(30), FX(10));
            sg.transform = &tr;
            CHECK(im_shadow_draw(&sg, NULL, cb, cr, FX(4)), "Translated shadow draw succeeds");
            CHECK((pix[50 * 160 + 110] & 0xFFu) < 250u && (pix[40 * 160 + 80] & 0xFFFFFFu) == 0x00FFFFFFu,
                  "Translated shadow renders at moved caster edge and leaves old position untouched");
            sg.transform = NULL;

            /* Bounded LRU eviction: cache holds 2 entries (~900B each); touching A before inserting C evicts B, not A! */
            im_shadow_cache lru;
            im_shadow_cache_init(&lru, alloc, 2000u);
            im_rect ra = { FX(10), FX(10), FX(30), FX(30) };
            im_rect rb = { FX(10), FX(10), FX(32), FX(30) };
            im_rect rc = { FX(10), FX(10), FX(34), FX(30) };
            CHECK(im_shadow_draw(&sg, &lru, ra, cr, FX(4)), "LRU insert A");
            CHECK(im_shadow_draw(&sg, &lru, rb, cr, FX(4)), "LRU insert B");
            CHECK(im_shadow_draw(&sg, &lru, ra, cr, FX(4)), "LRU touch A");
            uint64_t blurs_before_c = lru.blur_computations;
            CHECK(im_shadow_draw(&sg, &lru, rc, cr, FX(4)), "LRU insert C (evicts B)");
            uint64_t blurs_after_c = lru.blur_computations;
            CHECK(im_shadow_draw(&sg, &lru, ra, cr, FX(4)), "LRU hit A (still warm)");
            CHECK(lru.blur_computations == blurs_after_c && blurs_after_c == blurs_before_c + 1u,
                  "LRU kept refreshed A warm after C evicted B");
            CHECK(im_shadow_draw(&sg, &lru, rb, cr, FX(4)), "LRU re-blur evicted B");
            CHECK(lru.blur_computations == blurs_after_c + 1u, "Evicted B required fresh blur");
            im_shadow_cache_destroy(&lru);
        }

        /* Transparency-off forces exact theme fallback colour! */
        im_material_list_reset(&list);
        for (int i = 0; i < 160 * 100; i++) pix[i] = 0u;
        CHECK(im_material_draw(&s, &list, (im_rect){0, 0, FX(160), FX(100)}, (im_radii){0,0,0,0},
                               IM_MATERIAL_MICA, NULL, &pal_dark, false, true), "Draw transparency-off Mica");
        uint32_t want_dark_fb = im_brush_over(&pal_dark, IM_BRUSH_SolidBackgroundFillColorBaseBrush, 0, IM_ONE, IM_ONE);
        CHECK(list.items[0].uses_fallback && (pix[50 * 160 + 80] & 0xFFFFFFu) == (want_dark_fb & 0xFFFFFFu),
              "Transparency-off Mica paints exact theme SolidBackgroundFillColorBaseBrush fallback");

        /* All 4 built-in Contrast themes force SystemColorWindowColor fallback! */
        static const uint32_t want_hc_win[4] = { 0xFF202020u, 0xFFFFFAEFu, 0xFF2D3236u, 0xFF000000u };
        for (int ci = 0; ci < 4; ci++) {
            im_palette phc;
            im_palette_init_contrast(&phc, (im_contrast_theme)ci, 0xFF0078D7u);
            s.palette = phc;
            im_material_list_reset(&list);
            for (int i = 0; i < 160 * 100; i++) pix[i] = 0x00123456u;
            CHECK(im_material_draw(&s, &list, (im_rect){0, 0, FX(160), FX(100)}, (im_radii){0,0,0,0},
                                   IM_MATERIAL_ACRYLIC_BACKDROP, NULL, &phc, true, true), "Draw contrast Acrylic");
            CHECK(list.items[0].uses_fallback && (pix[50 * 160 + 80] & 0xFFFFFFu) == (want_hc_win[ci] & 0xFFFFFFu),
                  "Contrast theme forces exact SystemColorWindowColor fallback for all 4 presets");
        }

        /* OOM on material list growth refuses atomically preserving existing items. */
        hctx.limit = hctx.used;
        bool mat_oom = false;
        for (int k = 0; k < 16; k++) {
            if (!im_material_draw(&s, &list, (im_rect){0, 0, FX(10), FX(10)}, (im_radii){0,0,0,0},
                                  IM_MATERIAL_MICA, NULL, &pal_dark, true, true)) {
                mat_oom = true;
                break;
            }
        }
        CHECK(mat_oom && list.count >= 1u, "Material list OOM refuses atomically preserving prior regions");
        hctx.limit = 0u;
        im_material_list_destroy(&list);
    }

    CHECK(hctx.used == 0u, "all shadow and material tests freed 100% of allocated memory");
    if (g_failures > 0) {
        write_str(STD_ERROR_HANDLE, "test_shadow_material: FAILED\n");
        return 1;
    }
    write_str(STD_OUTPUT_HANDLE, "test_shadow_material: ALL PASSED\n");
    return 0;
}
