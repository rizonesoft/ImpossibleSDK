---
schema_version: 1
id: first-controls
domain: 01-interface
status: active
title: "TODO-01 -- Interface Engine and First Controls"
---

# TODO-01 -- Interface Engine and First Controls

> **Goal:** Impossible UI draws and runs the first four Windows 11 controls from WinUI's own templates, in portable C that uses no floating point and calls no operating system function.

> **Current state:** **Groomed 2026-11-23:** `windows-host` was never met in WSL, so §1, §6 and §7 could not start; `todo/plan.json` now proves it with a probe. §8 to §18 were missing. Measured 2026-11-23 at `237b2e9`. `ui/theme.c` resolves every WinUI brush and number for three themes, `ui/draw.c` draws rounded shapes, `ui/button.c` draws the standard, accent and subtle button directly, and `reference/` draws the same cases with real WinUI. 16 cases at 100 percent compare: straight edges within 1 of 255, standard corners within 3, accent corners within 27. There is no element tree, layout, text, input or animation.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: the approved design.
- `tools/winui/pin.json`: the WinUI commit (MIT) that every value and behaviour comes from.
- `abi/tiers.json`: the only Windows functions the Windows layer may call.
- `todo/01-interface/TODO-02-text.md`: the text engine beyond the first lines of §5.

## Outcome

- The four controls match real WinUI in every state, theme and scale within the limits of `tests/test_ui.py`.
- `tests/test_ui.py` compiles `ui/` with the OS flags and finds no undefined symbol beyond four memory routines.

**Adjacency:** all=not-applicable (a drawing and control library holds no records, settings or reports)

**Adjacency rationale:** Programs own data and settings; this library owns only how controls look and behave.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Exact corner smoothing | -- | [x] |
| 2 | §2 | Template compiler | -- | [x] |
| 3 | §3 | Element tree and layout | §2, §17 | [x] |
| 4 | §4 | Visual states and animation | §3 | [x] |
| 5 | §5 | Text in fixed point | §1, §17 | [ ] |
| 6 | §6 | Input, focus and the Windows layer | §4, §5 | [ ] |
| 7 | §7 | Button, toggle switch, text box and list view | §6, §8, §9, §14, §17, T02 §2, T02 §5, T02 §6, D07 T01 §2 | [ ] |
| 8 | §8 | Host callback table | §3 | [ ] |
| 9 | §9 | Dirty rectangles and frame timing | §4, §8 | [ ] |
| 10 | §10 | Clips, opacity and layers | §1, §17 | [x] |
| 11 | §11 | Transforms and pictures | §10 | [ ] |
| 12 | §12 | Shadows and materials | §8, §10 | [ ] |
| 13 | §13 | Paths, arcs and gradients | §1, §17 | [ ] |
| 14 | §14 | Theme, accent and contrast changes while running | §4, §8 | [ ] |
| 15 | §15 | Theme transitions | §4, §11 | [ ] |
| 16 | §16 | Remaining panels | §3 | [ ] |
| 17 | §17 | Reference program for any control | -- | [x] |
| 18 | §18 | Drawing speed budgets | §9, §10, §12 | [ ] |
| 19 | §19 | Corner smoothing doc polish | §1 | [x] |
| 20 | §20 | Native 150% DPI fractional layout | §17 | [ ] |

## 1. Exact corner smoothing

> **Started:** 2026-11-24T00:16Z

**Corrected 2026-11-24:** re-planned with `todo-graph.py replan`; the build first began at 2026-11-23T23:55:00Z; its review chain is kept as `docs/reviews/01-interface/D01-T01-s1-chain1.md`.

The accent button differs from real WinUI by up to 27 of 255 at corner pixels, while straight edges differ by at most 1. WinUI's outer radius is the corner plus half the border; its curve smoothing is softer than the exact area.

**Corrected 2026-11-24:** Direct measurement of real WinUI 3 on Windows 11 (`reference/App.xaml.cs` probes at radii 4, 8, 16 and scales 1, 1.5, 2) proved that WinUI 3 / Direct2D per-primitive GPU antialiasing is itself asymmetric on symmetric shapes: on a 48x48 white-on-black `Border`, the four corners differ from each other by 8 to 24 of 255 (4-way reflection lower bound 4 to 12); on `accent Normal Dark/Light`, the measured corner error under horizontal and vertical reflection symmetry is 14 of 255 (down from 27 of 255 with an exact circle). The actual WinUI geometry rule is `CGeometryBuilder::CalculateRoundedCornersRectangle` (`dxaml/xcp/core/core/elements/geometry.cpp`, edge-wise proportional corner clamping and ±thickness/2 offsets) combined with Direct2D cubic-bezier quarter-circle chord flattening by Wang's formula at `D2D1_DEFAULT_FLATTENING_TOLERANCE = 0.25` px (`N=2` for `r<=2.9`, `N=4` for `r<=11.6`, `N=8` for `r<=46.4`, `N=16` for `r<=185.6`, up to `N=256` for signed 16.16 radii `< 32,768` px). `**Needs:**` includes `display-session` for WinUI capture. `tests/fixtures/control_shots.c` accepts the 7 accent shades from `cases.json["accents"]` (falling back to `tests/fixtures/accents.h`) and renders all 26 cases (16 standard/accent buttons + 9 symmetric probes + 1 asymmetric 40x20 probe). `tests/test_ui.py` verifies exact expected case identities independently of `cases.json` (rejecting duplicates, omissions, or mismatched pixel dimensions for each case's scale), enforces edge <= 1, standard button corner <= 6, accent button corner <= 14 (down from 27; exact circle fails at 27), and probes <= 40 (exact circle fails on r=8 at 47, r=16 at 44, r=32 at 58; old clamping fails on `probe-asym-s1` at 255). Subtle button reference coverage belongs to `D01 T01 §7`, while `ui_tests.c` checks subtle corners. `docs/guide/button.md` is updated and verified with `python3 tools/site/build.py --check`.

**Phase:** 1
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Add the 9 symmetric radius probes (4, 8, 16, white on black, no border, at scales 1, 1.5, and 2) and 1 asymmetric corner-radii probe (40x20, top-left radius 16, other corners 0) to `reference/App.xaml.cs`, run `reference/run.py`, and compress the frames into `tests/reference/` with `cases.json` recording `scale`, `width`, `height`, and `radii` per case. Done when: all 26 `.bgra.z` frames (16 button/accent + 10 probes) and updated `cases.json` are in `tests/reference/`.
2. Inspect `dxaml/xcp/core/core/elements/geometry.cpp` (`CGeometryBuilder::ClampCornerRadii` / `CalculateRoundedCornersRectangle`) and the probe frames, update `ui/draw.c` (edge-wise proportional corner fitting and D2D Wang-formula chord flattening) and `ui/draw.h` (public fitting contract), document separately what comes from `CGeometryBuilder` and what coverage rule was inferred from measured frames, and update `tests/fixtures/control_shots.c` to consume `cases.json["accents"]` and render all 26 cases. Done when: `tests/test_ui.py` passes with edge <= 1, standard corner <= 6, accent corner <= 14, and probes <= 40.
3. Update `docs/guide/button.md` with the verified limits and probe coverage and verify `python3 tools/site/build.py --check`. Done when: the site check passes.

- [x] Draw the 9 symmetric radius probe frames (radii 4, 8, 16, white on black, no border) at scales 1, 1.5 and 2 and 1 asymmetric probe (40x20, top-left 16, other corners 0) in `reference/App.xaml.cs` and store compressed `.bgra.z` frames in `tests/reference/`. Done when: all 10 probe frames are in `tests/reference/` and `tests/reference/cases.json` records `scale`, `width`, `height`, and corner radii per case. Cheaper substitute: scaling one bitmap.
- [x] Update `fit()` in `ui/draw.c` and the contract in `ui/draw.h` to match WinUI's edge-wise proportional corner clamping (`CGeometryBuilder::CalculateRoundedCornersRectangle`), implement D2D Wang-formula chord flattening in fixed point in `ui/draw.c`, and comment separately what comes from `CGeometryBuilder` and what coverage rule was inferred from measured frames. Done when: `ui/draw.c` predicts every symmetric and asymmetric probe pixel within 40 of 255 in `tests/test_ui.py` (beating the exact circle's 44 to 58 and old clamping's 255) and `ui_tests.c` checks asymmetric fitting and subtle corners. Cheaper substitute: a per-control lookup table.
- [x] Update `tests/fixtures/control_shots.c` to take the captured accent palette from `tests/reference/cases.json` and render all button and probe cases, and tighten `tests/test_ui.py` to require the exact 26 case identities independently of the manifest, verify pixel dimensions against logical bounds and scale, enforce edge <= 1, standard corner <= 6, accent corner <= 14, and probes <= 40 (subtle button reference coverage belongs to `D01 T01 §7`). Done when: `python3 tools/build.py` passes, restoring the old curve fails, and unit tests prove missing probes, relabelled scales, or different host accent palettes are caught. Cheaper substitute: excluding corner pixels.
- [x] Update `docs/guide/button.md` with the verified limits and probe coverage and check its source excerpt. Done when: `python3 tools/site/build.py --check` exits 0. Cheaper substitute: leaving stale tolerance numbers in the guide.
- [x] Commit: `ui: corners match WinUI`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

