<!-- page: group="Guide" order=2 sources=ui/draw.h,ui/theme.h,ui/button.h,ui/tree.h,ui/layout.h,ui/clip.h,ui/layer.h,cmake/ImpossibleSDK.cmake reviewed=2026-11-24 -->
# Drawing

How do you draw Windows 11's shapes and colours? Describe your canvas as a surface, then fill shapes with the brushes of the theme.

## What does a button take?

```c program=drawn_button window tier=1
#include <impossible/window.h>
#include "button.h"

static void paint(im_window *window, im_canvas *canvas) {
    (void)window;
    im_surface surface = {
        .pixels = canvas->pixels, .width = canvas->width, .height = canvas->height, .stride = canvas->width,
        .palette = {.theme = IM_DARK, .slot = {[IM_SLOT_ACCENT_COLOR_LIGHT_2] = 0xFF60CDFF}},
        .scale = (im_fixed)(canvas->scale * IM_ONE),
    };
    for (int i = 0; i < canvas->width * canvas->height; i++)
        canvas->pixels[i] = 0x00202020;
    im_fixed unit = surface.scale;
    im_draw_button(&surface, (im_rect){20 * unit, 20 * unit, 140 * unit, 52 * unit}, IM_BUTTON_ACCENT, IM_REST);
}

int im_main(void) {
    im_window window = {.title = L"A button", .width = 320, .height = 120, .paint = paint};
    return im_window_run(&window, false) ? 0 : 1;
}
```

The button draws its ground and border, without text yet. [Controls](site:controls.html) says what is planned.

## What are the numbers?

Positions are `im_fixed`: 16 bits before the point and 16 after, so `IM_ONE` is one pixel. Multiply sizes given at 100 percent by the surface's scale. There is no floating point in Impossible UI, because an operating system's kernel is built without it.

## Where do the colours come from?

From WinUI's own theme, as [brushes](site:api-theme.html) for dark, light and contrast. The user chooses the accent and the contrast colours, so the program puts them in the palette's slots. A brush you name takes the right one.

## How do clips and opacity layers work?

Attach an `im_layer_ctx` to an `im_surface` with `im_layer_init`, passing a caller-supplied `im_allocator` and a live buffer `memory_limit`. Push rectangular or rounded clips with `im_layer_push_clip_rect` and `im_layer_push_clip_rounded` (calling `im_layer_pop_clip` only when push returned `true`), and push bounded group-opacity layers with `im_layer_push`. Every `im_fill`, `im_fill_argb`, `im_stroke`, and `im_draw_button` honours the active clip and layer automatically. When `im_layer_push` returns `IM_LAYER_OK` or `IM_LAYER_FALLBACK` (when `memory_limit` or the allocator refuses the offscreen buffer and direct-draw fallback runs instead), pair it with `im_layer_pop`; if it returns `IM_LAYER_ERR`, nothing was pushed.

| Call | What it does |
| --- | --- |
| [`im_fill`](site:api-draw.html#im_fill) | Fills a rounded shape with a brush, fitting adjacent radii proportionally along each edge |
| [`im_stroke`](site:api-draw.html#im_stroke) | Draws the line between a shape and the same shape moved in |
| [`im_inset`](site:api-draw.html#im_inset) | Moves a shape's edges in |
| [`im_value`](site:api-theme.html#im_value) | A number of the theme, such as a corner radius |

## Can Impossible OS take it?

Yes. Everything in `ui/` and `parts/` (including `parts/image/` and `parts/unicode/`) calls no system function and no C library. The tests compile each part alone as a kernel would and refuse any symbol beyond the four memory routines.
