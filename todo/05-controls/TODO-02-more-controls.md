---
schema_version: 1
id: more-controls
domain: 05-controls
status: active
title: "TODO-02 -- More Controls"
---

# TODO-02 -- More Controls

> **Goal:** The WinUI controls that are used less often, so that every one that stands on its own exists.

> **Current state:** Measured 2026-11-23 at `e06ab3a` against the folders of `controls/dev` at the pinned WinUI commit. None exists. `MapControl` and `WebView2` need a map provider and a browser, and `Charts` has only tests in that commit; `docs/winui-coverage.json` records the three.

## Inputs

- `todo/05-controls/TODO-01-controls.md`: the contract, peers, pop-ups and repeater these use.
- The WinUI checkout at `tools/winui/pin.json`.

## Outcome

- `docs/winui-coverage.json` names a section for every control of the pinned commit.

**Adjacency:** all=not-applicable (controls hold no records, settings or reports; programs own those)

**Adjacency rationale:** A control shows and edits what the program gives it; the program decides what is kept.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Text block, rich text block and links | T01 §5, D01 T02 §4, D01 T02 §5 | [ ] |
| 2 | §2 | Image and icon elements | T01 §5, D01 T01 §11, D01 T02 §6 | [ ] |
| 3 | §3 | Animated icons | §2 | [ ] |
| 4 | §4 | Animated visuals | §3, D01 T01 §13 | [ ] |
| 5 | §5 | Swipe control | T01 §4, D07 T01 §4 | [ ] |
| 6 | §6 | Pull to refresh | T01 §2, D07 T01 §4 | [ ] |
| 7 | §7 | Annotated scroll bar | T01 §31 | [ ] |
| 8 | §8 | Parallax view | T01 §2 | [ ] |
| 9 | §9 | Pager control | T01 §10, T01 §11 | [ ] |
| 10 | §10 | Pivot | T01 §20 | [ ] |
| 11 | §11 | Hub | T01 §2 | [ ] |
| 12 | §12 | List box | T01 §20 | [ ] |
| 13 | §13 | Frame and page navigation | T01 §5, D01 T01 §15 | [ ] |
| 14 | §14 | Table view columns and header | T01 §20, T01 §29 | [ ] |
| 15 | §15 | Table view rows and selection | §14 | [ ] |
| 16 | §16 | Table view groups and nested rows | §15 | [ ] |
| 17 | §17 | Table view editing | §15, T01 §9, T01 §11 | [ ] |
| 18 | §18 | Ink canvas | D01 T01 §13, D07 T01 §4 | [ ] |
| 19 | §19 | Ink tool bar | §18, T01 §13, T01 §8 | [ ] |
| 20 | §20 | Resize gripper and backdrop element | T01 §5, D01 T01 §12 | [ ] |
| 21 | §21 | Layout panel and monochrome overlay | T01 §20, D01 T01 §10 | [ ] |
| 22 | §22 | All controls accepted | §1, §2, §4, §5, §6, §7, §8, §9, §10, §11, §12, §13, §16, §17, §19, §20, §21, T03 §2, T03 §5, T03 §6, T03 §7, T03 §8, T03 §9, T01 §7, T01 §10, T01 §14, T01 §16, T01 §18, T01 §19, T01 §21, T01 §22, T01 §24, T01 §25, T01 §26, T01 §27, T01 §28, T01 §29, T01 §30, T01 §31, T01 §32, T01 §33, T01 §34, T01 §35, T01 §36, T01 §38, T01 §39, T01 §40, T01 §41, T01 §42, T01 §43, T01 §45, T01 §46, T01 §48, T01 §50, T01 §51, T01 §52, T01 §53, T01 §54, D07 T01 §3, D07 T01 §8, D07 T01 §9, D01 T01 §16, D01 T01 §18, D01 T02 §7, D01 T02 §12, D01 T02 §13 | [ ] |

## 1. Text block, rich text block and links

