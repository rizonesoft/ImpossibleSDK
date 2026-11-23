#include "button.h"

#define STATES(name) {IM_BRUSH_##name, IM_BRUSH_##name##PointerOver, IM_BRUSH_##name##Pressed, IM_BRUSH_##name##Disabled}

static const struct {
    enum im_brush_id background[4], border[4];
    bool under_border; /* BackgroundSizing OuterBorderEdge */
} styles[] = {
    [IM_BUTTON] = {STATES(ButtonBackground), STATES(ButtonBorderBrush), false},
    [IM_BUTTON_ACCENT] = {STATES(AccentButtonBackground), STATES(AccentButtonBorderBrush), true},
    [IM_BUTTON_SUBTLE] = {STATES(SubtleButtonBackground), STATES(SubtleButtonBorderBrush), true},
};

static im_fixed scaled(im_fixed value, im_fixed scale) {
    return (im_fixed)(((int64_t)value * scale + IM_ONE / 2) >> 16);
}

/* A border is a whole number of pixels, as WinUI's layout rounding makes it. */
static im_fixed whole(im_fixed value) {
    return (value + IM_ONE / 2) & ~(IM_ONE - 1);
}

void im_draw_button(im_surface *surface, im_rect bounds, im_button_style style, im_button_state state) {
    const im_fixed *corner = im_value(&surface->palette, IM_VALUE_ControlCornerRadius);
    im_fixed thickness = whole(scaled(im_value(&surface->palette, IM_VALUE_ButtonBorderThemeThickness)[0], surface->scale));
    im_radii corners = {scaled(corner[0], surface->scale), scaled(corner[1], surface->scale), scaled(corner[2], surface->scale), scaled(corner[3], surface->scale)};
    bool under = styles[style].under_border;
    im_fixed in = under ? 0 : thickness;
    im_rect fill = {bounds.left + in, bounds.top + in, bounds.right - in, bounds.bottom - in};
    im_fill(surface, fill, im_grow(corners, under ? thickness / 2 : -thickness / 2), styles[style].background[state]);
    im_stroke(surface, bounds, corners, thickness, styles[style].border[state]);
}
