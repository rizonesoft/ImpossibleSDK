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
| 3 | §3 | Element tree and layout | §2, §17 | [ ] |
| 4 | §4 | Visual states and animation | §3 | [ ] |
| 5 | §5 | Text in fixed point | §1, §17 | [ ] |
| 6 | §6 | Input, focus and the Windows layer | §4, §5 | [ ] |
| 7 | §7 | Button, toggle switch, text box and list view | §6, §8, §9, §14, §17, T02 §2, T02 §5, T02 §6, D07 T01 §2 | [ ] |
| 8 | §8 | Host callback table | §3 | [ ] |
| 9 | §9 | Dirty rectangles and frame timing | §4, §8 | [ ] |
| 10 | §10 | Clips, opacity and layers | §1, §17 | [ ] |
| 11 | §11 | Transforms and pictures | §10 | [ ] |
| 12 | §12 | Shadows and materials | §8, §10 | [ ] |
| 13 | §13 | Paths, arcs and gradients | §1, §17 | [ ] |
| 14 | §14 | Theme, accent and contrast changes while running | §4, §8 | [ ] |
| 15 | §15 | Theme transitions | §4, §11 | [ ] |
| 16 | §16 | Remaining panels | §3 | [ ] |
| 17 | §17 | Reference program for any control | -- | [ ] |
| 18 | §18 | Drawing speed budgets | §9, §10, §12 | [ ] |
| 19 | §19 | Corner smoothing doc polish | §1 | [x] |

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

The engine that measures and arranges the compiled elements: Grid, StackPanel, Border, ContentPresenter, with layout rounding as WinUI does it.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/tree.c` and `ui/layout.c`. Done when: the button template lays out from its data.
2. Port WinUI's rounding rules from the pinned source. Done when: sizes equal the reference at 100 to 300 percent.

- [ ] Implement the tree, properties and invalidation in `ui/tree.c` with a caller-supplied allocator. Done when: a failed allocation leaves the tree unchanged. Cheaper substitute: a fixed array.
- [ ] Implement measure and arrange for the four panels in `ui/layout.c`. Done when: element rectangles equal those dumped by `reference/`. Cheaper substitute: fixed offsets.
- [ ] Dump element rectangles from real WinUI in `reference/App.xaml.cs`. Done when: `tests/test_ui.py` compares them at five scales.
- [ ] Commit: `ui: element tree and layout`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Visual states and animation

States, transitions and storyboards run from a caller-supplied clock, so a frame can be drawn for any instant.

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/states.c`. Done when: hover and press change through the compiled states.
2. Write `ui/animate.c` with WinUI's easing in fixed point. Done when: the toggle knob's path matches the reference.

- [ ] Run state groups and setters in `ui/states.c`. Done when: every state of the button gives the frame the direct drawing gave. Cheaper substitute: keeping `ui/button.c`.
- [ ] Implement the key splines and brush transitions in `ui/animate.c`. Done when: values at 10 instants are within 1 of the XAML-defined curve. Cheaper substitute: linear steps.
- [ ] Honour the reduced-animation setting in `ui/animate.c`. Done when: a test sees the end state in the first frame.
- [ ] Commit: `ui: visual states and animation`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

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

The portable engine asks its host for everything the system owns: memory, time, clipboard, cursor, caret timing, input method, opening a link, sound, files and the screen reader. One table of callbacks, filled in by the Windows layer here and by the OS there.

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/host.h` with its first item. Done when: the first item below passes.
2. Cover it in `tests/fixtures/test_host.c`. Done when: the checks fail on a planted fault.

- [ ] Define the table, its version and its size field in `ui/host.h`. Done when: a host built for an older version is accepted and its missing callbacks are refused by name. Cheaper substitute: a global per service.
- [ ] Give every callback a defined answer when it is absent in `ui/host.c`. Done when: the tests run every control path with an empty table without a crash. Cheaper substitute: requiring all callbacks.
- [ ] Fill the memory, time and cursor callbacks in `src/host.c`. Done when: the window sample runs through the table only. Cheaper substitute: direct calls from `ui/`.
- [ ] Write a recording host in `tests/fixtures/test_host.c`. Done when: a test reads back every call in order. Cheaper substitute: a mock per test.
- [ ] Refuse a system call from `ui/` in `tests/test_ui.py`. Done when: a planted import fails the portable build test. Cheaper substitute: review.
- [ ] Commit: `ui: the host callback table`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

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

Rounded clips, opacity over a group and nested layers, as pop-ups, cards and disabled content need them.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/clip.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Clip drawing to rectangles and rounded rectangles in `ui/clip.c`. Done when: reference frames match at 100 to 300 percent. Cheaper substitute: rectangles only.
- [ ] Draw a group with one opacity through a bounded layer in `ui/layer.c`. Done when: overlapping children do not show through each other. Cheaper substitute: opacity on each child.
- [ ] Refuse a layer beyond the caller's memory limit and draw without it, in `ui/layer.c`. Done when: a planted allocation failure still draws every element. Cheaper substitute: a crash.
- [ ] Nest clips and layers in `ui/layer.c`. Done when: a random tree of depth 32 equals a plain reference renderer. Cheaper substitute: one level.
- [ ] Commit: `ui: clips, opacity and layers`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

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

1. Write `ui/panels.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Port Canvas, Viewbox and UniformGrid layout to `ui/panels.c`. Done when: rectangles equal those dumped by `reference/` at five scales. Cheaper substitute: fixed offsets.
- [ ] Port `controls/dev/WrapPanel` to `ui/panels.c`. Done when: the dumped rectangles of 200 random children are equal. Cheaper substitute: one row.
- [ ] Port RelativePanel with its refusal of circular relations to `ui/panels.c`. Done when: a circular layout is refused by name and the others are equal to the dump. Cheaper substitute: ignoring relations.
- [ ] Port VariableSizedWrapGrid spans to `ui/panels.c`. Done when: 200 random span lists are equal to the dump. Cheaper substitute: single cells.
- [ ] Mirror every panel right to left in `ui/layout.c`. Done when: each mirrored dump is equal. Cheaper substitute: moving the text only.
- [ ] Commit: `ui: the remaining panels`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 17. Reference program for any control

