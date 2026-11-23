---
schema_version: 1
id: printing
domain: 07-windows
status: active
title: "TODO-02 -- Printing"
---

# TODO-02 -- Printing

> **Goal:** A program prints what Impossible UI draws, with a preview, a printer choice and clear errors.

> **Current state:** Measured 2026-11-23 at `e06ab3a`. No print function is in `abi/tiers.json` and no print code exists. The owner added printing on 2026-11-23.

## Inputs

- `abi/tiers.json`: tier 2 Documents gains the print functions, so that a document program does not need tier 5.
- `todo/05-controls/TODO-03-dialogs.md`: the dialog frame.
- Microsoft Print to PDF, as the printer the tests use.

## Outcome

- A document of 100 pages prints to a PDF whose pages equal the preview.
- A printer that is missing, off or refused is told by name and loses nothing.

**Adjacency:** all=not-applicable (printing holds no records; the program owns the document and its settings)

**Adjacency rationale:** The job belongs to the system once sent; nothing is kept here.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Print functions in the tier list | -- | [ ] |
| 2 | §2 | Printers and what they can do | §1 | [ ] |
| 3 | §3 | Jobs and pages | §1, §2 | [ ] |
| 4 | §4 | Laying out pages | §3, D01 T02 §4 | [ ] |
| 5 | §5 | Progress, cancelling and errors | §3 | [ ] |
| 6 | §6 | Print preview | §4, D05 T01 §2, D05 T01 §8 | [ ] |
| 7 | §7 | Print dialog | §2, §5, §6, D05 T03 §1 | [ ] |

## 1. Print functions in the tier list

Each function added is work for Impossible OS, so each has a reason.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `abi/tiers.json` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_seed.py`. Done when: the checks fail on a planted fault.

- [ ] Add the needed `winspool.drv` and `gdi32.dll` functions to tier 2 in `abi/tiers.json`. Done when: a tier 1 program that uses one is refused. Cheaper substitute: an unchecked call.
- [ ] Write what each must do, in the order it is called, in `docs/os-printing.md`. Done when: every added function is in it and the test fails when one is missing. Cheaper substitute: a list of names.
- [ ] Check every name on Windows in `tests/test_seed.py`. Done when: each resolves in its DLL. Cheaper substitute: trusting the list.
- [ ] Tell Impossible OS over Heliograph, as `AGENTS.md` says. Done when: the message names the commit and `docs/os-printing.md`. Cheaper substitute: waiting for an import.
- [ ] Commit: `abi: print functions`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Printers and what they can do

The list of printers, the default, and each printer's paper, margins, colour, sides and resolution.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/printers.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] List printers and the default in `src/printers.c`. Done when: the list equals the one PowerShell gives. Cheaper substitute: the default only.
- [ ] Read paper sizes, printable area, resolutions, colour, sides and copies in `src/printers.c`. Done when: Microsoft Print to PDF and a planted record read back equal. Cheaper substitute: A4.
- [ ] Tell a printer that is off, paused, out of paper or not reachable in `src/printers.c`. Done when: each planted state has its name. Cheaper substitute: one error.
- [ ] Follow printers that come and go in `src/printers.c`. Done when: the list is right after each change. Cheaper substitute: at start only.
- [ ] Keep a slow network printer from stopping the window in `src/printers.c`. Done when: the window draws during a 10 second answer. Cheaper substitute: waiting.
- [ ] Commit: `printing: printers and what they can do`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Jobs and pages

One job: start, pages, end, as pixels in bands.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/print.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_print.py`. Done when: the checks fail on a planted fault.

- [ ] Start a job with a title and the chosen settings in `src/print.c`. Done when: the queue shows the title and settings. Cheaper substitute: defaults.
- [ ] Send pages as bands of pixels in `src/print.c`. Done when: a 600 DPI page uses under 64 MB. Cheaper substitute: a whole page at once.
- [ ] Send the same bytes for the same page in `src/print.c`. Done when: two jobs give equal PDF pictures. Cheaper substitute: timing in the output.
- [ ] End, or abort on a failure at any page, in `src/print.c`. Done when: an aborted job leaves nothing in the queue. Cheaper substitute: an open job.
- [ ] Print to a named PDF file in `tests/test_print.py`. Done when: the file opens and has every page. Cheaper substitute: a real printer.
- [ ] Commit: `printing: jobs and pages`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Laying out pages