> **Verified:** 2026-11-24 | §1 | 78ed5edee4701fa53bda6fe762efb225b579fc36
> **Review:** `docs/reviews/d01-t01-s1.json`

## 2. Template compiler

> **Started:** 2026-11-24T00:46Z

WinUI's control templates hold the element tree, bindings, visual states and timings. `tools/winui/resources.py` already resolves 1,934 brushes and 444 numbers; this compiles the templates themselves.

**Corrected 2026-11-24:** The compiler inputs are the four controls in the pinned WinUI `git archive` commit (`tools/winui/pin.json`): `DefaultButtonStyle`, `AccentButtonStyle`, and `SubtleButtonStyle` (`Button_themeresources.xaml`); `DefaultToggleSwitchStyle` (`ToggleSwitch_themeresources.xaml`); `DefaultTextBoxStyle` including nested `DeleteButtonStyle` (`TextBox_themeresources.xaml`); `ListView` default style including `ItemsPanel` (`ItemsStackPanel`), `ItemContainerTransitions`, and `Border > ScrollViewer > ItemsPresenter` (`dxaml/xcp/dxaml/themes/generic.xaml:9284-9346`); and `DefaultListViewItemStyle` + `ListViewItemExpanded` (`ListViewItem_themeresources.xaml`). `tools/winui/templates.py` resolves timing and key-spline string resources (`Common_themeresources_any.xaml` etc.) into typed millisecond durations and 16.16 fixed-point splines (failing on unresolved references), maps private-use `SymbolThemeFontFamily` glyphs (`&#xE711;`, `&#xE73E;`) to portable `enum im_icon_id` values (refusing unmapped glyphs), tracks exact attribute/element `<file>:<line>` including multiline attributes, refuses unknown elements, properties, animations, binding modes, and converters, and emits portable fixed-point C tables in `ui/template.h`, `ui/template_data.h`, and `ui/template_data.c` (with MIT notice and doc comments). `.github/workflows/build.yml` (`ubuntu-latest`) fetches the pinned sparse WinUI checkout (cached by `tools/winui/pin.json`) so CI executes `--check` drift tests. `tests/fixtures/ui_tests.c` and `tests/test_ui.py` verify decoded C tables (hierarchy, nested templates, bindings/TargetNullValue, state groups, exact brush/value/timing/spline/icon values), independent drift on `.h` and `.c` without auto-repair, all 5 refusal categories with exact multiline line numbers, and guide pages `docs/guide/button.md` and `docs/guide/testing.md` are reviewed with `python3 tools/site/build.py --check`.

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Write portable template types in `ui/template.h` and the compiler in `tools/winui/templates.py` over the pinned checkout. Done when: Button (`DefaultButtonStyle`, `AccentButtonStyle`, `SubtleButtonStyle`), ToggleSwitch (`DefaultToggleSwitchStyle`), TextBox (`DefaultTextBoxStyle` + `DeleteButtonStyle`), ListView (`ListView`), and ListViewItem (`DefaultListViewItemStyle`, `ListViewItemExpanded`) compile with resolved timings, splines, and SDK icon IDs, and `--list` names every element and state.
2. Emit C tables in `ui/template_data.h` and `ui/template_data.c`, wire sparse WinUI checkout in `.github/workflows/build.yml`, add C and Python verification tests in `tests/fixtures/ui_tests.c` and `tests/test_ui.py`, and review `docs/guide/button.md` and `docs/guide/testing.md`. Done when: `python3 tools/build.py` and `python3 tools/site/build.py --check` pass.

- [x] Parse `ControlTemplate`, `ItemsPanelTemplate`, setters, bindings (including `TargetNullValue`), visual states, transitions, storyboards, timing/spline resources, and SDK icon mappings in `tools/winui/templates.py` and emit portable fixed-point C tables in `ui/template.h`, `ui/template_data.h`, and `ui/template_data.c`. Done when: all 8 styles (including `ListView`, nested `DeleteButtonStyle` and `Thumb.Template`, resolved timing/spline values, and portable `im_icon_id` values) compile, `--list` round-trips every element and state, and `tests/fixtures/ui_tests.c` plus `tests/test_ui.py` verify decoded C table values (failing on value-only corruption such as swapped brush IDs or dropped fallbacks). Cheaper substitute: hand-written C for each control.
- [x] Refuse an unknown element, unknown property, unknown animation, unknown binding mode, or unknown converter with exact `<file>:<line>` (including multiline attributes) in `tools/winui/templates.py`. Done when: five separate fixture templates in `tests/test_ui.py` (including a multiline attribute case) each fail with non-zero exit and the exact offending file and line. Cheaper substitute: ignoring unknown items.
- [x] Add `--check` drift tests (verifying `.h` and `.c` independently in scratch without auto-repair) in `tests/test_ui.py`, provision the pinned sparse WinUI checkout in `.github/workflows/build.yml`, and review `docs/guide/button.md` and `docs/guide/testing.md`. Done when: `python3 tools/build.py` and `python3 tools/site/build.py --check` pass and portable OS compilation passes. Cheaper substitute: skipping drift checks on CI.
- [x] Commit: `ui: compile WinUI control templates`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

> **Verified:** 2026-11-24 | §2 | c04706def209d5a16e3391476b178e10eaf46a9d
> **Review:** `docs/reviews/d01-t01-s2.json`

## 3. Element tree and layout

> **Started:** 2026-11-24T22:57Z

The engine that measures and arranges the compiled elements: Grid, StackPanel, Border, ContentPresenter, with layout rounding as WinUI does it.

**Corrected 2026-11-24:** Add `IM_ELEM_STACK_PANEL` to `im_element_kind` in `ui/template.h` and map `<StackPanel>` (plus `Orientation`, `Spacing`) in `tools/winui/templates.py` (without altering existing `ui/template_data.{h,c}` enum values). Implement the portable freestanding (no floats, no OS calls, no libc) element tree, property storage, live `TemplateBinding` forwarding from templated parent (ignoring non-visual transition records such as `IM_ELEM_BRUSH_TRANSITION`), ancestor-bubbling measure/arrange invalidation, template instantiation (`im_tree_instantiate_template` from `ui/template_data.c` resolving theme resources via `im_value(palette, id)` from `ui/theme.h:54`), and transactional caller-supplied allocator (`im_allocator`) in `ui/tree.h` and `ui/tree.c` so any allocation failure returns failure while leaving the tree unchanged with zero leaks. Implement WinUI plateau layout rounding (`im_layout_round`, ported from pinned `uielement.cpp:4881-4915` and `inlined.cpp:127-130`: `floor(v * scale + 0.5) / scale` in 16.16 fixed point, honoring `use_layout_rounding` true/false), unbounded measure constraint `IM_FIXED_INF = INT32_MAX`, measure/arrange core (`Margin`, `Min`/`Max`/`Width`/`Height`, `HorizontalAlignment`/`VerticalAlignment`, `Visibility`, `FlowDirection` RTL mirroring), and the four panels (`Grid` including its own `Padding`/`BorderThickness` per `Grid.cpp:986`, `RowDefinitions`/`ColumnDefinitions` Pixel/Auto/Star with `Min`/`Max`, `RowSpacing`/`ColumnSpacing`, `RowSpan`/`ColumnSpan`, and finite vs unbounded measurement; `StackPanel` vertical/horizontal + `Spacing` ignoring `Collapsed` children + `Padding`/`BorderThickness`; `Border` with `BorderThickness`/`Padding`; and `ContentPresenter` with `BorderThickness`/`Padding` + `HorizontalContentAlignment`/`VerticalContentAlignment`) in `ui/layout.h` and `ui/layout.c`. Register `ui/tree.c` and `ui/layout.c` in `parts.json` under `"ui"` and review `docs/guide/drawing.md`, `docs/guide/button.md`, and `docs/guide/testing.md` (documenting what the offline fixture and live Hyper-V receipt each prove). Extend `reference/App.xaml.cs` and `reference/Dump.cs` with an opt-in `layout_at_scale` mode (default `false`, leaving all committed `tests/reference/` frames unchanged) that bypasses `Viewbox` enlargement, scales every logical layout input (`Width`, `Height`, `Min`/`Max`, `Margin`, `Padding`, `BorderThickness`, `Spacing`, `RowSpacing`, `ColumnSpacing`, `GridLength` pixel and row/column min/max) by `scale` at 100% host DPI, records `layout_capture_mode` (`"scaled-inputs"`), `layout_scale`, and `DesiredSize`, and adds support for `ContentPresenter` children/content alignment, `Visibility`, `MaxWidth`/`MaxHeight`, `UseLayoutRounding`, and object `{length, min, max}` row/column definitions. Add a committed WinUI-captured five-scale (`1.0`, `1.25`, `1.5`, `2.0`, `3.0`) panel layout oracle `tests/fixtures/layout_reference.json` with fractional positions distinguishing plateau rounding from enlargement, and include `tests/fixtures/layout_reference.json` in `tools/winvm/live.py` `COVERED` (with a `tests/test_winvm.py` regression proving one altered rectangle makes a pass owed). Verify baseline + per-allocation failure + unchanged state + retry + zero leaks, collapse/restore + child replacement + ancestor remeasurement, instantiated `Button` with sized child + asymmetric local `Padding` override/clear, compiled `<StackPanel>` template instantiation, finite vs unbounded `DesiredSize` + `Rect`, `tests/reference/cases.json` button rects, and explicit required case/node assertions across all 5 scales in `tests/fixtures/ui_tests.c`, `tests/test_ui.py`, and live Hyper-V VM `tests/test_live.py` (`python3 tools/winvm/live.py`).

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `IM_ELEM_STACK_PANEL` to `ui/template.h` and `<StackPanel>` to `tools/winui/templates.py`, write `ui/tree.h`, `ui/tree.c`, `ui/layout.h`, and `ui/layout.c`, register the new sources in `parts.json`, and review `docs/guide/drawing.md`, `docs/guide/button.md`, and `docs/guide/testing.md`. Done when: `IM_TEMPLATE_BUTTON` and a compiled `<StackPanel>` template instantiate with verified hierarchy and `im_value`-resolved properties, and `python3 tools/check_parts.py` + `python3 tools/site/build.py --check` pass.
2. Port WinUI's `LayoutRound` and measure/arrange rules for `Grid` (including own `Padding`/`BorderThickness`), `StackPanel`, `Border`, and `ContentPresenter` from the pinned WinUI source, extend `reference/App.xaml.cs`, `reference/Dump.cs`, and `tools/winvm/live.py` (`COVERED`), capture the 5-scale (`1.0`, `1.25`, `1.5`, `2.0`, `3.0`) WinUI layout oracle `tests/fixtures/layout_reference.json` in the Hyper-V VM, and add C/Python/live-VM geometry and mutation tests in `tests/fixtures/ui_tests.c`, `tests/test_ui.py`, `tests/test_winvm.py`, and `tests/test_live.py`. Done when: `python3 tools/build.py`, `python3 tools/winvm/live.py`, and `python3 tools/site/build.py --check` exit 0.

