---
schema_version: 1
id: windows-services
domain: 07-windows
status: active
title: "TODO-01 -- Windows Services"
---

# TODO-01 -- Windows Services

> **Goal:** The Windows layer gives Impossible UI and programs every service they need from the system, each through one small header and only through functions in `abi/tiers.json`.

> **Current state:** Measured 2026-11-23 at `e06ab3a`. `src/window.c` runs one window with pointer move, down and up and key down. Tiers 2 and 3 name clipboard, dropped files, file watching, starting programs and sound functions, and no code uses them.

## Inputs

- `abi/tiers.json`: each function added here is a deliberate change, and work for Impossible OS.
- `todo/01-interface/TODO-01-first-controls.md` §8: the callback table these services fill.

## Outcome

- Every service works in a program built for its tier and is refused below it.
- Each service has a fake in the tests, so that controls are tested without Windows.

**Adjacency:** all=not-applicable (services hold no records of their own; program settings belong to each program)

**Adjacency rationale:** The settings part keeps what a program gives it; the program decides what that is.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Several windows and pop-up windows | D01 T01 §6 | [ ] |
| 2 | §2 | Clipboard | D01 T01 §8 | [ ] |
| 3 | §3 | System appearance and user settings | D01 T01 §8, D01 T01 §14 | [ ] |
| 4 | §4 | Touch, pen and touchpad | D01 T01 §6 | [ ] |
| 5 | §5 | Input methods | D01 T01 §7 | [ ] |
| 6 | §6 | Dropping into a window | §1 | [ ] |
| 7 | §7 | Dragging out of a window | §2, §6 | [ ] |
| 8 | §8 | Screen reader bridge | D05 T01 §5 | [ ] |
| 9 | §9 | Screen reader text | §8, D05 T01 §9 | [ ] |
| 10 | §10 | Worker threads | D01 T01 §6 | [ ] |
| 11 | §11 | Watching files | §10 | [ ] |
| 12 | §12 | Sound playback | §10 | [ ] |
| 13 | §13 | Sound recording | §12 | [ ] |
| 14 | §14 | Program settings | D03 T01 §5 | [ ] |
| 15 | §15 | Starting programs and opening links | D01 T01 §8 | [ ] |
| 16 | §16 | Pictures on the clipboard and in drags | §2, §6, §7, D02 T01 §3, D02 T01 §5, D06 T01 §10 | [ ] |

## 1. Several windows and pop-up windows

Menus, tooltips and dialogs leave their window, so the layer runs more than one.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/window.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Run any number of windows on one thread in `src/window.c`. Done when: opening and closing 100 windows leaves none behind. Cheaper substitute: one window.
- [ ] Open an owned pop-up without taking focus in `src/popup.c`. Done when: the owner stays drawn active and the pop-up is above it. Cheaper substitute: a child area.
- [ ] Add its functions to tier 1 in `abi/tiers.json` with what the OS must give. Done when: the import check passes and a lower tier is refused. Cheaper substitute: an unchecked call.
- [ ] Place it by monitor work area and scale in `src/popup.c`. Done when: a pop-up at each edge of two monitors with different scales is whole. Cheaper substitute: the first monitor.
- [ ] Close pop-ups when the owner moves, loses focus or closes, in `src/popup.c`. Done when: a recorded script leaves none. Cheaper substitute: a timer.
- [ ] Report a window that could not be made in `include/impossible/window.h`. Done when: a planted failure reaches the program by name. Cheaper substitute: a silent return.
- [ ] Give per-window minimum size, state, icon and title-bar area in `include/impossible/window.h`. Done when: snapping, maximising and restoring keep the content right. Cheaper substitute: a fixed frame.
- [ ] Commit: `windows: several windows and pop-ups`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Clipboard

