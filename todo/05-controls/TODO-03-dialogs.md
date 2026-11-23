---
schema_version: 1
id: dialogs
domain: 05-controls
status: active
title: "TODO-03 -- Dialogs"
---

# TODO-03 -- Dialogs

> **Goal:** One set of common dialogs in one frame, drawn by Impossible UI, the same on Windows and on Impossible OS.

> **Current state:** Measured 2026-11-23 at `e06ab3a`. No dialog exists. The OS's `TODO-06 §8` moved here.

## Inputs

- `todo/05-controls/TODO-01-controls.md` §15: the frame.
- `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` §8.
- `ui/host.h`: files, folders and places come from the host, so the dialogs stay portable.

## Outcome

- A program asks a question, opens, saves and chooses in one call each.
- The tests run every dialog on a made-up file system.

**Adjacency:** all=not-applicable (a dialog returns an answer and keeps no record)

**Adjacency rationale:** Recent places and last folders are kept by the program through its settings.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Message box | T01 §15 | [ ] |
| 2 | §2 | Text input dialog | T01 §15, T01 §9, T01 §37 | [ ] |
| 3 | §3 | Files and places model | T01 §4, D01 T01 §8, D07 T01 §11 | [ ] |
| 4 | §4 | Open file dialog | §3, T01 §47, T01 §50, T01 §21 | [ ] |
| 5 | §5 | Save file dialog | §4, §1 | [ ] |
| 6 | §6 | Folder dialog | §4 | [ ] |
| 7 | §7 | Colour dialog | T01 §15, T01 §25 | [ ] |
| 8 | §8 | Font dialog | T01 §15, T01 §11, D01 T02 §1, D01 T02 §8 | [ ] |
| 9 | §9 | Property sheet and wizard | T01 §15, T01 §17, T01 §29 | [ ] |

## 1. Message box

A question or message with icon and up to three buttons.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the dialog records of the pinned inventory and the Windows dialogs for behaviour; the content dialog for the look.
**Job:** the user answers a question.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** the content dialog frame, or its own window when there is no owner.

**Build order:**

1. Write `ui/dialogs/message.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Show the seven button sets of Windows with their default and cancel in `ui/dialogs/message.c`. Done when: each returns its own answer. Cheaper substitute: OK.
- [ ] Show information, question, warning and error with icon and text in `ui/dialogs/message.c`. Done when: frames match the design page. Cheaper substitute: colour.
- [ ] Wrap and scroll long text, and let it be copied in `ui/dialogs/message.c`. Done when: 10,000 characters can be read. Cheaper substitute: cutting.
- [ ] Keep to the owner, or stand alone without one in `ui/dialogs/message.c`. Done when: both are modal to the right window. Cheaper substitute: always alone.
- [ ] Refuse Escape where no button cancels, as Windows does in `ui/dialogs/message.c`. Done when: the script agrees. Cheaper substitute: closing.
- [ ] Work by keyboard alone, by touch alone and through the automation peers alone in `ui/dialogs/message.c`. Done when: three scripts finish the task; the Windows client of D05 T02 §22 repeats the last. Cheaper substitute: pointer.
- [ ] Add the page `samples/gallery/message.c` and its rows in `tests/state_matrix.json`. Done when: light, dark, contrast, right to left and 100 to 300 percent pass. Cheaper substitute: light.
- [ ] Answer each item of §8 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `dialogs: message box`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Text input dialog

One question with a text answer.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the dialog records of the pinned inventory and the Windows dialogs for behaviour; the content dialog for the look.
**Job:** the user gives a name.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** the content dialog frame, or its own window when there is no owner.

**Build order:**

1. Write `ui/dialogs/input.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Show label, text and the default answer selected in `ui/dialogs/input.c`. Done when: typing replaces it. Cheaper substitute: an empty box.
- [ ] Check while typing and say why it cannot be accepted in `ui/dialogs/input.c`. Done when: OK follows the check. Cheaper substitute: at OK.
- [ ] Take a hidden answer too in `ui/dialogs/input.c`. Done when: copy is refused, the peer gives no value and the bytes are seen zero after closing, all through the dialog. Cheaper substitute: plain.
- [ ] Return cancel apart from an empty answer in `ui/dialogs/input.c`. Done when: the two are told apart. Cheaper substitute: empty.
- [ ] Work by keyboard alone, by touch alone and through the automation peers alone in `ui/dialogs/input.c`. Done when: three scripts finish the task; the Windows client of D05 T02 §22 repeats the last. Cheaper substitute: pointer.
- [ ] Add the page `samples/gallery/input.c` and its rows in `tests/state_matrix.json`. Done when: light, dark, contrast, right to left and 100 to 300 percent pass. Cheaper substitute: light.
- [ ] Answer each item of §8 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `dialogs: text input dialog`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Files and places model