Impossible UI drawn at the printer's resolution on the chosen paper.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/print_layout.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Measure the page in the printer's printable area in `ui/print_layout.c`. Done when: a ruler page measures exactly in the PDF. Cheaper substitute: screen pixels.
- [ ] Draw elements at the printer's scale in `ui/print_layout.c`. Done when: text and lines are sharp at 300, 600 and 1200 DPI. Cheaper substitute: enlarging the screen.
- [ ] Break content into pages between lines and rows in `ui/print_layout.c`. Done when: no line is cut in 1,000 random documents. Cheaper substitute: cutting anywhere.
- [ ] Add header, footer, page numbers and margins in `ui/print_layout.c`. Done when: a table of 20 settings passes. Cheaper substitute: none.
- [ ] Print in light colours whatever the theme in `ui/print_layout.c`. Done when: a dark program prints on white. Cheaper substitute: the screen's theme.
- [ ] Scale to fit, to a percentage or several pages per sheet in `ui/print_layout.c`. Done when: each choice measures right in the PDF. Cheaper substitute: full size.
- [ ] Commit: `printing: laying out pages`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Progress, cancelling and errors

What the user sees while printing.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/print.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Run the job on a worker in `src/print.c`. Done when: the window draws during 500 pages. Cheaper substitute: blocking.
- [ ] Tell the page being printed and cancel at any moment in `src/print.c`. Done when: random cancels leave no job and no thread. Cheaper substitute: after the page.
- [ ] Name each error and what was already sent in `src/print.c`. Done when: a table of planted failures passes. Cheaper substitute: one error.
- [ ] Report a failed job as ended, with what may have come out, and print again only when the program asks, in `src/print.c`. Done when: a job aborted at page 60 of 100 ends; dismissing the message makes no second job, and asking makes one of 100 pages. Cheaper substitute: a retry by itself.
- [ ] Print from a snapshot or an unchanging revision kept until the job ends, in `include/impossible/print.h`. Done when: editing page 80 while page 10 prints, or closing the document, prints page 80 as it was when the job was sent, and everything is freed. Cheaper substitute: a page source that can change.
- [ ] Commit: `printing: progress, cancelling and errors`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Print preview

The pages as they will print.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the pages of the printed PDF are the reference for content; the Windows print dialog, captured on Windows, for how it is used.
**Job:** the user sees exactly what will come out of the printer.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/controls/print_preview.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Draw pages through the print layout in `ui/controls/print_preview.c`. Done when: each preview page equals the PDF's page scaled down, within 4 of 255. Cheaper substitute: a second layout.
- [ ] Show one, two or many pages with zoom in `ui/controls/print_preview.c`. Done when: a recorded script reads back page and zoom. Cheaper substitute: one page.
- [ ] Keep previews within a byte limit in `ui/controls/print_preview.c`. Done when: 1,000 pages at 1200 DPI and each zoom stay under 96 MB at their peak. Cheaper substitute: counting pictures.
- [ ] Move by Page Up, Page Down, Home, End and a page number in `ui/controls/print_preview.c`. Done when: the key script passes. Cheaper substitute: the scroll bar.
- [ ] Name pages to the screen reader in `ui/controls/print_preview.c`. Done when: the peer gives page and count. Cheaper substitute: a picture.
- [ ] Commit: `printing: preview`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Print dialog

Printer, copies, pages, sides, colour, paper, direction and scale beside the preview.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the Windows print dialog, captured on Windows, for choices and keys; the content dialog frame for the look.
**Job:** the user chooses a printer and settings and prints.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** none of its own; the gallery window hosts it.

**Build order:**

1. Write `ui/dialogs/print.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Show only what the printer can do in `ui/dialogs/print.c`. Done when: a black-only printer shows no colour choice. Cheaper substitute: everything.
- [ ] Read page ranges such as 1-3, 5 and refuse wrong ones in `ui/dialogs/print.c`. Done when: a table of 40 texts passes. Cheaper substitute: all pages.
- [ ] Update the preview with every choice in `ui/dialogs/print.c`. Done when: each choice changes the pages it should. Cheaper substitute: at printing.
- [ ] Say when there is no printer and how to add one, in `ui/dialogs/print.c`. Done when: with none the dialog opens and cannot print. Cheaper substitute: a crash.
- [ ] Work by keyboard and screen reader alone in `ui/dialogs/print.c`. Done when: a script prints without the pointer. Cheaper substitute: the pointer.
- [ ] Return the choices to the program in `ui/dialogs/print.c`. Done when: the next opening shows them. Cheaper substitute: defaults each time.
- [ ] Commit: `printing: the dialog`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- `samples/print/` prints 100 pages to Microsoft Print to PDF and every page equals its preview.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
