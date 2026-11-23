---
schema_version: 1
id: controls
domain: 05-controls
status: active
title: "TODO-01 -- Controls"
---

# TODO-01 -- Controls

> **Goal:** Every everyday control of WinUI, and the few the OS adds, drawn and behaving as the real one, on the OS's engines or on this kit's own.

> **Current state:** **Groomed 2026-11-23:** 29 of the 30 sections carried the same four items, none waited for the engine and none was a surface. §1 to §30 keep their addresses; sections that held several controls were split into §31 to §55. Template files, states and automation roles were read from the pinned WinUI commit. Measured 2026-11-23: the button is drawn; `01-interface` builds the first four controls.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: where a feature belongs, and parts.
- `docs/os-roadmap/`: the OS sections whose work moved here.
- `todo/05-controls/TODO-02-more-controls.md` and `todo/05-controls/TODO-03-dialogs.md`: the rest.
- The WinUI checkout at `tools/winui/pin.json`; paths named `controls/dev/` and `dxaml/` are in it.

## Outcome

- Each control meets the authoring contract.
- The OS can import the controls and run them on its own engines.

**Adjacency:** all=not-applicable (controls hold no records, settings or reports; programs own those)

**Adjacency rationale:** A control shows and edits what the program gives it; the program decides what is kept.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Inventory, coverage and authoring contract | D01 T01 §7, D01 T01 §17, D06 T01 §1 | [ ] |
| 2 | §2 | Scroll view | §1, §5, D01 T01 §9, D07 T01 §4 | [ ] |
| 3 | §3 | Pop-ups, flyouts and light dismissal | §1, §5, D01 T01 §12, D01 T01 §15, D07 T01 §1 | [ ] |
| 4 | §4 | Commands and data lists | §1 | [ ] |
| 5 | §5 | Automation peers and inventory ids | §1 | [ ] |
| 6 | §6 | Repeat, toggle and hyperlink buttons | §4, §5 | [ ] |
| 7 | §7 | Check box | §5, D01 T01 §13 | [ ] |
| 8 | §8 | Slider | §5, §14 | [ ] |
| 9 | §9 | Text box fields and multi-line text | §2, §12, §4, §5, D01 T02 §4 | [ ] |
| 10 | §10 | Number box | §9 | [ ] |
| 11 | §11 | Combo box | §2, §3, §9 | [ ] |
| 12 | §12 | Menu flyout and context menu | §2, §3, §4 | [ ] |
| 13 | §13 | Command bar and tool bar | §6, §12 | [ ] |
| 14 | §14 | Tooltips | §3 | [ ] |
| 15 | §15 | Content dialog | §3, §6 | [ ] |
| 16 | §16 | Info bar | §6 | [ ] |
| 17 | §17 | Tab view | §6, §12, §20, D07 T01 §7 | [ ] |
| 18 | §18 | Navigation view | §2, §12, §44 | [ ] |
| 19 | §19 | Expander | §6 | [ ] |
| 20 | §20 | Items repeater and items view | §2, §4 | [ ] |
| 21 | §21 | Tree view | §20, D07 T01 §7 | [ ] |
| 22 | §22 | Flip view and pips pager | §20 | [ ] |
| 23 | §23 | Split view | §3 | [ ] |
| 24 | §24 | Calendar view | §2, §5, D06 T01 §5 | [ ] |
| 25 | §25 | Colour picker | §8, §10, §11, D01 T01 §13 | [ ] |
| 26 | §26 | Rating control | §5 | [ ] |
| 27 | §27 | Title bar | §6, D07 T01 §1 | [ ] |
| 28 | §28 | Media transport controls | §6, §8, §12 | [ ] |
| 29 | §29 | Classic controls with no modern form | §6, §9, §10, §13, §20 | [ ] |
| 30 | §30 | Controls gallery | §9, §18, §20, §55 | [ ] |
| 31 | §31 | Scroll bars | §2 | [ ] |
| 32 | §32 | Split, toggle split and drop-down buttons | §6, §12 | [ ] |
| 33 | §33 | Radio buttons | §5 | [ ] |
| 34 | §34 | Group box and separator | §1, §5 | [ ] |
| 35 | §35 | Progress bar | §5 | [ ] |
| 36 | §36 | Progress ring | §5, D01 T01 §13 | [ ] |
| 37 | §37 | Password box | §9 | [ ] |
| 38 | §38 | Rich edit box | §9, D01 T02 §4, D01 T02 §14 | [ ] |
| 39 | §39 | Spelling marks | §9 | [ ] |
| 40 | §40 | Auto-suggest and search box | §2, §3, §9 | [ ] |
| 41 | §41 | Menu bar | §12 | [ ] |
| 42 | §42 | Command bar flyout | §13 | [ ] |
| 43 | §43 | Teaching tip | §3, §6 | [ ] |
| 44 | §44 | Info badge | §5 | [ ] |
| 45 | §45 | Person picture | §44, D01 T01 §11 | [ ] |
| 46 | §46 | Selector bar and segmented control | §20 | [ ] |
| 47 | §47 | Breadcrumb bar | §12, §20 | [ ] |
| 48 | §48 | Settings card and settings expander | §6, §19 | [ ] |
| 49 | §49 | Grid view | §20, D07 T01 §7 | [ ] |
| 50 | §50 | List view groups, reorder and columns | §20, §31, §29, T02 §5, D07 T01 §7 | [ ] |
| 51 | §51 | Semantic zoom | §49 | [ ] |
| 52 | §52 | Two-pane view and splitter | §23 | [ ] |
| 53 | §53 | Date and time pickers | §3, §11, §24 | [ ] |
| 54 | §54 | Status bar | §1, §5 | [ ] |
| 55 | §55 | State matrix | §4, §5 | [ ] |

## 1. Inventory, coverage and authoring contract

One contract for every control, one list that names where each WinUI control is carried, and the OS's Windows 11 inventory beside WinUI's source as the reference.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `tools/inventory.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Copy the inventory's control records, states, metrics and motions, `controls.md` and `tokens.json` at the commit in `inventory.json` with `tools/inventory.py`. Done when: a changed byte is refused. Cheaper substitute: a copy by hand.
- [ ] Write `docs/control-contract.md`, with the rule that a control the reference package lacks is checked against its compiled template, and: template, states, keyboard, pointer, touch, pen, automation, mirroring, density, themes, text size, gallery page, matrix row. Done when: each point names its check. Cheaper substitute: a list.
- [ ] Check the mechanical points in `tools/check_control.py`. Done when: removing any one from a fixture control names it. Cheaper substitute: review.
- [ ] Add a new control's files and stubs with `tools/new_control.py`. Done when: the made control passes the contract's structure and fails its first test. Cheaper substitute: copying another.
- [ ] Name every folder of `controls/dev` and every template of `generic.xaml` with its section here, or the reason it is left out, in `docs/winui-coverage.json`. Done when: a template added by a pin move fails the check. Cheaper substitute: memory.
- [ ] Record there that `MapControl` and `WebView2` are services of a map provider and a browser, and that `Charts` has only tests in the pinned commit. Done when: the check accepts only these named reasons. Cheaper substitute: silence.
- [ ] Name every inventory record that belongs to a control, and those that have none, in `inventory.json`. Done when: the counts agree with the inventory. Cheaper substitute: the known ones.
- [ ] Answer each item of §1 and §3 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md`, and §7 and §8 of `docs/os-roadmap/08-graphics-ui-TODO-05-widget-library-core.md`, in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Map every item of `docs/os-roadmap/` to its section in `docs/os-roadmap-answers.md`. Done when: `tools/check_roadmap.py` finds no item without one. Cheaper substitute: a summary.
- [ ] Commit: `controls: inventory, coverage and contract`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Scroll view

