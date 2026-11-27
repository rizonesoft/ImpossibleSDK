---
schema_version: 1
id: outside-toolkits
domain: 09-platform
status: active
title: "TODO-05 -- Interfaces for Outside Toolkits"
---

# TODO-05 -- Interfaces for Outside Toolkits

> **Goal:** A C++ toolkit that is not Impossible UI (Vellum, `isotone::ui`, Office's) can sit on the SDK: it calls `draw` and `text` through a stable interface, takes text from an input method, gives a screen reader its own element tree, prints, and learns how from a guide.

> **Current state:** Measured 2026-11-27 at `8d5bf9a`. Drawing and text are planned as separate parts in `D08 T01` §1 and built in `D01 T01` §5, §10, §13 and `D01 T02` §1 to §14, but each is written for Impossible UI's controls and has no declared stable interface. Input methods (`D07 T01` §5) draw composition inside Impossible UI's text box, and the screen reader bridge (`D07 T01` §8, §9) reads Impossible UI's automation peers. Printing (`D07 T02` §3) sends pixel bands and needs no Impossible UI. `docs/guide/` has no C++ page and no toolkit page.

## Inputs

- `docs/specs/2026-11-27-sdk-as-platform-design.md`: "What does the SDK add?" items 3, 5 and 6.
- `todo/01-interface/TODO-01-first-controls.md` §5, §10, §13 and `todo/01-interface/TODO-02-text.md` §1 to §14: the drawing and text engines; the font dialog is `D05 T03` §8 and is not repeated here.
- -> XREF: D08 T01 §1 -- publishing `draw` and `text` as separately importable parts is that section's work; this file adds their stable external interface
- -> XREF: D07 T01 §5 -- input methods; this file adds the interface an outside toolkit uses
- -> XREF: D07 T01 §8 -- the screen reader bridge; this file adds the provider an outside toolkit fills
- -> XREF: D07 T02 §3 -- print jobs and pages send bands of pixels; §4 here proves an outside toolkit prints through them
- -> XREF: D01 T01 §13 -- paths, arcs and gradients
- -> XREF: D01 T02 §14 -- the text engine's last section, after shaping and fallback
- -> XREF: D09 T01 §9 -- the C++ proof sample the guide shows
- -> XREF: D09 T03 §9 -- the GPU proof sample the guide shows

## Outcome

- A toolkit outside the SDK draws a scene, lays out and draws text, takes composed text and exposes an element tree to Narrator using only headers in `include/impossible/`.
- `docs/guide/cxx.md` states the supported C++ subset and `docs/guide/own-toolkit.md` shows how a toolkit sits on the SDK; both build through the site build.

**Adjacency:** all=not-applicable (interfaces and guides hold no records, settings, reports or notices)

**Adjacency rationale:** Each toolkit owns its documents and settings; the SDK only draws, reads input and prints.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Stable draw and text interface | D08 T01 §1, D01 T02 §14, D09 T02 §11 | [ ] |
| 2 | §2 | Input method interface for toolkits | D07 T01 §5, §1 | [ ] |
| 3 | §3 | Screen reader tree for an outside toolkit | D07 T01 §9, §1 | [ ] |
| 4 | §4 | Printing from an outside toolkit | D07 T02 §3, §1 | [ ] |
| 5 | §5 | The supported C++ subset page | D09 T01 §9 | [ ] |
| 6 | §6 | Guide: Bring your own toolkit | §1, §2, §3, §4, §5, D09 T03 §12 | [ ] |

## 1. Stable draw and text interface

`draw` and `text` become public parts with a stable interface that a toolkit outside the SDK can call, at the quality Direct2D and DirectWrite gave these apps. Their engines belong to the sections named in the XREF lines; this section fixes and proves the interface and adds nothing to the engines.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. List the functions a toolkit calls, from Recto's and Isotone's use. Done when: `docs/draw-text-interface.md` names each with a source.
2. Freeze them in public headers with versioning. Done when: a header check fails on a breaking change.

- [ ] List the draw functions a toolkit needs (paths, arcs, gradients, clips, layers, shadows, pictures, transforms) in `docs/draw-text-interface.md`, each tied to the owning section in `D01 T01`. Done when: every function names an owner and an engine item that already exists, and a missing one is filed as a section of that owner. Cheaper substitute: exposing everything.
- [ ] List the text functions (fonts, fallback, shaping, layout, caret, hit testing, selection, colour glyphs) in `docs/draw-text-interface.md`, each tied to its `D01 T02` section. Done when: every function names its owner and its fixed-point types. Cheaper substitute: exposing the control-facing calls.
- [ ] Publish the interface as `include/impossible/draw.h` and `include/impossible/text.h` with `IM_MEMORY` and `IM_CONTEXT` markers and a version number. Done when: `tools/header_check.py` passes and the site's API reference lists every function with a comment. Cheaper substitute: `ui/draw.h` as it is.
- [ ] Add a version test in `tests/test_parts.py` that compares the headers with a committed interface snapshot. Done when: removing or changing a function fails the test unless the version is bumped. Cheaper substitute: review.
- [ ] Write a toolkit-shaped client in `tests/fixtures/toolkit_client.cpp` that uses only those headers and no `ui/` or Windows header. Done when: it draws a path, a gradient and a shaped paragraph and its pixels equal Impossible UI's for the same scene. Cheaper substitute: a client that includes `ui/`.
- [ ] Prove the client builds as an external project from the part copy of `tools/copy_part.py`. Done when: the copied `draw` and `text` with their needs build and the client passes alone. Cheaper substitute: building in the SDK tree.
- [ ] Commit: `parts: a stable draw and text interface`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the client test fails when a function of `include/impossible/draw.h` is removed without a version bump.

## 2. Input method interface for toolkits

An outside toolkit has its own text box. It needs composition text, clauses, cursor, candidate-window placement and reconversion without Impossible UI's text box.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `include/impossible/ime.h`. Done when: it has no Windows type.
2. Connect it to `src/ime.c`. Done when: a client receives composition events.

- [ ] Declare composition start, update, commit and cancel events, with clauses and cursor, and a caret-rectangle callback in `include/impossible/ime.h`. Done when: `tools/header_check.py` passes and the header compiles as C++23. Cheaper substitute: committed text only.
- [ ] Make `src/ime.c` (`D07 T01 §5`) deliver them to a window's registered client. Done when: the same recorded Japanese, Chinese and Korean sessions give the same committed text with Impossible UI's text box and with the client. Cheaper substitute: a second implementation.
- [ ] Place the candidate window at the rectangle the client reports in `src/ime.c`. Done when: it follows scrolling, scaling and moving for the client. Cheaper substitute: a corner.
- [ ] Reconvert and cancel through the interface in `src/ime.c`. Done when: the text returns exactly for the client. Cheaper substitute: losing it.
- [ ] Turn composition off for a client's secret field in `src/ime.c`. Done when: no composition starts there. Cheaper substitute: on always.
- [ ] Test the client in `tests/fixtures/toolkit_client.cpp` with recorded sessions. Done when: the committed text equals the expected string in each session and the test fails when `src/ime.c` drops a clause. Cheaper substitute: committed text only.
- [ ] Commit: `windows: input methods for outside toolkits`

**Test checkpoint:** `python3 tests/test_live.py` exits 0 on a Windows host and the IME case fails when `src/ime.c` does not call the client's callback.

## 3. Screen reader tree for an outside toolkit

A toolkit with its own element tree needs Narrator to read it. This section lets the toolkit supply provider nodes where `D07 T01 §8` reads Impossible UI's peers.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `include/impossible/access.h`. Done when: it has no Windows type.
2. Read a client's tree in `src/uia.c`. Done when: Inspect shows a client's nodes.

- [ ] Declare a node (id, parent, role, name, rectangle, state, patterns, children) and events in `include/impossible/access.h`. Done when: `tools/header_check.py` passes and the header compiles as C++23. Cheaper substitute: a flat list.
- [ ] Feed the same provider code of `src/uia.c` and `src/uia_fragment.c` from either source, peers or a client's tree. Done when: the dump of the client's tree equals the dump of an Impossible UI tree for the same structure. Cheaper substitute: a second provider.
- [ ] Support the text pattern for a client's text node in `src/uia_text.c`. Done when: reading, moving and selecting in it equals `D07 T01 §9` for Impossible UI. Cheaper substitute: names only.
- [ ] Raise structure, property and focus events on request in `src/uia_events.c`. Done when: a listening client receives each exactly once. Cheaper substitute: none.
- [ ] Survive a toolkit that frees a node a client still holds, in `src/uia.c`. Done when: 1,000 random frees give the documented error and no crash. Cheaper substitute: keeping the node alive.
- [ ] Test it in `tests/fixtures/toolkit_client.cpp` with a UI Automation client. Done when: Inspect or a script reads the tree and the test fails when the node's name is dropped. Cheaper substitute: reading the header.
- [ ] Commit: `windows: a screen reader tree for outside toolkits`

**Test checkpoint:** `python3 tests/test_live.py` exits 0 on a Windows host and the access case fails when `src/uia.c` stops reading the client's tree.

## 4. Printing from an outside toolkit

The print job (`D07 T02 §3`) takes bands of pixels, so an outside toolkit prints through it without Impossible UI; this section proves it and holds back the parts that need Impossible UI.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `tests/fixtures/toolkit_print.cpp`. Done when: it prints a page through the job functions and `draw`.
2. Compare it with the PDF. Done when: the page equals what the toolkit drew.

- [ ] Print a page drawn through `include/impossible/draw.h` with the job functions in `tests/fixtures/toolkit_print.cpp`. Done when: the PDF opens and its page equals the drawn pixels within the stated tolerance. Cheaper substitute: an Impossible UI page.
- [ ] Check that `src/print.c` and `include/impossible/print.h` include no `ui/` header in `tools/check_parts.py`. Done when: a planted include fails. Cheaper substitute: review.
- [ ] State in `docs/draw-text-interface.md` that layout, preview and the print dialog of `D07 T02 §4`, §6 and §7 are Impossible UI's and an outside toolkit supplies its own. Done when: the page lists them with their section addresses. Cheaper substitute: silence.
- [ ] Commit: `tests: an outside toolkit prints`

**Test checkpoint:** `python3 tests/test_print.py` exits 0 and the toolkit case fails when `src/print.c` includes a `ui/` header.

## 5. The supported C++ subset page

The reference that Impossible OS links to instead of writing its own.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `docs/guide/cxx.md`. Done when: the site build compiles each example.
2. Check it against the proof sample. Done when: every claim has a test.

- [ ] List what works (C++23 language, exceptions, RTTI, `thread_local`, static constructors, `atexit`, the libc++ parts) in `docs/guide/cxx.md`, each with the test or sample that proves it. Done when: each line cites a file under `tests/` or `samples/cxx/`. Cheaper substitute: prose.
- [ ] List what does not work (a Microsoft runtime DLL, a TLS directory, a prebuilt library, filesystem and locale beyond the C locale) with the diagnostic each gives. Done when: each diagnostic is run by a test. Cheaper substitute: a general warning.
- [ ] State how an app keeps an MSVC build for its own tools and that it does not ship. Done when: the page and `docs/guide/own-program.md` agree. Cheaper substitute: leaving it out.
- [ ] Add the page to the guide's index and give it a `reviewed` date. Done when: `tools/site/build.py --check` passes. Cheaper substitute: an unlinked page.
- [ ] Commit: `docs: the supported C++ subset`

**Test checkpoint:** `python3 tools/site/build.py --check` exits 0 and fails when `docs/guide/cxx.md` is removed from the index.

## 6. Guide: Bring your own toolkit

How a C++ toolkit sits on the SDK: windows, draw, text, input, the GPU tier, printing, and the checks that prove the boundary.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `docs/guide/own-toolkit.md`. Done when: its programs build through the site build.
2. Link the samples and the checks. Done when: every command in it runs.

- [ ] Write the page `docs/guide/own-toolkit.md` with a minimal toolkit that opens a window, draws and shapes text. Done when: the site build compiles and runs its program and `tools/site/build.py --check` passes. Cheaper substitute: prose.
- [ ] Show input methods, the screen reader tree and printing from `tests/fixtures/toolkit_client.cpp` and `toolkit_print.cpp` in the page. Done when: each snippet is taken from the file by the site build. Cheaper substitute: a copy that can drift.
- [ ] Show the C++ and GPU proof samples and the CPU fallback rule in the page, with links to `docs/guide/cxx.md` and `docs/guide/gpu.md`. Done when: the links resolve in the site build. Cheaper substitute: a list of names.
- [ ] Explain the link rule, the source check and the header check from `docs/guide/boundary.md` for a toolkit author. Done when: the page names each command and the page's programs pass them. Cheaper substitute: a link only.
- [ ] Add it to the guide's index with a `reviewed` date. Done when: `tools/site/build.py --check` passes. Cheaper substitute: an unlinked page.
- [ ] Commit: `docs: bring your own toolkit`

**Test checkpoint:** `python3 tools/site/build.py --check` exits 0 and fails when a program in `docs/guide/own-toolkit.md` does not build.

## Verification

- `tests/fixtures/toolkit_client.cpp` and `toolkit_print.cpp` use only the public headers and pass: draw and text pixels equal Impossible UI's, composition text and the screen reader tree arrive, and a page prints.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