What the file dialogs show, from the host, without drawing.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the dialog records of the pinned inventory and the Windows dialogs for behaviour; the content dialog for the look.
**Job:** the user finds a file.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** the content dialog frame, or its own window when there is no owner.

**Build order:**

1. Write `ui/dialogs/files.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] List folders, files and places through callbacks in `ui/dialogs/files.c`. Done when: a made-up tree of 100,000 entries lists in pieces. Cheaper substitute: Windows calls.
- [ ] Sort by name as Explorer does, by date, size and type in `ui/dialogs/files.c`. Done when: 1,000 names agree with Explorer. Cheaper substitute: bytes.
- [ ] Filter by type patterns and hidden state in `ui/dialogs/files.c`. Done when: a table of 60 passes. Cheaper substitute: extensions.
- [ ] Follow changes while open in `ui/dialogs/files.c`. Done when: each made, removed and renamed entry shows. Cheaper substitute: at opening.
- [ ] Say why a folder cannot be read in `ui/dialogs/files.c`. Done when: refused, gone and slow are told apart. Cheaper substitute: empty.
- [ ] Parse what the user types: names, paths, wildcards, several names in quotes in `ui/dialogs/files.c`. Done when: a table of 80 equals Windows' dialog. Cheaper substitute: a name.
- [ ] Refuse reserved and too long names by name in `ui/dialogs/files.c`. Done when: a table of 40 passes. Cheaper substitute: at saving.
- [ ] Fill the callbacks for Windows in `src/files.c`. Done when: places, drives and libraries equal Explorer's. Cheaper substitute: drives.
- [ ] Work by keyboard alone, by touch alone and through the automation peers alone in `ui/dialogs/files.c`. Done when: three scripts finish the task; the Windows client of D05 T02 §22 repeats the last. Cheaper substitute: pointer.
- [ ] Add the page `samples/gallery/files.c` and its rows in `tests/state_matrix.json`. Done when: light, dark, contrast, right to left and 100 to 300 percent pass. Cheaper substitute: light.
- [ ] Commit: `dialogs: files and places model`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Open file dialog

Choosing one or more files.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the dialog records of the pinned inventory and the Windows dialogs for behaviour; the content dialog for the look.
**Job:** the user opens a file.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** the content dialog frame, or its own window when there is no owner.

**Build order:**

1. Write `ui/dialogs/open.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Lay out places, path, search, list, name, type and buttons in `ui/dialogs/open.c`. Done when: sizes equal the inventory's open dialog. Cheaper substitute: a list.
- [ ] Move by path, places, back, forward, up and typing in `ui/dialogs/open.c`. Done when: a script visits 20 folders. Cheaper substitute: click.
- [ ] Choose several, and all in `ui/dialogs/open.c`. Done when: the returned list keeps order. Cheaper substitute: one.
- [ ] Switch details, list and icons, and sort in `ui/dialogs/open.c`. Done when: the choice returns at the next opening when the program keeps it. Cheaper substitute: details.
- [ ] Search in the folder while listing goes on in `ui/dialogs/open.c`. Done when: the window draws all the time. Cheaper substitute: waiting.
- [ ] Show a preview the program draws in `ui/dialogs/open.c`. Done when: a picture shows. Cheaper substitute: none.
- [ ] Refuse a missing file or wrong type with a message in `ui/dialogs/open.c`. Done when: nothing is returned. Cheaper substitute: returning it.
- [ ] Work by keyboard alone, by touch alone and through the automation peers alone in `ui/dialogs/open.c`. Done when: three scripts finish the task; the Windows client of D05 T02 §22 repeats the last. Cheaper substitute: pointer.
- [ ] Add the page `samples/gallery/open.c` and its rows in `tests/state_matrix.json`. Done when: light, dark, contrast, right to left and 100 to 300 percent pass. Cheaper substitute: light.
- [ ] Answer each item of §8 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `dialogs: open file dialog`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Save file dialog

