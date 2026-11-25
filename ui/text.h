/* Impossible UI: fixed-point UTF-8 text shaping, UAX #14 line breaking, alignment and drawing. Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "font.h"

/* Horizontal alignment of laid-out text lines within max_width (or natural width when unbounded). */
typedef enum im_text_align {
    IM_TEXT_ALIGN_LEFT = 0,
    IM_TEXT_ALIGN_CENTER = 1,
    IM_TEXT_ALIGN_RIGHT = 2
} im_text_align;

/* Line wrapping policy matching WinUI TextWrapping. */
typedef enum im_text_wrap {
    IM_TEXT_NO_WRAP = 0,
    IM_TEXT_WRAP = 1,
    IM_TEXT_WRAP_WHOLE_WORDS = 2
} im_text_wrap;

/* Parameters governing text shaping, line breaking, alignment, and drawing. */
typedef struct im_text_style {
    const im_font *font;
    im_fixed font_size;   /* 16.16 pixels (must be > 0) */
    im_fixed line_height; /* 16.16 pixels; <= 0 uses font natural line height */
    im_fixed max_width;   /* 16.16 pixels; <= 0 means unbounded single-line/explicit-break layout */
    im_text_align align;
    im_text_wrap wrap;
    uint32_t color;       /* straight 0xAARRGGBB */
    bool kerning;         /* enable pair kerning from GPOS / kern */
} im_text_style;

/* One positioned glyph within a laid-out text block. */
typedef struct im_text_glyph {
    uint16_t glyph_id;
    uint32_t byte_offset; /* UTF-8 byte offset in source text */
    uint32_t byte_len;    /* UTF-8 byte length of source scalar */
    im_fixed x;           /* 16.16 pixel offset relative to layout origin (0, 0) */
    im_fixed y;           /* 16.16 baseline Y relative to layout origin (0, 0) */
    im_fixed advance;     /* 16.16 positioned advance (nominal + kerning within line) */
    im_fixed nominal_adv; /* 16.16 nominal unkerned advance */
} im_text_glyph;

/* One laid-out line of text with byte range, glyph range, and metrics. */
typedef struct im_text_line {
    uint32_t byte_start;     /* inclusive UTF-8 byte start in source text */
    uint32_t byte_end;       /* exclusive UTF-8 byte end (excluding consumed hard line separator) */
    uint32_t glyph_start;    /* index into layout->glyphs */
    uint32_t glyph_count;    /* number of glyphs on this line */
    im_fixed x;              /* 16.16 aligned horizontal origin */
    im_fixed y;              /* 16.16 top of line box */
    im_fixed width;          /* 16.16 visible width excluding trailing whitespace */
    im_fixed full_width;     /* 16.16 width including trailing whitespace */
    im_fixed height;         /* 16.16 line box height */
    im_fixed baseline;       /* 16.16 baseline offset from line top */
} im_text_line;

/* Caller-allocator backed text layout holding positioned lines and glyphs.
   Does not retain pointers to the source UTF-8 buffer; the caller must keep style->font valid during draw. */
typedef struct im_text_layout {
    im_allocator alloc;
    im_text_line *lines;
    uint32_t line_count;
    im_text_glyph *glyphs;
    uint32_t glyph_count;
    im_fixed width;
    im_fixed height;
} im_text_layout;

/* Decode one UTF-8 scalar value from src[0..len-1] using the Unicode maximal subpart rule.
   Writes the decoded scalar (or U+FFFD on invalid bytes) to *out_cp and returns bytes consumed (>= 1 when len > 0). */
size_t im_utf8_decode(const char *src, size_t len, uint32_t *out_cp);

/* Shape, break (UAX #14 with UAX #29 grapheme emergency fallback), and align UTF-8 text into *layout.
   Zeroes *layout and releases any temporary buffers on error or allocation failure. */
bool im_text_layout_build(im_text_layout *layout, im_allocator alloc,
                          const char *utf8, size_t utf8_len, const im_text_style *style);

/* Release all memory owned by layout and zero the struct. */
void im_text_layout_destroy(im_text_layout *layout);

/* Measure the natural width and height of UTF-8 text under style without retaining the layout. */
bool im_text_layout_measure(im_allocator alloc, const char *utf8, size_t utf8_len,
                            const im_text_style *style, im_fixed *out_width, im_fixed *out_height);

/* Draw all glyphs in layout at origin (x, y) on surface using style->font and style->color. */
bool im_text_layout_draw(im_surface *surface, im_allocator alloc,
                         const im_text_layout *layout, const im_text_style *style,
                         im_fixed x, im_fixed y);
