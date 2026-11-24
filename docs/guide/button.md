<!-- page: group="Impossible UI" order=1 sources=ui/button.h,ui/draw.c,tests/test_ui.py,reference/App.xaml.cs,tests/reference/cases.json,tests/fixtures/control_shots.c,tools/winui/resources.py reviewed=2026-11-24 -->
# How a control is compared

How do we know it looks like Windows 11? A small program draws the same control with real WinUI 3, and the tests compare the two pixel by pixel.

## What is compared today?

The standard and the accent button, at rest, hovered, pressed and disabled, dark and light (sixteen cases at 100 percent, without text), plus ten white-on-black corner radius probes (radii 4, 8 and 16 at scales 1, 1.5 and 2, and an asymmetric 40 by 20 probe with a single radius-16 corner) are compared pixel by pixel against `control-shots.exe`. Additional multi-scale (150, 200 and 300 percent), RTL, SDK-owned icon, disabled-property, and scripted input captures live in the same verified store (`tests/reference/cases.json`), alongside the five-scale (100, 125, 150, 200 and 300 percent) panel layout oracle (`tests/fixtures/layout_reference.json`) for `Grid`, `StackPanel`, `Border`, `ContentPresenter`, and instantiated `Button` templates.

Run `python3 tools/winvm/live.py` to hold fresh WinUI captures against the stored ones in the test machine, and `IMPOSSIBLE_DISPLAY=desktop python3 reference/run.py --update-store` to atomically refresh `tests/reference/`.

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

Corner clamping follows WinUI's `CGeometryBuilder::CalculateRoundedCornersRectangle`, which partitions adjacent corner radii proportionally along each shared edge. Each quarter-circle arc is represented as a cubic Bezier (`kappa = 4*(sqrt(2)-1)/3`) and flattened in 16.16 fixed point into `N = 2^k` chords by Wang's formula at Direct2D's `0.25` px flattening tolerance (`N = 2` up to 2.9 px, `N = 4` up to 11.6 px, `N = 8` up to 46.4 px, `N = 16` up to 185.6 px, scaling up to `N = 256` for signed 16.16 radii under 32,768 px). The remaining difference on probe corners comes from Direct2D's GPU per-primitive triangle antialiasing, which varies by 8 to 24 of 255 across the four corners of a symmetric probe (4-way reflection floor of 4 to 12). On accent buttons, the measured corner limit of 14 of 255 stands on horizontal and vertical reflection symmetry (not diagonal symmetry).

## Where do the values come from?

From WinUI's MIT source at a pinned commit, generated into [theme values](site:values.html). Where a copy of that source is at hand, the tests fail when the generated file is not what it gives. No Microsoft font, icon, picture, sound or wording is used.