Text that is read: styles, runs, links, overflow and selection.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user reads text.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/text_block.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/text_block.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/text_block.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the TextBlock styles of `controls/dev/CommonStyles/TextBlock_themeresources.xaml` and `BaseRichTextBlockStyle` with those based on it in `dxaml/xcp/dxaml/themes/generic.xaml`. Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/text_block.c`, with its cases in `reference/cases/text_block.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Apply the named type styles and any run's own in `ui/controls/text_block.c`. Done when: frames and line boxes match. Cheaper substitute: one size.
- [ ] Wrap, trim, limit lines and set line height in `ui/controls/text_block.c`. Done when: 100 widths equal real WinUI. Cheaper substitute: wrapping.
- [ ] Hold inline links that focus and open in `ui/controls/text_block.c`. Done when: Tab reaches each and Enter opens it once. Cheaper substitute: a button.
- [ ] Select and copy by pointer, touch and key when allowed in `ui/controls/text_block.c`. Done when: the copied text equals real WinUI's. Cheaper substitute: none.
- [ ] Flow rich text over several columns and blocks in `ui/controls/text_block.c`. Done when: line breaks equal real WinUI. Cheaper substitute: one block.
- [ ] Hold elements inside a line in `ui/controls/text_block.c`. Done when: a button in text stands on its baseline. Cheaper substitute: text only.
- [ ] Mark access keys and search hits in `ui/controls/text_block.c`. Done when: frames match. Cheaper substitute: none.
- [ ] Handle Tab, Enter, arrows with Shift, Ctrl-A and Ctrl-C in `ui/controls/text_block.c`. Done when: the script `reference/scripts/text_block.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/text_block.c`: Text with Text, Hyperlink with Invoke, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/text_block.c`. Done when: `tools/check_control.py text_block` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/text_block.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §7 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: text block, rich text block and links`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Image and icon elements

Pictures and icons as elements: Image, FontIcon, SymbolIcon, PathIcon, ImageIcon and BitmapIcon.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees a picture.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/image.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/image.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/image.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the icon element rules of `controls/dev/IconSource` and `controls/dev/ImageIcon`. Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/image.c`, with its cases in `reference/cases/image.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Show a picture in memory with each stretch and nine parts in `ui/controls/image.c`. Done when: frames match within 2 of 255. Cheaper substitute: fill.
- [ ] Choose a picture by scale and theme in `ui/controls/image.c`. Done when: the choice equals real WinUI's rules. Cheaper substitute: one.
- [ ] Show a place for one that loads or fails in `ui/controls/image.c`. Done when: events and frames match. Cheaper substitute: nothing.
- [ ] Colour icons from the foreground and mirror those that turn in `ui/controls/image.c`. Done when: frames match. Cheaper substitute: one colour.
- [ ] Play animated pictures given frame by frame in `ui/controls/image.c`. Done when: timed frames match. Cheaper substitute: the first.
- [ ] Give the automation peer in `ui/controls/image.c`: Image, named by the program or kept out of the tree, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/image.c`. Done when: `tools/check_control.py image` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/image.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: image and icon elements`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Animated icons

Icons that move between named states.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees an action answer.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/animated_icon.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/animated_icon.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/animated_icon.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the states the templates of `controls/dev/AnimatedIcon` name. Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/animated_icon.c`, with its cases in `reference/cases/animated_icon.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Run from one state to another by its marks in `ui/controls/animated_icon.c`. Done when: frames at 10 instants match for the five built-in icons. Cheaper substitute: a fade.
- [ ] Interrupt, speed up and reverse in `ui/controls/animated_icon.c`. Done when: random changes never jump. Cheaper substitute: waiting.
- [ ] Show a still icon where there is no animation or motion is reduced in `ui/controls/animated_icon.c`. Done when: the frame equals the fallback. Cheaper substitute: nothing.
- [ ] Compile our own drawings of back, settings, chevron, find and pane to `ui/icon_data.c`. Done when: the drift test finds a changed source. Cheaper substitute: Microsoft's drawings.
- [ ] Give the automation peer in `ui/controls/animated_icon.c`: none of its own; its owner names it, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/animated_icon.c`. Done when: `tools/check_control.py animated_icon` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/animated_icon.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: animated icons`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Animated visuals

Compiled vector animations, as AnimatedVisualPlayer plays.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees a moving picture.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/animated_visual.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/animated_visual.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/animated_visual.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the player rules of `controls/dev/AnimatedVisualPlayer`. Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/animated_visual.c`, with its cases in `reference/cases/animated_visual.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Compile a pinned subset of Lottie to tables in `tools/lottie.py`. Done when: an unsupported feature is refused by name. Cheaper substitute: playing JSON.
- [ ] Play, loop, pause, seek and set speed in `ui/controls/animated_visual.c`. Done when: positions equal a fixed clock. Cheaper substitute: play.
- [ ] Fit by each stretch in `ui/controls/animated_visual.c`. Done when: frames match. Cheaper substitute: fill.
- [ ] Show the fallback when it cannot play in `ui/controls/animated_visual.c`. Done when: the events equal real WinUI. Cheaper substitute: nothing.
- [ ] Give the automation peer in `ui/controls/animated_visual.c`: Image, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/animated_visual.c`. Done when: `tools/check_control.py animated_visual` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/animated_visual.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: animated visuals`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Swipe control