Content larger than its place, moved by wheel, touch, keys and code, as ScrollPresenter, ScrollView and ScrollViewer do.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user moves through content larger than its place.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/scroll_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/scroll_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/scroll_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `ScrollView` of `controls/dev/ScrollView/ScrollView.xaml` (states NoIndicator, TouchIndicator, MouseIndicator, ScrollBarsSeparatorCollapsed, ScrollBarsSeparatorCollapsedDisabled, ScrollBarsSeparatorExpanded, ScrollBarsSeparatorDisplayedWithoutAnimation, ScrollBarsSeparatorExpandedWithoutAnimation, ScrollBarsSeparatorCollapsedWithoutAnimation, ContentClipped, ContentNotClipped); `ScrollViewer` of `controls/dev/CommonStyles/ScrollViewer_themeresources.xaml` (states NoIndicator, TouchIndicator, MouseIndicator, MouseIndicatorFull, ScrollBarSeparatorCollapsed, ScrollBarSeparatorExpanded, ScrollBarSeparatorExpandedWithoutAnimation, ScrollBarSeparatorCollapsedWithoutAnimation). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/scroll_view.c`, with its cases in `reference/cases/scroll_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Hold offset, extent, viewport and zoom in `ui/controls/scroll_view.c`. Done when: random changes keep the offset in range and equal real WinUI. Cheaper substitute: an offset alone.
- [ ] Scroll by wheel, touchpad, touch with inertia, and pen in `ui/controls/scroll_view.c`. Done when: positions at 10 instants agree within 2 units. Cheaper substitute: whole lines.
- [ ] Chain to the outer view at the end, and lock to one direction in `ui/controls/scroll_view.c`. Done when: a nested script ends at real WinUI's offsets. Cheaper substitute: both moving.
- [ ] Bring an element into view with and without animation in `ui/controls/scroll_view.c`. Done when: 200 requests give real WinUI's offsets. Cheaper substitute: the top.
- [ ] Snap to points and anchor content that grows above in `ui/controls/scroll_view.c`. Done when: a chat list keeps its place. Cheaper substitute: jumping.
- [ ] Zoom around a point between limits in `ui/controls/scroll_view.c`. Done when: the point stays under the pointer. Cheaper substitute: from the corner.
- [ ] Handle arrows, Page Up, Page Down, Home, End and Ctrl with each in `ui/controls/scroll_view.c`. Done when: the script `reference/scripts/scroll_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/scroll_view.c`: Pane with the Scroll pattern, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/scroll_view.c`. Done when: `tools/check_control.py scroll_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/scroll_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §9 of `docs/os-roadmap/08-graphics-ui-TODO-05-widget-library-core.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: scroll view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Pop-ups, flyouts and light dismissal

The layer that menus, lists, tips and dialogs open in.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees extra content over the page and leaves it at once.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/popup.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Open content in its own window or over the page in `ui/popup.c`. Done when: a pop-up larger than its window is whole. Cheaper substitute: clipping.
- [ ] Place it by the 13 placements with flipping and sliding at edges in `ui/placement.c`. Done when: 500 random targets give real WinUI's rectangles. Cheaper substitute: below always.
- [ ] Dismiss on a press outside, Escape, focus loss and scrolling, and pass or swallow that press, in `ui/popup.c`. Done when: recorded scripts give real WinUI's events. Cheaper substitute: closing only.
- [ ] Nest pop-ups and close the right number in `ui/popup.c`. Done when: a three level script closes one per Escape. Cheaper substitute: all.
- [ ] Move focus in, keep it in when asked, and return it, in `ui/popup.c`. Done when: the focus log equals real WinUI's. Cheaper substitute: no return.
- [ ] Draw shadow, border, Acrylic area and the opening motion in `ui/popup.c`. Done when: frames at 10 instants match. Cheaper substitute: at once.
- [ ] Draw the overlay behind when asked in `ui/popup.c`. Done when: the covered page matches real WinUI. Cheaper substitute: none.
- [ ] Give the Window pattern and modal state in `ui/popup.c`. Done when: the dump equals real WinUI's. Cheaper substitute: a group.
- [ ] Commit: `controls: pop-ups, flyouts and light dismissal`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Commands and data lists

One command behind a button, menu item, key and access key; and lists that tell what changed. Neither draws anything.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/command.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Define a command with label, icon, keys, enabled and checked state in `ui/command.c`. Done when: changing it changes every place that shows it. Cheaper substitute: a callback per place.
- [ ] Add the standard commands (cut, copy, paste, select all, delete, undo, redo, open, save, close, share and the rest of StandardUICommandKind) in `ui/command.c`. Done when: keys and icon rectangles equal WinUI's, and every icon and label is from our own `icons/` and `ui/strings/`. Cheaper substitute: Microsoft's icons and wording.
- [ ] Resolve keyboard accelerators by scope in `ui/accelerator.c`. Done when: 200 recorded presses run the command real WinUI runs. Cheaper substitute: the window only.
- [ ] Show and resolve access keys in `ui/access_key.c`. Done when: an Alt script equals real WinUI's. Cheaper substitute: underlines only.
- [ ] Define an observable list with insert, remove, move, replace and reset in `ui/list.c`. Done when: random edits replayed from events give the same list. Cheaper substitute: reset always.
- [ ] Sort, filter and group a list through a view in `ui/list_view_model.c`. Done when: random edits equal a fresh sort. Cheaper substitute: once.
- [ ] Keep selection and current item through edits in `ui/selection_model.c`. Done when: 10,000 random edits keep the same items selected. Cheaper substitute: indexes.
- [ ] Load more rows near the end in `ui/list.c`. Done when: a slow source is asked once per page. Cheaper substitute: all rows.
- [ ] Commit: `controls: commands and data lists`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Automation peers and inventory ids

What a screen reader or a test can read and do, in portable C. It draws nothing; the Windows bridge is in `todo/07-windows/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/peer.h` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Define the peer: role, name, value, state, rectangle, parent, children, actions and events in `ui/peer.h`. Done when: a test reads the tree of the four first controls. Cheaper substitute: a name.
- [ ] Define each pattern's calls in `ui/peer_patterns.c`. Done when: a fake client works each on a fixture. Cheaper substitute: Invoke only.
- [ ] Compute a name from content, header, label and tooltip in `ui/peer_name.c`. Done when: 100 cases equal real WinUI's dump. Cheaper substitute: the text.
- [ ] Give landmarks, headings, position in set, level, live settings and help text in `ui/peer.c`. Done when: the dump equals real WinUI's. Cheaper substitute: none.
- [ ] Raise events once per change in `ui/peer_events.c`. Done when: a listener's log equals real WinUI's. Cheaper substitute: polling.
- [ ] Carry the inventory id in `ui/peer.c`. Done when: the contract's check finds each. Cheaper substitute: a comment.
- [ ] Survive an element removed during a call in `ui/peer.c`. Done when: 10,000 random removals give the named error. Cheaper substitute: a dangling pointer.
- [ ] Commit: `controls: automation peers`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Repeat, toggle and hyperlink buttons

The single-part buttons beyond the standard one, with icon and text content.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user starts an action, holds it, or switches it on.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/buttons.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/buttons.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/buttons.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `RepeatButton` of `controls/dev/CommonStyles/RepeatButton_themeresources.xaml` (states Normal, PointerOver, Pressed, Disabled); `ToggleButton` of `controls/dev/CommonStyles/ToggleButton_themeresources.xaml` (states Normal, PointerOver, Pressed, Disabled, Checked, CheckedPointerOver, CheckedPressed, CheckedDisabled, Indeterminate, IndeterminatePointerOver, IndeterminatePressed, IndeterminateDisabled); `HyperlinkButton` of `controls/dev/CommonStyles/HyperlinkButton_themeresources.xaml` (states Normal, PointerOver, Pressed, Disabled). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/buttons.c`, with its cases in `reference/cases/buttons.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Repeat after the system's delay and interval, and stop outside in `ui/controls/buttons.c`. Done when: a 1 second hold fires as often as real WinUI. Cheaper substitute: one click.
- [ ] Toggle on, off and indeterminate in `ui/controls/buttons.c`. Done when: each order of clicks gives real WinUI's states. Cheaper substitute: two states.
- [ ] Open a hyperlink through the host, and show its visited state in `ui/controls/buttons.c`. Done when: the recording host receives the address once. Cheaper substitute: a click event.
- [ ] Draw standard, accent, subtle and icon-only styles of one type in `ui/controls/buttons.c`. Done when: one state matrix covers all. Cheaper substitute: a type per style.
- [ ] Make one default and one cancel button per scope in `ui/controls/buttons.c`. Done when: Enter and Escape reach them as in real WinUI. Cheaper substitute: focus only.
- [ ] Run a command and follow its enabled and checked state in `ui/controls/buttons.c`. Done when: changing the command restyles the button. Cheaper substitute: once.
- [ ] Handle Space on release, Enter on press and Escape to cancel in `ui/controls/buttons.c`. Done when: the script `reference/scripts/buttons.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/buttons.c`: Button with Invoke or Toggle, Hyperlink with Invoke, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/buttons.c`. Done when: `tools/check_control.py buttons` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/buttons.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §2 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: repeat, toggle and hyperlink buttons`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Check box

Two and three state choice with its drawn mark.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user turns a choice on, off or sees it mixed.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/check_box.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/check_box.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/check_box.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `CheckBox` of `controls/dev/CommonStyles/CheckBox_themeresources.xaml` (states UncheckedNormal, UncheckedPointerOver, UncheckedPressed, UncheckedDisabled, CheckedNormal, CheckedPointerOver, CheckedPressed, CheckedDisabled, IndeterminateNormal, IndeterminatePointerOver, IndeterminatePressed, IndeterminateDisabled). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/check_box.c`, with its cases in `reference/cases/check_box.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Cycle unchecked, checked and indeterminate by the allowed states in `ui/controls/check_box.c`. Done when: each order equals real WinUI. Cheaper substitute: two states.
- [ ] Animate the mark's stroke in `ui/controls/check_box.c`. Done when: frames at 10 instants match. Cheaper substitute: at once.
- [ ] Take presses on the label as well as the box in `ui/controls/check_box.c`. Done when: the hit area equals real WinUI's. Cheaper substitute: the box only.
- [ ] Set a parent box from its children and back in `ui/controls/check_box.c`. Done when: a select-all script equals the WinUI Gallery sample. Cheaper substitute: by the program.
- [ ] Handle Space in `ui/controls/check_box.c`. Done when: the script `reference/scripts/check_box.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/check_box.c`: CheckBox with Toggle, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/check_box.c`. Done when: `tools/check_control.py check_box` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/check_box.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §1 of `docs/os-roadmap/08-graphics-ui-TODO-05-widget-library-core.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: check box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. Slider

A value chosen along a track.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user chooses a value in a range.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/slider.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/slider.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/slider.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `Slider` of `controls/dev/CommonStyles/Slider_themeresources.xaml` (states Normal, PointerOver, Pressed, Disabled, FocusDisengaged, FocusEngagedHorizontal, FocusEngagedVertical); `Thumb` of `controls/dev/CommonStyles/Slider_themeresources.xaml` (states Normal, PointerOver, Pressed, Disabled). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/slider.c`, with its cases in `reference/cases/slider.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Drag the thumb, press the track and turn the wheel in `ui/controls/slider.c`. Done when: values equal real WinUI's for a recorded script. Cheaper substitute: drag only.
- [ ] Snap to steps and ticks, and draw ticks in each placement in `ui/controls/slider.c`. Done when: frames and values match. Cheaper substitute: continuous.
- [ ] Work horizontally, vertically and reversed in `ui/controls/slider.c`. Done when: each direction's script passes. Cheaper substitute: horizontal.
- [ ] Show the value tip with a formatted text while dragging in `ui/controls/slider.c`. Done when: its place and text match. Cheaper substitute: none.
- [ ] Handle arrows, Page Up, Page Down, Home and End in `ui/controls/slider.c`. Done when: the script `reference/scripts/slider.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/slider.c`: Slider with RangeValue, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/slider.c`. Done when: `tools/check_control.py slider` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/slider.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §3 of `docs/os-roadmap/08-graphics-ui-TODO-05-widget-library-core.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: slider`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. Text box fields and multi-line text

The text box of the first controls, completed.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user enters and edits text.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/text_box.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/text_box.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/text_box.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `TextBox` of `controls/dev/CommonStyles/TextBox_themeresources.xaml` (states Normal, Disabled, PointerOver, Focused, ButtonVisible, ButtonCollapsed). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/text_box.c`, with its cases in `reference/cases/text_box.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Add header, description, placeholder and clear button in `ui/controls/text_box.c`. Done when: every state of the template matches. Cheaper substitute: text only.
- [ ] Wrap lines and scroll both ways in `ui/controls/text_box.c`. Done when: a 10,000 line text scrolls and edits within the frame budget. Cheaper substitute: one line.
- [ ] Limit length, casing and allowed characters, and refuse in one step in `ui/controls/text_box.c`. Done when: pasted text is cut as real WinUI cuts it. Cheaper substitute: typing only.
- [ ] Select by word, line and drag, with touch handles in `ui/controls/text_box.c`. Done when: recorded scripts give equal selections. Cheaper substitute: single clicks.
- [ ] Undo and redo in the steps real WinUI makes in `ui/controls/text_box.c`. Done when: a script of 200 edits agrees at every step. Cheaper substitute: each key.
- [ ] Move selected text by drag and take dropped text in `ui/controls/text_box.c`. Done when: the ends equal real WinUI's. Cheaper substitute: refusing.
- [ ] Show composition from the input method through `ui/host.h`. Done when: a composition played by the recording host draws, changes and commits as recorded. Cheaper substitute: committed text.
- [ ] Open its own menu with cut, copy, paste, undo, redo and select all in `ui/controls/text_box.c`. Done when: each entry is enabled exactly when real WinUI enables it. Cheaper substitute: keys only.
- [ ] Be read-only, and tell the keyboard what kind of text is wanted in `ui/controls/text_box.c`. Done when: the recording host sees the scope. Cheaper substitute: text always.
- [ ] Handle the whole editing key table of the inventory in `ui/controls/text_box.c`. Done when: the script `reference/scripts/text_box.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/text_box.c`: Edit with Value and Text, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/text_box.c`. Done when: `tools/check_control.py text_box` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/text_box.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §4 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: text box fields and multi-line text`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 10. Number box

