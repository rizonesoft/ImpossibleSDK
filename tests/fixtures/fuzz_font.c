/* Native Linux libFuzzer target for ui/font.c: exercises init, cmap, metrics, kerning, and outline decoding. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "font.h"

static void *fuzz_alloc(void *ctx, size_t n) { (void)ctx; return malloc(n ? n : 1u); }
static void *fuzz_realloc(void *ctx, void *p, size_t old_n, size_t n) { (void)ctx; (void)old_n; return realloc(p, n ? n : 1u); }
static void fuzz_free(void *ctx, void *p, size_t old_n) { (void)ctx; (void)old_n; free(p); }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    im_font font;
    uint32_t face = (size > 0) ? (uint32_t)(data[size - 1u] & 3u) : 0u;
    if (!im_font_init_face(&font, data, size, face)) {
        if (!im_font_init(&font, data, size)) return 0;
    }

    im_font_metrics fm = im_font_get_metrics(&font);
    uint16_t g_a = im_font_glyph_index(&font, 'A');
    uint16_t g_v = im_font_glyph_index(&font, 'V');
    uint16_t g_supp = im_font_glyph_index(&font, 0x1F600u);
    (void)im_font_kerning(&font, g_a, g_v);

    im_allocator alloc = { fuzz_alloc, fuzz_realloc, fuzz_free, NULL };
    im_path path;
    if (!im_path_init(&path, alloc, IM_FILL_NON_ZERO)) return 0;

    uint16_t probe[6] = {
        0u, g_a, g_v, g_supp,
        (uint16_t)(fm.num_glyphs > 0 ? fm.num_glyphs - 1u : 0u),
        (uint16_t)(size > 2 ? ((uint16_t)data[size - 1u] % (fm.num_glyphs ? fm.num_glyphs : 1u)) : 0u)
    };
    for (int i = 0; i < 6; ++i) {
        im_glyph_metrics gm;
        (void)im_font_glyph_metrics(&font, probe[i], &gm);
        (void)im_font_glyph_outline(&font, probe[i], 16 * IM_ONE, &path);
    }
    im_path_destroy(&path);
    return 0;
}