Actions behind a row, by touch.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user acts on a row by touch.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/swipe.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/swipe.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/swipe.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `SwipeControl` of `controls/dev/SwipeControl/SwipeControl.xaml` (no states of its own). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/swipe.c`, with its cases in `reference/cases/swipe.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Reveal actions, or run one past the point, on four sides in `ui/controls/swipe.c`. Done when: scripts equal real WinUI. Cheaper substitute: reveal.
- [ ] Follow the finger with resistance in `ui/controls/swipe.c`. Done when: positions at 10 instants agree within 2 units. Cheaper substitute: steps.
- [ ] Close when another opens, on scrolling and on a press elsewhere in `ui/controls/swipe.c`. Done when: the log equals real WinUI. Cheaper substitute: staying.
- [ ] Offer the same actions in the context menu in `ui/controls/swipe.c`. Done when: each is there. Cheaper substitute: touch only.
- [ ] Handle the menu key in `ui/controls/swipe.c`. Done when: the script `reference/scripts/swipe.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/swipe.c`: the actions on the row it covers, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/swipe.c`. Done when: `tools/check_control.py swipe` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/swipe.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: swipe control`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Pull to refresh

Pulling a list to bring new items.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user asks for new items.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/refresh.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/refresh.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/refresh.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `RefreshContainer` of `controls/dev/PullToRefresh/RefreshContainer/RefreshContainer.xaml` (no states of its own); `RefreshVisualizer` of `controls/dev/PullToRefresh/RefreshVisualizer/RefreshVisualizer.xaml` (no states of its own). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/refresh.c`, with its cases in `reference/cases/refresh.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Move through the six states by distance in `ui/controls/refresh.c`. Done when: scripts equal real WinUI. Cheaper substitute: two.
- [ ] Hold until the program says it is done in `ui/controls/refresh.c`. Done when: the deferral script matches. Cheaper substitute: a time.
- [ ] Pull from each side in `ui/controls/refresh.c`. Done when: four directions match. Cheaper substitute: the top.
- [ ] Refresh by command and key too in `ui/controls/refresh.c`. Done when: F5 runs it once. Cheaper substitute: touch only.
- [ ] Handle F5 in `ui/controls/refresh.c`. Done when: the script `reference/scripts/refresh.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/refresh.c`: a live message when it starts and ends, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/refresh.c`. Done when: `tools/check_control.py refresh` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/refresh.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: pull to refresh`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Annotated scroll bar

A scroll bar with marks and labels.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user jumps through a long list.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/annotated_scroll_bar.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/annotated_scroll_bar.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/annotated_scroll_bar.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `AnnotatedScrollBar` of `controls/dev/AnnotatedScrollBar/AnnotatedScrollBar.xaml` (no states of its own). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/annotated_scroll_bar.c`, with its cases in `reference/cases/annotated_scroll_bar.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Place labels, hiding those that would touch in `ui/controls/annotated_scroll_bar.c`. Done when: positions equal real WinUI's for 200 sets. Cheaper substitute: all.
- [ ] Show the detail label for the pointer's place in `ui/controls/annotated_scroll_bar.c`. Done when: text and place match. Cheaper substitute: none.
- [ ] Scroll by click, drag and the small buttons in `ui/controls/annotated_scroll_bar.c`. Done when: scripts equal real WinUI. Cheaper substitute: drag.
- [ ] Handle the keys of the view it belongs to in `ui/controls/annotated_scroll_bar.c`. Done when: the script `reference/scripts/annotated_scroll_bar.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/annotated_scroll_bar.c`: ScrollBar with RangeValue, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/annotated_scroll_bar.c`. Done when: `tools/check_control.py annotated_scroll_bar` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/annotated_scroll_bar.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: annotated scroll bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. Parallax view

A ground that moves slower than its content.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees depth.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/parallax.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/parallax.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/parallax.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the rules of `controls/dev/ParallaxView`. Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/parallax.c`, with its cases in `reference/cases/parallax.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Move by the source's offset in both directions in `ui/controls/parallax.c`. Done when: 200 offsets equal real WinUI. Cheaper substitute: vertical.
- [ ] Keep to the shift and speed limits in `ui/controls/parallax.c`. Done when: the limits hold in random scrolls. Cheaper substitute: none.
- [ ] Stand still with reduced animation in `ui/controls/parallax.c`. Done when: the offset stays. Cheaper substitute: moving.
- [ ] Give the automation peer in `ui/controls/parallax.c`: none; it is decoration, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/parallax.c`. Done when: `tools/check_control.py parallax` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/parallax.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: parallax view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. Pager control

