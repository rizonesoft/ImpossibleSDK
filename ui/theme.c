#include "theme.h"

static uint32_t stop_color(const im_palette *palette, const im_stop *stop) {
    uint32_t argb = stop->slot < 0 ? stop->argb : palette->slot[stop->slot];
    uint32_t alpha = (uint32_t)(((uint64_t)(argb >> 24) * (uint32_t)stop->opacity + IM_ONE / 2) >> 16);
    return alpha << 24 | (argb & 0xFFFFFF);
}

static uint32_t mix(uint32_t a, uint32_t b, im_fixed part) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        int from = (int)(a >> shift & 255), to = (int)(b >> shift & 255);
        out |= (uint32_t)(((int64_t)from * (IM_ONE - part) + (int64_t)to * part + IM_ONE / 2) >> 16) << shift;
    }
    return out;
}

uint32_t im_brush_at(const im_palette *palette, enum im_brush_id id, int64_t y, int64_t height, im_fixed scale) {
    const im_brush *brush = &im_theme_brushes[palette->theme][id];
    const im_stop *stop = &im_theme_stops[brush->first];
    if (brush->kind == IM_SOLID || brush->count == 1) return stop_color(palette, stop);
    bool relative = brush->kind == IM_GRADIENT_RELATIVE || brush->kind == IM_GRADIENT_RELATIVE_FLIPPED;
    if (brush->kind == IM_GRADIENT_FLIPPED || brush->kind == IM_GRADIENT_RELATIVE_FLIPPED) y = height - y;
    int64_t length = relative ? ((int64_t)brush->end * height) >> 16 : ((int64_t)brush->end * scale) >> 16;
    if (length <= 0) return stop_color(palette, stop + brush->count - 1);
    int64_t place = y * IM_ONE / length;
    if (place <= stop[0].offset) return stop_color(palette, stop);
    for (int i = 1; i < brush->count; i++) {
        if (place > stop[i].offset) continue;
        int64_t span = stop[i].offset - stop[i - 1].offset;
        im_fixed part = span ? (im_fixed)((place - stop[i - 1].offset) * IM_ONE / span) : IM_ONE;
        return mix(stop_color(palette, stop + i - 1), stop_color(palette, stop + i), part);
    }
    return stop_color(palette, stop + brush->count - 1);
}