A number typed, stepped or calculated.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user enters an exact number.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/number_box.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/number_box.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/number_box.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `NumberBox` of `controls/dev/NumberBox/NumberBox.xaml` (states Normal, Disabled, SpinButtonsCollapsed, SpinButtonsVisible, SpinButtonsPopup, UpSpinButtonEnabled, UpSpinButtonDisabled, DownSpinButtonEnabled, DownSpinButtonDisabled). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/number_box.c`, with its cases in `reference/cases/number_box.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Parse and format by the locale's number form in `ui/controls/number_box.c`. Done when: a table of 60 texts equals real WinUI. Cheaper substitute: one form.
- [ ] Evaluate simple sums exactly in fixed point in `ui/controls/number_box.c`. Done when: 2+3*4 gives 14 and a malformed sum is refused. Cheaper substitute: no sums.
- [ ] Step with hidden, inline and compact buttons, small and large in `ui/controls/number_box.c`. Done when: recorded scripts give equal values. Cheaper substitute: inline.
- [ ] Keep to limits, or wrap, or overwrite invalid text in `ui/controls/number_box.c`. Done when: the three modes equal real WinUI. Cheaper substitute: limits only.
- [ ] Return to the last good value and show the error state in `ui/controls/number_box.c`. Done when: typing abc restores the value. Cheaper substitute: keeping it.
- [ ] Handle Up, Down, Page Up, Page Down, Enter and Escape in `ui/controls/number_box.c`. Done when: the script `reference/scripts/number_box.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/number_box.c`: Spinner with RangeValue, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/number_box.c`. Done when: `tools/check_control.py number_box` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/number_box.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §5 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: number box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 11. Combo box

One choice from a list that opens over the field, editable or not.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user chooses one of many in little space.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/combo_box.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/combo_box.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/combo_box.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `ComboBox` of `controls/dev/ComboBox/ComboBox_themeresources.xaml` (17 states in the groups CommonStates 4, FocusStates 5, DropDownStates 2, EditableModeStates 6); `ComboBoxItem` of `controls/dev/ComboBox/ComboBox_themeresources.xaml` (states Normal, PointerOver, Disabled, Pressed, Selected, SelectedUnfocused, SelectedDisabled, SelectedPointerOver, SelectedPressed, InputModeDefault, TouchInputMode, GameControllerInputMode). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/combo_box.c`, with its cases in `reference/cases/combo_box.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Open with the selected item over the field, within the work area in `ui/controls/combo_box.c`. Done when: rectangles equal real WinUI's for 100 positions. Cheaper substitute: below.
- [ ] Size the list to its widest item and limit its height in `ui/controls/combo_box.c`. Done when: widths match. Cheaper substitute: the field's width.
- [ ] Choose while closed and while open, by wheel too in `ui/controls/combo_box.c`. Done when: recorded scripts give the events real WinUI gives. Cheaper substitute: open only.
- [ ] Search by typing letters in `ui/controls/combo_box.c`. Done when: a timed script chooses equal items. Cheaper substitute: first letter.
- [ ] Edit text, complete it and accept a new value in `ui/controls/combo_box.c`. Done when: the submitted text equals real WinUI's. Cheaper substitute: a fixed list.
- [ ] Hold 10,000 items without making each in `ui/controls/combo_box.c`. Done when: fewer than 100 containers live. Cheaper substitute: all.
- [ ] Handle Alt-Down, F4, arrows, Home, End, Enter, Escape and letters in `ui/controls/combo_box.c`. Done when: the script `reference/scripts/combo_box.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/combo_box.c`: ComboBox with ExpandCollapse, Selection and Value, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/combo_box.c`. Done when: `tools/check_control.py combo_box` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/combo_box.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §5 of `docs/os-roadmap/08-graphics-ui-TODO-05-widget-library-core.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: combo box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 12. Menu flyout and context menu

One menu for context menus, drop-down buttons, menu bars and the system menu.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user runs one of many actions.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/menu.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/menu.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/menu.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `MenuFlyoutItem` of `controls/dev/CommonStyles/MenuFlyout_themeresources.xaml` (states Normal, PointerOver, Pressed, Disabled, NoPlaceholder, CheckPlaceholder, IconPlaceholder, CheckAndIconPlaceholder, DefaultPadding, NarrowPadding, KeyboardAcceleratorTextCollapsed, KeyboardAcceleratorTextVisible); `ToggleMenuFlyoutItem` of `controls/dev/CommonStyles/MenuFlyout_themeresources.xaml` (states Normal, PointerOver, Pressed, Disabled, Unchecked, Checked, UncheckedWithIcon, CheckedWithIcon, DefaultPadding, NarrowPadding, KeyboardAcceleratorTextCollapsed, KeyboardAcceleratorTextVisible); `MenuFlyoutSubItem` of `controls/dev/CommonStyles/MenuFlyout_themeresources.xaml` (states Normal, PointerOver, Pressed, SubMenuOpened, Disabled, NoPlaceholder, CheckPlaceholder, IconPlaceholder, CheckAndIconPlaceholder, DefaultPadding, NarrowPadding); `SplitMenuFlyoutItem` of `controls/dev/CommonStyles/MenuFlyout_themeresources.xaml` (states Normal, PrimaryPointerOver, PrimaryPressed, SecondaryPointerOver, SecondaryPressed, SubMenuOpened, Disabled, NoPlaceholder, CheckPlaceholder, IconPlaceholder, CheckAndIconPlaceholder, DefaultPadding, NarrowPadding); `MenuFlyoutSeparator` of `controls/dev/CommonStyles/MenuFlyout_themeresources.xaml` (no states of its own); `MenuFlyoutPresenter` of `controls/dev/CommonStyles/MenuFlyout_themeresources.xaml` (no states of its own). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/menu.c`, with its cases in `reference/cases/menu.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Build items from commands: command, toggle, radio, submenu, split and separator in `ui/controls/menu.c`. Done when: a command's change shows in an open menu. Cheaper substitute: a copy.
- [ ] Align icon, check, label, key text and chevron columns over all items in `ui/controls/menu.c`. Done when: rectangles equal real WinUI's. Cheaper substitute: each item alone.
- [ ] Open a submenu by delay, click and key, and keep it while the pointer moves toward it in `ui/controls/menu.c`. Done when: timed scripts equal real WinUI. Cheaper substitute: at once.
- [ ] Scroll a menu taller than the work area in `ui/controls/menu.c`. Done when: every item can be reached. Cheaper substitute: cut off.
- [ ] Raise one context request for right click, hold, the menu key and Shift-F10 in `ui/controls/menu.c`. Done when: four scripts give one event each with its place. Cheaper substitute: right click.
- [ ] Skip disabled items and return focus on closing in `ui/controls/menu.c`. Done when: the focus log equals real WinUI's. Cheaper substitute: no return.
- [ ] Restyle for touch with taller items when opened by touch in `ui/controls/menu.c`. Done when: heights match. Cheaper substitute: one height.
- [ ] Handle arrows, Enter, Space, Escape, Home, End, Alt and letters in `ui/controls/menu.c`. Done when: the script `reference/scripts/menu.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/menu.c`: Menu and MenuItem with Invoke, Toggle or ExpandCollapse, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/menu.c`. Done when: `tools/check_control.py menu` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/menu.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §12 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: menu flyout and context menu`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 13. Command bar and tool bar

A row of commands with an overflow menu. The tool bar of older programs is the same bar.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user finds the common actions of a page.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/command_bar.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/command_bar.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/command_bar.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `CommandBar` of `controls/dev/CommonStyles/CommandBar_themeresources.xaml` (16 states in the groups CommonStates 2, DisplayModeStates 9, AvailableCommandsStates 3, DynamicOverflowStates 2); `AppBarButton` of `controls/dev/CommonStyles/AppBarButton_themeresources.xaml` (23 states in the groups ApplicationViewStates 8, CommonStates 8, InputModeStates 3, KeyboardAcceleratorTextVisibility 2, FlyoutStates 2); `AppBarToggleButton` of `controls/dev/CommonStyles/AppBarToggleButton_themeresources.xaml` (25 states in the groups ApplicationViewStates 6, CommonStates 14, InputModeStates 3, KeyboardAcceleratorTextVisibility 2); `AppBarSeparator` of `controls/dev/CommonStyles/AppBarSeparator_themeresources.xaml` (states FullSize, Compact, Overflow); `AppBar` of `dxaml/xcp/dxaml/themes/generic.xaml` (states Normal, Disabled, CompactClosed, CompactOpenUp, CompactOpenDown, MinimalClosed, MinimalOpenUp, MinimalOpenDown, HiddenClosed, HiddenOpenUp, HiddenOpenDown). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/command_bar.c`, with its cases in `reference/cases/command_bar.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Show labels below, at the right or collapsed, and the compact form in `ui/controls/command_bar.c`. Done when: each form's frames match. Cheaper substitute: one.
- [ ] Move commands to the overflow in the declared order as width shrinks, and back in `ui/controls/command_bar.c`. Done when: from 600 to 200 units none is lost. Cheaper substitute: cutting off.
- [ ] Open and close, stay open, and hide in `ui/controls/command_bar.c`. Done when: the AppBar display modes equal real WinUI. Cheaper substitute: always open.
- [ ] Hold other content at the start in `ui/controls/command_bar.c`. Done when: a search box in it works by key and pointer. Cheaper substitute: buttons only.
- [ ] Show one command in both places with one state in `ui/controls/command_bar.c`. Done when: disabling it disables both. Cheaper substitute: two.
- [ ] Handle arrows inside the bar, Tab out, Alt-Down and Escape in `ui/controls/command_bar.c`. Done when: the script `reference/scripts/command_bar.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/command_bar.c`: AppBar with ExpandCollapse and Toggle, Button for each, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/command_bar.c`. Done when: `tools/check_control.py command_bar` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/command_bar.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §13 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` and §3 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: command bar and tool bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 14. Tooltips

A short text over an element, by pointer and by keyboard.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user learns what an element is.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/tooltip.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/tooltip.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/tooltip.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `ToolTip` of `controls/dev/CommonStyles/ToolTip_themeresources.xaml` (states Closed, Opened). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/tooltip.c`, with its cases in `reference/cases/tooltip.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Open after the system's time, at once when one was just shown, and close by time or movement in `ui/controls/tooltip.c`. Done when: timed scripts equal real WinUI. Cheaper substitute: one delay.
- [ ] Place it by pointer or element within the work area in `ui/controls/tooltip.c`. Done when: 200 positions match. Cheaper substitute: below.
- [ ] Open on keyboard focus and close with Escape or Ctrl in `ui/controls/tooltip.c`. Done when: the script equals real WinUI. Cheaper substitute: pointer only.
- [ ] Stay while the pointer is over it in `ui/controls/tooltip.c`. Done when: it can be read and left. Cheaper substitute: closing.
- [ ] Show the full text of trimmed text by itself in `ui/controls/tooltip.c`. Done when: a trimmed block has one and a whole one has none. Cheaper substitute: by the program.
- [ ] Give the automation peer in `ui/controls/tooltip.c`: ToolTip, and the help text of its owner, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/tooltip.c`. Done when: `tools/check_control.py tooltip` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/tooltip.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §7 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: tooltips`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 15. Content dialog

The one dialog frame, modal to its window.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user answers before going on.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/content_dialog.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/content_dialog.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/content_dialog.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `ContentDialog` of `controls/dev/CommonStyles/ContentDialog_themeresources.xaml` (19 states in the groups DialogShowingStates 3, DialogSizingStates 2, ButtonsVisibilityStates 8, DefaultButtonStates 4, DialogBorderStates 2). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/content_dialog.c`, with its cases in `reference/cases/content_dialog.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Lay out title, content, and up to three buttons with the default one in `ui/controls/content_dialog.c`. Done when: every state of the template matches, with and without full size. Cheaper substitute: two buttons.
- [ ] Keep focus inside and return it in `ui/controls/content_dialog.c`. Done when: Tab cycles as in real WinUI. Cheaper substitute: leaving.
- [ ] Map Enter to the default and Escape to close, unless the content uses them in `ui/controls/content_dialog.c`. Done when: the script equals real WinUI. Cheaper substitute: always.
- [ ] Let the program hold or refuse closing, also after a wait in `ui/controls/content_dialog.c`. Done when: a deferral script gives real WinUI's events. Cheaper substitute: closing at once.
- [ ] Refuse a second dialog in the same window by name in `ui/controls/content_dialog.c`. Done when: the first stays. Cheaper substitute: two.
- [ ] Scroll content that is too tall and follow a changed window size in `ui/controls/content_dialog.c`. Done when: 320 to 548 units wide match. Cheaper substitute: cutting.
- [ ] Handle Enter, Escape, Tab and arrows between buttons in `ui/controls/content_dialog.c`. Done when: the script `reference/scripts/content_dialog.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/content_dialog.c`: Window, modal, with its title as the name, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/content_dialog.c`. Done when: `tools/check_control.py content_dialog` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/content_dialog.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §14 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: content dialog`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 16. Info bar

A message in the page with its severity.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user is told of a state that lasts.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/info_bar.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/info_bar.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/info_bar.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `InfoBar` of `controls/dev/InfoBar/InfoBar.xaml` (15 states in the groups SeverityLevels 4, IconStates 3, ? 2, InfoBarVisibility 2, ? 2, ? 2). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/info_bar.c`, with its cases in `reference/cases/info_bar.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Show informational, success, warning and error with icon and colour in `ui/controls/info_bar.c`. Done when: every state of the template matches. Cheaper substitute: colour only.
- [ ] Wrap text and move the action below when narrow in `ui/controls/info_bar.c`. Done when: heights equal real WinUI's at 50 widths. Cheaper substitute: one line.
- [ ] Close by button or program, with the cause in `ui/controls/info_bar.c`. Done when: three causes are told apart. Cheaper substitute: one.
- [ ] Tell the screen reader when it opens in `ui/controls/info_bar.c`. Done when: the event log equals real WinUI's. Cheaper substitute: silence.
- [ ] Handle Tab, Enter and Space in `ui/controls/info_bar.c`. Done when: the script `reference/scripts/info_bar.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/info_bar.c`: StatusBar, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/info_bar.c`. Done when: `tools/check_control.py info_bar` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/info_bar.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §9 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: info bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 17. Tab view

