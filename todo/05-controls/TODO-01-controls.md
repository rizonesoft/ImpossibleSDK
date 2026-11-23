---
schema_version: 1
id: controls
domain: 05-controls
status: active
title: "TODO-01 -- Controls"
---

# TODO-01 -- Controls

> **Goal:** Every control of WinUI that a program or the OS uses, drawn and behaving as the real one, on the OS's engines or on this kit's own.

> **Current state:** Measured 2026-11-23. The button is drawn; `01-interface` builds the first four controls.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: where a feature belongs, and parts.
- `docs/os-roadmap/`: the OS sections whose work moved here.


## Outcome

- Each control meets the authoring contract.
- The OS can import the controls and run them on its own engines.


**Adjacency:** all=not-applicable (parts hold no records, settings or reports; programs own those)

**Adjacency rationale:** A part does one thing; the program decides what is kept.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Inventory and authoring contract | -- | [ ] |
| 2 | §2 | Scrolling and scroll bars | §1 | [ ] |
| 3 | §3 | Pop-ups, flyouts and light dismissal | §1 | [ ] |
| 4 | §4 | Commands and data lists | §1 | [ ] |
| 5 | §5 | Automation peers and inventory ids | §1 | [ ] |
| 6 | §6 | Button family: repeat, split, drop-down, hyperlink and toggle buttons | §1, §5 | [ ] |
| 7 | §7 | Check box, radio buttons and group box | §5 | [ ] |
| 8 | §8 | Slider and progress bar and ring | §5 | [ ] |
| 9 | §9 | Text box family: password, rich edit and spelling marks | §2, §5 | [ ] |
| 10 | §10 | Number box and auto-suggest box | §9 | [ ] |
| 11 | §11 | Combo box | §2, §3 | [ ] |
| 12 | §12 | Menus, menu bar and context menus | §3, §4 | [ ] |
| 13 | §13 | Tool bar, command bar and command bar flyout | §6, §12 | [ ] |
| 14 | §14 | Tooltips and teaching tips | §3 | [ ] |
| 15 | §15 | Content dialogs and message boxes | §3, §6 | [ ] |
| 16 | §16 | Info bar, info badge and person picture | §5 | [ ] |
| 17 | §17 | Tabs, tab view and selector bar | §6 | [ ] |
| 18 | §18 | Navigation view and breadcrumb bar | §2, §12 | [ ] |
| 19 | §19 | Expander and settings cards | §6 | [ ] |
| 20 | §20 | List, grid and items views | §2, §4 | [ ] |
| 21 | §21 | Tree view | §20 | [ ] |
| 22 | §22 | Flip view, pips and semantic zoom | §20 | [ ] |
| 23 | §23 | Split view and two-pane view | §1 | [ ] |
| 24 | §24 | Date, time and calendar pickers | §3, §6 | [ ] |
| 25 | §25 | Colour picker | §8, §10 | [ ] |
| 26 | §26 | Rating control | §5 | [ ] |
| 27 | §27 | Title bar and status bar | §6 | [ ] |
| 28 | §28 | Media transport controls | §6, §8 | [ ] |
| 29 | §29 | Classic look of old controls | §6, §9 | [ ] |
| 30 | §30 | Gallery and the state matrix | §6, §9, §20 | [ ] |

## 1. Inventory and authoring contract

The OS's Windows 11 inventory is the reference beside WinUI's source: its control records, state, metric and motion catalogues, `controls.md` and `tokens.json` are copied at a pinned commit. Every control meets one contract.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `tools/inventory.py`. Done when: the copy is the pinned commit's, file by file
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Copy what the interface needs at the commit in `inventory.json`, and refuse a changed file. Done when: a changed byte is refused. Cheaper substitute: a copy by hand.
- [ ] Write `docs/control-contract.md`: template, states, keyboard, pointer, touch, automation, mirroring, density, themes. Done when: `tools/check_control.py` refuses a control that misses one. Cheaper substitute: a list.
- [ ] Name every inventory record that belongs to a control here, and the ones that have none. Done when: the counts agree with the inventory. Cheaper substitute: the records one knows.
- [ ] Commit: `controls: inventory and contract`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Scrolling and scroll bars

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/scrolling_and_scroll_bars.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: scrolling and scroll bars`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Pop-ups, flyouts and light dismissal

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/pop_ups.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: pop-ups, flyouts and light dismissal`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Commands and data lists

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/commands_and_data_lists.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: commands and data lists`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Automation peers and inventory ids

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/automation_peers_and_inventory_ids.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: automation peers and inventory ids`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Button family: repeat, split, drop-down, hyperlink and toggle buttons

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/button_family.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: button family`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Check box, radio buttons and group box

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/check_box.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: check box, radio buttons and group box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. Slider and progress bar and ring

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/slider_and_progress_bar_and_ring.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: slider and progress bar and ring`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. Text box family: password, rich edit and spelling marks

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/text_box_family.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: text box family`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 10. Number box and auto-suggest box

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/number_box_and_auto_suggest_box.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: number box and auto-suggest box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 11. Combo box

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/combo_box.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: combo box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 12. Menus, menu bar and context menus

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/menus.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: menus, menu bar and context menus`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 13. Tool bar, command bar and command bar flyout

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/tool_bar.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: tool bar, command bar and command bar flyout`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 14. Tooltips and teaching tips

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/tooltips_and_teaching_tips.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: tooltips and teaching tips`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 15. Content dialogs and message boxes

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/content_dialogs_and_message_boxes.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: content dialogs and message boxes`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 16. Info bar, info badge and person picture

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/info_bar.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: info bar, info badge and person picture`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 17. Tabs, tab view and selector bar

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/tabs.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: tabs, tab view and selector bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 18. Navigation view and breadcrumb bar

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/navigation_view_and_breadcrumb_bar.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: navigation view and breadcrumb bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 19. Expander and settings cards

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/expander_and_settings_cards.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: expander and settings cards`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 20. List, grid and items views

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/list.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: list, grid and items views`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 21. Tree view

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/tree_view.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: tree view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 22. Flip view, pips and semantic zoom

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/flip_view.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: flip view, pips and semantic zoom`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 23. Split view and two-pane view

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/split_view_and_two_pane_view.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: split view and two-pane view`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 24. Date, time and calendar pickers

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/date.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: date, time and calendar pickers`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 25. Colour picker

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/colour_picker.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: colour picker`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 26. Rating control

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/rating_control.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: rating control`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 27. Title bar and status bar

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/title_bar_and_status_bar.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: title bar and status bar`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 28. Media transport controls

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/media_transport_controls.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: media transport controls`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 29. Classic look of old controls

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/classic_look_of_old_controls.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: classic look of old controls`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 30. Gallery and the state matrix

As WinUI and the inventory have it, in every state, theme and scale. The OS sections it answers are in `docs/os-roadmap/`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/gallery_and_the_state_matrix.c`. Done when: reference pictures match
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Draw every state from the theme data. Done when: pictures of real WinUI match at 100, 150 and 200 percent in light, dark and contrast. Cheaper substitute: one theme.
- [ ] Behave by keyboard, pointer, touch and pen as the inventory records it. Done when: each recorded script gives the same events and focus. Cheaper substitute: pointer only.
- [ ] Give name, role, value, state, actions and inventory id through the automation peer. Done when: the contract's check passes. Cheaper substitute: a name.
- [ ] Answer its items of `docs/os-roadmap/` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `controls: gallery and the state matrix`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- The gallery shows every control, and the state matrix passes on Windows
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