- [x] Implement the tree, properties, live `TemplateBinding` forwarding, template instantiation (`im_value`), and invalidation in `ui/tree.h` and `ui/tree.c` with a caller-supplied allocator (`im_allocator`), plus `<StackPanel>` support in `ui/template.h` and `tools/winui/templates.py`. Done when: `tests/fixtures/ui_tests.c` proves baseline layout, observable failure on every injected allocation with unchanged tree/geometry and successful retry with zero leaks, collapse/restore, child replacement, ancestor remeasurement, `Button` local `Padding` override/clear, and compiled `<StackPanel>` template instantiation. Cheaper substitute: a fixed array.
- [x] Implement WinUI layout rounding (`use_layout_rounding` on/off), `IM_FIXED_INF` unbounded measurement, and measure/arrange for `Grid` (with own `Padding`/`BorderThickness`, Pixel/Auto/Star, `Min`/`Max`, spans, spacing), `StackPanel` (vertical/horizontal, `Spacing`, `Collapsed`), `Border`, and `ContentPresenter` (content alignment, RTL) in `ui/layout.h` and `ui/layout.c` and register in `parts.json`. Done when: desired sizes and element rectangles equal those dumped by `reference/` across `tests/fixtures/layout_reference.json` and `tests/reference/cases.json`. Cheaper substitute: fixed offsets.
- [x] Extend `reference/App.xaml.cs` and `reference/Dump.cs` with `layout_at_scale` (`"scaled-inputs"`, `DesiredSize`, `ContentPresenter`, `Visibility`, `MaxWidth`/`MaxHeight`, `UseLayoutRounding`, row/col `Min`/`Max`), add `tests/fixtures/layout_reference.json` to `tools/winvm/live.py` `COVERED`, capture the 5-scale (`1.0`, `1.25`, `1.5`, `2.0`, `3.0`) WinUI layout oracle in the Hyper-V VM, and verify explicit required cases and nodes in `tests/test_ui.py`, `tests/test_winvm.py`, and `tests/test_live.py`. Done when: `tests/test_ui.py` compares `ui/layout.c` rectangles and desired sizes at all five scales and `python3 tools/winvm/live.py` verifies the oracle against real WinUI.
- [x] Commit: `ui: element tree and layout`

**Test checkpoint:** `python3 tools/build.py`, `python3 tools/winvm/live.py --check`, and `python3 tools/site/build.py --check` exit 0, and the new tests fail when their production change is reverted.

> **Verified:** 2026-11-25 | §3 | 66c68845104c2efa45e9813370571578800b37d1
> **Review:** `docs/reviews/d01-t01-s3.json`

## 4. Visual states and animation

> **Started:** 2026-11-25T02:45Z

States, transitions and storyboards run from a caller-supplied clock, so a frame can be drawn for any instant.