The document tabs of Notepad, Explorer and terminals.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user keeps several documents in one window.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/tab_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/tab_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/tab_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `TabView` of `controls/dev/TabView/TabView.xaml` (states NormalBottomBorderLine, SingleBottomBorderLine); `TabViewItem` of `controls/dev/TabView/TabView.xaml` (40 states in the groups CommonStates 6, DisabledStates 2, DataVirtualizationStates 2, ReorderHintStates 5, DragStates 11, IconStates 2, TabWidthModes 2, CloseIconStates 2, ? 2, ? 4, ? 2); `TabViewListView` of `controls/dev/TabView/TabView.xaml` (states LeftBottomBorderLineNormal, LeftBottomBorderLineShort, RightBottomBorderLineNormal, RightBottomBorderLineShort, NormalBottomBorderLine, NoBottomBorderLine). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/tab_view.c`, with its cases in `reference/cases/tab_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Draw the selected tab joined to its page, with icon, title and close button in each mode in `ui/controls/tab_view.c`. Done when: every state of the template matches. Cheaper substitute: flat.
- [ ] Size tabs equally, to content or compact, then scroll with buttons in `ui/controls/tab_view.c`. Done when: widths equal real WinUI's for 1 to 100 tabs. Cheaper substitute: one mode.
- [ ] Reorder by drag with the others moving aside in `ui/controls/tab_view.c`. Done when: the list's order and motion match. Cheaper substitute: swapping at the end.
- [ ] Tear a tab out to a new window and drop one from another in `ui/controls/tab_view.c`. Done when: no tab is lost or doubled in 1,000 random drags. Cheaper substitute: inside only.
- [ ] Choose the next tab after a close in `ui/controls/tab_view.c`. Done when: the order equals real WinUI's. Cheaper substitute: the first.
- [ ] Add a tab, and show unsaved state and the tab's own menu in `ui/controls/tab_view.c`. Done when: scripts read back each. Cheaper substitute: none.
- [ ] Show more rows than memory should hold lazily in `ui/controls/tab_view.c`. Done when: 500 tabs keep one live page. Cheaper substitute: all pages.
- [ ] Handle Ctrl-Tab, Ctrl-Shift-Tab, Ctrl-1 to Ctrl-9, Ctrl-T, Ctrl-W and Ctrl-F4 in `ui/controls/tab_view.c`. Done when: the script `reference/scripts/tab_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/tab_view.c`: Tab with Selection, TabItem with SelectionItem, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/tab_view.c`. Done when: `tools/check_control.py tab_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/tab_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §17 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` and §6 of `docs/os-roadmap/08-graphics-ui-TODO-05-widget-library-core.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: tab view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 18. Navigation view

The pane, its items and the page area of most Windows 11 programs.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user moves between the parts of a program.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/navigation_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/navigation_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/navigation_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `NavigationView` of `controls/dev/NavigationView/NavigationView.xaml` (27 states in the groups DisplayModeGroup 5, TogglePaneGroup 2, HeaderGroup 2, AutoSuggestGroup 2, PaneStateGroup 2, PaneStateListSizeGroup 2, PaneOverlayGroup 2, TitleBarVisibilityGroup 2, OverflowLabelGroup 2, BackButtonGroup 2, PaneVisibilityGroup 2, PaneSeparatorStates 2); `NavigationViewItem` of `controls/dev/NavigationView/NavigationView_themeresources.xaml` (states Normal, PointerOver, Pressed, Selected, PointerOverSelected, PressedSelected, Enabled, Disabled); `NavigationViewItemPresenter` of `controls/dev/NavigationView/NavigationView_themeresources.xaml` (26 states in the groups PointerStates 6, DisabledStates 2, IconStates 2, InfoBadgeStates 2, ChevronStates 3, PointerChevronStates 9, PaneAndTopLevelItemStates 2); `NavigationViewItemHeader` of `controls/dev/NavigationView/NavigationView.xaml` (states HeaderTextVisible, HeaderTextCollapsed, LeftMode, TopMode). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/navigation_view.c`, with its cases in `reference/cases/navigation_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Switch left, compact, minimal and top by width in `ui/controls/navigation_view.c`. Done when: modes change at real WinUI's widths. Cheaper substitute: one mode.
- [ ] Expand and collapse nested items in place, and in a flyout when compact in `ui/controls/navigation_view.c`. Done when: scripts give equal trees. Cheaper substitute: flat.
- [ ] Move the selection mark between items in `ui/controls/navigation_view.c`. Done when: frames at 10 instants match. Cheaper substitute: jumping.
- [ ] Show header, search, footer, settings and back button in `ui/controls/navigation_view.c`. Done when: each place matches. Cheaper substitute: items only.
- [ ] Move top items to the overflow as width shrinks in `ui/controls/navigation_view.c`. Done when: none is lost. Cheaper substitute: cutting off.
- [ ] Raise item invoked and selection changed, and let the program refuse in `ui/controls/navigation_view.c`. Done when: the event log equals real WinUI's. Cheaper substitute: one event.
- [ ] Hold a badge on an item in `ui/controls/navigation_view.c`. Done when: it shows in each mode. Cheaper substitute: expanded only.
- [ ] Handle arrows, Enter, Space, Home, End, F6 and Alt-Left in `ui/controls/navigation_view.c`. Done when: the script `reference/scripts/navigation_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/navigation_view.c`: Custom with Selection, ListItem or TabItem with SelectionItem and ExpandCollapse, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/navigation_view.c`. Done when: `tools/check_control.py navigation_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/navigation_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §15 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: navigation view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 19. Expander

A header that shows and hides its content.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user opens more detail in place.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/expander.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/expander.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/expander.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `Expander` of `controls/dev/Expander/Expander.xaml` (states ExpandUp, CollapseDown, ExpandDown, CollapseUp, Down, Up). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/expander.c`, with its cases in `reference/cases/expander.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Open down and up with the recorded motion in `ui/controls/expander.c`. Done when: frames at 10 instants match. Cheaper substitute: at once.
- [ ] Keep hidden content out of the tab order and the automation tree in `ui/controls/expander.c`. Done when: a Tab script skips it. Cheaper substitute: drawing nothing.
- [ ] Hold a control in the header in `ui/controls/expander.c`. Done when: pressing it does not toggle. Cheaper substitute: toggling always.
- [ ] Handle Space and Enter in `ui/controls/expander.c`. Done when: the script `reference/scripts/expander.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/expander.c`: Button with ExpandCollapse, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/expander.c`. Done when: `tools/check_control.py expander` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/expander.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §18 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: expander`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 20. Items repeater and items view

Many items, few live elements, any layout.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user looks through many items.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/items_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/items_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/items_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `ItemsView` of `controls/dev/ItemsView/ItemsView.xaml` (no states of its own); `ItemContainer` of `controls/dev/ItemContainer/ItemContainer.xaml` (states UnselectedNormal, UnselectedPointerOver, UnselectedPressed, SelectedNormal, SelectedPointerOver, SelectedPressed, Single, Multiple, Enabled, Disabled). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/items_view.c`, with its cases in `reference/cases/items_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Make and reuse elements for visible items only, in `ui/repeater.c`. Done when: 1,000,000 items keep under 200 live. Cheaper substitute: all.
- [ ] Lay out as stack, uniform grid, flow and lines in `ui/repeater_layout.c`. Done when: rectangles equal real WinUI's for 1,000 random sizes. Cheaper substitute: stack.
- [ ] Keep the view steady when items above change in `ui/controls/items_view.c`. Done when: the first visible item stays. Cheaper substitute: jumping.
- [ ] Select none, single, multiple and extended in `ui/controls/items_view.c`. Done when: 200 recorded scripts give equal selections. Cheaper substitute: single.
- [ ] Animate added, removed and moved items in `ui/controls/items_view.c`. Done when: frames at 10 instants match. Cheaper substitute: at once.
- [ ] Invoke by click, double click or Enter by mode in `ui/controls/items_view.c`. Done when: events equal real WinUI's. Cheaper substitute: click.
- [ ] Handle arrows, Ctrl and Shift with them, Space, Enter, Ctrl-A, Home, End and Page keys in `ui/controls/items_view.c`. Done when: the script `reference/scripts/items_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/items_view.c`: List with Selection, ListItem with SelectionItem and Invoke, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/items_view.c`. Done when: `tools/check_control.py items_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/items_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §19 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: items repeater and items view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 21. Tree view

Nested items.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user works in a hierarchy.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/tree_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/tree_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/tree_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `TreeView` of `controls/dev/TreeView/TreeView.xaml` (no states of its own); `TreeViewItem` of `controls/dev/TreeView/TreeViewItem.xaml` (states Normal, PointerOver, Pressed, Selected, Disabled, PointerOverSelected, PressedSelected, SelectedDisabled, ReorderedPlaceholder, TreeViewMultiSelectDisabled, TreeViewMultiSelectEnabledUnselected, TreeViewMultiSelectEnabledSelected, NotDragging, MultipleDraggingPrimary). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/tree_view.c`, with its cases in `reference/cases/tree_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Show only expanded rows of a tree in `ui/controls/tree_view.c`. Done when: 100,000 nodes keep under 200 live. Cheaper substitute: all rows.
- [ ] Expand by chevron, double click, key and program, and load children late in `ui/controls/tree_view.c`. Done when: a slow source is asked once. Cheaper substitute: all at the start.
- [ ] Select single and several with mixed boxes in `ui/controls/tree_view.c`. Done when: parents and children agree in random scripts. Cheaper substitute: leaves only.
- [ ] Drag nodes before, after and into others, never into themselves in `ui/controls/tree_view.c`. Done when: random drags keep every node once. Cheaper substitute: moving up and down.
- [ ] Edit a name in place in `ui/controls/tree_view.c`. Done when: Enter keeps it and Escape returns the old. Cheaper substitute: a dialog.
- [ ] Handle arrows, Home, End, Enter, Space, F2, letters and *  in `ui/controls/tree_view.c`. Done when: the script `reference/scripts/tree_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/tree_view.c`: Tree, TreeItem with ExpandCollapse and SelectionItem, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/tree_view.c`. Done when: `tools/check_control.py tree_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/tree_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §2 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: tree view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 22. Flip view and pips pager