Numbered pages.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user moves between pages of results.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/pager.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/pager.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/pager.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `PagerControl` of `controls/dev/PagerControl/PagerControl.xaml` (25 states in the groups FirstPageButtonVisibilityStates 3, FirstPageButtonIsEnabledStates 2, PreviousPageButtonVisibilityStates 3, PreviousPageButtonIsEnabledStates 2, NextPageButtonVisibilityStates 2, NextPageButtonIsEnabledStates 3, LastPageButtonStates 2, ? 3, PagerDisplayModeStates 3, InfiniteItemsDisplayMode 2). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/pager.c`, with its cases in `reference/cases/pager.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Show combo, number box, buttons or numbers by count in `ui/controls/pager.c`. Done when: every state of the template matches. Cheaper substitute: numbers.
- [ ] Shorten long rows with ellipses in `ui/controls/pager.c`. Done when: buttons equal real WinUI's for 1 to 1,000 pages. Cheaper substitute: all.
- [ ] Work with an unknown count in `ui/controls/pager.c`. Done when: scripts match. Cheaper substitute: known only.
- [ ] Handle arrows, Home, End and Enter in `ui/controls/pager.c`. Done when: the script `reference/scripts/pager.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/pager.c`: Menu with Selection, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/pager.c`. Done when: `tools/check_control.py pager` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/pager.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: pager control`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 10. Pivot

Headers that slide over pages.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user moves between a few pages.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/pivot.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/pivot.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/pivot.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `Pivot` of `controls/dev/CommonStyles/Pivot_themeresources.xaml` (states Portrait, Landscape, NavigationButtonsHidden, NavigationButtonsVisible, PreviousButtonVisible, NextButtonVisible, HeaderDynamic, HeaderStatic); `PivotHeaderItem` of `controls/dev/CommonStyles/Pivot_themeresources.xaml` (states Disabled, Unselected, UnselectedLocked, Selected, UnselectedPointerOver, SelectedPointerOver, UnselectedPressed, SelectedPressed); `PivotItem` of `controls/dev/CommonStyles/Pivot_themeresources.xaml` (states Right, Left, Center). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/pivot.c`, with its cases in `reference/cases/pivot.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Slide pages by swipe, wheel and header, round the ends when asked in `ui/controls/pivot.c`. Done when: scripts equal real WinUI. Cheaper substitute: click.
- [ ] Show moving or fixed headers by width in `ui/controls/pivot.c`. Done when: frames match. Cheaper substitute: fixed.
- [ ] Show side buttons on pointer in `ui/controls/pivot.c`. Done when: timed frames match. Cheaper substitute: always.
- [ ] Keep only near pages live in `ui/controls/pivot.c`. Done when: 100 pages keep three. Cheaper substitute: all.
- [ ] Handle arrows, Ctrl-Page Up, Ctrl-Page Down, Home and End in `ui/controls/pivot.c`. Done when: the script `reference/scripts/pivot.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/pivot.c`: Tab with Selection, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/pivot.c`. Done when: `tools/check_control.py pivot` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/pivot.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: pivot`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 11. Hub