Choosing where a file goes.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the dialog records of the pinned inventory and the Windows dialogs for behaviour; the content dialog for the look.
**Job:** the user saves a file.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** the content dialog frame, or its own window when there is no owner.

**Build order:**

1. Write `ui/dialogs/save.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Give the default name and add the type's ending as Windows does in `ui/dialogs/save.c`. Done when: a table of 40 names equals Windows. Cheaper substitute: always.
- [ ] Ask before replacing, with the safe answer as the default in `ui/dialogs/save.c`. Done when: Enter alone never replaces. Cheaper substitute: replacing.
- [ ] Change the ending with the type in `ui/dialogs/save.c`. Done when: names follow. Cheaper substitute: fixed.
- [ ] Make a folder and name it in `ui/dialogs/save.c`. Done when: it is made and entered. Cheaper substitute: Explorer.
- [ ] Refuse a place that cannot be written, with the reason in `ui/dialogs/save.c`. Done when: nothing is returned. Cheaper substitute: at saving.
- [ ] Work by keyboard alone, by touch alone and through the automation peers alone in `ui/dialogs/save.c`. Done when: three scripts finish the task; the Windows client of D05 T02 §22 repeats the last. Cheaper substitute: pointer.
- [ ] Add the page `samples/gallery/save.c` and its rows in `tests/state_matrix.json`. Done when: light, dark, contrast, right to left and 100 to 300 percent pass. Cheaper substitute: light.
- [ ] Answer each item of §8 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `dialogs: save file dialog`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Folder dialog

Choosing a folder.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the dialog records of the pinned inventory and the Windows dialogs for behaviour; the content dialog for the look.
**Job:** the user chooses a folder.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** the content dialog frame, or its own window when there is no owner.

**Build order:**

1. Write `ui/dialogs/folder.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Show folders only, with a tree and places in `ui/dialogs/folder.c`. Done when: no file shows. Cheaper substitute: all.
- [ ] Make, name and rename a folder in `ui/dialogs/folder.c`. Done when: each is done on the made-up and real file system. Cheaper substitute: none.
- [ ] Choose several when asked in `ui/dialogs/folder.c`. Done when: the list is returned. Cheaper substitute: one.
- [ ] Return the folder shown when nothing is selected in `ui/dialogs/folder.c`. Done when: the script agrees with Windows. Cheaper substitute: nothing.
- [ ] Work by keyboard alone, by touch alone and through the automation peers alone in `ui/dialogs/folder.c`. Done when: three scripts finish the task; the Windows client of D05 T02 §22 repeats the last. Cheaper substitute: pointer.
- [ ] Add the page `samples/gallery/folder.c` and its rows in `tests/state_matrix.json`. Done when: light, dark, contrast, right to left and 100 to 300 percent pass. Cheaper substitute: light.
- [ ] Commit: `dialogs: folder dialog`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Colour dialog

The colour picker in the frame.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the dialog records of the pinned inventory and the Windows dialogs for behaviour; the content dialog for the look.
**Job:** the user chooses a colour.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** the content dialog frame, or its own window when there is no owner.

**Build order:**

