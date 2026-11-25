<!-- page: group="Guide" order=2 sources=ui/draw.h,ui/theme.h,ui/button.h,ui/tree.h,ui/layout.h,ui/clip.h,ui/layer.h,ui/animate.h,ui/states.h,ui/host.h,ui/dirty.h,ui/frame.h,ui/transform.h,ui/picture.h,ui/input.h,cmake/ImpossibleSDK.cmake reviewed=2026-11-25 -->
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

Attach an `im_layer_ctx` to an `im_surface` with `im_layer_init`, passing a caller-supplied `im_allocator` and a live buffer `memory_limit`. Push rectangular or rounded clips with `im_layer_push_clip_rect` and `im_layer_push_clip_rounded` (calling `im_layer_pop_clip` only when push returned `true`; fractional clip rectangles are intersected exactly in 64-bit fixed point via `im_clip_pixel_coverage_range`, including wide surfaces of 32,768 pixels or more), and push bounded group-opacity layers with `im_layer_push`. Every `im_fill`, `im_fill_argb`, `im_stroke`, and `im_draw_button` honours the active clip and layer automatically. When `im_layer_push` returns `IM_LAYER_OK` or `IM_LAYER_FALLBACK` (when `memory_limit` or the allocator refuses the offscreen buffer and direct-draw fallback runs instead), pair it with `im_layer_pop`; if it returns `IM_LAYER_ERR`, nothing was pushed.

## How do visual states and animations run?

Initialize an `im_state_machine` with `im_states_init` for any compiled WinUI control template (`IM_TEMPLATE_BUTTON`, `IM_TEMPLATE_TOGGLE_SWITCH`, `IM_TEMPLATE_TEXT_BOX`, `IM_TEMPLATE_LIST_VIEW_ITEM_EXPANDED`, and others) and transition groups with `im_states_go_to(sm, state_name, now_ms)` using a caller-supplied millisecond clock. Cubic Bezier key splines (`im_spline_eval_ratio`), keyframe tracks (`im_anim_eval_fixed`, `im_anim_eval_color`, `im_anim_eval_object`), theme animations (`Reposition`, `FadeIn`, `FadeOut`, `DragOver`, `DragItem`, `DropTarget`), generated return transitions, and straight-ARGB `BrushTransition` fades (`im_brush_transition`) run entirely in portable integer fixed-point math. Call `im_states_set_reduced_motion(sm, true, now_ms)` to snap both active and future transitions immediately to their end state, `im_states_apply_to_tree` to project layout-affecting visual-state values onto an `im_tree` without losing underlying local values, and `im_states_draw_button` to render a Button directly from its active state machine.

## How does the portable engine talk to the host?

Every system-owned service (memory, monotonic clock, clipboard, pointer cursor, caret blink timing, input method, opening URLs, sound, file pickers, and screen-reader announcements) goes through a single versioned and sized `im_host` callback table (`ui/host.h`, `ui/host.c`). Hosts built for an older smaller table version are accepted by `im_host_accept` (with v1 tables capped at `IM_HOST_V1_SIZE` regardless of backing size), missing callbacks are refused by exact name through `im_host_require`, and every `im_host_*` wrapper returns a defined safe result and clears output buffers on error or cancellation. On Windows, call `im_win_host_init` (`include/impossible/win_host.h`, `src/host.c`) to fill the Tier 0/1 memory, monotonic time, and cursor callbacks bound to your `im_window`.

## How do dirty rectangles and frame timing work?

Collect damaged device-pixel regions from an `im_tree` with `im_dirty_collect_tree` (`ui/dirty.h`, `ui/dirty.c`), which drains detached or destroyed subtree bounds from `tree->pending_damage`, scales logical bounds by the display scale with outward pixel rounding before clipping, marks the full viewport dirty on scale changes, and merges rectangles down to at most `IM_DIRTY_MAX_RECTS` (16) using exact minimum added waste area. Drive repaint scheduling with `im_frame_scheduler` (`ui/frame.h`, `ui/frame.c`): `im_frame_step` requests a one-shot frame (`im_frame_request_fn`, returning `true` when accepted by the host) only while damage, an active animation, or a settled final frame is owed, acknowledges only the painted generation via `im_frame_begin_paint` and `im_frame_on_painted` so damage or settlement queued during paint is never lost, and makes zero calls while the window is idle.

## How do transforms, pictures, and pointer hit-testing work?

Construct 2D affine transforms in Q30 signed 64-bit fixed point with `ui/transform.h` (`im_transform_translate`, `im_transform_scale`, `im_transform_rotate`, `im_transform_skew`, `im_transform_composite`, `im_transform_multiply`, `im_transform_invert`, `im_transform_viewport_bounds_q30`). Set `surface->transform` to rasterize filled shapes, gradient strokes, zero-padded bilinear alpha-mask glyphs (`im_draw_alpha_mask`), and premultiplied pictures (`im_picture_draw`, `im_picture_draw_nine_slice` in `ui/picture.h`) through any invertible transform while honouring `surface->layers` clips and group opacity via `im_layer_blend_premul_pixel`. Attach a `RenderTransform` to an `im_node` with `im_node_set_render_transform` (or let `im_states_apply_to_tree` project compiled `RenderTransformOrigin` and named transform animations automatically) so painting (`im_node_device_transform`), dirty-rectangle collection (`im_dirty_collect_tree`, tracking transformed node damage directly in viewport-clipped device pixels with wide integer-pixel clips via `im_clip_push_irect` and `im_layer_push_clip_irect`), and device-pixel pointer hit-testing (`im_input_hit_test`, `im_input_to_local` in `ui/input.h`) share the exact same transform chain.

| Call | What it does |
| --- | --- |
| [`im_fill`](site:api-draw.html#im_fill) | Fills a rounded shape with a brush, fitting adjacent radii proportionally along each edge |
| [`im_stroke`](site:api-draw.html#im_stroke) | Draws the line between a shape and the same shape moved in (`im_stroke_argb` takes a straight ARGB colour) |
| [`im_inset`](site:api-draw.html#im_inset) | Moves a shape's edges in |
| [`im_value`](site:api-theme.html#im_value) | A number of the theme, such as a corner radius |

## Can Impossible OS take it?

Yes. Everything in `ui/` and `parts/` (including `parts/image/` and `parts/unicode/`) calls no system function and no C library. The tests compile each part alone as a kernel would and refuse any symbol beyond the four memory routines.