**Corrected 2026-11-25:** Implement portable freestanding (no floats, no OS calls, no libc) fixed-point animation evaluation in `ui/animate.h`/`ui/animate.c`, compiled visual-state execution in `ui/states.h`/`ui/states.c`, and non-destructive visual-state layout override support in `ui/tree.h`/`ui/tree.c`, registering `ui/animate.c` and `ui/states.c` in `parts.json` under `"ui"` (`cmake/ImpossibleSDK.cmake` discovers `ui/*.c` automatically) and adding `ui/animate.h,ui/states.h` to `docs/guide/drawing.md` `sources=` (reviewed `2026-11-25`). `ui/animate.{h,c}` provides: (a) high-precision cubic Bezier `KeySpline` evaluation (`im_spline_eval(x1, y1, x2, y2, t)` and exact-ratio `im_spline_eval_ratio(x1, y1, x2, y2, num, den)`) solving `B_x(u) = num / den` in 32.32 / 64-bit integer arithmetic via monotonic bisection without pre-quantizing time to 16.16, so track outputs match exact double-precision XAML cubic Bezier curves within 1 raw 16.16 unit at every millisecond including `1 ms / 83 ms` on `{0,0,0,1}`, endpoints, and descending tracks; (b) unsigned 32-bit wrap-safe elapsed time `(uint32_t)(now_ms - start_ms)` (valid for elapsed `< 2^31` ms), clamping before normalization and widening to `int64_t` before endpoint arithmetic; (c) track evaluators (`im_anim_eval_fixed`, `im_anim_eval_color`, `im_anim_eval_object`) covering `IM_ANIM_OBJECT_KEYFRAMES` (discrete typed values), `IM_ANIM_DOUBLE`, `IM_ANIM_DOUBLE_KEYFRAMES` (`IM_KF_DISCRETE`, `IM_KF_LINEAR_DOUBLE`, `IM_KF_SPLINE_DOUBLE`, `IM_KF_EASING_DOUBLE`), `IM_ANIM_COLOR_KEYFRAMES` (`IM_KF_LINEAR_COLOR` with both `IM_TVAL_COLOR` and `IM_TVAL_BRUSH` endpoints including `IM_CONTRAST` palettes, implicit start before first key, and multi-key tracks from 0), `IM_ANIM_REPOSITION_THEME` (PVL 367 ms, cubic Bezier `0.1, 0.9, 0.2, 1.0`, independently animating `from_val` on X and `from_y_val` on Y to 0, supporting both literal `IM_TVAL_FIXED` and `IM_TVAL_BINDING`), `IM_ANIM_FADE_IN_THEME` / `IM_ANIM_FADE_OUT_THEME` (167 ms linear `Opacity` to `IM_ONE` / `0`), `IM_ANIM_DRAG_OVER_THEME` (200 ms cubic Bezier `0.1, 0.9, 0.2, 1.0` directional offset on `DragOverX`/`DragOverY` with sign `-` for `Left`/`Top` and `+` for `Right`/`Bottom`), `IM_ANIM_DRAG_ITEM_THEME` (240 ms `ScaleX`/`ScaleY` to `1.05` and `Opacity` multiplier `0.65` per pinned `ThemeAnimations.cpp`), and `IM_ANIM_DROP_TARGET_THEME` (240 ms `ScaleX`/`ScaleY` to `0.95` per pinned `ThemeAnimations.cpp`); (d) `im_brush_transition` (83 ms straight ARGB 4-channel interpolation, including alpha, on Button `ContentPresenter` `Background` only, per `IM_ELEM_BRUSH_TRANSITION`; `BorderBrush` switches immediately at `t = 0`), retargeting seamlessly from the current presentation ARGB on mid-flight interruption even without an intervening draw; (e) `reduced_motion` snapping both newly started and currently in-flight animations/transitions (including both X and Y reposition axes) to their end state on the same tick (`now_ms == start_ms`) with `is_animating == false`, and allowing later transitions to animate when re-enabled. `ui/states.{h,c}` provides `im_state_machine` over compiled `im_template` tables (`ui/template_data.c`) supporting up to 8 independent state groups, WinUI transition selection (`VisualStateManager.cpp:219-252`: case-insensitive state name matching, retaining the last default `(NULL,NULL)` transition, and strict-greater-than first-win scoring across exact `(from,to)`=3, destination-only `(NULL,to)`=2, source-only `(from,NULL)`=1), zero-track generated transitions (`duration_ms > 0, anim_count == 0`, interpolating affected outgoing properties smoothly back to destination/base values over `duration_ms`), caller-supplied bindings (`im_states_set_binding`) with `{ThemeResource ...}` fallback resolution (e.g. TextBox `PlaceholderForeground` resolving caller binding when supplied or `{ThemeResource TextControlPlaceholderForeground*}` fallback when absent, and unbound `TemplateSettings.*` offsets defaulting cleanly to `0`), outgoing-state removal within only the changed group, no-op `false` return on repeating the current state or unknown state, `im_states_apply_to_tree` connecting layout-affecting presentation values (`Width`, `Height`, `Margin`, `Visibility`, `HorizontalAlignment`, `VerticalAlignment`, `BorderThickness`) to `im_tree` without losing underlying local values, and `im_states_draw_button` reading solely the state machine's resolved `Background` (as straight ARGB during transition or brush when settled), `BorderBrush`, and `BackgroundSizing`. Verify in `tests/fixtures/ui_tests.c` and `tests/test_ui.py`: (1) `im_states_draw_button` on settled frames (`now_ms >= start_ms + 83` or `reduced_motion`) matches `im_draw_button` byte-for-byte across all 24 Dark/Light x Standard/Accent/Subtle x 4-state frames, PLUS a custom-template test proving changing one compiled brush changes `im_states_draw_button` pixels and checking `Foreground` + `AnimatedIcon.State` setters; (2) `im_spline_eval` and `im_anim_eval_fixed` match exact double-precision Bezier within 1 raw 16.16 unit at 11+ ticks (including `1 ms / 83 ms`, wrap-crossing `0xFFFFFFF0u`, late sample, descending track), ToggleSwitch composed knob position (`KnobTranslateTransform.X + SwitchKnob.RepositionX`) matches for `Off -> On`, `On -> Off`, partial drag release, and unbound default 0, plus a 2D reposition test verifying independent X/Y literal + binding offsets reaching 0 and snapping under `reduced_motion`; (3) Button 83 ms `Background` `BrushTransition` checks straight ARGB alpha + composited center pixel on a dark backdrop on standard Dark Button (`0x0FFFFFFF -> 0x15FFFFFF`) and RGB channels on Accent Button at 21 ms and 62 ms, immediate `BorderBrush` switch at 0 ms, continuous ARGB handoff on interruption at 40 ms without an intervening draw, and settled frame at 83 ms; (4) wildcard scoring `(A->*)` vs `(*->B)` + case-insensitive matching + last-default `(NULL,NULL)` retention, independent groups + mid-flight reversal + repeat/unknown no-op, TextBox `Focused -> Normal` with and without caller `PlaceholderForeground` binding plus nested `DeleteButton` press while TextBox stays `Focused`, ListViewItemExpanded `DisabledStates`/`MultiSelectStates` (visibility either side of 333 ms)/`DataVirtualizationStates`/`ReorderHintStates`/`DragStates` including directional `DragOver`, `DragItem`, `FadeIn`/`FadeOut`, `DropTarget`, and interior + settled values on both 200 ms generated returns (`NoReorderHint`, `NotDragging`), `im_states_apply_to_tree` proving layout measured/arranged geometry changes on ToggleSwitch `Pressed` and ListViewItem `MultiSelectEnabled` and restores the underlying local value when cleared, `im_anim_eval_color` (`IM_TVAL_COLOR` + `IM_TVAL_BRUSH` + `IM_CONTRAST`), and `reduced_motion` active/mid-flight/re-enable.

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/animate.h`, `ui/animate.c`, `ui/states.h`, `ui/states.c`, add visual-state layout override support in `ui/tree.h`/`ui/tree.c`, register `ui/animate.c` and `ui/states.c` in `parts.json` under `"ui"`, and update `docs/guide/drawing.md`. Done when: `python3 tools/check_parts.py` and `python3 tools/site/build.py --check` pass and Button, ToggleSwitch, TextBox (including nested DeleteButton), and ListViewItemExpanded state machines resolve expected brushes, transforms, visibility, and layout overrides.
2. Add comprehensive spline/clock-wrap/color/object-keyframe, ToggleSwitch composed knob + 2D reposition, Button 83 ms ARGB brush-transition + interruption + custom-brush parity, wildcard/default priority, independent-group, TextBox binding/fallback + nested DeleteButton, ListViewItem multi-select/drag/fade/generated-return, layout-tree integration, and reduced-motion tests in `tests/fixtures/ui_tests.c` and `tests/test_ui.py`. Done when: `python3 tools/build.py` and `python3 tools/site/build.py --check` exit 0.

- [x] Run state groups, transitions, bindings/fallbacks, layout overrides, and setters in `ui/states.h`/`ui/states.c` and `ui/tree.h`/`ui/tree.c`. Done when: every settled state (`Normal`, `PointerOver`, `Pressed`, `Disabled`) of `IM_TEMPLATE_BUTTON`, `IM_TEMPLATE_BUTTON_ACCENT`, and `IM_TEMPLATE_BUTTON_SUBTLE` in Dark and Light themes produces the exact same frame as `im_draw_button` (0 pixel diffs), and ToggleSwitch, TextBox, and ListViewItemExpanded state values and layout overrides match WinUI. Cheaper substitute: keeping `ui/button.c`.
- [x] Implement high-precision cubic Bezier key splines, keyframe tracks, theme animations (`Reposition`, `FadeIn`, `FadeOut`, `DragOver`, `DragItem`, `DropTarget`), generated transitions, and ARGB `BrushTransition` in `ui/animate.h`/`ui/animate.c`. Done when: values at 10+ instants (including `1 ms / 83 ms`) match the exact XAML-defined cubic Bezier curve within 1 raw 16.16 unit and mid-flight brush transitions interpolate ARGB and retarget cleanly. Cheaper substitute: linear steps.
- [x] Honour the reduced-animation setting (`reduced_motion`) in `ui/animate.c` and `ui/states.c`. Done when: a test sees the exact end state on the first frame (`now_ms == start_ms`) with `im_states_is_animating == false`, both for newly started and interrupted transitions, and animation resumes when cleared.
- [x] Commit: `ui: visual states and animation`

**Test checkpoint:** `python3 tools/build.py`, `python3 tools/check_parts.py`, and `python3 tools/site/build.py --check` exit 0, and the new tests fail when their production change is reverted.

> **Verified:** 2026-11-25 | §4 | 2e0fa09c93330d9c301539ba1b543a871f0860f9
> **Review:** `docs/reviews/d01-t01-s4.json`

## 5. Text in fixed point

Glyph outlines, hinting-free smoothing, shaping order for left-to-right text and line layout, with no floating point.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/font.c` reading TrueType and OpenType outlines. Done when: every glyph of Selawik and Cascadia Code draws.
2. Write `ui/text.c`. Done when: advance widths equal DirectWrite's for the same font.

- [ ] Parse tables with bounds checks in `ui/font.c`. Done when: a libFuzzer run of 60 seconds on native Linux finds nothing. Cheaper substitute: trusting the file.
- [ ] Rasterise outlines with the coverage code of `ui/draw.c`. Done when: glyph boxes and advances equal `reference/` output. Cheaper substitute: floating point.
- [ ] Break and align lines in `ui/text.c`. Done when: the same paragraph breaks at the same words as a WinUI TextBlock using Segoe UI. Cheaper substitute: breaking on spaces only.
- [ ] Commit: `ui: text without floating point`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Input, focus and the Windows layer

Pointer, keyboard, text input and focus move through the tree; `src/window.c` becomes the thin Windows layer.

**Phase:** 1
**Needs:** windows-host
**Surface:** yes
**Fidelity:** real WinUI at the pinned commit, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user reads and operates the control exactly as on Windows 11.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/input.c` and `ui/focus.c`. Done when: a recorded event list gives the expected states.
2. Extend `src/window.c` with wheel, text input, touch and DPI changes inside tier 1. Done when: the import check passes.

- [ ] Hit-test, capture and route pointer events in `ui/input.c`. Done when: press, leave and release cancels a click. Cheaper substitute: per-control hit tests.
- [ ] Tab, arrow and access-key movement in `ui/focus.c` ported from WinUI's focus rules. Done when: the order equals real WinUI for the gallery page. Cheaper substitute: creation order.
- [ ] Draw the two-ring focus visual only for keyboard focus in `ui/input.c`. Done when: the frame equals `reference/`. Cheaper substitute: a single outline.
- [ ] Commit: `ui: input, focus and the Windows layer`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Button, toggle switch, text box and list view

The first four controls from compiled templates, with behaviour ported from the pinned MIT source and the file named beside it.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the pinned commit, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user reads and operates the control exactly as on Windows 11.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/controls/` for the four. Done when: every state equals `reference/` in dark, light and contrast at 100 to 300 percent.
2. Write `samples/gallery/main.c`. Done when: the four work by keyboard alone.

