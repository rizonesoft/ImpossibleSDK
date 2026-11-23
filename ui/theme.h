/* Impossible UI: theme values. Portable: integers only, no system calls. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "theme_data.h"

typedef int32_t im_fixed; /* 16.16 */
#define IM_ONE 65536

typedef enum im_theme { IM_DARK, IM_LIGHT, IM_CONTRAST } im_theme;

typedef enum im_brush_kind {
    IM_SOLID,
    IM_GRADIENT,          /* runs down from the top; end is in 96 DPI units */
    IM_GRADIENT_FLIPPED,  /* the same from the bottom up */
    IM_GRADIENT_RELATIVE, /* end is a share of the height */
    IM_GRADIENT_RELATIVE_FLIPPED
} im_brush_kind;

typedef struct im_brush {
    uint8_t kind, count;
    uint16_t first; /* in im_theme_stops */
    im_fixed end;
} im_brush;

typedef struct im_stop {
    im_fixed offset;
    int32_t slot; /* an im_color_slot, or -1 for argb */
    uint32_t argb;
    im_fixed opacity;
} im_stop;

/* The colours the user chooses: the accent family and the contrast theme. */
typedef struct im_palette {
    im_theme theme;
    uint32_t slot[IM_SLOT_COUNT];
} im_palette;

extern const im_brush im_theme_brushes[3][IM_BRUSH_COUNT];
extern const im_stop im_theme_stops[];
extern const int32_t im_theme_values[3][IM_VALUE_COUNT][4];

/* The colour of a brush at a point: y and height in pixels, scale in 16.16 (1.0 at 96 DPI). */
uint32_t im_brush_at(const im_palette *palette, enum im_brush_id id, im_fixed y, int height, im_fixed scale);

/* One number, or the left, top, right, bottom of a thickness or the four corners of a radius. */
static inline const im_fixed *im_value(const im_palette *palette, enum im_value_id id) {
    return im_theme_values[palette->theme][id];
}