> **Started:** 2026-11-24T09:19Z

**Corrected 2026-11-24:** re-planned with `todo-graph.py replan`; the build first began at 2026-11-24T05:45Z; its review chain is kept as `docs/reviews/01-interface/D01-T01-s17-chain1.md`. Clarified the store-lock contract for the compressed store item: current publishers expose `.lockdir` and `.lockv2` atomically with pre-populated `tok-*` owner records and never leave an empty `.lockdir` on release; empty `.lockdir` directories are always preserved regardless of age so mutual exclusion against mkdir-only lock holders is never broken, while dead locks with owner metadata (`tok-*`, `owner.json`) and empty or `.tok-*.tmp`-only `.lockv2` residue are reclaimed automatically on ordinary read-back.

`reference/` has been extended to data-driven `reference/cases/*.json`, `Dump.cs`, `Script.cs`, `reference/package.json`, and compressed `tests/reference/` captures for Light and Dark themes at 100, 150, 200 and 300 percent (`reference/App.xaml.cs` records `host_rasterization_scale`, `effective_rasterization_scale`, and `scale_mode`: native DPI when host matches case scale, or vector `Viewbox` scaling on a 100% host); the fifth scale (125 percent) required by §3 and §16 is added in `D01 T01 §3`, and Windows high-contrast palette capture is added in `D01 T01 §14` before contrast verification.

**Phase:** 1
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Harden `reference/App.xaml.cs`, `reference/Dump.cs`, `reference/Script.cs`, `reference/run.py`, and `tools/winui/check_package.py` for plan-review findings (scale mode reporting, strict storyboard timeout, panel-root automation traversal, sequenced/chord/focused scripts with balanced input cleanup, stock-label refusal, strict missing-control field checks, package provenance binding, store unowned-file refusal, and 26-frame baseline comparison). Done when: `python3 reference/run.py --update-store --compare-baseline tests/reference` passes.
2. Cover all new invariants in `tests/test_ui.py` (including 4-channel alpha-aware rectangle masking and live panel-root, script, and cleanup tests). Done when: `python3 tools/build.py`, `IMPOSSIBLE_LIVE_REFERENCE=1 python3 -m unittest tests.test_ui.LiveReference -v` (0 skipped), `python3 tools/winui/check_package.py --require-live`, and `python3 tools/site/build.py --check` all exit 0.

- [x] Read cases from `reference/cases/*.json`: control, properties, state, theme, scale, flow direction. Done when: the present 16 cases come from a file with equal pixels. Cheaper substitute: C# per control.
- [x] Force a visual state by name and wait for its storyboard in `reference/App.xaml.cs`. Done when: a state that does not exist stops the run with its name. Cheaper substitute: timed delays.
- [x] Draw at 100, 150, 200 and 300 percent in `reference/App.xaml.cs`. Done when: frames and `cases.json` record the scale. Cheaper substitute: scaling one picture.
- [x] Dump element rectangles and the automation tree per case in `reference/Dump.cs`. Done when: `tests/test_ui.py` reads both for the button. Cheaper substitute: pixels only.
- [x] Play a recorded pointer, key and touch script and write the events and focus that follow, in `reference/Script.cs`. Done when: the script of a button gives the same log twice. Cheaper substitute: a person.
- [x] Mask text and icon pixels by rectangle in `tests/test_ui.py`. Done when: text differences do not fail and a moved rectangle does. Cheaper substitute: a loose limit for everything.
- [x] Record which source commit the Windows App SDK package of `reference/Reference.csproj` was built from, and every template that differs from `tools/winui/pin.json`, in `reference/package.json`. Done when: moving either without the other fails `tests/test_ui.py`. Cheaper substitute: one version number.
- [x] Name the controls the package lacks in `reference/package.json`. Done when: their cases are checked against the compiled template's own values and say so. Cheaper substitute: leaving them out.
- [x] Store frames compressed with one index in `tests/reference/`. Done when: a missing, extra or stale frame fails the suite. Cheaper substitute: loose files.
- [ ] Commit: `reference: any control, any state`

**Test checkpoint:** `python3 tools/build.py`, `IMPOSSIBLE_LIVE_REFERENCE=1 python3 -m unittest tests.test_ui.LiveReference -v` (0 skipped), `python3 tools/winui/check_package.py --require-live`, and `python3 tools/site/build.py --check` all exit 0 and the new tests fail when their production change is reverted.

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

## Verification

- The gallery runs on Windows and every control works by keyboard alone.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