- [ ] Replace `ui/button.c` with the compiled button and its click, repeat and key rules. Done when: the old comparison still passes. Cheaper substitute: keeping both.
- [ ] Build the toggle switch with drag in `ui/controls/toggle.c`. Done when: drag, click and Space give WinUI's results. Cheaper substitute: click only.
- [ ] Build the text box with caret, selection, clipboard and undo in `ui/controls/textbox.c`. Done when: a recorded key script gives the same text and selection as WinUI. Cheaper substitute: append-only input.
- [ ] Build the list view with selection modes and virtual rows in `ui/controls/listview.c`. Done when: 100,000 rows scroll with at most the visible rows drawn. Cheaper substitute: drawing all rows.
- [ ] Name every `.c` below `ui/` in `ui/sources.json` as portable or optional, read by `cmake/ImpossibleSDK.cmake` and the portable test of `tests/test_ui.py`; both read only `ui/*.c` today. Done when: an unnamed file is refused, optional files stay out of the portable build, a Windows call planted in `ui/controls/` and one in `ui/dialogs/` each fail the portable test. Cheaper substitute: a folder at a time.
- [ ] Show the list view's hover, pressed, selected and focus states in `ui/controls/listview.c`. Done when: every state of the pinned ListViewItem template matches. Cheaper substitute: hover and selected.
- [ ] Add the gallery page and its keyboard walk to `tests/test_ui.py`. Done when: the walk passes on Windows.
- [ ] Commit: `ui: the first four controls`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. Host callback table

> **Started:** 2026-11-25T03:48Z

The portable engine asks its host for everything the system owns: memory, time, clipboard, cursor, caret timing, input method, opening a link, sound, files and the screen reader. One table of callbacks, filled in by the Windows layer here and by the OS there.