Text, file lists and private formats, safely. Pictures are §16.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/clipboard.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Read and write Unicode text in `src/clipboard.c`. Done when: text of 0 to 10 million characters round trips with another program. Cheaper substitute: one size.
- [ ] Keep every format another program offers readable by name and size in `src/clipboard.c`. Done when: the list equals the one Windows gives. Cheaper substitute: known formats.
- [ ] Read and write file lists in `src/clipboard.c`. Done when: files copied in Explorer arrive with their paths. Cheaper substitute: text paths.
- [ ] Offer a private format with a plain-text form beside it in `src/clipboard.c`. Done when: a program that knows neither receives the text. Cheaper substitute: one format.
- [ ] Add its functions to tier 2 in `abi/tiers.json` with what the OS must give. Done when: the import check passes and a lower tier is refused. Cheaper substitute: an unchecked call.
- [ ] Wait a bounded time for a busy clipboard in `src/clipboard.c`. Done when: a program holding it open gives a named error in under a second. Cheaper substitute: an endless loop.
- [ ] Refuse sizes and offsets that do not fit in `src/clipboard.c`. Done when: a table of 40 planted bad blocks is refused. Cheaper substitute: trusting the block.
- [ ] Commit: `windows: clipboard`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. System appearance and user settings

Theme, accent, contrast, text size, reduced animation, caret rate, double-click time and scroll lines, read at start and followed while running.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/appearance.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/fixtures/test_host.c`. Done when: the checks fail on a planted fault.

- [ ] Read each value in `src/appearance.c`. Done when: a table of changed settings reads back equal. Cheaper substitute: defaults.
- [ ] Follow changes while running in `src/appearance.c`. Done when: switching theme, accent, contrast and scale in Windows restyles the sample without a restart. Cheaper substitute: at start only.
- [ ] Apply the text size setting in `ui/resources.c`. Done when: 100 to 225 percent match real WinUI's layout. Cheaper substitute: a scale of everything.
- [ ] Add the functions to `abi/tiers.json`. Done when: a tier 0 program that uses one is refused. Cheaper substitute: an unchecked call.
- [ ] Give fixed values to tests in `tests/fixtures/test_host.c`. Done when: no test depends on the machine's settings. Cheaper substitute: the machine.
- [ ] Commit: `windows: appearance and user settings`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Touch, pen and touchpad

Contacts, pressure and precise scrolling reach the engine as one kind of event.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `abi/tiers.json` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Add its functions to tier 1 in `abi/tiers.json` with what the OS must give. Done when: the import check passes and a lower tier is refused. Cheaper substitute: an unchecked call.
- [ ] Receive pointer messages for touch and pen in `src/pointer.c`. Done when: a recorded two-finger script gives the events real WinUI gives. Cheaper substitute: mouse messages.
- [ ] Carry contact id, pressure, tilt and eraser in `include/impossible/window.h`. Done when: a planted pen script reads back equal. Cheaper substitute: position only.
- [ ] Read high-resolution wheel and touchpad distances in `src/pointer.c`. Done when: small steps add up to the same distance. Cheaper substitute: whole lines.
- [ ] Recognise tap, double tap, hold, drag and pinch in `ui/gesture.c`. Done when: recorded scripts give real WinUI's gestures and times. Cheaper substitute: tap only.
- [ ] Run scroll inertia and its bounce in `ui/gesture.c`. Done when: positions at 10 instants agree with real WinUI within 2 units. Cheaper substitute: stopping at once.
- [ ] Cancel a contact cleanly when the system takes it, in `ui/gesture.c`. Done when: no control stays pressed. Cheaper substitute: waiting for up.
- [ ] Commit: `windows: touch, pen and touchpad`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Input methods

Japanese, Chinese, Korean and other text that is composed before it is entered.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/ime.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Receive composition text, its clauses and its cursor in `src/ime.c`. Done when: recorded Japanese, Chinese and Korean sessions give the text the user committed. Cheaper substitute: committed text only.
- [ ] Draw composition underlines inline in `ui/controls/textbox.c`. Done when: frames match real WinUI for each clause kind. Cheaper substitute: a separate box.
- [ ] Place the candidate window at the caret in `src/ime.c`. Done when: it follows scrolling, scaling and moving. Cheaper substitute: a corner.
- [ ] Reconvert a selection and cancel a composition in `src/ime.c`. Done when: the text returns exactly. Cheaper substitute: losing it.
- [ ] Turn it off for password boxes in `src/ime.c`. Done when: no composition starts there. Cheaper substitute: on always.
- [ ] Add the functions to `abi/tiers.json` with what the OS must give. Done when: the import check passes at tier 1. Cheaper substitute: an unchecked call.
- [ ] Commit: `windows: input methods`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Dropping into a window

Files and text dropped from other programs, with the effect shown while dragging.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/drop.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Accept dropped files in `src/drop.c`. Done when: 1,000 files with long names arrive whole. Cheaper substitute: the first file.
- [ ] Add the OLE start, drop target and data functions to tier 2 in `abi/tiers.json` with what the OS must give. Done when: a tier 1 program that uses one is refused. Cheaper substitute: an unchecked call.
- [ ] Accept dropped text through an OLE drop target written in C in `src/drop.c`. Done when: text from another program arrives. Cheaper substitute: files only.
- [ ] Route enter, over, leave and drop to elements in `ui/drop.c`. Done when: the element under the pointer decides the effect. Cheaper substitute: the window decides.
- [ ] Refuse what no element accepts in `ui/drop.c`. Done when: the pointer shows it and nothing changes. Cheaper substitute: ignoring it later.
- [ ] Scroll lists while dragging at their edge in `ui/drop.c`. Done when: a script reaches the last row. Cheaper substitute: no scrolling.
- [ ] Commit: `windows: dropping into a window`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Dragging out of a window

Dragging elements inside a program and out to others.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `ui/drag.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Start a drag after the system distance in `ui/drag.c`. Done when: smaller moves stay clicks. Cheaper substitute: any move.
- [ ] Draw the dragged element and its effect text in `ui/drag.c`. Done when: frames match real WinUI's drag visual. Cheaper substitute: a cursor.
- [ ] Offer files and text through an OLE data object written in C in `src/drag.c`. Done when: Explorer and Notepad receive them. Cheaper substitute: inside only.
- [ ] Render the data when it is asked for in `src/drag.c`. Done when: a file of 1 GB is not read before the drop. Cheaper substitute: reading first.
- [ ] Cancel with Escape and on a lost window in `src/drag.c`. Done when: nothing is moved and no object stays. Cheaper substitute: a half move.
- [ ] Add the OLE drag source functions to `abi/tiers.json` at tier 2 with what the OS must give. Done when: a tier 1 program that uses one is refused. Cheaper substitute: an unchecked call.
- [ ] Commit: `windows: dragging out of a window`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. Screen reader bridge

