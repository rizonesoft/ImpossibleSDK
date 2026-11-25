/* Impossible UI: fixed-point TrueType and OpenType CFF font parser and outline rasteriser. Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "draw.h"
#include "path.h"
#include "tree.h"

/* Per-glyph safety limits preventing malicious fonts from causing unbounded work or allocation. */
#define IM_FONT_MAX_DEPTH 16u
#define IM_FONT_MAX_COMPONENTS 64u
#define IM_FONT_MAX_OPS 4096u
#define IM_FONT_MAX_POINTS 4096u

/* Font-wide vertical metrics in font design units (units_per_em). */
typedef struct im_font_metrics {
    uint16_t units_per_em;
    uint16_t num_glyphs;
    int16_t ascent;      /* positive distance above baseline */
    int16_t descent;     /* negative distance below baseline (or <= 0) */
    int16_t line_gap;    /* additional leading between lines */
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
    bool is_cff;
} im_font_metrics;

/* Per-glyph horizontal metrics and bounding box in font design units. */
typedef struct im_glyph_metrics {
    uint16_t advance_width;
    int16_t left_side_bearing;
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
    bool is_empty;
} im_glyph_metrics;

/* Parsed table offsets into a caller-owned SFNT/TTC/OTF font buffer.
   The caller must keep data[0..size-1] valid and unmodified for the lifetime of im_font. */
typedef struct im_font {
    const uint8_t *data;
    size_t size;
    im_font_metrics metrics;
    uint16_t num_hmetrics;
    int16_t index_to_loc_format;
    uint32_t cmap_off;
    uint32_t cmap_len;
    uint32_t cmap_subtable_off;
    uint32_t cmap_subtable_end;
    uint16_t cmap_format;
    uint32_t hmtx_off;
    uint32_t hmtx_len;
    uint32_t loca_off;
    uint32_t loca_len;
    uint32_t glyf_off;
    uint32_t glyf_len;
    uint32_t cff_off;
    uint32_t cff_len;
    uint32_t cff_charstrings_off;
    uint32_t cff_gsubrs_off;
    uint32_t cff_lsubrs_off;
    int32_t cff_default_width_x;
    int32_t cff_nominal_width_x;
    uint32_t kern_off;
    uint32_t kern_len;
    uint32_t gpos_off;
    uint32_t gpos_len;
    uint32_t gpos_kern_feature_off;
    bool has_gpos_kern;
} im_font;

/* Parse and validate a TrueType, OpenType CFF v1, or TTC (face 0) font buffer without heap allocation.
   Returns false and zeroes *font on any truncated, out-of-bounds, or unsupported table. */
bool im_font_init(im_font *font, const uint8_t *data, size_t size);

/* Parse and validate a specific face index inside a TrueType Collection (or face 0 for single-face fonts).
   Returns false and zeroes *font on invalid face_index or malformed font data. */
bool im_font_init_face(im_font *font, const uint8_t *data, size_t size, uint32_t face_index);

/* Return the font-wide metrics in font design units. */
im_font_metrics im_font_get_metrics(const im_font *font);

/* Scale a font design-unit value to 16.16 fixed-point pixels at size_px (raw 16.16 fixed point)
   using 64-bit integer arithmetic with symmetric rounding. */
im_fixed im_font_scale_units(const im_font *font, int32_t units, im_fixed size_px);

/* Map a Unicode scalar value to its glyph ID via cmap (Format 12 or Format 4). Returns 0 (.notdef) if unmapped. */
uint16_t im_font_glyph_index(const im_font *font, uint32_t codepoint);

/* Read horizontal metrics and design-unit bounding box for glyph_id.
   Returns false and zeroes *out if font is NULL or glyph_id >= num_glyphs. */
bool im_font_glyph_metrics(const im_font *font, uint16_t glyph_id, im_glyph_metrics *out);

/* Return the horizontal kerning adjustment in font design units for (left_glyph, right_glyph).
   Uses GPOS kern PairPos when present, falling back to legacy kern Format 0 without double-applying. */
int32_t im_font_kerning(const im_font *font, uint16_t left_glyph, uint16_t right_glyph);

/* Extract the scaled Y-down glyph outline into out_path (cleared first) at size_px (16.16 pixels),
   with origin at the baseline left (0, 0). Returns false and clears out_path on invalid glyph,
   malformed outline, exceeded safety limits, or allocation failure. */
bool im_font_glyph_outline(const im_font *font, uint16_t glyph_id, im_fixed size_px, im_path *out_path);

/* Extract and rasterise glyph_id at baseline origin (x, y) and size_px (16.16 pixels) with straight
   0xAARRGGBB color argb through the analytic fixed-point path rasteriser, honouring surface->transform
   and surface->layers. Returns true for valid empty glyphs (drawing nothing) and false on error/OOM. */
bool im_font_draw_glyph(im_surface *surface, im_allocator alloc, const im_font *font,
                        uint16_t glyph_id, im_fixed x, im_fixed y, im_fixed size_px, uint32_t argb);