**Corrected 2026-11-25:** Define the versioned and sized portable host callback table `im_host` (`IM_HOST_VERSION = 2`, accepting `version` in `[1, IM_HOST_VERSION]` and refusing `version == 0` or `version > IM_HOST_VERSION` with `IM_HOST_INVALID`; minimum readable prefix `offsetof(im_host, ctx) + sizeof(host->ctx)`; `IM_HOST_V1_SIZE` covering the v1 prefix `{version, size, ctx, alloc, realloc, free, now_ms}`; v2 adding clipboard `clipboard_get_utf8`/`clipboard_set_utf8`, `set_cursor`, `caret_blink_ms`, `ime_set_rect`, `open_url`, `play_sound`, `file_pick`, and screen-reader `announce`; `im_host_result` with `IM_HOST_OK`, `IM_HOST_UNSUPPORTED`, `IM_HOST_INVALID`, `IM_HOST_FAILED`, `IM_HOST_CANCELLED`) in `ui/host.h` and portable freestanding safe-default wrappers (`im_host_accept`, `im_host_require`, `im_host_allocator`, and one wrapper per callback) in `ui/host.c`. Require a partial memory callback set (missing any of `alloc`, `realloc`, or `free`) to be refused as a unit so allocations always have a matching release. Define explicit output contracts when callbacks are absent (`*out_len = 0` and NUL-terminated buffer for `clipboard_get_utf8`/`file_pick` returning `IM_HOST_UNSUPPORTED`, `*out_ms = 530` returning `IM_HOST_UNSUPPORTED` for `caret_blink_ms`, `0` for `now_ms`, caller-owned output buffers and borrowed `const char *` input strings). Register `ui/host.c` in `parts.json` under `"ui"`. Implement the Windows host initializer `im_win_host_init` with a distinct header stem `include/impossible/win_host.h` (forward-declaring `struct im_host` so consumers need only `ImpossibleSDK`'s `include/` path and `tools/site/build.py` publishes both `api-host.html` and `api-win_host.html`) and `src/host.c` (registered in `cmake/ImpossibleSDK.cmake`), filling memory (`HeapAlloc` when `ptr == NULL` in `realloc`, `HeapFree` when `new_size == 0`, `HeapReAlloc` otherwise), monotonic `now_ms` (`QueryPerformanceCounter`/`QueryPerformanceFrequency`), and cursor (`LoadCursorW`/`SetCursor`, retaining the requested client cursor on `im_window` across `WM_SETCURSOR` in `src/window.c`), using only Tier 0/1 imports from `abi/tiers.json`. Wire `samples/window/main.c` to use `im_win_host_init` (driving cursor and monotonic clock through the table) and review `docs/guide/drawing.md` (adding `ui/host.h` to `sources=`), `docs/guide/first-window.md`, and `docs/guide/own-program.md`. Add `tests/fixtures/test_host.c` (`test-host` CONSOLE TIER 1 in `CMakeLists.txt`) implementing a recording host and verifying: (1) genuinely shorter v1 backing struct accepted by `im_host_accept`, absent trailing slot, present NULL slot, partial memory callback set refused as a unit, and incompatible version (`0` and `99`) refused with exact names/return codes/output states; (2) empty and NULL table safe defaults across all wrappers plus `im_tree`/`im_layer_ctx`/`im_state_machine`; (3) real consumers driven by the recording host (`im_tree` first child append via `realloc(NULL, ...)`, growth preserving children, refusal preserving children, zero-leak teardown, `im_layer_ctx`, `im_state_machine` transition driven by advancing host clock) plus all 10 wrapper domains forwarding `ctx`, arguments, results, and outputs in exact recorded order; (4) live `im_win_host_init` memory (`realloc(NULL, ...)` first append, resize, free), monotonic clock, and non-arrow cursor selection retained across `WM_SETCURSOR` followed by restoration to arrow. Run `test-host.exe` in `tests/test_ui.py` and refactor `Portable` to run the same freestanding symbol checker on both clean `ROOT/ui` and a planted-import temp copy (asserting clean compilation and that the planted symbol appears in the unresolved set).

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/host.h`, `ui/host.c`, `include/impossible/host.h`, and `src/host.c`, register `ui/host.c` in `parts.json` and `src/host.c` in `cmake/ImpossibleSDK.cmake`, wire `samples/window/main.c` through `im_win_host_init`, and update `docs/guide/drawing.md`. Done when: `python3 tools/check_parts.py`, `python3 tools/site/build.py --check`, and `cmake --build build/linux --target hello-window` pass.
2. Add `tests/fixtures/test_host.c` (recording host, version/size compatibility, empty-table safety, Windows host callbacks) in `CMakeLists.txt`, and wire `test-host.exe` + the planted-import negative check in `tests/test_ui.py`. Done when: `python3 tools/build.py`, `python3 tools/check_parts.py`, and `python3 tools/site/build.py --check` exit 0.

- [ ] Define the table, its version and its size field in `ui/host.h`. Done when: a host built for an older version (smaller `size`) is accepted by `im_host_accept` and its missing callbacks are refused by exact name via `im_host_require` without out-of-bounds reads. Cheaper substitute: a global per service.
- [ ] Give every callback a defined answer when it is absent in `ui/host.c`. Done when: `test-host.exe` runs every callback wrapper and `im_tree`/`im_layer_ctx`/`im_state_machine` with an empty or NULL table without crashing. Cheaper substitute: requiring all callbacks.
- [ ] Fill the memory, time and cursor callbacks in `src/host.c` (`include/impossible/host.h`). Done when: `samples/window/main.c` runs through `im_win_host_init` and `test-host.exe` proves Tier 0/1 memory, monotonic time, and cursor callbacks. Cheaper substitute: direct calls from `ui/`.
- [ ] Write a recording host in `tests/fixtures/test_host.c`. Done when: `test-host.exe` invokes all 10 callback domains and reads back every call and argument in exact order. Cheaper substitute: a mock per test.
- [ ] Refuse a system call from `ui/` in `tests/test_ui.py`. Done when: a planted OS import in a temporary copy of `ui/` fails the portable freestanding build test. Cheaper substitute: review.
- [ ] Commit: `ui: the host callback table`

**Test checkpoint:** `python3 tools/build.py`, `python3 tools/check_parts.py`, and `python3 tools/site/build.py --check` exit 0, and the new tests fail when their production change is reverted.

## 9. Dirty rectangles and frame timing

A frame redraws only what changed, and nothing runs when nothing changes.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/dirty.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Collect changed rectangles from the tree in `ui/dirty.c`. Done when: random changes drawn partially equal a full redraw, byte for byte, 10,000 times. Cheaper substitute: redrawing the window.
- [ ] Merge rectangles under a fixed limit in `ui/dirty.c`. Done when: 1,000 changes give at most 16 rectangles that cover them all. Cheaper substitute: one bounding box.
- [ ] Ask the host for the next frame only while an animation or change is pending, in `ui/frame.c`. Done when: an idle window makes no call for 10 seconds. Cheaper substitute: a steady timer.
- [ ] Pass the rectangles to Windows in `src/window.c`. Done when: a hover over one button paints only its area, read from the update region. Cheaper substitute: painting everything.
- [ ] Bring back a frame after the window is moved, covered or resized, in `src/window.c`. Done when: the pixels equal a fresh draw. Cheaper substitute: waiting for the next change.
- [ ] Commit: `ui: dirty rectangles and frame timing`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 10. Clips, opacity and layers

> **Started:** 2026-11-24T23:38Z

Rounded clips, opacity over a group and nested layers, as pop-ups, cards and disabled content need them.

**Corrected 2026-11-24:** Export shared rounded-rectangle coverage (`im_shape_coverage`) from `ui/draw.h`/`ui/draw.c` and add an optional `struct im_layer_ctx *layers` field to `im_surface` (NULL by default, preserving all existing initializers and the opaque `0x00RRGGBB` canvas contract) so `im_fill`, `im_stroke`, and `im_draw_button` automatically honor active clips, active fallback opacity, and active offset premultiplied-RGBA layers. Implement portable freestanding (no floats, no OS calls, no libc) growable rectangular and rounded-rectangle clip stacks in `ui/clip.h`/`ui/clip.c` (caller-supplied `im_allocator`, intersected integer bounds, `bool` push leaving the stack unchanged on growth failure). Implement portable freestanding bounded premultiplied-RGBA offscreen layers in `ui/layer.h`/`ui/layer.c` with: (a) bounds intersected against parent layer and active clip box BEFORE checking `bytes_used + needed_bytes <= memory_limit`, refusing over-budget buffers before calling `alloc.alloc` and tracking cumulative live buffer bytes across nested layers; (b) `im_layer_result` (`IM_LAYER_OK`, `IM_LAYER_FALLBACK`, `IM_LAYER_ERR`) where `IM_LAYER_FALLBACK` pushes a direct-draw frame multiplying group opacity into child draws while honoring outer clips and matching `im_layer_pop`, and `IM_LAYER_ERR` leaves stack state unchanged; (c) growable stacks supporting arbitrary interleaved clip-inside-layer and layer-inside-clip nesting beyond depth 32. Register `ui/clip.c` and `ui/layer.c` in `parts.json` under `"ui"`, add `ui/draw.h`, `ui/draw.c`, `ui/clip.h`, `ui/clip.c`, `ui/layer.h`, and `ui/layer.c` to `tools/winvm/live.py` `COVERED`, and review `docs/guide/drawing.md` (documenting push/pop/fallback contract), `docs/guide/button.md`, and `docs/guide/testing.md`. Verify in `tests/fixtures/ui_tests.c`, `tests/test_ui.py`, `tests/test_winvm.py`, and live Hyper-V VM `tests/test_live.py` (`python3 tools/winvm/live.py`): (1) `im_draw_button` inside an offset layer with a rounded clip crossing fill and stroke; (2) oversized contrasting child clipped by rect/rounded-rect at 100%, 150%, 200%, 300%, empty clip intersection, and post-pop sibling, plus a real WinUI rounded-clip + group-opacity scene compared in the Hyper-V VM (straight edges <= 2, rounded corners <= 40); (3) group opacity at 0, 128, and 255 over a colored background checking untouched pixel, overlapping opaque children (no bleed-through), and translucent/antialiased child premultiplication; (4) exact-fit, over-budget, cumulative nested over-budget (recording allocator proves refused buffer is never requested), parent-pop capacity reuse, and injected stack-growth/buffer-alloc failures with sibling integrity and retry; (5) deterministic random tree of depth 40 (> 32) with interleaved clip-inside-layer and layer-inside-clip matching an independent plain reference renderer at max depth and after unwinding.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Export `im_shape_coverage` and wire `surface->layers` in `ui/draw.{h,c}`, write `ui/clip.{h,c}` and `ui/layer.{h,c}`, register both in `parts.json`, add `ui/{draw,clip,layer}.{h,c}` to `tools/winvm/live.py` `COVERED`, and review `docs/guide/drawing.md`, `docs/guide/button.md`, and `docs/guide/testing.md`. Done when: `python3 tools/check_parts.py` and `python3 tools/site/build.py --check` pass and `ui/` builds freestanding without floats or OS calls.
2. Cover `im_draw_button` under clip+layer, rounded/rect clips at 100% to 300% + empty intersection + post-pop sibling, group opacity at 0/128/255 with translucent child, cumulative `memory_limit` + recording allocator + allocation-failure stack recovery, depth-40 (> 32) interleaved random tree vs plain reference renderer, and live Hyper-V WinUI clip+opacity comparison in `tests/fixtures/ui_tests.c`, `tests/test_ui.py`, `tests/test_winvm.py`, and `tests/test_live.py`. Done when: `python3 tools/build.py`, `python3 tools/winvm/live.py`, and `python3 tools/site/build.py --check` exit 0.

- [x] Clip drawing (`im_fill`, `im_stroke`, `im_draw_button`) to rectangles and rounded rectangles in `ui/clip.{h,c}` and `ui/draw.{h,c}`. Done when: oversized contrasting children, empty intersections, post-pop siblings, and 100% to 300% rounded-clip frames (including real WinUI clip+opacity in the Hyper-V VM) match within stated limits. Cheaper substitute: rectangles only.
- [x] Draw a group with one opacity through a bounded premultiplied-RGBA layer in `ui/layer.{h,c}`. Done when: group opacity at 0, 128, and 255 over a colored background matches exact source-over math for untouched, overlapping opaque (no bleed-through), and translucent/antialiased child pixels, including `im_draw_button` in an offset layer. Cheaper substitute: opacity on each child.
- [x] Enforce cumulative `memory_limit` across nested layers and recover cleanly from allocation failure in `ui/layer.{h,c}` and `ui/clip.{h,c}`. Done when: a recording allocator proves over-budget buffers are never requested, parent pop frees budget for reuse, and injected stack-growth or buffer failures leave outer clips intact, draw every element without crashing, preserve subsequent siblings, retry cleanly, and leak zero bytes. Cheaper substitute: a crash.
- [x] Nest clips and layers to arbitrary depth (> 32) in `ui/layer.{h,c}`. Done when: a deterministic random tree of depth 40 with interleaved clip-inside-layer and layer-inside-clip equals a plain independent reference renderer byte-for-byte at max depth and after unwinding. Cheaper substitute: one level or a fixed 32-entry stack.
- [x] Commit: `ui: clips, opacity and layers`

**Test checkpoint:** `python3 tools/build.py`, `python3 tools/winvm/live.py`, `python3 tools/winvm/live.py --check`, and `python3 tools/site/build.py --check` exit 0, and the new tests fail when their production change is reverted.

> **Verified:** 2026-11-25 | §10 | 66c68845104c2efa45e9813370571578800b37d1
> **Review:** `docs/reviews/d01-t01-s10.json`

## 11. Transforms and pictures

Moving, scaling and turning elements, and drawing a picture held in memory.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/transform.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Apply translate, scale, rotate and skew in fixed point in `ui/transform.c`. Done when: points agree with exact arithmetic within 1/256 pixel. Cheaper substitute: translate only.
- [ ] Draw shapes and text through a transform in `ui/draw.c`. Done when: a chevron turned 90 degrees equals the reference within the corner limit. Cheaper substitute: a second drawing.
- [ ] Draw premultiplied pictures with the filter WinUI uses, in `ui/picture.c`. Done when: reference frames for none, fill, uniform and uniform-to-fill match within 2 of 255. Cheaper substitute: nearest pixel.
- [ ] Cut nine-part pictures in `ui/picture.c`. Done when: corners keep their size at every scale. Cheaper substitute: stretching.
- [ ] Map pointer positions back through transforms in `ui/input.c`. Done when: random transforms hit the element a plain inverse hits. Cheaper substitute: untransformed hit tests.
- [ ] Commit: `ui: transforms and pictures`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 12. Shadows and materials

Elevation shadows, and the solid or see-through grounds of windows, flyouts and menus. The layer behind the window draws Mica and Acrylic; the engine marks where and draws the fallback colour.

**Phase:** 1
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `ui/shadow.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Blur a rounded shape in integers in `ui/shadow.c`. Done when: shadows at elevations 2, 4, 8, 16, 32 and 128 match real WinUI within 3 of 255. Cheaper substitute: a dark outline.
- [ ] Cache shadows by size, radius and elevation in `ui/shadow.c`. Done when: the second frame of a menu computes no blur. Cheaper substitute: blurring each frame.
- [ ] Name the Mica, Mica Alt and Acrylic areas in the frame given to the host, in `ui/material.c`. Done when: the recording host receives each area with its tint and fallback. Cheaper substitute: a fixed colour.
- [ ] Draw the fallback colours of the theme data in `ui/material.c`. Done when: contrast themes and transparency-off match real WinUI. Cheaper substitute: one colour.
- [ ] Ask Windows for its backdrop where it has one, in `src/window.c`. Done when: Mica shows in the window sample and the fallback shows where the call fails. Cheaper substitute: no material.
- [ ] Commit: `ui: shadows and materials`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 13. Paths, arcs and gradients

Filled and stroked curves for check marks, rings, icons and charts of any kind, and the radial gradient.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/path.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Fill cubic and quadratic paths with both fill rules in `ui/path.c`. Done when: 1,000 random paths agree with a 16 times oversampled reference within 4 of 255. Cheaper substitute: straight lines.
- [ ] Stroke with caps, joins and dashes in `ui/path.c`. Done when: WinUI Path frames match within the corner limit. Cheaper substitute: a polygon.
- [ ] Draw arcs given in angles in `ui/path.c`. Done when: a quarter ring covers exactly 90 degrees at every size from 8 to 128 pixels. Cheaper substitute: line pieces.
- [ ] Draw the linear and radial gradients of `controls/dev/RadialGradientBrush` in `ui/gradient.c`. Done when: reference frames match within 2 of 255. Cheaper substitute: one colour.
- [ ] Read the path markup of WinUI templates in `tools/winui/templates.py`. Done when: every path in the pinned templates compiles and a malformed one is refused with file and line. Cheaper substitute: typed-in points.
- [ ] Commit: `ui: paths, arcs and gradients`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 14. Theme, accent and contrast changes while running

Light, dark, contrast and accent change at any moment, on the whole window or on one subtree.

**Phase:** 1
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `ui/resources.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Resolve theme resources per subtree in `ui/resources.c`. Done when: a light panel inside a dark window matches the reference. Cheaper substitute: one theme for the window.
- [ ] Let a program override a named resource in `ui/resources.c`. Done when: the override changes every state that names it and nothing else. Cheaper substitute: editing the table.
- [ ] Restyle the tree on change without rebuilding it, in `ui/resources.c`. Done when: focus, selection, scroll position and text stay. Cheaper substitute: a new tree.
- [ ] Derive the six accent shades from one colour in `ui/accent.c`. Done when: 1,000 colours agree with the shades Windows gives, read by `reference/`. Cheaper substitute: fixed steps.
- [ ] Honour contrast themes' system colours in `ui/resources.c`. Done when: the four built-in contrast themes match real WinUI. Cheaper substitute: one contrast theme.
- [ ] Commit: `ui: themes change while running`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 15. Theme transitions

The entrance, pop-in, add, delete, reposition, reorder, content and drill transitions that WinUI templates and lists name.

**Phase:** 1
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `tools/winui/templates.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Compile the named theme transitions of the pinned source in `tools/winui/templates.py`. Done when: each name used by a pinned template has a table and an unknown one is refused. Cheaper substitute: a fade.
- [ ] Run them in `ui/transition.c`. Done when: positions and opacities at 10 instants agree with real WinUI within 1 unit. Cheaper substitute: linear steps.
- [ ] Interrupt and reverse a running transition in `ui/transition.c`. Done when: a script of random interruptions never jumps more than one frame's distance. Cheaper substitute: waiting for the end.
- [ ] Move straight to the end with reduced animation, in `ui/transition.c`. Done when: the first frame is the last. Cheaper substitute: shorter times.
- [ ] Commit: `ui: theme transitions`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 16. Remaining panels

Canvas, WrapPanel, RelativePanel, VariableSizedWrapGrid, Viewbox, UniformGrid and the ItemsStackPanel and ItemsWrapGrid sizes that lists need.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add an independent populated-child reference geometry oracle for `UniformGrid` and `WrapPanel` (controls absent from the pinned Windows App SDK package) and prove width-reflow onto a new row and distinct grid cell rectangles. Done when: populated `UniformGrid` and `WrapPanel` reference dumps match expected multi-row and distinct-cell geometry.
2. Write `ui/panels.c` with its first item. Done when: the first item below passes.
3. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Port Canvas, Viewbox and UniformGrid layout to `ui/panels.c`. Done when: rectangles equal those dumped by `reference/` at five scales. Cheaper substitute: fixed offsets.
- [ ] Port `controls/dev/WrapPanel` to `ui/panels.c`. Done when: the dumped rectangles of 200 random children are equal. Cheaper substitute: one row.
- [ ] Port RelativePanel with its refusal of circular relations to `ui/panels.c`. Done when: a circular layout is refused by name and the others are equal to the dump. Cheaper substitute: ignoring relations.
- [ ] Port VariableSizedWrapGrid spans to `ui/panels.c`. Done when: 200 random span lists are equal to the dump. Cheaper substitute: single cells.
- [ ] Mirror every panel right to left in `ui/layout.c`. Done when: each mirrored dump is equal. Cheaper substitute: moving the text only.
- [ ] Commit: `ui: the remaining panels`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 17. Reference program for any control

> **Started:** 2026-11-24T14:49Z

**Corrected 2026-11-24:** re-planned with `todo-graph.py replan`; the build first began at 2026-11-24T12:29Z; its review chain is kept as `docs/reviews/01-interface/D01-T01-s17-chain4.md`.

**Corrected 2026-11-24:** re-planned with `todo-graph.py replan`; the build first began at 2026-11-24T10:16Z; its review chain is kept as `docs/reviews/01-interface/D01-T01-s17-chain3.md`.

**Corrected 2026-11-24:** re-planned with `todo-graph.py replan`; the build first began at 2026-11-24T09:19Z; its review chain is kept as `docs/reviews/01-interface/D01-T01-s17-chain2.md`.

**Corrected 2026-11-24:** re-planned with `todo-graph.py replan`; the build first began at 2026-11-24T05:45Z; its review chain is kept as `docs/reviews/01-interface/D01-T01-s17-chain1.md`. Clarified the store-lock contract for the compressed store item: current publishers expose `.lockdir` and `.lockv2` atomically with pre-populated `tok-*` owner records and never leave an empty `.lockdir` on release; empty `.lockdir` directories are always preserved regardless of age so mutual exclusion against mkdir-only lock holders is never broken, while dead locks with owner metadata (`tok-*`, `owner.json`) and empty or `.tok-*.tmp`-only `.lockv2` residue are reclaimed automatically on ordinary read-back.

`reference/` has been extended to data-driven `reference/cases/*.json`, `Dump.cs`, `Script.cs`, `reference/package.json`, and compressed `tests/reference/` captures for Light and Dark themes at 100, 150, 200 and 300 percent (`reference/App.xaml.cs` records `host_rasterization_scale`, `effective_rasterization_scale`, and `scale_mode`: native DPI when host matches case scale, or vector `Viewbox` scaling on a 100% host); the fifth scale (125 percent) required by §3 and §16 is added in `D01 T01 §3`, and Windows high-contrast palette capture is added in `D01 T01 §14` before contrast verification.

**Phase:** 1
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Harden `reference/App.xaml.cs`, `reference/Dump.cs`, `reference/Script.cs`, `reference/run.py`, `reference/cases/button.json`, and `tools/winui/check_package.py` for plan-review findings (Light and Dark captures at 100%, 150%, 200%, and 300% with truthful `scale_mode`; strict storyboard and `VisualTransition` completion waiting under one combined timeout deadline with case+state timeout errors; data-driven attached layout properties `Grid.Row`/`Grid.Column`/`Grid.RowSpan`/`Grid.ColumnSpan`/`Canvas.Left`/`Canvas.Top`/`Canvas.ZIndex`/`VariableSizedWrapGrid.RowSpan`/`VariableSizedWrapGrid.ColumnSpan` plus `Grid` `RowDefinitions`/`ColumnDefinitions`; panel-root automation traversal; nested child states; preserved `IsTabStop`; non-clobbering overlapping modifiers; deterministic in-process clipboard replay via WinUI `CopyingToClipboard`/`CuttingToClipboard`/`Paste` interception with `e.Handled = true` and zero host desktop clipboard access, preserving `RichEditBox` RTF formatting, handling `Shift+Delete` as private cut alongside `Control+X`, and allowing `Control+V`/`Shift+Insert` only after a same-script copy/cut on an intercepted writable text control while refusing read-only sources/destinations, ambient paste, and non-text clipboard chords; deterministic `touch_tap` focus settling honoring `AllowFocusOnInteraction`; multi-monitor placement defaulting to a 96-DPI non-primary monitor with `IMPOSSIBLE_REFERENCE_MONITOR` override and cursor save/restore; OpenType `name`-table family resolution and all-face non-zero-glyph `cmap` coverage on declared and post-script `rendered_text`; `compiled-template` missing-control template verification with populated `UniformGrid`/`WrapPanel` child geometry assigned to `D01 T01 §16`, and open-popup capture plus `pointer_wheel` input assigned to `D05 T01 §11`; package provenance binding; unindexed-file refusal in both raw output and store directories; complete press-and-release script-event validation at publication and read-back; and 26-frame legacy baseline comparison). Done when: `python3 reference/run.py --update-store` passes on a 100% Windows display host (single-scale `native-dpi` 150% capture is deferred to `D01 T01 §20`).
2. Cover all new invariants in `tests/test_ui.py` (including Light 150/200/300% frames, 4-channel alpha-aware rectangle masking rejecting 1-device-pixel displacement at 100/150/200/300% scales, unindexed `.bgra` preservation in raw capture output, missing `PointerReleased` rejection at publication and read-back, live attached-property rectangle movement, live same-script copy/cut(`Shift+Delete`)/paste/undo + read-only source/destination refusal + ambient paste refusal, live combined transition+destination storyboard timeout, live `RichEditBox` `AllCaps` and used vs unused `TabLeader` detection, and dynamic registered permitted-font host-to-publication `name`+`cmap` verification for covered and uncovered glyphs). Done when: `python3 tools/build.py`, `python3 tools/winvm/live.py --check` (a live pass in the test machine, `python3 tools/winvm/live.py`), `python3 tools/winui/check_package.py --require-live`, and `python3 tools/site/build.py --check` all exit 0.

- [x] Read cases from `reference/cases/*.json`: control, properties, state, theme, scale, flow direction. Done when: the present 16 cases come from a file with equal pixels. Cheaper substitute: C# per control.
- [x] Force a visual state by name and wait for its storyboard in `reference/App.xaml.cs`. Done when: a state that does not exist stops the run with its name. Cheaper substitute: timed delays.
- [x] Draw at 100, 150, 200 and 300 percent in `reference/App.xaml.cs`. Done when: frames and `cases.json` record the scale. Cheaper substitute: scaling one picture.
> **Deferred:** Live native-DPI 150% OS-scaling fractional-DIP layout-rounding capture (WinUI 3 `XamlRoot.RasterizationScale` is read-only and bound to the Windows monitor DPI = 1.0 on this unattended host; `reference/App.xaml.cs` already implements the `native-dpi` path when host DPI matches case scale and truthfully labels 100%-host vector captures as `viewbox-vector` with `effective_rasterization_scale=1.0`) -> XREF: D01 T01 §20
- [x] Dump element rectangles and the automation tree per case in `reference/Dump.cs`. Done when: `tests/test_ui.py` reads both for the button. Cheaper substitute: pixels only.
- [x] Play a recorded pointer, key and touch script and write the events and focus that follow, in `reference/Script.cs`. Done when: the script of a button gives the same log twice. Cheaper substitute: a person.
- [x] Mask text and icon pixels by rectangle in `tests/test_ui.py`. Done when: text differences do not fail and a moved rectangle does. Cheaper substitute: a loose limit for everything.
- [x] Record which source commit the Windows App SDK package of `reference/Reference.csproj` was built from, and every template that differs from `tools/winui/pin.json`, in `reference/package.json`. Done when: moving either without the other fails `tests/test_ui.py`. Cheaper substitute: one version number.
- [x] Name the controls the package lacks in `reference/package.json`. Done when: their cases are checked against the compiled template's own values and say so. Cheaper substitute: leaving them out.
- [x] Store frames compressed with one index in `tests/reference/`. Done when: a missing, extra or stale frame fails the suite. Cheaper substitute: loose files.
- [x] Commit: `reference: any control, any state`

**Test checkpoint:** `python3 tools/build.py`, `python3 tools/winvm/live.py --check` (a live pass in the test machine, `python3 tools/winvm/live.py`), `python3 tools/winui/check_package.py --require-live`, and `python3 tools/site/build.py --check` all exit 0 and the new tests fail when their production change is reverted.

> **Verified:** 2026-11-25 | §17 | 8e6ca5b3a0cd97608a6ee58285c9a18289c145f6
> **Review:** `docs/reviews/d01-t01-s17.json`

## 18. Drawing speed budgets

The engine has no floating point and no SIMD, so its speed is measured from the start.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `tools/bench.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/fixtures/bench.c`. Done when: the checks fail on a planted fault.

- [ ] Measure scenes in `tests/fixtures/bench.c`: fills, rounded shapes, shadows, layers and 100 buttons. Done when: `tools/bench.py` prints cycles per pixel for each, stable within 5 percent. Cheaper substitute: a stopwatch.
- [ ] Record budgets in `tests/budgets.json` and fail above them. Done when: a planted slow path fails the check. Cheaper substitute: a printed number.
- [ ] Add fast paths for opaque and constant rows in `ui/draw.c`. Done when: the bytes are equal to the plain path on random scenes. Cheaper substitute: different rounding.
- [ ] Add optional SIMD files in `ui/simd/` chosen by the host. Done when: the bytes are equal with and without, and `ui/sources.json` names them optional. Cheaper substitute: SSE in the portable files.
- [ ] Scroll by moving pixels where nothing else changed, in `ui/dirty.c`. Done when: a moved area of a made-up tree redraws only the new strip. Cheaper substitute: full redraw.
- [ ] Commit: `ui: drawing speed budgets`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 19. Corner smoothing doc polish

> **Started:** 2026-11-24T04:47Z

Follow-up P3 documentation fixes filed from `D01 T01 §1` reconciliation (`docs/reviews/01-interface/D01-T01-s1.md`): fix the max chord count documented in `docs/guide/button.md` (`N = 256` for signed 16.16 max radius < 32,768 px rather than the 1,024 loop cap), clarify that the measured accent limit of 14 stands on horizontal and vertical reflection symmetry rather than diagonal symmetry, and align `todo/01-interface/TODO-01-first-controls.md` §1's note with that measured basis.

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Update `docs/guide/button.md` and `todo/01-interface/TODO-01-first-controls.md` §1, move `test_guide_documents_verified_limits` into an unguarded `TestCase` in `tests/test_ui.py` so it runs on Linux and Windows, assert the Wang sentence (`N = 256`, no `N = 1024`) and the accent-button sentence (`14 of 255` under horizontal and vertical reflection), and verify `docs/guide/testing.md` review freshness. Done when: `python3 tools/build.py && python3 tools/site/build.py --check && python3 -m unittest tests.test_ui -v` passes.

- [x] Update `docs/guide/button.md` to state `N = 256` as the maximum Wang chord count reachable for signed 16.16 radii (< 32,768 px) instead of the `1,024` loop cap, clarify in the accent-button sentence that the 14 of 255 limit is measured under horizontal and vertical reflection symmetry, align §1's note in `todo/01-interface/TODO-01-first-controls.md`, move `test_guide_documents_verified_limits` into an unguarded `TestCase` in `tests/test_ui.py` asserting both sentences, and verify `docs/guide/testing.md` review metadata. Done when: `python3 tools/build.py && python3 tools/site/build.py --check && python3 -m unittest tests.test_ui -v` passes. Cheaper substitute: leaving stale 1,024 or Windows-only doc test.
- [x] Commit: `docs: align corner smoothing chord bound with 16.16 range`

**Test checkpoint:** `python3 tools/build.py && python3 tools/site/build.py --check && python3 -m unittest tests.test_ui -v` exits 0.

> **Verified:** 2026-11-24 | §19 | dd4d4e93a440d5b385372242c67e7cb0afb22abd
> **Review:** `docs/reviews/d01-t01-s19.json`

## 20. Native 150% DPI fractional layout

Operator-only follow-up deferred from `D01 T01 §17` (`docs/reviews/01-interface/D01-T01-s17-chain2.md`): capture and commit a live WinUI `scale_mode="native-dpi"` 150% fractional-DIP layout case on a Windows display session explicitly configured to 150% OS scaling (where `XamlRoot.RasterizationScale == 1.5`), and verify that `tests/test_ui.py` accepts the native 150% rounded rectangle while rejecting its enlarged 100% counterpart.

**Phase:** 99
**Needs:** windows-host, display-session, operator
**Surface:** no

**Build order:**

1. On a Windows display session configured to 150% OS scaling, capture a fractional-DIP layout case (`scale: 1.5`) via `reference/run.py` confirming `scale_mode == "native-dpi"` and `effective_rasterization_scale == 1.5`. Done when: the captured case records a native 150%-rounded rectangle that differs from an enlarged 100% layout.
2. Add a regression test in `tests/test_ui.py` asserting the committed native-DPI 150% rectangle passes while an enlarged 100% layout fails. Done when: `python3 tools/build.py` exits 0 and substituting the enlarged 100% width fails the test.

- [ ] Capture a live `native-dpi` 150% fractional-DIP layout case on a 150%-scaled Windows display session and verify `scale_mode == "native-dpi"` and `effective_rasterization_scale == 1.5`. Done when: the captured layout rectangle reflects native 150% WinUI rounding distinct from enlarged 100% geometry. Cheaper substitute: `viewbox-vector` capture on a 100% host.
- [ ] Add a `tests/test_ui.py` check verifying the native 150% rectangle passes and an enlarged 100% rectangle fails. Done when: `python3 tools/build.py` exits 0 and substituting the enlarged 100% rectangle fails. Cheaper substitute: checking scale metadata without geometry.
- [ ] Commit: `reference: native 150% DPI fractional-layout reference capture`

**Test checkpoint:** `python3 tools/build.py` exits 0 and substituting an enlarged 100% rectangle fails the new test.

## Verification

- The gallery runs on Windows and every control works by keyboard alone.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