1. Write `ui/dialogs/color.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Show the picker, the program's palette and own colours in `ui/dialogs/color.c`. Done when: a chosen colour returns exactly. Cheaper substitute: the picker.
- [ ] Keep own colours through the program in `ui/dialogs/color.c`. Done when: they are there next time when it gives them. Cheaper substitute: lost.
- [ ] Tell each change for a live preview, and return the old on cancel in `ui/dialogs/color.c`. Done when: the program's view returns. Cheaper substitute: at OK.
- [ ] Work by keyboard alone, by touch alone and through the automation peers alone in `ui/dialogs/color.c`. Done when: three scripts finish the task; the Windows client of D05 T02 §22 repeats the last. Cheaper substitute: pointer.
- [ ] Add the page `samples/gallery/color.c` and its rows in `tests/state_matrix.json`. Done when: light, dark, contrast, right to left and 100 to 300 percent pass. Cheaper substitute: light.
- [ ] Answer each item of §8 of `docs/os-roadmap/08-graphics-ui-TODO-06-widget-dialogs.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `dialogs: colour dialog`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. Font dialog

Choosing a font, style and size.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the dialog records of the pinned inventory and the Windows dialogs for behaviour; the content dialog for the look.
**Job:** the user chooses a font.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** the content dialog frame, or its own window when there is no owner.

**Build order:**

1. Write `ui/dialogs/font.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] List families in their own face, few live in `ui/dialogs/font.c`. Done when: 1,000 fonts scroll within the frame budget. Cheaper substitute: names.
- [ ] Show only fixed-width, or only with a script, when asked in `ui/dialogs/font.c`. Done when: the lists equal Windows'. Cheaper substitute: all.
- [ ] Show the sample with the chosen style, size, colour and lines in `ui/dialogs/font.c`. Done when: frames match the text engine. Cheaper substitute: a fixed text.
- [ ] Return a font that is missing here by name in `ui/dialogs/font.c`. Done when: it is kept and marked. Cheaper substitute: replaced.
- [ ] Work by keyboard alone, by touch alone and through the automation peers alone in `ui/dialogs/font.c`. Done when: three scripts finish the task; the Windows client of D05 T02 §22 repeats the last. Cheaper substitute: pointer.
- [ ] Add the page `samples/gallery/font.c` and its rows in `tests/state_matrix.json`. Done when: light, dark, contrast, right to left and 100 to 300 percent pass. Cheaper substitute: light.
- [ ] Commit: `dialogs: font dialog`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. Property sheet and wizard

Pages with OK, Cancel and Apply, and steps with Back and Next.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the dialog records of the pinned inventory and the Windows dialogs for behaviour; the content dialog for the look.
**Job:** the user changes properties or follows steps.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** the content dialog frame, or its own window when there is no owner.

**Build order:**

1. Write `ui/dialogs/property_sheet.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/state_matrix.json`. Done when: the checks fail on a planted fault.

- [ ] Show pages by tabs, making each when first seen in `ui/dialogs/property_sheet.c`. Done when: 20 pages make one at the start. Cheaper substitute: all.
- [ ] Follow changes, and let a page refuse OK with its reason in `ui/dialogs/property_sheet.c`. Done when: the wrong page is shown. Cheaper substitute: closing.
- [ ] Apply without closing, and cancel back to the last apply in `ui/dialogs/property_sheet.c`. Done when: a model shows each. Cheaper substitute: at OK.
- [ ] Step forward and back by rules, and skip in `ui/dialogs/property_sheet.c`. Done when: a script follows three ways. Cheaper substitute: in order.
- [ ] Refuse leaving a step, and confirm cancelling in `ui/dialogs/property_sheet.c`. Done when: nothing half done stays. Cheaper substitute: closing.
- [ ] Move by Ctrl-Tab and Ctrl-Page keys in `ui/dialogs/property_sheet.c`. Done when: the script agrees with Windows. Cheaper substitute: Tab.
- [ ] Work by keyboard alone, by touch alone and through the automation peers alone in `ui/dialogs/property_sheet.c`. Done when: three scripts finish the task; the Windows client of D05 T02 §22 repeats the last. Cheaper substitute: pointer.
- [ ] Add the page `samples/gallery/property_sheet.c` and its rows in `tests/state_matrix.json`. Done when: light, dark, contrast, right to left and 100 to 300 percent pass. Cheaper substitute: light.
- [ ] Answer each item of §24 of `docs/os-roadmap/08-graphics-ui-TODO-19-fluent-component-library.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `dialogs: property sheet and wizard`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- Every dialog works in the gallery on the made-up file system and on Windows.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