One item at a time, with dots.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user looks at one item after another.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/flip_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/flip_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/flip_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `FlipView` of `controls/dev/CommonStyles/FlipView_themeresources.xaml` (no states of its own); `FlipViewItem` of `controls/dev/CommonStyles/FlipViewItem_themeresources.xaml` (no states of its own); `PipsPager` of `controls/dev/PipsPager/PipsPager.xaml` (states PreviousPageButtonVisible, PreviousPageButtonHidden, PreviousPageButtonCollapsed, PreviousPageButtonEnabled, PreviousPageButtonDisabled, NextPageButtonVisible, NextPageButtonHidden, NextPageButtonCollapsed, NextPageButtonEnabled, NextPageButtonDisabled, VerticalOrientationView, HorizontalOrientationView). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/flip_view.c`, with its cases in `reference/cases/flip_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Move by button, swipe, wheel and key, stopping at the ends in `ui/controls/flip_view.c`. Done when: scripts equal real WinUI. Cheaper substitute: buttons.
- [ ] Show buttons on pointer and hide them after a time in `ui/controls/flip_view.c`. Done when: timed frames match. Cheaper substitute: always.
- [ ] Keep only the neighbours live in `ui/controls/flip_view.c`. Done when: 10,000 pictures keep three. Cheaper substitute: all.
- [ ] Draw dots in both directions with more than fit, and wrap when asked in `ui/controls/flip_view.c`. Done when: frames match for 1 to 100 pages. Cheaper substitute: ten.
- [ ] Join dots and view both ways in `ui/controls/flip_view.c`. Done when: either changes the other once. Cheaper substitute: one way.
- [ ] Handle arrows, Home and End in `ui/controls/flip_view.c`. Done when: the script `reference/scripts/flip_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/flip_view.c`: FlipView with Selection, Menu with Selection for the dots, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/flip_view.c`. Done when: `tools/check_control.py flip_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/flip_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §19 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: flip view and pips pager`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 23. Split view

A pane beside or over content.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user opens a side pane.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/split_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/split_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/split_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `SplitView` of `controls/dev/SplitView/SplitView_themeresources.xaml` (states Closed, ClosedCompactLeft, ClosedCompactRight, OpenOverlayLeft, OpenOverlayRight, OpenInlineLeft, OpenInlineRight, OpenCompactOverlayLeft, OpenCompactOverlayRight, OverlayNotVisible, OverlayVisible). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/split_view.c`, with its cases in `reference/cases/split_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Open as overlay, inline, compact overlay and compact inline, left or right in `ui/controls/split_view.c`. Done when: every state of the template matches. Cheaper substitute: overlay.
- [ ] Run the opening motion and reverse it when interrupted in `ui/controls/split_view.c`. Done when: frames at 10 instants match. Cheaper substitute: at once.
- [ ] Close an overlay pane by a press outside and Escape, and return focus in `ui/controls/split_view.c`. Done when: the event log equals real WinUI's. Cheaper substitute: a button.
- [ ] Handle Escape and F6 in `ui/controls/split_view.c`. Done when: the script `reference/scripts/split_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/split_view.c`: Window for the open overlay pane, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/split_view.c`. Done when: `tools/check_control.py split_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/split_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §20 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: split view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 24. Calendar view

Month, year and decade with one or more chosen days.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user chooses days.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/calendar_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/calendar_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/calendar_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `CalendarView` of `controls/dev/CommonStyles/CalendarView_themeresources.xaml` (states Normal, Disabled, ViewChanged, ViewChanging, Month, Year, Decade); `CalendarViewDayItem` of `controls/dev/CommonStyles/CalendarView_themeresources.xaml` (no states of its own). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/calendar_view.c`, with its cases in `reference/cases/calendar_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Lay out 2 to 8 weeks with the locale's first day and names in `ui/controls/calendar_view.c`. Done when: dates from 1600 to 9999 stand right. Cheaper substitute: one locale.
- [ ] Zoom between month, year and decade with the recorded motion in `ui/controls/calendar_view.c`. Done when: frames match. Cheaper substitute: at once.
- [ ] Select none, one or several, within limits and around blocked days in `ui/controls/calendar_view.c`. Done when: scripts equal real WinUI. Cheaper substitute: one.
- [ ] Mark today, other months and dots for busy days in `ui/controls/calendar_view.c`. Done when: frames match. Cheaper substitute: today.
- [ ] Use the calendars Windows offers (Gregorian, Hijri, Hebrew, Thai, Japanese and others) in `ui/controls/calendar_view.c`. Done when: 1,000 random dates agree with Windows. Cheaper substitute: Gregorian.
- [ ] Handle arrows, Page Up, Page Down, Home, End, Ctrl-Up and Ctrl-Down in `ui/controls/calendar_view.c`. Done when: the script `reference/scripts/calendar_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/calendar_view.c`: Calendar with Grid, Table, Selection and Value; DataItem, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/calendar_view.c`. Done when: `tools/check_control.py calendar_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/calendar_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §8 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: calendar view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 25. Colour picker

A colour by spectrum, sliders, numbers or hex.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user chooses a colour.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/color_picker.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/color_picker.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/color_picker.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `ColorPicker` of `controls/dev/ColorPicker/ColorPicker.xaml` (28 states in the groups ColorSpectrumVisibility 2, ColorPreviewVisibility 2, PreviousColorVisibility 4, ThirdDimensionSliderVisibility 2, AlphaSliderVisibility 2, MoreButtonVisibility 2, TextEntryGridVisibility 2, ColorChannelTextInputVisibility 2, AlphaTextInputVisibility 2, ColorRepresentationSelected 2, HexInputVisibility 2, AlphaEnabledState 2, Orientation 2); `ColorSpectrum` of `controls/dev/ColorPicker/ColorSpectrum.xaml` (states Normal, PointerOver, Pressed, PressedLarge, SelectionEllipseLight, SelectionEllipseDark, BoxSelected, RingSelected, Unfocused, Focused, PointerFocused). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/color_picker.c`, with its cases in `reference/cases/color_picker.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Convert RGB, HSV and hex in fixed point in `ui/color_math.c`. Done when: 4,096 colours return within 1. Cheaper substitute: doubles.
- [ ] Draw the box and the ring in integers in `ui/controls/color_picker.c`. Done when: frames match within 3 of 255. Cheaper substitute: box.
- [ ] Choose each spectrum's two axes and the third slider in `ui/controls/color_picker.c`. Done when: the six orders match. Cheaper substitute: hue and saturation.
- [ ] Edit alpha over a checker ground in `ui/controls/color_picker.c`. Done when: frames match. Cheaper substitute: opaque.
- [ ] Type hex and numbers with refusals in `ui/controls/color_picker.c`. Done when: a table of 40 texts equals real WinUI. Cheaper substitute: hex.
- [ ] Show new beside old, and fold parts away in `ui/controls/color_picker.c`. Done when: every state of the template matches. Cheaper substitute: all shown.
- [ ] Handle arrows, and Ctrl with arrows for the large step in `ui/controls/color_picker.c`. Done when: the script `reference/scripts/color_picker.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/color_picker.c`: Slider with Value that says the colour name, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/color_picker.c`. Done when: `tools/check_control.py color_picker` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/color_picker.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §9 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: colour picker`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 26. Rating control

A row of stars.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user gives a rating.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/rating.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/rating.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/rating.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `RatingControl` of `controls/dev/RatingControl/RatingControl.xaml` (states Disabled, Placeholder, PointerOverPlaceholder, PointerOverUnselected, Set, PointerOverSet). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/rating.c`, with its cases in `reference/cases/rating.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Set by press, drag and key, and clear by pressing again in `ui/controls/rating.c`. Done when: scripts equal real WinUI. Cheaper substitute: press.
- [ ] Show a placeholder, read-only and fraction in `ui/controls/rating.c`. Done when: frames match. Cheaper substitute: whole.
- [ ] Use other glyphs and counts in `ui/controls/rating.c`. Done when: frames match for 1 to 10. Cheaper substitute: five stars.
- [ ] Open the larger stars while touched in `ui/controls/rating.c`. Done when: frames match. Cheaper substitute: one size.
- [ ] Handle arrows, Home, End, digits and Enter in `ui/controls/rating.c`. Done when: the script `reference/scripts/rating.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/rating.c`: Slider with Value and RangeValue, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/rating.c`. Done when: `tools/check_control.py rating` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/rating.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §11 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: rating control`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 27. Title bar

The top of a window, drawn by the program.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user knows and moves the window.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/title_bar.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/title_bar.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/title_bar.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `TitleBar` of `controls/dev/TitleBar/TitleBar.xaml` (30 states in the groups BackButtonGroup 3, PaneToggleButtonGroup 3, LeftHeaderGroup 3, IconGroup 3, TitleTextGroup 3, SubtitleTextGroup 3, ContentGroup 3, RightHeaderGroup 3, HeightGroup 2, DisplayModeGroup 2, LeftHeaderSpacingGroup 2). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/title_bar.c`, with its cases in `reference/cases/title_bar.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Lay out back, pane button, icon, title, subtitle, content and the space of the window buttons in `ui/controls/title_bar.c`. Done when: every state of the template matches. Cheaper substitute: title.
- [ ] Drag, double click, right click and Alt-Space as the system does in `ui/controls/title_bar.c`. Done when: the recorded messages equal a WinUI window's. Cheaper substitute: drag.
- [ ] Leave controls in it usable in `ui/controls/title_bar.c`. Done when: a press on a search box does not move the window. Cheaper substitute: all drags.
- [ ] Show the snap choices over the maximise button in `ui/controls/title_bar.c`. Done when: hovering opens Windows' own. Cheaper substitute: none.
- [ ] Draw active, inactive, full screen and tablet forms in `ui/controls/title_bar.c`. Done when: frames match. Cheaper substitute: active.
- [ ] Handle Alt-Space and Alt-Left in `ui/controls/title_bar.c`. Done when: the script `reference/scripts/title_bar.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/title_bar.c`: TitleBar, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/title_bar.c`. Done when: `tools/check_control.py title_bar` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/title_bar.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §22 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: title bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 28. Media transport controls

The buttons under a video or sound.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user controls playing media.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/media_transport.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/media_transport.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/media_transport.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `MediaTransportControls` of `controls/dev/CommonStyles/MediaTransportControls_themeresources.xaml` (23 states in the groups ControlPanelVisibilityStates 2, MediaStates 5, AudioSelectionAvailablityStates 2, CCSelectionAvailablityStates 2, FocusStates 3, MediaTransportControlMode 2, PlayPauseStates 2, VolumeMuteStates 2, RepeatStates 3). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/media_transport.c`, with its cases in `reference/cases/media_transport.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Bind to a media session interface in `ui/media.h`. Done when: a fake session receives each action once. Cheaper substitute: a player.
- [ ] Seek with buffered range, time tip and steps in `ui/controls/media_transport.c`. Done when: positions equal real WinUI's. Cheaper substitute: drag.
- [ ] Move secondary buttons to the menu when narrow in `ui/controls/media_transport.c`. Done when: none is lost. Cheaper substitute: cutting off.
- [ ] Choose speed, sound track, captions, repeat and full screen in `ui/controls/media_transport.c`. Done when: each reaches the session. Cheaper substitute: play only.
- [ ] Hide after a time and show on movement, touch or key in `ui/controls/media_transport.c`. Done when: timed scripts equal real WinUI. Cheaper substitute: always.
- [ ] Handle Space, arrows, M, F, Escape and the media keys in `ui/controls/media_transport.c`. Done when: the script `reference/scripts/media_transport.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/media_transport.c`: Custom group with named buttons and a Slider, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/media_transport.c`. Done when: `tools/check_control.py media_transport` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/media_transport.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §23 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: media transport controls`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 29. Classic controls with no modern form

Up-down, IP address, hot key, header, rebar and animation controls that older programs and the OS adapters need, in the modern look. The OS owns the window-class adapters.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the classic controls on Windows for behaviour, and the pinned inventory for the look.
**Job:** the user of an older program meets the same look.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/controls/up_down.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Write the class map in `docs/classic-map.md`: each classic class, the control that draws it and the differences. Done when: every class of the inventory's `classic-controls` is in it. Cheaper substitute: the common ones.
- [ ] Build the up-down and its buddy in `ui/controls/up_down.c`. Done when: recorded steps and wraps equal the classic control's. Cheaper substitute: number box only.
- [ ] Build the IP address field in `ui/controls/ip_address.c`. Done when: typing, dots, limits and paste equal the classic control. Cheaper substitute: a text box.
- [ ] Build the hot key field in `ui/controls/hot_key.c`. Done when: every key combination reads as the classic control reads it. Cheaper substitute: typed text.
- [ ] Build the header and its column drag in `ui/controls/header.c`. Done when: sizes and orders equal the classic control. Cheaper substitute: fixed columns.
- [ ] Build the rebar and pager in `ui/controls/rebar.c`. Done when: bands move, size and wrap as the classic control. Cheaper substitute: a row.
- [ ] Follow dark, accent and contrast in each, in `ui/controls/header.c` and the others. Done when: `tools/check_control.py` passes for each. Cheaper substitute: light only.
- [ ] Give each its peer in `ui/controls/up_down.c` and the others. Done when: roles equal the classic controls' dump. Cheaper substitute: a pane.
- [ ] Answer each item of §24 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: classic controls with no modern form`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 30. Controls gallery

A program that shows every control, for people and for tests.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** each shown control has its own; the frame is the navigation view of §18.
**Job:** the user sees and tries every control.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `samples/gallery/main.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_gallery.py`. Done when: the checks fail on a planted fault.

- [ ] Build the navigation, search and page register in `samples/gallery/main.c`. Done when: every page file made so far is found, shown and searched. Cheaper substitute: a list.
- [ ] Switch disabled, right-to-left, theme, accent, contrast, text size and scale per page in `samples/gallery/options.c`. Done when: each switch changes the page at once. Cheaper substitute: the window.
- [ ] Hold each state still in a strip in `samples/gallery/strip.c`. Done when: the matrix reads every state without input. Cheaper substitute: live only.
- [ ] Open a page and state from the command line in `samples/gallery/main.c`. Done when: `gallery button/pressed` shows it. Cheaper substitute: by hand.
- [ ] Show the source that makes each example in `samples/gallery/source.c`. Done when: the text equals the compiled file. Cheaper substitute: typed again.
- [ ] Work by keyboard and by touch alone in `tests/test_gallery.py`. Done when: two scripts visit every page made so far. Cheaper substitute: pointer.
- [ ] Answer each item of §25 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: the gallery`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 31. Scroll bars