UI Automation providers, written in C over the engine's own automation peers, so that Narrator and other tools read every control.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/uia.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Return the window's root provider in `src/uia.c`. Done when: Inspect shows the tree of the gallery. Cheaper substitute: a title.
- [ ] Give fragments, rectangles, hit tests and focus in `src/uia_fragment.c`. Done when: the dump equals the peers' tree. Cheaper substitute: a flat list.
- [ ] Give the Invoke, Toggle, Value, RangeValue, ExpandCollapse, Selection, SelectionItem, Scroll, ScrollItem, Grid, GridItem, Table, TableItem, ItemContainer, VirtualizedItem, Drag, DropTarget, Window and Transform patterns in `src/uia_patterns.c`. Done when: a Windows client works a fixture of every pattern, a table cell's row, column and header among them. Cheaper substitute: Invoke only.
- [ ] Raise property, structure, focus and notification events in `src/uia_events.c`. Done when: a listening client receives each exactly once. Cheaper substitute: none.
- [ ] Survive a client that keeps an element after its window closes, in `src/uia.c`. Done when: 1,000 random closings give the documented error and no crash. Cheaper substitute: freeing at once.
- [ ] Cost nothing while no client listens, in `src/uia.c`. Done when: the idle program makes no provider. Cheaper substitute: always.
- [ ] Add the functions to `abi/tiers.json` at tier 1 with what the OS must give. Done when: the import check passes. Cheaper substitute: an unchecked call.
- [ ] Commit: `windows: the screen reader bridge`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. Screen reader text