Wide sections side by side.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user looks over a program.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/hub.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/hub.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/hub.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `Hub` of `dxaml/xcp/dxaml/themes/generic.xaml` (states Horizontal, Vertical); `HubSection` of `dxaml/xcp/dxaml/themes/generic.xaml` (no states of its own). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/hub.c`, with its cases in `reference/cases/hub.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Lay sections out sideways or down with a header in `ui/controls/hub.c`. Done when: rectangles match. Cheaper substitute: sideways.
- [ ] Make a header press when it leads somewhere in `ui/controls/hub.c`. Done when: the event comes once. Cheaper substitute: always.
- [ ] Tell which sections are in view in `ui/controls/hub.c`. Done when: the list equals real WinUI's in a scroll script. Cheaper substitute: all.
- [ ] Build far sections late in `ui/controls/hub.c`. Done when: 20 sections keep the near ones live. Cheaper substitute: all.
- [ ] Handle arrows, Tab, Home and End in `ui/controls/hub.c`. Done when: the script `reference/scripts/hub.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/hub.c`: Group with Scroll, ListItem, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/hub.c`. Done when: `tools/check_control.py hub` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/hub.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: hub`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 12. List box

The plain list of older pages.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user chooses from a list.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/list_box.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/list_box.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/list_box.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `ListBox` of `controls/dev/CommonStyles/ListBox_themeresources.xaml` (no states of its own); `ListBoxItem` of `controls/dev/CommonStyles/ListBox_themeresources.xaml` (states Normal, Disabled, PointerOver, Pressed, Selected, SelectedUnfocused, SelectedPointerOver, SelectedPressed). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/list_box.c`, with its cases in `reference/cases/list_box.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Select single, multiple and extended in `ui/controls/list_box.c`. Done when: 100 scripts equal real WinUI. Cheaper substitute: single.
- [ ] Draw the older item look in `ui/controls/list_box.c`. Done when: every state of the template matches. Cheaper substitute: the list view's.
- [ ] Hold 10,000 rows with few live in `ui/controls/list_box.c`. Done when: under 100. Cheaper substitute: all.
- [ ] Handle arrows, Ctrl and Shift with them, Space, Home, End and letters in `ui/controls/list_box.c`. Done when: the script `reference/scripts/list_box.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/list_box.c`: List with Selection, ListItem, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/list_box.c`. Done when: `tools/check_control.py list_box` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/list_box.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: list box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 13. Frame and page navigation

Pages with a way back.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user moves through a program and back.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/frame.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/frame.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/frame.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `Frame` of `dxaml/xcp/dxaml/themes/generic.xaml` (no states of its own). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/frame.c`, with its cases in `reference/cases/frame.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Go to, back and forward with a stack in `ui/controls/frame.c`. Done when: random moves equal a plain model. Cheaper substitute: one page.
- [ ] Pass values, and let a page refuse to be left in `ui/controls/frame.c`. Done when: the event log equals real WinUI. Cheaper substitute: leaving.
- [ ] Keep a number of pages live in `ui/controls/frame.c`. Done when: memory stays under the limit. Cheaper substitute: all.
- [ ] Save and restore the stack as text in `ui/controls/frame.c`. Done when: a saved stack returns the same page. Cheaper substitute: none.
- [ ] Run entrance, drill, slide and no motion in `ui/controls/frame.c`. Done when: frames at 10 instants match. Cheaper substitute: a fade.
- [ ] Answer Alt-Left, the back button of the mouse and the title bar in `ui/controls/frame.c`. Done when: each goes back once. Cheaper substitute: one.
- [ ] Handle Alt-Left, Alt-Right and Backspace where real WinUI takes it in `ui/controls/frame.c`. Done when: the script `reference/scripts/frame.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/frame.c`: Group named by the page, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/frame.c`. Done when: `tools/check_control.py frame` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/frame.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: frame and page navigation`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 14. Table view columns and header

Columns of the data grid.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user arranges a table.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/table_view_columns.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/table_view_columns.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/table_view_columns.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `TableView` of `controls/dev/TableView/TableView.xaml` (no states of its own); `TableViewColumnHeader` of `controls/dev/TableView/TableView.xaml` (16 states in the groups CommonStates 4, HeaderEdgeStates 4, HighContrastStates 4, SortStates 4). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/table_view_columns.c`, with its cases in `reference/cases/table_view_columns.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Size columns fixed, by content and by share, within limits in `ui/controls/table_view_columns.c`. Done when: widths equal real WinUI's for 200 sets. Cheaper substitute: fixed.
- [ ] Drag to size and to reorder, with the line that shows where in `ui/controls/table_view_columns.c`. Done when: scripts match. Cheaper substitute: size.
- [ ] Freeze columns at the sides in `ui/controls/table_view_columns.c`. Done when: they stay in a sideways scroll. Cheaper substitute: all scroll.
- [ ] Sort by press, and by more with Shift in `ui/controls/table_view_columns.c`. Done when: orders and arrows match. Cheaper substitute: one.
- [ ] Hide, show and filter from the header's menu in `ui/controls/table_view_columns.c`. Done when: each choice reads back. Cheaper substitute: none.
- [ ] Handle arrows, Enter, Space, Alt-Down and Ctrl with arrows in `ui/controls/table_view_columns.c`. Done when: the script `reference/scripts/table_view_columns.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/table_view_columns.c`: DataGrid with Grid and Table, HeaderItem with Invoke and Transform, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/table_view_columns.c`. Done when: `tools/check_control.py table_view_columns` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/table_view_columns.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: table view columns and header`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 15. Table view rows and selection

Rows and cells, few of them live.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user reads and chooses rows.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/table_view_rows.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/table_view_rows.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/table_view_rows.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `TableViewRow` of `controls/dev/TableView/TableView.xaml` (19 states in the groups CommonStates 13, RowExpansionStates 2, RowExpandabilityStates 2, RowHierarchyStates 2). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/table_view_rows.c`, with its cases in `reference/cases/table_view_rows.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Make only visible rows and cells in `ui/controls/table_view_rows.c`. Done when: 1,000,000 rows and 100 columns keep under 4,000 cells. Cheaper substitute: all.
- [ ] Select none or one row as the pinned TableView does in `ui/controls/table_view_rows.c`. Done when: 100 scripts equal real WinUI or, where the package lacks it, the pinned source. Cheaper substitute: always one.
- [ ] Add selection of many rows, and of cells by drag, as our own extension in `ui/controls/table_view_rows.c`. Done when: a table of 60 expected results in `tests/table_selection.json` passes. Cheaper substitute: one row.
- [ ] Move the current cell, round the ends by Tab in `ui/controls/table_view_rows.c`. Done when: the log equals real WinUI. Cheaper substitute: rows.
- [ ] Copy the selection as text and table in `ui/controls/table_view_rows.c`. Done when: another program keeps rows and columns. Cheaper substitute: text.
- [ ] Keep selection and place while data changes in `ui/controls/table_view_rows.c`. Done when: 10,000 random edits agree. Cheaper substitute: by number.
- [ ] Handle arrows, Tab, Page keys, Home, End, Ctrl and Shift with them, Ctrl-A and Ctrl-C in `ui/controls/table_view_rows.c`. Done when: the script `reference/scripts/table_view_rows.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/table_view_rows.c`: DataItem with SelectionItem, GridItem and TableItem, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/table_view_rows.c`. Done when: `tools/check_control.py table_view_rows` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/table_view_rows.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: table view rows and selection`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 16. Table view groups and nested rows

Grouped and nested data.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees data by group.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/table_view_groups.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/table_view_groups.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/table_view_groups.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `TableViewGroupHeader` of `controls/dev/TableView/TableView.xaml` (states Normal, PointerOver, Pressed, Disabled, Collapsed, Expanded, Expandable, NotExpandable). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/table_view_groups.c`, with its cases in `reference/cases/table_view_groups.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Group by one or more columns with counts and sums in `ui/controls/table_view_groups.c`. Done when: random data equals a plain model. Cheaper substitute: one.
- [ ] Open and close groups and nested rows in `ui/controls/table_view_groups.c`. Done when: the visible rows equal the model. Cheaper substitute: all open.
- [ ] Keep group headers in view in `ui/controls/table_view_groups.c`. Done when: positions match. Cheaper substitute: scrolling away.
- [ ] Load children late in `ui/controls/table_view_groups.c`. Done when: a slow source is asked once. Cheaper substitute: at the start.
- [ ] Handle Left, Right, * , Plus and Minus in `ui/controls/table_view_groups.c`. Done when: the script `reference/scripts/table_view_groups.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/table_view_groups.c`: Group with ExpandCollapse and GridItem, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/table_view_groups.c`. Done when: `tools/check_control.py table_view_groups` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/table_view_groups.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: table view groups and nested rows`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 17. Table view editing

Changing cells.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user changes data.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/table_view_editing.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/table_view_editing.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/table_view_editing.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the cell templates of `controls/dev/TableView`. Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/table_view_editing.c`, with its cases in `reference/cases/table_view_editing.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Edit by F2, typing, double click and Enter in `ui/controls/table_view_editing.c`. Done when: the log equals real WinUI. Cheaper substitute: double click.
- [ ] Use text, number, check, choice, date and own editors in `ui/controls/table_view_editing.c`. Done when: each commits and returns. Cheaper substitute: text.
- [ ] Keep a row open until it is right, or refuse with a message in `ui/controls/table_view_editing.c`. Done when: a wrong value never reaches the data. Cheaper substitute: accepting.
- [ ] Return a cell, then the row, by Escape in `ui/controls/table_view_editing.c`. Done when: values equal the old ones. Cheaper substitute: the cell.
- [ ] Add and remove rows, with undo in `ui/controls/table_view_editing.c`. Done when: 1,000 random edits undo to the start. Cheaper substitute: none.
- [ ] Paste a table over cells in `ui/controls/table_view_editing.c`. Done when: sizes that do not fit are named. Cheaper substitute: the first cell.
- [ ] Handle F2, Enter, Escape, Tab, Delete and Ctrl-V in `ui/controls/table_view_editing.c`. Done when: the script `reference/scripts/table_view_editing.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/table_view_editing.c`: DataItem with Value, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/table_view_editing.c`. Done when: `tools/check_control.py table_view_editing` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/table_view_editing.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: table view editing`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 18. Ink canvas

Drawing by pen, touch and pointer.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user draws and writes.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/ink_canvas.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/ink_canvas.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/ink_canvas.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the rules of `controls/dev/InkCanvas`. Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/ink_canvas.c`, with its cases in `reference/cases/ink_canvas.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Smooth points to strokes in fixed point in `ui/ink.c`. Done when: strokes agree with the recorded reference within 1 unit. Cheaper substitute: lines between points.
- [ ] Draw pen, pencil and highlighter by pressure and tilt in `ui/controls/ink_canvas.c`. Done when: frames match within 6 of 255. Cheaper substitute: one width.
- [ ] Show a stroke within one frame of the input in `ui/controls/ink_canvas.c`. Done when: timed frames prove it. Cheaper substitute: at the end.
- [ ] Erase by stroke and by point in `ui/controls/ink_canvas.c`. Done when: random erasing equals a plain model. Cheaper substitute: stroke.
- [ ] Select by lasso, then move, size, copy and delete in `ui/controls/ink_canvas.c`. Done when: scripts match. Cheaper substitute: none.
- [ ] Save and load strokes safely in `ui/ink_file.c`. Done when: a 120 second native fuzz run finds nothing. Cheaper substitute: a picture.
- [ ] Undo each action in `ui/controls/ink_canvas.c`. Done when: 1,000 random actions return to the start. Cheaper substitute: the last.
- [ ] Handle Delete, Ctrl-Z, Ctrl-Y, Ctrl-C and Ctrl-V in `ui/controls/ink_canvas.c`. Done when: the script `reference/scripts/ink_canvas.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/ink_canvas.c`: Pane with the count of strokes, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/ink_canvas.c`. Done when: `tools/check_control.py ink_canvas` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/ink_canvas.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: ink canvas`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 19. Ink tool bar

Tools for the canvas.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user chooses how to draw.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/ink_toolbar.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/ink_toolbar.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/ink_toolbar.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `InkToolbar` of `controls/dev/InkToolbar/InkToolbar.xaml` (no states of its own); `InkToolbarBallpointPenButton` of `controls/dev/InkToolbar/InkToolbar.xaml` (16 states in the groups CommonStates 8, FlowDirectionStates 2, ButtonFlyoutDirectionStates 6); `InkToolbarEraserButton` of `controls/dev/InkToolbar/InkToolbar.xaml` (16 states in the groups CommonStates 8, FlowDirectionStates 2, ButtonFlyoutDirectionStates 6); `InkToolbarStencilButton` of `controls/dev/InkToolbar/InkToolbar.xaml` (17 states in the groups CommonStates 9, FlowDirectionStates 2, ButtonFlyoutDirectionStates 6). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/ink_toolbar.c`, with its cases in `reference/cases/ink_toolbar.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Choose pen, pencil, highlighter, eraser and own tools in `ui/controls/ink_toolbar.c`. Done when: each reaches the canvas once. Cheaper substitute: pen.
- [ ] Choose colour and width in the tool's flyout in `ui/controls/ink_toolbar.c`. Done when: the next stroke has them. Cheaper substitute: fixed.
- [ ] Show and turn the ruler and protractor, and draw along them in `ui/controls/ink_toolbar.c`. Done when: strokes lie on the edge within 1 unit. Cheaper substitute: a picture.
- [ ] Clear all, with a question and undo in `ui/controls/ink_toolbar.c`. Done when: undo returns every stroke. Cheaper substitute: at once.
- [ ] Handle arrows, Enter, Space and Escape in `ui/controls/ink_toolbar.c`. Done when: the script `reference/scripts/ink_toolbar.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/ink_toolbar.c`: ToolBar, RadioButton for tools, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/ink_toolbar.c`. Done when: `tools/check_control.py ink_toolbar` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/ink_toolbar.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: ink tool bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 20. Resize gripper and backdrop element