The thin and the full bar over content.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees where they are and moves.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/scroll_bar.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/scroll_bar.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/scroll_bar.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `ScrollBar` of `controls/dev/CommonStyles/ScrollBar_themeresources.xaml` (states Normal, Disabled, TouchIndicator, MouseIndicator, NoIndicator, Collapsed, Expanded, ExpandedWithoutAnimation, CollapsedWithoutAnimation). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/scroll_bar.c`, with its cases in `reference/cases/scroll_bar.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Grow from the thin line when the pointer nears, after the recorded time, and shrink in `ui/controls/scroll_bar.c`. Done when: timed frames match. Cheaper substitute: always full.
- [ ] Size and place the thumb with its least length in `ui/controls/scroll_bar.c`. Done when: 1,000 random ratios equal real WinUI. Cheaper substitute: proportion only.
- [ ] Page by the track, step by arrows and drag, returning when dragged far in `ui/controls/scroll_bar.c`. Done when: scripts equal real WinUI. Cheaper substitute: drag.
- [ ] Lie over content without changing its width, or beside it when asked in `ui/controls/scroll_bar.c`. Done when: text does not reflow. Cheaper substitute: beside.
- [ ] Follow the user's always-show setting in `ui/controls/scroll_bar.c`. Done when: both settings match. Cheaper substitute: one.
- [ ] Give the automation peer in `ui/controls/scroll_bar.c`: ScrollBar with RangeValue, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/scroll_bar.c`. Done when: `tools/check_control.py scroll_bar` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/scroll_bar.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §9 of `docs/os-roadmap/08-graphics-ui-TODO-05-widget-library-core.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: scroll bars`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 32. Split, toggle split and drop-down buttons

Buttons with a second part that opens a flyout.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user runs the usual action or picks another.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/split_button.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/split_button.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/split_button.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `SplitButton` of `controls/dev/SplitButton/SplitButton.xaml` (17 states in the groups CommonStates 15, SecondaryButtonPlacementStates 2). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/split_button.c`, with its cases in `reference/cases/split_button.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Draw two parts with their own hover and press in `ui/controls/split_button.c`. Done when: every state of the template matches. Cheaper substitute: one.
- [ ] Open by the chevron, Alt-Down and F4, without running the first action in `ui/controls/split_button.c`. Done when: the event log equals real WinUI's. Cheaper substitute: both.
- [ ] Toggle the first part in the toggle form in `ui/controls/split_button.c`. Done when: scripts equal real WinUI. Cheaper substitute: no toggle.
- [ ] Turn the drop-down's chevron on press in `ui/controls/split_button.c`. Done when: frames at 10 instants match. Cheaper substitute: still.
- [ ] Handle Space, Enter, Alt-Down, F4 and Escape in `ui/controls/split_button.c`. Done when: the script `reference/scripts/split_button.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/split_button.c`: SplitButton with Invoke or Toggle, and ExpandCollapse, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/split_button.c`. Done when: `tools/check_control.py split_button` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/split_button.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §2 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: split, toggle split and drop-down buttons`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 33. Radio buttons

One choice of a few, alone or as a group in columns.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user chooses one of a few.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/radio.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/radio.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/radio.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `RadioButton` of `controls/dev/CommonStyles/RadioButton_themeresources.xaml` (states Normal, PointerOver, Pressed, Disabled, Checked, Unchecked, Indeterminate); `RadioButtons` of `controls/dev/RadioButtons/RadioButtons.xaml` (states Normal, Disabled). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/radio.c`, with its cases in `reference/cases/radio.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Keep one chosen per group name and scope in `ui/controls/radio.c`. Done when: random clicks keep one. Cheaper substitute: per panel.
- [ ] Lay out a group in columns with a header in `ui/controls/radio.c`. Done when: rectangles match for 1 to 20 items. Cheaper substitute: one column.
- [ ] Grow and shrink the dot by state in `ui/controls/radio.c`. Done when: frames at 10 instants match. Cheaper substitute: still.
- [ ] Be one tab stop and choose as focus moves, or not with Ctrl in `ui/controls/radio.c`. Done when: scripts equal real WinUI. Cheaper substitute: each a stop.
- [ ] Handle arrows, Home, End, Space and Ctrl with arrows in `ui/controls/radio.c`. Done when: the script `reference/scripts/radio.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/radio.c`: RadioButton with SelectionItem, Group, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/radio.c`. Done when: `tools/check_control.py radio` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/radio.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §2 of `docs/os-roadmap/08-graphics-ui-TODO-05-widget-library-core.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: radio buttons`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 34. Group box and separator

Grouping by a heading and card, and by a line. WinUI has neither; the OS design page gives the look.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the design page and tokens of the pinned inventory.
**Job:** the user sees what belongs together.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/controls/group_box.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Draw heading and card by `docs/design/controls.md` of the pinned inventory in `ui/controls/group_box.c`. Done when: sizes and colours equal its tokens. Cheaper substitute: the classic frame.
- [ ] Draw horizontal and vertical lines with margins in `ui/controls/separator.c`. Done when: one device pixel at every scale. Cheaper substitute: a filled box.
- [ ] Name the group for the screen reader in `ui/controls/group_box.c`. Done when: the peer is a Group and its children carry it. Cheaper substitute: text.
- [ ] Add pages in `samples/gallery/group_box.c` and rows in `tests/state_matrix.json`. Done when: light, dark and contrast pass. Cheaper substitute: light.
- [ ] Answer each item of §6 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: group box and separator`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 35. Progress bar

How far work is, or that it goes on.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees that work goes on and how far.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/progress_bar.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/progress_bar.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/progress_bar.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `ProgressBar` of `controls/dev/ProgressBar/ProgressBar.xaml` (states Normal, Determinate, Updating, UpdatingError, Error, Paused, Indeterminate, IndeterminateError, IndeterminatePaused). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/progress_bar.c`, with its cases in `reference/cases/progress_bar.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Draw value, paused, error and the moving forms in `ui/controls/progress_bar.c`. Done when: every state of the template matches at 10 instants. Cheaper substitute: value.
- [ ] Move smoothly to a new value in `ui/controls/progress_bar.c`. Done when: frames match. Cheaper substitute: jumping.
- [ ] Ask for no frame while hidden or finished in `ui/controls/progress_bar.c`. Done when: the host sees none. Cheaper substitute: always.
- [ ] Give the automation peer in `ui/controls/progress_bar.c`: ProgressBar with RangeValue, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/progress_bar.c`. Done when: `tools/check_control.py progress_bar` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/progress_bar.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §4 of `docs/os-roadmap/08-graphics-ui-TODO-05-widget-library-core.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: progress bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 36. Progress ring

A ring for the same.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees that work goes on.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/progress_ring.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/progress_ring.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/progress_ring.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `ProgressRing` of `controls/dev/ProgressRing/ProgressRing.xaml` (states Inactive, DeterminateActive, Active). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/progress_ring.c`, with its cases in `reference/cases/progress_ring.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Draw an arc with round ends for each value in `ui/controls/progress_ring.c`. Done when: 25 percent spans 90 degrees at 16 to 128 units. Cheaper substitute: a polygon.
- [ ] Run the endless form by the pinned curve in `ui/controls/progress_ring.c`. Done when: frames at 20 instants match within the corner limit. Cheaper substitute: a plain turn.
- [ ] Become a still ring with reduced animation in `ui/controls/progress_ring.c`. Done when: the first frame is the last. Cheaper substitute: spinning.
- [ ] Ask for no frame while inactive in `ui/controls/progress_ring.c`. Done when: the host sees none. Cheaper substitute: always.
- [ ] Give the automation peer in `ui/controls/progress_ring.c`: ProgressBar with RangeValue, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/progress_ring.c`. Done when: `tools/check_control.py progress_ring` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/progress_ring.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §10 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: progress ring`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 37. Password box

Text that is hidden and wiped.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user enters a secret.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/password_box.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/password_box.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/password_box.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `PasswordBox` of `controls/dev/CommonStyles/PasswordBox_themeresources.xaml` (states Disabled, Normal, PointerOver, Focused, ButtonVisible, ButtonCollapsed). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/password_box.c`, with its cases in `reference/cases/password_box.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Draw one dot per character and nothing else in `ui/controls/password_box.c`. Done when: no glyph of the text is drawn unless shown. Cheaper substitute: drawing both.
- [ ] Show while the button is held, by mode in `ui/controls/password_box.c`. Done when: scripts equal real WinUI. Cheaper substitute: a toggle.
- [ ] Refuse copy, cut and drag in `ui/controls/password_box.c`. Done when: the clipboard stays as it was, and a drop target receives no data object. Cheaper substitute: the clipboard only.
- [ ] Wipe the buffer on change, clear and close in `ui/controls/password_box.c`. Done when: the bytes are seen zero in the built program. Cheaper substitute: freeing.
- [ ] Turn off the input method and say that Caps Lock is on in `ui/controls/password_box.c`. Done when: both are seen. Cheaper substitute: neither.
- [ ] Handle the editing keys without copy and cut in `ui/controls/password_box.c`. Done when: the script `reference/scripts/password_box.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/password_box.c`: Edit with Value that is never readable, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/password_box.c`. Done when: `tools/check_control.py password_box` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/password_box.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §4 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: password box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 38. Rich edit box

Styled text that can be edited.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user writes styled text.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/rich_edit.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/rich_edit.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/rich_edit.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `RichEditBox` of `controls/dev/CommonStyles/RichEditBox_themeresources.xaml` (states Disabled, Normal, PointerOver, Focused). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/rich_edit.c`, with its cases in `reference/cases/rich_edit.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Hold runs and paragraphs with bold, italic, underline, font, size, colour, links, lists and alignment in `ui/rich_text.c`. Done when: random edits equal a plain model. Cheaper substitute: one style.
- [ ] Change style by key and command, for the selection and the next typing in `ui/controls/rich_edit.c`. Done when: scripts equal real WinUI. Cheaper substitute: selection only.
- [ ] Read and write RTF safely in `ui/rtf.c`. Done when: 200 files round trip with WordPad's meaning, and a 120 second native fuzz run finds nothing. Cheaper substitute: plain text.
- [ ] Copy and paste as RTF and text in `ui/controls/rich_edit.c`. Done when: another program keeps the styles. Cheaper substitute: text.
- [ ] Hold pictures in text in `ui/controls/rich_edit.c`. Done when: a picture moves with its line. Cheaper substitute: none.
- [ ] Undo styles and text as one history in `ui/controls/rich_edit.c`. Done when: 200 steps agree. Cheaper substitute: text only.
- [ ] Handle the editing key table with Ctrl-B, Ctrl-I and Ctrl-U in `ui/controls/rich_edit.c`. Done when: the script `reference/scripts/rich_edit.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/rich_edit.c`: Edit and Document with Text, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/rich_edit.c`. Done when: `tools/check_control.py rich_edit` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/rich_edit.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §4 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: rich edit box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 39. Spelling marks

Wrong words underlined, with suggestions, from a speller the host gives.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees and corrects wrong words.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/spelling.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Ask the host for wrong ranges off the window's work in `ui/spelling.c`. Done when: typing 10,000 words never waits. Cheaper substitute: at once.
- [ ] Draw the marks in `ui/spelling.c`. Done when: frames match real WinUI. Cheaper substitute: a line.
- [ ] Offer suggestions, ignore and add in the text menu in `ui/spelling.c`. Done when: each changes the text or the list. Cheaper substitute: replace.
- [ ] Add the COM functions Windows' speller needs to tier 2 in `abi/tiers.json`, with what the OS must give. Done when: the import check passes at tier 2 and refuses tier 1. Cheaper substitute: an unchecked call.
- [ ] Fill the speller callback from Windows in `src/spell.c`. Done when: marks equal a WinUI text box's for 500 sentences, and a correction, an ignored and an added word each work through the text menu. Cheaper substitute: none.
- [ ] Work without a speller in `ui/spelling.c`. Done when: nothing is marked and no menu entry shows. Cheaper substitute: a crash.
- [ ] Give marks to the screen reader in `ui/spelling.c`. Done when: the text annotations equal real WinUI's. Cheaper substitute: drawing.
- [ ] Commit: `controls: spelling marks`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 40. Auto-suggest and search box

A text box with suggestions and a search form.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user finds by typing.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/auto_suggest.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/auto_suggest.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/auto_suggest.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `AutoSuggestBox` of `controls/dev/AutoSuggestBox/AutoSuggestBox_themeresources.xaml` (states Landscape, Portrait). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/auto_suggest.c`, with its cases in `reference/cases/auto_suggest.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Open for typed text only, after the typing pause in `ui/controls/auto_suggest.c`. Done when: a programmed change opens nothing. Cheaper substitute: always.
- [ ] Fill the box while moving through suggestions and return the typed text in `ui/controls/auto_suggest.c`. Done when: scripts equal real WinUI. Cheaper substitute: keeping it.
- [ ] Raise suggestion chosen and query submitted once each, in order in `ui/controls/auto_suggest.c`. Done when: the log equals real WinUI's. Cheaper substitute: one event.
- [ ] Place the list above when there is no room below in `ui/controls/auto_suggest.c`. Done when: 100 positions match. Cheaper substitute: below.
- [ ] Show a late answer only for the text it was asked for in `ui/controls/auto_suggest.c`. Done when: random delays never show an old list. Cheaper substitute: the last answer.
- [ ] Handle Up, Down, Enter, Escape and Tab in `ui/controls/auto_suggest.c`. Done when: the script `reference/scripts/auto_suggest.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/auto_suggest.c`: Group with Invoke, Edit and List, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/auto_suggest.c`. Done when: `tools/check_control.py auto_suggest` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/auto_suggest.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §6 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: auto-suggest and search box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 41. Menu bar

