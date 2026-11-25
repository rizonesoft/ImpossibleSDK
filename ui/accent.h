/* Impossible UI: perceptual fixed-point accent shade derivation. Portable: integers only, no system calls. */
#pragma once
#include <stdint.h>
#include "theme.h"

/* The seven accent colours: the base accent and its three lighter and three darker shades (0xAARRGGBB). */
typedef struct im_accent_shades {
    uint32_t accent;
    uint32_t light1;
    uint32_t light2;
    uint32_t light3;
    uint32_t dark1;
    uint32_t dark2;
    uint32_t dark3;
} im_accent_shades;

/* Derive the six perceptual D65 CIELAB accent shades from one 0xAARRGGBB colour using integer fixed-point
   sRGB <-> linear <-> CIE L*a*b* with hue-preserving sRGB gamut chroma bisection. Preserves alpha = 0xFF. */
void im_accent_derive(uint32_t accent_argb, im_accent_shades *out);

/* Populate the seven accent slots (and IM_SLOT_COLOR_CONTROL_ACCENT) of palette from one derived accent colour. */
void im_accent_fill_palette(im_palette *palette, uint32_t accent_argb);

/* Install seven OS-provided accent shades (e.g. from Windows UISettings.GetColorValue) verbatim into palette. */
void im_accent_set_os_shades(im_palette *palette, const im_accent_shades *shades);