A corner or edge that sizes, and a part of the page with the window ground.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sizes part of a page.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/resize_gripper.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/resize_gripper.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/resize_gripper.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `ResizeGripper` of `controls/dev/ResizeGripper/ResizeGripper.xaml` (states Normal, PointerOver, Pressed, Disabled, Horizontal, Vertical). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/resize_gripper.c`, with its cases in `reference/cases/resize_gripper.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Size its target by drag and key within limits in `ui/controls/resize_gripper.c`. Done when: scripts equal real WinUI. Cheaper substitute: drag.
- [ ] Draw the edge and corner forms in `ui/controls/resize_gripper.c`. Done when: every state of the template matches. Cheaper substitute: one.
- [ ] Mark the area of `controls/dev/SystemBackdropElement` for the host in `ui/material.c`. Done when: the recording host receives area and corners. Cheaper substitute: a colour.
- [ ] Show the fallback where the host has none in `ui/controls/resize_gripper.c`. Done when: frames match. Cheaper substitute: nothing.
- [ ] Handle arrows in `ui/controls/resize_gripper.c`. Done when: the script `reference/scripts/resize_gripper.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/resize_gripper.c`: Thumb with Transform, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/resize_gripper.c`. Done when: `tools/check_control.py resize_gripper` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/resize_gripper.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Commit: `controls: resize gripper and backdrop element`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 21. Layout panel and monochrome overlay

A panel whose layout is given to it, and an overlay that draws an element in one colour.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the program arranges and recolours parts of a page.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/controls/layout_panel.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Take a layout object and its context in `ui/controls/layout_panel.c`, from `controls/dev/LayoutPanel`. Done when: the repeater's layouts give the same rectangles in it. Cheaper substitute: a stack.
- [ ] Draw another element through two replacement colours in `ui/controls/monochrome_overlay.c`, from `controls/dev/MonochromaticOverlayPresenter`. Done when: frames match or, where the package lacks it, equal the pinned shader's results within 2. Cheaper substitute: one tint.
- [ ] Follow changes of the source element in `ui/controls/monochrome_overlay.c`. Done when: its next frame follows. Cheaper substitute: a copy.
- [ ] Add pages in `samples/gallery/layout_panel.c` and rows in `tests/state_matrix.json`. Done when: light, dark and contrast pass. Cheaper substitute: light.
- [ ] Commit: `controls: layout panel and monochrome overlay`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 22. All controls accepted

The gallery, the matrix, the screen reader and the speed budgets over everything at once.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** each control has its own; this section reads all of their results.
**Job:** the user meets one complete set of controls.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `tools/check_roadmap.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_gallery.py`. Done when: the checks fail on a planted fault.

- [ ] Find a gallery page and a passing matrix row for every carried control in `tests/test_gallery.py`. Done when: `docs/winui-coverage.json` has no control without both. Cheaper substitute: the common ones.
- [ ] Walk every page and every dialog by keyboard, by touch and by a Windows screen reader client in `tests/test_gallery.py`. Done when: three scripts finish and their logs equal the recorded ones. Cheaper substitute: pointer.
- [ ] Run the matrix with right to left, text at 225 percent and each contrast theme in `tests/test_state_matrix.py`. Done when: every cell passes. Cheaper substitute: light at 100 percent.
- [ ] Measure opening a menu and scrolling a list of 100,000 rows in `tests/fixtures/bench_controls.c`. Done when: both stay in the budgets of `tests/budgets.json`. Cheaper substitute: primitives only.
- [ ] Find no item of the `docs/os-roadmap/08-graphics-ui` files without an answer by `tools/check_roadmap.py`. Done when: it exits 0. Cheaper substitute: a summary.
- [ ] Commit: `controls: all accepted`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- The gallery shows each, and the state matrix passes on Windows.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