The row of menus at the top.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user finds every action of a program.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/menu_bar.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/menu_bar.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/menu_bar.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `MenuBar` of `controls/dev/MenuBar/MenuBar.xaml` (no states of its own); `MenuBarItem` of `controls/dev/MenuBar/MenuBarItem.xaml` (states Normal, PointerOver, Pressed, Selected). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/menu_bar.c`, with its cases in `reference/cases/menu_bar.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Open by click, then follow the pointer along the bar in `ui/controls/menu_bar.c`. Done when: scripts equal real WinUI. Cheaper substitute: a click each.
- [ ] Enter by Alt and F10, show access keys, and leave by Escape to where focus was in `ui/controls/menu_bar.c`. Done when: the focus log equals real WinUI's. Cheaper substitute: click.
- [ ] Run accelerators while closed in `ui/controls/menu_bar.c`. Done when: every command's key works. Cheaper substitute: open only.
- [ ] Fold into one button when narrow in `ui/controls/menu_bar.c`. Done when: no menu is lost. Cheaper substitute: cutting off.
- [ ] Handle Alt, F10, arrows, letters, Enter and Escape in `ui/controls/menu_bar.c`. Done when: the script `reference/scripts/menu_bar.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/menu_bar.c`: MenuBar, MenuItem with Invoke and ExpandCollapse, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/menu_bar.c`. Done when: `tools/check_control.py menu_bar` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/menu_bar.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §4 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: menu bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 42. Command bar flyout

The floating bar on selected text or an item.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user acts on what is selected.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/command_bar_flyout.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/command_bar_flyout.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/command_bar_flyout.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `CommandBarFlyoutCommandBar` of `controls/dev/CommandBarFlyout/CommandBarFlyout_themeresources.xaml` (20 states in the groups CommonStates 2, DisplayModeStates 3, ExpansionStates 3, AvailableCommandsStates 3, ? 5, ? 2, PrimaryLabelStates 2). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/command_bar_flyout.c`, with its cases in `reference/cases/command_bar_flyout.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Show a primary row and a secondary list, opened or closed in `ui/controls/command_bar_flyout.c`. Done when: every state of the template matches. Cheaper substitute: a menu.
- [ ] Open expanded on right click and small on selection in `ui/controls/command_bar_flyout.c`. Done when: scripts equal real WinUI. Cheaper substitute: one way.
- [ ] Fit the row to the work area and move the rest in `ui/controls/command_bar_flyout.c`. Done when: none is lost. Cheaper substitute: cutting off.
- [ ] Use it in text boxes for cut, copy, paste and style in `ui/controls/command_bar_flyout.c`. Done when: each entry is enabled as in real WinUI. Cheaper substitute: a menu.
- [ ] Handle arrows, Tab, Enter and Escape in `ui/controls/command_bar_flyout.c`. Done when: the script `reference/scripts/command_bar_flyout.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/command_bar_flyout.c`: Menu or ToolBar with Invoke, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/command_bar_flyout.c`. Done when: `tools/check_control.py command_bar_flyout` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/command_bar_flyout.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §13 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: command bar flyout`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 43. Teaching tip

A callout that points at something.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user is shown something new.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/teaching_tip.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/teaching_tip.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/teaching_tip.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `TeachingTip` of `controls/dev/TeachingTip/TeachingTip.xaml` (32 states in the groups LightDismissStates 2, ButtonsStates 4, ContentStates 2, CloseButtonLocations 2, IconStates 2, HeroContentPlacementStates 2, PlacementStates 14, TitleBlockStates 2, SubtitleBlockStates 2). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/teaching_tip.c`, with its cases in `reference/cases/teaching_tip.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Place it by 13 placements with its tail in `ui/controls/teaching_tip.c`. Done when: 300 targets give real WinUI's rectangle and tail. Cheaper substitute: below.
- [ ] Show title, subtitle, icon, picture, action and close in `ui/controls/teaching_tip.c`. Done when: every state of the template matches. Cheaper substitute: text.
- [ ] Follow a moving target and close when it leaves in `ui/controls/teaching_tip.c`. Done when: scripts equal real WinUI. Cheaper substitute: staying.
- [ ] Dismiss lightly or not in `ui/controls/teaching_tip.c`. Done when: both event logs match. Cheaper substitute: one.
- [ ] Run the opening and closing motion in `ui/controls/teaching_tip.c`. Done when: frames at 10 instants match. Cheaper substitute: at once.
- [ ] Handle F6, Tab, Enter and Escape in `ui/controls/teaching_tip.c`. Done when: the script `reference/scripts/teaching_tip.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/teaching_tip.c`: Window or Pane, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/teaching_tip.c`. Done when: `tools/check_control.py teaching_tip` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/teaching_tip.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §14 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: teaching tip`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 44. Info badge

A dot, number or icon on another element.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user sees that something is new.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/info_badge.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/info_badge.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/info_badge.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `InfoBadge` of `controls/dev/InfoBadge/InfoBadge_themeresources.xaml` (states Dot, Icon, FontIcon, Value). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/info_badge.c`, with its cases in `reference/cases/info_badge.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Draw dot, number and icon in the five colours in `ui/controls/info_badge.c`. Done when: every state matches in each colour. Cheaper substitute: dot.
- [ ] Grow with the number and shorten large ones in `ui/controls/info_badge.c`. Done when: widths equal real WinUI's for 0 to 99,999. Cheaper substitute: clipping.
- [ ] Attach to a corner of any element in `ui/controls/info_badge.c`. Done when: rectangles match in both directions. Cheaper substitute: a fixed place.
- [ ] Add its value to the owner's name in `ui/controls/info_badge.c`. Done when: the dump equals real WinUI's. Cheaper substitute: silence.
- [ ] Give the automation peer in `ui/controls/info_badge.c`: Text within its owner, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/info_badge.c`. Done when: `tools/check_control.py info_badge` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/info_badge.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §11 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: info badge`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 45. Person picture

A round picture, initials or a figure.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user recognises a person.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/person_picture.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/person_picture.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/person_picture.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `PersonPicture` of `controls/dev/PersonPicture/PersonPicture.xaml` (states Photo, Initials, NoPhotoOrInitials, Group, NoBadge, BadgeWithoutImageSource, BadgeWithImageSource). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/person_picture.c`, with its cases in `reference/cases/person_picture.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Draw a picture, or initials, or the group and person figures in `ui/controls/person_picture.c`. Done when: every state of the template matches. Cheaper substitute: a picture.
- [ ] Make initials from names of every script in `ui/controls/person_picture.c`. Done when: a table of 100 names equals real WinUI. Cheaper substitute: first letters.
- [ ] Show a badge with number, glyph or picture in `ui/controls/person_picture.c`. Done when: frames match. Cheaper substitute: none.
- [ ] Give the automation peer in `ui/controls/person_picture.c`: Text with the name and badge, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/person_picture.c`. Done when: `tools/check_control.py person_picture` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/person_picture.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §11 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: person picture`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 46. Selector bar and segmented control

Switching between a few views.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user switches view.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/selector_bar.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/selector_bar.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/selector_bar.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `SelectorBar` of `controls/dev/SelectorBar/SelectorBar.xaml` (no states of its own); `SelectorBarItem` of `controls/dev/SelectorBar/SelectorBar.xaml` (states UnselectedNormal, UnselectedPointerOver, UnselectedPressed, SelectedNormal, SelectedPointerOver, SelectedPressed, Enabled, Disabled). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/selector_bar.c`, with its cases in `reference/cases/selector_bar.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Slide the selection line between items in `ui/controls/selector_bar.c`. Done when: frames at 10 instants match. Cheaper substitute: jumping.
- [ ] Join segments with their corners, one or several chosen in `ui/controls/selector_bar.c`. Done when: frames equal the OS design page. Cheaper substitute: buttons.
- [ ] Be one tab stop in `ui/controls/selector_bar.c`. Done when: scripts equal real WinUI. Cheaper substitute: each a stop.
- [ ] Hold icons, text and both in `ui/controls/selector_bar.c`. Done when: frames match. Cheaper substitute: text.
- [ ] Handle arrows, Home, End and Space in `ui/controls/selector_bar.c`. Done when: the script `reference/scripts/selector_bar.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/selector_bar.c`: Tab or RadioButton with SelectionItem, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/selector_bar.c`. Done when: `tools/check_control.py selector_bar` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/selector_bar.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §21 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: selector bar and segmented control`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 47. Breadcrumb bar

The path to here.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user goes back up a path.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/breadcrumb.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/breadcrumb.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/breadcrumb.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `BreadcrumbBar` of `controls/dev/Breadcrumb/BreadcrumbBar.xaml` (no states of its own); `BreadcrumbBarItem` of `controls/dev/Breadcrumb/BreadcrumbBar.xaml` (states Inline, EllipsisDropDown, Normal, PointerOver, Pressed, Disabled, Default, DefaultRTL, LastItem, Ellipsis, EllipsisRTL). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/breadcrumb.c`, with its cases in `reference/cases/breadcrumb.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Fold the first items into a menu when narrow in `ui/controls/breadcrumb.c`. Done when: six in a width for three show the ellipsis and the last three. Cheaper substitute: cutting off.
- [ ] Raise the pressed item with its index in `ui/controls/breadcrumb.c`. Done when: events equal real WinUI. Cheaper substitute: the text.
- [ ] Open a menu of siblings at a chevron when the program gives them in `ui/controls/breadcrumb.c`. Done when: choosing one raises it once. Cheaper substitute: none.
- [ ] Turn to an editable path on a press in the empty space when asked in `ui/controls/breadcrumb.c`. Done when: Enter keeps it and Escape returns. Cheaper substitute: no editing.
- [ ] Handle Left, Right, Home, End, Enter and Alt-Down in `ui/controls/breadcrumb.c`. Done when: the script `reference/scripts/breadcrumb.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/breadcrumb.c`: Button with Invoke, in a list, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/breadcrumb.c`. Done when: `tools/check_control.py breadcrumb` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/breadcrumb.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §16 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: breadcrumb bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 48. Settings card and settings expander

