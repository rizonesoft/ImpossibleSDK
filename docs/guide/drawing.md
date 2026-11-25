<!-- page: group="Guide" order=2 sources=ui/draw.h,ui/theme.h,ui/accent.h,ui/resources.h,ui/shadow.h,ui/material.h,ui/button.h,ui/tree.h,ui/layout.h,ui/panels.h,ui/clip.h,ui/layer.h,ui/animate.h,ui/transition.h,ui/states.h,ui/host.h,ui/dirty.h,ui/frame.h,ui/transform.h,ui/picture.h,ui/input.h,ui/path.h,ui/gradient.h,ui/font.h,ui/text.h,parts/deflate/deflate.h,parts/deflate/deflate.c,parts/png/png.h,parts/png/write.c,parts/bmp/bmp.h,parts/bmp/write.c,cmake/ImpossibleSDK.cmake reviewed=2026-11-25 -->
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

From WinUI's own theme, as [brushes](site:api-theme.html) for dark, light and contrast. The user chooses the accent and the contrast colours, so the program puts them in the palette's slots (either verbatim from Windows via `im_accent_set_os_shades` or derived perceptually in fixed-point D65 CIELAB via `im_accent_derive` / `im_accent_fill_palette` in `ui/accent.h`, and with all four Windows 11 contrast presets `Aquatic`, `Desert`, `Dusk`, and `Night Sky` via `im_palette_init_contrast` in `ui/resources.h`). Set per-subtree `RequestedTheme` with `im_node_set_requested_theme` (rejecting system-wide `IM_REQ_THEME_CONTRAST` on nodes) and per-subtree accent overrides with `im_node_set_subtree_accent`, override individual WinUI brush or numeric value keys per subtree with `im_resources_set_brush_override_by_name` / `im_resources_set_value_override_by_name`, attach resource context to state machines via `im_states_set_resource_context`, and restyle a live tree in place on `IM_THEME_CHANGED` with `im_resources_restyle_tree` without losing focus, selection, scroll position, text, or explicit local padding/border overrides (clear local overrides with `im_node_clear_local_padding` / `im_node_clear_local_border_thickness`).

## How do clips and opacity layers work?

Attach an `im_layer_ctx` to an `im_surface` with `im_layer_init`, passing a caller-supplied `im_allocator` and a live buffer `memory_limit`. Push rectangular or rounded clips with `im_layer_push_clip_rect` and `im_layer_push_clip_rounded` (calling `im_layer_pop_clip` only when push returned `true`; fractional clip rectangles are intersected exactly in 64-bit fixed point via `im_clip_pixel_coverage_range`, including wide surfaces of 32,768 pixels or more), and push bounded group-opacity layers with `im_layer_push`. Every `im_fill`, `im_fill_argb`, `im_stroke`, `im_draw_button`, `im_shadow_draw` (`ui/shadow.h`: pinned `DropShadowRecipe.h` hollow directional+ambient elevation shadows cached in an LRU `im_shadow_cache` with `im_shadow_outset` for dirty bounds), and `im_material_draw` (`ui/material.h`: Mica, Mica Alt, and Acrylic region recording into `im_material_list` plus theme/contrast/transparency-off fallback painting, paired with `im_window_set_backdrop` in `include/impossible/window.h`) honours the active clip and layer automatically. When `im_layer_push` returns `IM_LAYER_OK` or `IM_LAYER_FALLBACK` (when `memory_limit` or the allocator refuses the offscreen buffer and direct-draw fallback runs instead), pair it with `im_layer_pop`; if it returns `IM_LAYER_ERR`, nothing was pushed.

## How do visual states and animations run?

Initialize an `im_state_machine` with `im_states_init` for any compiled WinUI control template (`IM_TEMPLATE_BUTTON`, `IM_TEMPLATE_TOGGLE_SWITCH`, `IM_TEMPLATE_TEXT_BOX`, `IM_TEMPLATE_LIST_VIEW_ITEM_EXPANDED`, and others) and transition groups with `im_states_go_to(sm, state_name, now_ms)` using a caller-supplied millisecond clock. Cubic Bezier key splines (`im_spline_eval_ratio`), keyframe tracks (`im_anim_eval_fixed`, `im_anim_eval_color`, `im_anim_eval_object`), theme animations (`Reposition`, `FadeIn`, `FadeOut`, `DragOver`, `DragItem`, `DropTarget`), generated return transitions, straight-ARGB `BrushTransition` fades (`im_brush_transition`), and named WinUI theme transitions in `ui/transition.h` (`Entrance`, `Popup`, `AddDelete` with detached exit-ghost ownership via `im_transition_ghosts`, `Reposition`, `Reorder`, `Content`, and `NavigationThemeTransition` `DrillIn`/`DrillOut` with zero-jump `im_transition_retarget`/`im_transition_reverse` and distinct `transition_transform`/`transition_opacity` that preserve authored `RenderTransform` and `Opacity`) run entirely in portable integer fixed-point math. Call `im_states_set_reduced_motion(sm, true, now_ms)` or `im_transition_set_reduced_motion(tr, true, now_ms)` to snap active and future transitions immediately to their end state, `im_states_apply_to_tree` to project layout-affecting visual-state values onto an `im_tree` without losing underlying local values, and `im_states_draw_button` to render a Button directly from its active state machine.

