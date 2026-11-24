<!-- page: group="Impossible UI" order=1 sources=ui/button.h,ui/draw.c,tests/test_ui.py,reference/App.xaml.cs,tests/reference/cases.json,tests/fixtures/control_shots.c,tools/winui/resources.py reviewed=2026-11-24 -->
# How a control is compared

How do we know it looks like Windows 11? A small program draws the same control with real WinUI 3, and the tests compare the two pixel by pixel.

## What is compared today?

The standard and the accent button, at rest, hovered, pressed and disabled, dark and light (sixteen cases at 100 percent, without text), plus ten white-on-black corner radius probes (radii 4, 8 and 16 at scales 1, 1.5 and 2, and an asymmetric 40 by 20 probe with a single radius-16 corner).

| Part | May differ by |
| --- | --- |
| Straight edges and fills | 1 of 255, which is rounding |
| Corners of the standard button (dark) | 3 of 255 |
| Corners of the standard button (light) | 6 of 255 |
| Corners of the accent button | 14 of 255 |
| White-on-black radius probes | 40 of 255 |

The limits are in the test itself:

```c from=tests/fixtures/control_shots.c
                im_draw_button(&surface, (im_rect){20 * pad, 16 * pad, 140 * pad, 48 * pad}, (im_button_style)style, (im_button_state)state);
```

## How are corners smoothed?

Corner clamping follows WinUI's `CGeometryBuilder::CalculateRoundedCornersRectangle`, which partitions adjacent corner radii proportionally along each shared edge. Each quarter-circle arc is approximated by `N = 2^k` equal-angle inscribed chords using radius buckets matched to Direct2D's `0.25` px flattening step-ups (`N = 2` up to 3 px, `N = 4` up to 11 px, `N = 8` up to 46 px, `N = 16` up to 185 px, and exact ellipse above 185 px). The remaining difference on corners comes from Direct2D's GPU per-primitive triangle antialiasing, which is itself asymmetric by 8 to 24 of 255 across the four corners of a symmetric probe and by 11 of 255 between the top and left edges of an accent button, while Impossible UI preserves horizontal and vertical reflection symmetry.

## Where do the values come from?

From WinUI's MIT source at a pinned commit, generated into [theme values](site:values.html). Where a copy of that source is at hand, the tests fail when the generated file is not what it gives. No Microsoft font, icon, picture, sound or wording is used.