The row every settings page is made of. WinUI has none; the Windows Community Toolkit and the OS design page give the look.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the Settings records and metrics of the pinned inventory.
**Job:** the user changes one setting.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/controls/settings_card.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Lay out icon, title, description and a control in `ui/controls/settings_card.c`. Done when: sizes equal the pinned inventory's metrics. Cheaper substitute: a panel.
- [ ] Make the whole card press when it navigates in `ui/controls/settings_card.c`. Done when: Enter, Space and press raise it once. Cheaper substitute: the chevron.
- [ ] Wrap the control below when narrow in `ui/controls/settings_card.c`. Done when: the change comes at the recorded width. Cheaper substitute: squeezing.
- [ ] Nest cards in an expander in `ui/controls/settings_expander.c`. Done when: hidden cards leave the tab order. Cheaper substitute: a list.
- [ ] Join cards in a group with first, middle and last corners in `ui/controls/settings_card.c`. Done when: frames match the inventory. Cheaper substitute: gaps.
- [ ] Take the card's title as the control's name in `ui/controls/settings_card.c`. Done when: a toggle in it reads as its title. Cheaper substitute: on.
- [ ] Add pages in `samples/gallery/settings_card.c` and rows in `tests/state_matrix.json`. Done when: every state passes. Cheaper substitute: light.
- [ ] Answer each item of §18 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: settings card and expander`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 49. Grid view

Tiles that wrap.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user looks through tiles.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/grid_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/grid_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/grid_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `GridView` of `dxaml/xcp/dxaml/themes/generic.xaml` (no states of its own); `GridViewItem` of `controls/dev/CommonStyles/GridViewItem_themeresources.xaml` (no states of its own); `GridViewHeaderItem` of `dxaml/xcp/dxaml/themes/generic.xaml` (no states of its own). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/grid_view.c`, with its cases in `reference/cases/grid_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Wrap tiles by rows or columns with equal or own sizes in `ui/controls/grid_view.c`. Done when: rectangles equal real WinUI's. Cheaper substitute: equal.
- [ ] Keep group headers in view while scrolling in `ui/controls/grid_view.c`. Done when: positions match. Cheaper substitute: scrolling away.
- [ ] Select with boxes on hover and touch, by rectangle drag too in `ui/controls/grid_view.c`. Done when: 100 scripts give equal selections. Cheaper substitute: click.
- [ ] Reorder by drag and carry several out in `ui/controls/grid_view.c`. Done when: the order and data match. Cheaper substitute: one.
- [ ] Load more near the end in `ui/controls/grid_view.c`. Done when: 50,000 tiles keep under 200 live. Cheaper substitute: all.
- [ ] Handle arrows, Page keys, Home, End, Ctrl-A, Space and Enter in `ui/controls/grid_view.c`. Done when: the script `reference/scripts/grid_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/grid_view.c`: List with Selection and Scroll, ListItem, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/grid_view.c`. Done when: `tools/check_control.py grid_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/grid_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §19 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: grid view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 50. List view groups, reorder and columns

The list view of the first controls, completed, with the details form of older programs.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user works with rows.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/list_view_more.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/list_view_more.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/list_view_more.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `ListViewItem` of `controls/dev/CommonStyles/ListViewItem_themeresources.xaml` (28 states in the groups CommonStates 6, DisabledStates 2, MultiSelectStates 2, DataVirtualizationStates 2, ReorderHintStates 5, DragStates 11); `ListViewHeaderItem` of `dxaml/xcp/dxaml/themes/generic.xaml` (no states of its own); `ListView` of `dxaml/xcp/dxaml/themes/generic.xaml` (no states of its own). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/list_view_more.c`, with its cases in `reference/cases/list_view_more.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Group rows with headers that stay in `ui/controls/list_view_more.c`. Done when: positions equal real WinUI. Cheaper substitute: scrolling away.
- [ ] Reorder by drag and carry rows out in `ui/controls/list_view_more.c`. Done when: random drags keep every row once. Cheaper substitute: one.
- [ ] Swap rows in with the swipe form of D05 T02 §5 in `ui/controls/list_view_more.c`. Done when: a script reveals and runs a command. Cheaper substitute: none.
- [ ] Show columns with sort, size, order and choice in `ui/controls/list_view_more.c`. Done when: recorded scripts equal the classic details view. Cheaper substitute: fixed.
- [ ] Edit a cell or name in place in `ui/controls/list_view_more.c`. Done when: Enter keeps and Escape returns. Cheaper substitute: a dialog.
- [ ] Switch details, list, tiles and icons in `ui/controls/list_view_more.c`. Done when: selection stays. Cheaper substitute: a new list.
- [ ] Handle arrows, Ctrl and Shift with them, F2, letters, Enter and Delete in `ui/controls/list_view_more.c`. Done when: the script `reference/scripts/list_view_more.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/list_view_more.c`: List or DataGrid with Selection, Grid and Table, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/list_view_more.c`. Done when: `tools/check_control.py list_view_more` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/list_view_more.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §1 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: list view groups, reorder and columns`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 51. Semantic zoom

From all items to their groups and back.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user jumps to a group.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/semantic_zoom.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/semantic_zoom.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/semantic_zoom.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `SemanticZoom` of `dxaml/xcp/dxaml/themes/generic.xaml` (states ZoomInView, ZoomOutView, ZoomOutButtonVisible, ZoomOutButtonHidden). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/semantic_zoom.c`, with its cases in `reference/cases/semantic_zoom.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Change by pinch, Ctrl and wheel, key and button in `ui/controls/semantic_zoom.c`. Done when: scripts equal real WinUI. Cheaper substitute: a button.
- [ ] Land on the chosen group in `ui/controls/semantic_zoom.c`. Done when: the group's first item stands at the start. Cheaper substitute: the top.
- [ ] Run both motions in `ui/controls/semantic_zoom.c`. Done when: frames at 10 instants match. Cheaper substitute: at once.
- [ ] Handle Ctrl-plus, Ctrl-minus and Escape in `ui/controls/semantic_zoom.c`. Done when: the script `reference/scripts/semantic_zoom.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/semantic_zoom.c`: SemanticZoom with Toggle, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/semantic_zoom.c`. Done when: `tools/check_control.py semantic_zoom` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/semantic_zoom.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §19 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: semantic zoom`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 52. Two-pane view and splitter

Two panes that stand beside, above or alone, and a line to size them.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user works in two places at once.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/two_pane_view.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/two_pane_view.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/two_pane_view.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the template `TwoPaneView` of `controls/dev/TwoPaneView/TwoPaneView.xaml` (states ViewMode_LeftRight, ViewMode_RightLeft, ViewMode_TopBottom, ViewMode_BottomTop, ViewMode_OneOnly, ViewMode_TwoOnly). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/two_pane_view.c`, with its cases in `reference/cases/two_pane_view.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Choose wide, tall or single by size, with a first pane in `ui/controls/two_pane_view.c`. Done when: every state of the template matches. Cheaper substitute: wide.
- [ ] Drag the splitter within limits, by key too, and double click to reset in `ui/controls/two_pane_view.c`. Done when: positions follow a recorded script. Cheaper substitute: drag.
- [ ] Remember nothing by itself, but tell each change in `ui/controls/two_pane_view.c`. Done when: the program reads the size back. Cheaper substitute: a file.
- [ ] Keep focus when a pane goes away in `ui/controls/two_pane_view.c`. Done when: it moves to the pane that stays. Cheaper substitute: lost.
- [ ] Handle arrows on the splitter and F6 in `ui/controls/two_pane_view.c`. Done when: the script `reference/scripts/two_pane_view.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/two_pane_view.c`: Thumb with Transform, Group for each pane, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/two_pane_view.c`. Done when: `tools/check_control.py two_pane_view` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/two_pane_view.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §20 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: two-pane view and splitter`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 53. Date and time pickers

The fields that open a calendar or rolling columns.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** real WinUI at the commit in `tools/winui/pin.json`, drawn by `reference/`; the limits are in `tests/test_ui.py`.
**Job:** the user enters a date or time.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Compile the template and lay it out in `ui/controls/date_time_pickers.c`. Done when: the rectangles equal real WinUI's dump.
2. Add states, behaviour and the peer in `ui/controls/date_time_pickers.c`. Done when: the recorded script and the automation dump are equal.
3. Show it in `samples/gallery/date_time_pickers.c`. Done when: the state matrix row passes.

- [ ] Compile into `ui/template_data.c` the templates `CalendarDatePicker` of `controls/dev/CommonStyles/CalendarDatePicker_themeresources.xaml` (states Normal, PointerOver, Pressed, Disabled, Unfocused, PointerFocused, Focused, Unselected, Selected, TopHeader, LeftHeader); `DatePicker` of `controls/dev/CommonStyles/DatePicker_themeresources.xaml` (states Normal, Disabled, HasDate, HasNoDate); `TimePicker` of `controls/dev/CommonStyles/TimePicker_themeresources.xaml` (states Normal, Disabled, HasTime, HasNoTime); `LoopingSelectorItem` of `controls/dev/CommonStyles/DateTimePickerFlyout_themeresources.xaml` (states Normal, Expanded, Selected, PointerOver, Pressed). Done when: the listing names every state and part, and an unknown property is refused. Cheaper substitute: drawing by hand.
- [ ] Draw every state from the theme data in `ui/controls/date_time_pickers.c`, with its cases in `reference/cases/date_time_pickers.json`. Done when: frames equal real WinUI at 100, 150, 200 and 300 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Build the endless column with wheel, drag, snap and typing in `ui/controls/looping_selector.c`. Done when: positions equal real WinUI. Cheaper substitute: a list.
- [ ] Order day, month, year, hour, minute and half by locale in `ui/controls/date_time_pickers.c`. Done when: 50 locales equal Windows. Cheaper substitute: one.
- [ ] Accept, cancel and clear in `ui/controls/date_time_pickers.c`. Done when: the event log equals real WinUI's. Cheaper substitute: accept.
- [ ] Keep limits, steps and no value in `ui/controls/date_time_pickers.c`. Done when: a table of 40 cases passes. Cheaper substitute: no limits.
- [ ] Open the calendar flyout on the chosen date and close on choice in `ui/controls/date_time_pickers.c`. Done when: scripts equal real WinUI. Cheaper substitute: staying open.
- [ ] Handle arrows, Page keys, Home, End, Enter, Escape and digits in `ui/controls/date_time_pickers.c`. Done when: the script `reference/scripts/date_time_pickers.json` gives the events and focus real WinUI gives. Cheaper substitute: pointer only.
- [ ] Give the automation peer in `ui/controls/date_time_pickers.c`: Group and Button with Invoke and Value, with name, state and inventory id. Done when: the dump equals real WinUI's. Cheaper substitute: a name.
- [ ] Mirror right to left, keep a 40 unit touch target and honour reduced animation in `ui/controls/date_time_pickers.c`. Done when: `tools/check_control.py date_time_pickers` passes every point of the contract. Cheaper substitute: left to right only.
- [ ] Add the page `samples/gallery/date_time_pickers.c` and its row in `tests/state_matrix.json`. Done when: the page shows each state and the row passes. Cheaper substitute: a screenshot.
- [ ] Answer each item of §8 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: date and time pickers`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 54. Status bar

Panes along the bottom. WinUI has none; the OS design page gives the look.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the design page and tokens of the pinned inventory.
**Job:** the user sees the state of the document.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/controls/status_bar.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Lay out panes with fixed, content and filling widths in `ui/controls/status_bar.c`. Done when: widths equal a table of 40 cases. Cheaper substitute: one text.
- [ ] Hold text, icon, progress and a button in `ui/controls/status_bar.c`. Done when: each works by key and pointer. Cheaper substitute: text.
- [ ] Trim and show the whole in a tooltip in `ui/controls/status_bar.c`. Done when: a trimmed pane has one. Cheaper substitute: cutting.
- [ ] Tell changed text politely in `ui/controls/status_bar.c`. Done when: the peer raises one event per change. Cheaper substitute: silence.
- [ ] Show the size grip when the window can be sized, in `ui/controls/status_bar.c`. Done when: dragging it sizes the window. Cheaper substitute: none.
- [ ] Add pages in `samples/gallery/status_bar.c` and rows in `tests/state_matrix.json`. Done when: every state passes. Cheaper substitute: light.
- [ ] Answer each item of §5 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: status bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 55. State matrix

Every control, state, theme and scale compared as rows of one table; each control adds its own row.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `build/state-matrix/` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_state_matrix.py`. Done when: the checks fail on a planted fault.

- [ ] Read rows of control, state, theme, scale and direction in `tests/test_state_matrix.py`. Done when: a missing cell of a listed control fails. Cheaper substitute: a list of frames.
- [ ] Compare frames, rectangles and automation dumps with `tests/reference/`. Done when: a planted one pixel shift fails with its cell named. Cheaper substitute: by eye.
- [ ] Measure text contrast of 4.5 to 1, and 3 to 1 for large text and marks, in `tests/test_state_matrix.py`. Done when: a planted weak colour fails. Cheaper substitute: trusting the theme.
- [ ] Check colour is never the only sign in `tests/test_state_matrix.py`. Done when: a planted colour-only error state fails. Cheaper substitute: review.
- [ ] Write one report of differences as pictures in `build/state-matrix/`. Done when: a failing cell shows ours, real and difference. Cheaper substitute: numbers.
- [ ] Finish within 5 minutes in `tools/build.py` by running cells side by side. Done when: the time is printed and kept to. Cheaper substitute: a night run.
- [ ] Answer each item of §26 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: the state matrix`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- The gallery shows every control, and the state matrix passes on Windows.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