## How does the portable engine talk to the host?

Every system-owned service (memory, monotonic clock, clipboard, pointer cursor, caret blink timing, input method, opening URLs, sound, file pickers, and screen-reader announcements) goes through a single versioned and sized `im_host` callback table (`ui/host.h`, `ui/host.c`). Hosts built for an older smaller table version are accepted by `im_host_accept` (with v1 tables capped at `IM_HOST_V1_SIZE` regardless of backing size), missing callbacks are refused by exact name through `im_host_require`, and every `im_host_*` wrapper returns a defined safe result and clears output buffers on error or cancellation. On Windows, call `im_win_host_init` (`include/impossible/win_host.h`, `src/host.c`) to fill the Tier 0/1 memory, monotonic time, and cursor callbacks bound to your `im_window`.

## How do dirty rectangles and frame timing work?

Collect damaged device-pixel regions from an `im_tree` with `im_dirty_collect_tree` (`ui/dirty.h`, `ui/dirty.c`), which drains detached or destroyed subtree bounds from `tree->pending_damage`, scales logical bounds by the display scale with outward pixel rounding before clipping, marks the full viewport dirty on scale changes, and merges rectangles down to at most `IM_DIRTY_MAX_RECTS` (16) using exact minimum added waste area. Drive repaint scheduling with `im_frame_scheduler` (`ui/frame.h`, `ui/frame.c`): `im_frame_step` requests a one-shot frame (`im_frame_request_fn`, returning `true` when accepted by the host) only while damage, an active animation, or a settled final frame is owed, acknowledges only the painted generation via `im_frame_begin_paint` and `im_frame_on_painted` so damage or settlement queued during paint is never lost, and makes zero calls while the window is idle.

## How do transforms, pictures, and pointer hit-testing work?

Construct 2D affine transforms in Q30 signed 64-bit fixed point with `ui/transform.h` (`im_transform_translate`, `im_transform_scale`, `im_transform_rotate`, `im_transform_skew`, `im_transform_composite`, `im_transform_multiply`, `im_transform_invert`, `im_transform_viewport_bounds_q30`). Set `surface->transform` to rasterize filled shapes, gradient strokes, zero-padded bilinear alpha-mask glyphs (`im_draw_alpha_mask`), and premultiplied pictures (`im_picture_draw`, `im_picture_draw_nine_slice` in `ui/picture.h`) through any invertible transform while honouring `surface->layers` clips and group opacity via `im_layer_blend_premul_pixel`. Attach a `RenderTransform` to an `im_node` with `im_node_set_render_transform` (or let `im_states_apply_to_tree` project compiled `RenderTransformOrigin` and named transform animations automatically) and declare any visual paint overhang (such as `im_draw_alpha_mask_outset` for magnified A8 masks) with `im_node_set_paint_outset` so painting (`im_node_device_transform`), dirty-rectangle collection (`im_dirty_collect_tree`, tracking transformed or outset node damage directly in viewport-clipped device pixels with wide integer-pixel clips via `im_clip_push_irect` and `im_layer_push_clip_irect`), and device-pixel pointer hit-testing (`im_input_hit_test`, `im_input_to_local` in `ui/input.h`) share the exact same transform chain.

## How do paths, arcs, gradients, and extended panels work?

