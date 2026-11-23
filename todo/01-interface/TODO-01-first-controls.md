---
schema_version: 1
id: first-controls
domain: 01-interface
status: active
title: "TODO-01 -- Interface Engine and First Controls"
---

# TODO-01 -- Interface Engine and First Controls

> **Goal:** Impossible UI draws and runs the first four Windows 11 controls from WinUI's own templates, in portable C that uses no floating point and calls no operating system function.

> **Current state:** Measured 2026-11-23 at `237b2e9`. `ui/theme.c` resolves every WinUI brush and number for three themes, `ui/draw.c` draws rounded shapes, `ui/button.c` draws the standard, accent and subtle button directly, and `reference/` draws the same cases with real WinUI. 16 cases at 100 percent compare: straight edges within 1 of 255, standard corners within 3, accent corners within 27. There is no element tree, layout, text, input or animation.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: the approved design.
- `tools/winui/pin.json`: the WinUI commit (MIT) that every value and behaviour comes from.
- `abi/tiers.json`: the only Windows functions the Windows layer may call.

## Outcome

- The four controls match real WinUI in every state, theme and scale within the limits of `tests/test_ui.py`.
- `tests/test_ui.py` compiles `ui/` with the OS flags and finds no undefined symbol beyond four memory routines.

**Adjacency:** all=not-applicable (a drawing and control library holds no records, settings or reports)

**Adjacency rationale:** Programs own data and settings; this library owns only how controls look and behave.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Exact corner smoothing | -- | [ ] |
| 2 | §2 | Template compiler | -- | [ ] |
| 3 | §3 | Element tree and layout | §2 | [ ] |
| 4 | §4 | Visual states and animation | §3 | [ ] |
| 5 | §5 | Text in fixed point | §1 | [ ] |
| 6 | §6 | Input, focus and the Windows layer | §4, §5 | [ ] |
| 7 | §7 | Button, toggle switch, text box and list view | §6 | [ ] |

## 1. Exact corner smoothing

The accent button differs from real WinUI by up to 27 of 255 at corner pixels, while straight edges differ by at most 1. WinUI's outer radius is the corner plus half the border; its curve smoothing is softer than the exact area.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add radius probes (4, 8, 16, white on black, no border) to `reference/App.xaml.cs`. Done when: the probe frames are in `tests/reference/`.
2. Change `ui/draw.c` to the measured rule. Done when: `tests/test_ui.py` passes with a corner limit of 4.

- [ ] Draw the probe frames in `reference/App.xaml.cs` at scales 1, 1.5 and 2. Done when: the frames and `cases.json` record scale per case. Cheaper substitute: scaling one bitmap.
- [ ] Find the rule from WinUI's `GeometryBuilder` and the probes and write it in a comment in `ui/draw.c`. Done when: the rule predicts every probe pixel within 2. Cheaper substitute: a per-control lookup table.
- [ ] Lower the corner limit in `tests/test_ui.py` to 4 for every style. Done when: the suite passes and fails on the old curve. Cheaper substitute: excluding corner pixels.
- [ ] Commit: `ui: corners match WinUI`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Template compiler

WinUI's control templates hold the element tree, bindings, visual states and timings. `tools/winui/resources.py` already resolves 1,934 brushes and 444 numbers; this compiles the templates themselves.

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Write `tools/winui/templates.py` over the pinned checkout. Done when: Button, ToggleSwitch, TextBox and ListViewItem compile.
2. Emit C tables in `ui/template_data.c`. Done when: the portable build test passes.

- [ ] Parse `ControlTemplate`, setters, visual states and storyboards in `tools/winui/templates.py`. Done when: each of the four templates round-trips to a listing that names every element and state. Cheaper substitute: hand-written C for each control.
- [ ] Refuse an element, property or converter the engine does not know, naming file and line. Done when: a fixture template with an unknown property fails. Cheaper substitute: ignoring it.
- [ ] Add `--check` to the drift test in `tests/test_ui.py`. Done when: editing the generated file fails the test.
- [ ] Commit: `ui: compile WinUI control templates`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Element tree and layout

The engine that measures and arranges the compiled elements: Grid, StackPanel, Border, ContentPresenter, with layout rounding as WinUI does it.

**Phase:** 1
**Needs:** none
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
**Needs:** none
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
- [ ] Draw the two-ring focus visual only for keyboard focus. Done when: the frame equals `reference/`. Cheaper substitute: a single outline.
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
- [ ] Add the gallery page and its keyboard walk to `tests/test_ui.py`. Done when: the walk passes on Windows.
- [ ] Commit: `ui: the first four controls`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- The gallery runs on Windows and every control works by keyboard alone.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
