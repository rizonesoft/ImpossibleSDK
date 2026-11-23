<!-- page: group="Impossible UI" order=1 sources=ui/button.h,tests/test_ui.py,reference/App.xaml.cs,tests/reference/cases.json reviewed=2026-11-24 -->
# How a control is compared

How do we know it looks like Windows 11? A small program draws the same control with real WinUI 3, and the tests compare the two pixel by pixel.

## What is compared today?

The standard and the accent button, at rest, hovered, pressed and disabled, dark and light: sixteen cases at 100 percent, without text.

| Part | May differ by |
| --- | --- |
| Straight edges and fills | 1 of 255, which is rounding |
| Corners of the standard button | 3 of 255 |
| Corners of the accent button | 27 of 255 |

The limits are in the test itself:

```c from=tests/fixtures/control_shots.c
                im_draw_button(&surface, (im_rect){20 * pad, 16 * pad, 140 * pad, 48 * pad}, (im_button_style)style, (im_button_state)state);
```

## Why are accent corners further off?

WinUI smooths its curves a little more softly than the exact area. Finding its rule is the first section of the [plan](site:controls.html).

## Where do the values come from?

From WinUI's MIT source at a pinned commit, generated into [theme values](site:values.html). The build fails when the generated file is not what the pinned source gives. No Microsoft font, icon, picture, sound or wording is used.