Extend `ui/layout.h` with `ui/panels.h` (`im_panels_measure_override`, `im_panels_arrange_override`) for `Canvas` (with stable `Canvas.ZIndex` paint order via `im_node_paint_order` and matching reverse hit-testing), `Viewbox` (with single-child enforcement, shared `im_stretch` from `ui/template.h`, and an independent `viewbox_transform` that preserves a child's `RenderTransform` and RTL placement), `UniformGrid`, `WrapPanel` (matching pinned `WrapPanel.cpp`), `RelativePanel` (node-owned name-based sibling resolution with cycle and missing-target diagnostics via `im_layout_last_error`), `VariableSizedWrapGrid` (compact bitset occupancy-map span placement, measured grid reuse, and `HorizontalChildrenAlignment`/`VerticalChildrenAlignment`), `ItemsWrapGrid`, and `ItemsStackPanel` (variable containers, group headers, and `GroupPadding`), all mirrored right-to-left under `IM_FLOW_RTL`.

Build fixed-point vector paths with `ui/path.h` (`im_path_move_to`, `im_path_line_to`, `im_path_quad_to`, `im_path_cubic_to`, `im_path_arc_to`, `im_path_arc_angles`, `im_path_close`, `im_node_set_path_intrinsic_bounds`) or zero-copy static `im_path_data` tables (sharing `im_path_seg` with `ui/template.h`, including SDK-owned `im_path_sdk_checkmark`, `im_path_sdk_window_maximize`, and `im_path_sdk_window_restore`), with compiled `IM_ELEM_PATH` painting via `im_states_draw_path_node`. Fill paths with `im_path_fill_argb` or `im_path_fill_shaded` using 16-subscanline analytic horizontal coverage (`IM_FILL_EVEN_ODD` or `IM_FILL_NON_ZERO`), and stroke paths in local space before `surface->transform` with `im_path_stroke_argb` or `im_path_stroke_shaded` (supporting Flat, Square, Round, and Triangle caps, Miter with `miter_limit`, Bevel, and Round joins, and `dash_array`/`dash_offset`). Evaluate and paint WinUI 3 `LinearGradientBrush` and `RadialGradientBrush` (`Center`, `RadiusX`, `RadiusY`, `GradientOrigin`, `IM_MAPPING_RELATIVE`/`IM_MAPPING_ABSOLUTE`, `IM_SPREAD_PAD`/`IM_SPREAD_REFLECT`/`IM_SPREAD_REPEAT`) over rounded rectangles (using `im_shape_contains` under transforms) and paths with `ui/gradient.h` (`im_gradient_fill_rect_linear`, `im_gradient_fill_rect_radial`, `im_gradient_fill_path_linear`, `im_gradient_fill_path_radial`, `im_gradient_stroke_path_linear`, `im_gradient_stroke_path_radial`).

| Call | What it does |
| --- | --- |
| [`im_fill`](site:api-draw.html#im_fill) | Fills a rounded shape with a brush, fitting adjacent radii proportionally along each edge |
| [`im_stroke`](site:api-draw.html#im_stroke) | Draws the line between a shape and the same shape moved in (`im_stroke_argb` takes a straight ARGB colour) |
| [`im_inset`](site:api-draw.html#im_inset) | Moves a shape's edges in |
| [`im_value`](site:api-theme.html#im_value) | A number of the theme, such as a corner radius |

## Fixed-point text

`ui/font.h` parses TrueType (`glyf`/`loca`, simple and composite glyphs), TrueType Collections (`ttcf`), and non-CID OpenType CFF v1 (`OTTO`, Type 2 charstrings with local/global subroutines and hint masks; CFF2, CID-keyed CFF, and deprecated `seac` are refused) directly from caller-owned memory with bounds checks and per-glyph complexity limits, scaling outlines in 64-bit integer fixed point (using `__int128` intermediates for CFF 16.16 conversion) and rasterising them through `im_path_fill_argb`. `ui/text.h` decodes UTF-8, shapes left-to-right runs with nominal advances and GPOS `latn`/`DFLT` or legacy `kern` adjustments, breaks lines with UAX #14 (`im_unicode_line_breaks`) and UAX #29 grapheme-cluster emergency fallback, aligns lines Left/Center/Right, and draws into any `im_surface`.

## Can Impossible OS take it?

Yes. Everything in `ui/` and `parts/` (including `parts/bmp/`, `parts/checksum/`, `parts/deflate/`, `parts/gif/`, `parts/image/`, `parts/inflate/`, `parts/png/`, and `parts/unicode/`) calls no system function and no C library. Streaming compression (`parts/deflate/deflate.h`, with fixed caller-owned `im_deflate` state) and decompression (`parts/inflate/inflate.h`) support raw DEFLATE, zlib, and gzip in bounded memory. Image decoders and lossless metadata-free writers (`parts/bmp/bmp.h`, `parts/gif/gif.h`, and `parts/png/png.h` with `parts/image/image.h`; `IM_PNG_WRITE_AUTO` picks the colour type with the smallest uncompressed row plus `PLTE`/`tRNS` estimate while `IM_PNG_WRITE_RGBA` encodes directly without a classification pass, scores all five row filters per scanline, and splits large streams across consecutive `IDAT` chunks) work on gamma-free straight 8-bit encoded samples (`IM_PIXEL_BGRA8` or `IM_PIXEL_RGBA8`) without colour-profile conversion. The tests compile each part alone as a kernel would and refuse any symbol beyond the four memory routines.