Reading, moving and selecting in text by screen reader.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/uia_text.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Give the Text and Text2 patterns in `src/uia_text.c`. Done when: a client reads by character, word, line, paragraph and document. Cheaper substitute: whole text.
- [ ] Give attributes (font, size, weight, colour, link, spelling error) in `src/uia_text.c`. Done when: a table of 30 runs reads back equal. Cheaper substitute: none.
- [ ] Give rectangles and selection, both ways, in `src/uia_text.c`. Done when: a client's selection shows and the user's is read. Cheaper substitute: reading only.
- [ ] Tell typed and composed text as it changes in `src/uia_text.c`. Done when: Narrator's event log equals the one for a WinUI text box. Cheaper substitute: a whole-text event.
- [ ] Hide password text in `src/uia_text.c`. Done when: no pattern returns it. Cheaper substitute: masking on screen only.
- [ ] Commit: `windows: screen reader text`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 10. Worker threads

Work off the window thread, with results posted back and a clean stop.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `abi/tiers.json` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_worker.py`. Done when: the checks fail on a planted fault.

- [ ] Add its functions to tier 1 in `abi/tiers.json` with what the OS must give. Done when: the import check passes and a lower tier is refused. Cheaper substitute: an unchecked call.
- [ ] Start, stop and wait for workers in `src/worker.c`. Done when: 1,000 starts and stops leave no thread. Cheaper substitute: a thread per call.
- [ ] Post a result to the window's thread in order in `src/worker.c`. Done when: 100,000 posts from eight threads arrive once each. Cheaper substitute: a shared variable.
- [ ] Cancel at any moment in `src/worker.c`. Done when: random cancels leave no lock, memory or thread. Cheaper substitute: waiting for the end.
- [ ] Drop results for a closed window in `src/worker.c`. Done when: closing during work never calls a freed window. Cheaper substitute: a flag.
- [ ] Test under a thread checker in `tests/test_worker.py`. Done when: a native build with thread checking finds nothing. Cheaper substitute: one run.
- [ ] Commit: `windows: worker threads`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 11. Watching files

Know when a file or folder changes.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `abi/tiers.json` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Add its functions to tier 2 in `abi/tiers.json` with what the OS must give. Done when: the import check passes and a lower tier is refused. Cheaper substitute: an unchecked call.
- [ ] Watch a folder with and without its children in `src/watch.c`. Done when: 10,000 random changes are all told or an overflow is told. Cheaper substitute: a timer.
- [ ] Join rapid changes in `src/watch.c`. Done when: one save by each of five editors gives one change. Cheaper substitute: every event.
- [ ] Recover when the folder goes away and returns in `src/watch.c`. Done when: removing a drive gives a named state. Cheaper substitute: silence.
- [ ] Stop at once in `src/watch.c`. Done when: closing leaves no handle. Cheaper substitute: at exit.
- [ ] Commit: `windows: watching files`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 12. Sound playback

Sound out through tier 3 with steady buffers.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `abi/tiers.json` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Add its functions to tier 3 in `abi/tiers.json` with what the OS must give. Done when: the import check passes and a lower tier is refused. Cheaper substitute: an unchecked call.
- [ ] Open a device and format, or name why not, in `src/sound_out.c`. Done when: no device, a busy one and a bad format are told apart. Cheaper substitute: one error.
- [ ] Feed buffers from a callback in `src/sound_out.c`. Done when: a 10 minute sine recorded through a loop has no gap. Cheaper substitute: blocking writes.
- [ ] Pause, resume, stop and seek in `src/sound_out.c`. Done when: positions stay within 10 ms of the clock. Cheaper substitute: restarting.
- [ ] Mix several sources with volume in fixed point in `parts/mix/mix.c`. Done when: the sum equals exact arithmetic with saturation. Cheaper substitute: one source.
- [ ] Change rate in `parts/mix/resample.c`. Done when: noise stays under minus 90 dB for a sweep. Cheaper substitute: nearest sample.
- [ ] Follow a device that is removed in `src/sound_out.c`. Done when: playing goes on with the next one or stops with a name. Cheaper substitute: a hang.
- [ ] Commit: `windows: sound playback`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 13. Sound recording

Sound in through tier 3.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/sound_in.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Open a device and format, or name why not, in `src/sound_in.c`. Done when: no device and a refused permission are told apart. Cheaper substitute: one error.
- [ ] Deliver buffers in order in `src/sound_in.c`. Done when: 10 minutes have no gap or repeat. Cheaper substitute: best effort.
- [ ] Give levels while recording in `parts/mix/level.c`. Done when: a known tone reads its exact peak. Cheaper substitute: a moving average.
- [ ] Stop without losing the last buffer in `src/sound_in.c`. Done when: the recorded length equals the time within one buffer. Cheaper substitute: dropping it.
- [ ] Commit: `windows: sound recording`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 14. Program settings

Where each program keeps its settings, with one safe way to write them.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/settings.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Find the program's own folder in `src/settings.c`. Done when: two programs never share one. Cheaper substitute: beside the program.
- [ ] Read and write typed values with defaults in `src/settings.c`. Done when: every type and an unknown key survive a round trip. Cheaper substitute: text only.
- [ ] Replace the file safely in `src/settings.c`. Done when: killing the program at any moment leaves the old or the new file. Cheaper substitute: writing in place.
- [ ] Set a damaged file aside and start in `src/settings.c`. Done when: every cut-off prefix starts cleanly and the old file is kept. Cheaper substitute: deleting it.
- [ ] Move settings between versions in `src/settings.c`. Done when: a file of each older version loads. Cheaper substitute: starting over.
- [ ] Let two running copies agree in `src/settings.c`. Done when: both copies' changes to different keys remain. Cheaper substitute: the last writer wins.
- [ ] Commit: `windows: program settings`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 15. Starting programs and opening links

Open a file, a folder or a link with the user's own program, and start a program with arguments.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/launch.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Open `https`, `mailto` and files by the user's choice in `src/launch.c`. Done when: each opens and a refused scheme is named. Cheaper substitute: any text.
- [ ] Refuse schemes outside the program's list in `src/launch.c`. Done when: a table of 40 hostile links starts nothing. Cheaper substitute: passing it on.
- [ ] Start a program with arguments quoted exactly in `src/launch.c`. Done when: 10,000 random argument lists arrive equal. Cheaper substitute: joining with spaces.
- [ ] Add its functions to tier 2 in `abi/tiers.json` with what the OS must give. Done when: the import check passes and a lower tier is refused. Cheaper substitute: an unchecked call.
- [ ] Read its output and end, with a time limit, in `src/launch.c`. Done when: a program that never ends is stopped with its children. Cheaper substitute: waiting.
- [ ] Give the opened file to a running copy of the program in `src/instance.c`. Done when: opening a second file shows it in the first window. Cheaper substitute: a second copy always.
- [ ] Commit: `windows: starting programs and links`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 16. Pictures on the clipboard and in drags

Pictures between programs, through the SDK's own readers and writers.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/clipboard_picture.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Write PNG and bitmap forms in `src/clipboard_picture.c`. Done when: Paint and a browser receive the pixels, transparency where they keep it. Cheaper substitute: a bitmap.
- [ ] Read PNG and every bitmap form Windows gives in `src/clipboard_picture.c`. Done when: 40 recorded blocks give the right pixels. Cheaper substitute: one.
- [ ] Refuse a picture beyond the caller's limit or with wrong sizes in `src/clipboard_picture.c`. Done when: planted blocks are refused before any allocation. Cheaper substitute: trusting the header.
- [ ] Take and give pictures in drags in `src/drag_picture.c`. Done when: another program receives one and gives one. Cheaper substitute: the clipboard only.
- [ ] Make the bytes only when they are asked for in `src/clipboard_picture.c`. Done when: copying a 100 megapixel picture returns at once. Cheaper substitute: at copying.
- [ ] Commit: `windows: pictures on the clipboard and in drags`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- Each service works from `samples/services/` on Windows, and the same tests pass on the fakes.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
