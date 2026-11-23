---
schema_version: 1
id: parts-and-release
domain: 08-release
status: active
title: "TODO-01 -- Parts, Packaging and Release"
---

# TODO-01 -- Parts, Packaging and Release

> **Goal:** Programs are packaged, installed, updated and removed on Windows, the SDK is documented and versioned, and a first release exists.

> **Current state:** Measured 2026-11-23 at `e06ab3a`. `tools/new_app.py` makes a program repository and `tools/app_build.py` builds it. 41 repositories carry an `app.json` (Heliograph among them) and nothing reads `opens`. There is no version, installer, API reference or release.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: parts, `app.json`, and that the installer tool reads it on Windows.
- `tools/new_app.py`, `tools/app_build.py`, `tools/contribute.py`.

## Outcome

- A program built with the SDK installs, opens its file types, updates and leaves nothing behind.
- A stranger can write, build and package a program from the guide alone.

**Adjacency:** all=not-applicable (build tools and documents hold no user records; the installer keeps only what it needs to remove a program)

**Adjacency rationale:** What an installed program keeps is its own; the installer records only its own files.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Parts for drawing, text and controls | D02 T01 §1, D05 T02 §22, D06 T01 §1 | [ ] |
| 2 | §2 | The `app.json` format | -- | [ ] |
| 3 | §3 | Package builder | §2, D06 T01 §2 | [ ] |
| 4 | §4 | Installer | §3, D06 T01 §2 | [ ] |
| 5 | §5 | File types | §4, D07 T01 §15 | [ ] |
| 6 | §6 | Update and removal | §4, §5 | [ ] |
| 7 | §7 | Program template on Impossible UI | D05 T01 §30, D05 T03 §4, D07 T01 §14 | [ ] |
| 8 | §8 | API reference | §1 | [ ] |
| 9 | §9 | Programming guide | §7, §8 | [ ] |
| 10 | §10 | Versions and compatibility | §8 | [ ] |
| 11 | §11 | Whole-program proof | §6, §7, D05 T02 §22, D07 T01 §5, D07 T01 §7, D07 T01 §9, D07 T01 §12, D07 T02 §7 | [ ] |
| 12 | §12 | Release archive | §9, §10, §11 | [ ] |
| 13 | §13 | First public release | §12 | [ ] |

## 1. Parts for drawing, text and controls

The OS imports one part at a commit without the rest.

**Phase:** 6
**Needs:** none
**Surface:** no

**Build order:**

1. Write `tools/check_parts.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_parts.py`. Done when: the checks fail on a planted fault.

- [ ] List `base`, `draw`, `text`, `ui`, `controls`, `dialogs` and the Windows layer in `parts.json`. Done when: `tools/check_parts.py` refuses an undeclared include. Cheaper substitute: one part.
- [ ] Build each alone with its declared parts in `tests/test_parts.py`. Done when: removing any other folder still builds it. Cheaper substitute: together.
- [ ] Copy one part and its needs to a folder in `tools/copy_part.py`. Done when: the copy builds and its tests pass alone. Cheaper substitute: a manual copy.
- [ ] Record licence notices per part in `parts.json`. Done when: each copied part carries its own. Cheaper substitute: one notice.
- [ ] Commit: `parts: drawing, text and controls`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. The `app.json` format

One checked format for what every program says about itself.

**Phase:** 6
**Needs:** none
**Surface:** no

**Build order:**

1. Write `tools/app_json.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Write the rules in `docs/app-json.md` and `tools/app_json.py`: name, label, version, tier, `opens`, `needs`, icon, publisher. Done when: a table of 60 good and bad files is judged right. Cheaper substitute: a sample.
- [ ] Check the 41 existing files with `tools/app_json.py`. Done when: `python3 tools/app_json.py --all ..` names every fault with file and field. Cheaper substitute: new ones only.
- [ ] Measure that the tier equals the program's imports in `tools/app_build.py`. Done when: a program above or below its tier is named. Cheaper substitute: trusting it.
- [ ] Read `needs` as `tools/new_app.py` documents it, `id` and `what`, with an optional `fallback`, in `tools/app_json.py`. Done when: an existing declaration passes the package builder unchanged and a wrong id is refused. Cheaper substitute: a new format.
- [ ] Correct each file that fails, in its own repository. Done when: `python3 tools/app_json.py --all ..` exits 0. Cheaper substitute: a list of faults.
- [ ] Commit: `tools: the app.json format`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Package builder

One installable file from a built program.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `tools/package.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Collect the program, its icon, licences and `app.json` in `tools/package.py`. Done when: the same inputs give the same bytes. Cheaper substitute: a folder.
- [ ] Compress through the SDK's own deflate in `tools/package.py`. Done when: the package opens in Windows as a ZIP after its header. Cheaper substitute: uncompressed.
- [ ] Refuse a program that fails its import check, tests or version in `tools/package.py`. Done when: each planted fault stops it. Cheaper substitute: a warning.
- [ ] Sign when a certificate is configured, and say so when none is, in `tools/package.py`. Done when: both paths are tested with a test certificate. Cheaper substitute: always unsigned and silent.
- [ ] Commit: `tools: the package builder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Installer

Installs for the current user without administrator rights.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** the content dialog of `todo/05-controls/TODO-01-controls.md` §15; its state matrix rows are the reference.
**Job:** the user installs a program in one step.
**Treatment:** WinUI's own states, sizes, timings and focus visual; Selawik or Segoe text in the same rectangle.
**Chrome:** a content dialog of its own.

**Build order:**

1. Write `tools/installer/main.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Write the setup program in `tools/installer/main.c` with its own list `abi/installer.json`, since it is for Windows only. Done when: a function outside the list fails its build. Cheaper substitute: an unchecked program.
- [ ] Copy to a new version folder and switch in one step in `tools/installer/install.c`. Done when: killing it at any moment leaves the old or the new program whole. Cheaper substitute: overwriting.
- [ ] Refuse a damaged or changed package in `tools/installer/install.c`. Done when: every flipped byte is refused before anything is written. Cheaper substitute: at the first use.
- [ ] Refuse paths that leave the program's folder in `tools/installer/install.c`. Done when: a table of 40 hostile names writes nothing. Cheaper substitute: trusting names.
- [ ] Add Start entries and the Windows list of installed programs in `tools/installer/register.c`. Done when: both show name, version, icon and publisher. Cheaper substitute: files only.
- [ ] Show progress and errors with Impossible UI, and run without a window on request, in `tools/installer/main.c`. Done when: both ways give the same result and code. Cheaper substitute: a console.
- [ ] Commit: `installer: install`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. File types

The types a program says it opens.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `tools/installer/register.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_installer.py`. Done when: the checks fail on a planted fault.

- [ ] Register each type of `opens` for the user in `tools/installer/register.c`. Done when: the program is in Open With for each and no default is taken. Cheaper substitute: changing defaults.
- [ ] Give each type its icon and name in `tools/installer/register.c`. Done when: Explorer shows them. Cheaper substitute: the program's icon.
- [ ] Open the file through the running copy in `tests/test_installer.py`. Done when: a double click opens the file once. Cheaper substitute: a second copy.
- [ ] Leave other programs' entries as they were in `tools/installer/register.c`. Done when: a snapshot before and after differs only by ours. Cheaper substitute: a rewrite.
- [ ] Commit: `installer: file types`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Update and removal

A newer version replaces the old; removal leaves nothing.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `tools/installer/update.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Replace a running program after it closes in `tools/installer/update.c`. Done when: the open window goes on and the next start is new. Cheaper substitute: a forced stop.
- [ ] Keep the user's files and settings in `tools/installer/update.c`. Done when: they are equal before and after. Cheaper substitute: a fresh start.
- [ ] Return to the old version when the new one does not start in `tools/installer/update.c`. Done when: a planted broken version is undone. Cheaper substitute: leaving it.
- [ ] Refuse an older version unless asked in `tools/installer/update.c`. Done when: the refusal is named. Cheaper substitute: installing it.
- [ ] Remove files, entries and types, and ask about the user's data, in `tools/installer/remove.c`. Done when: a snapshot before installing equals the one after removing. Cheaper substitute: leaving entries.
- [ ] Commit: `installer: update and removal`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Program template on Impossible UI

`tools/new_app.py` starts a program with the look and parts of the others.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `tools/new_app.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_new_app.py`. Done when: the checks fail on a planted fault.

- [ ] Write a window with title bar, menu, settings page and about box in `tools/new_app.py`. Done when: a new program builds, passes its tests and follows the theme. Cheaper substitute: a blank window.
- [ ] Include open, save, recent files and unsaved-changes questions for document programs in `tools/new_app.py`. Done when: the template's script loses no text. Cheaper substitute: a note.
- [ ] Add tests that run without Windows through the fakes in `tools/new_app.py`. Done when: they run in WSL alone. Cheaper substitute: Windows only.
- [ ] Prove it in `tests/test_new_app.py`. Done when: each kind of program is made, built and run. Cheaper substitute: one kind.
- [ ] Commit: `tools: the program template`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. API reference

Every public name explained, from the headers.

**Phase:** 6
**Needs:** none
**Surface:** no

**Build order:**

1. Write `tools/api_docs.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_docs.py`. Done when: the checks fail on a planted fault.

- [ ] Read the headers with the pinned clang in `tools/api_docs.py`. Done when: every public type, function and value is in `docs/api/`. Cheaper substitute: by hand.
- [ ] Refuse a public name without a comment in `tests/test_docs.py`. Done when: a planted bare function fails. Cheaper substitute: a warning.
- [ ] State tier, thread and failure for each function in `include/impossible/`. Done when: the check names one that does not. Cheaper substitute: prose.
- [ ] Build each example in the reference in `tests/test_docs.py`. Done when: a planted wrong example fails. Cheaper substitute: unbuilt text.
- [ ] Commit: `docs: API reference`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. Programming guide

From nothing to an installed program.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `tools/guide_check.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_docs.py`. Done when: the checks fail on a planted fault.

- [ ] Write the guide in `docs/guide/`: first window, layout, controls, commands, text, files, settings, threads, printing, packaging. Done when: each chapter's program builds and runs in `tests/test_docs.py`. Cheaper substitute: an outline.
- [ ] Write the rules for keyboard, screen reader, contrast, scale and right-to-left in `docs/guide/`. Done when: each rule names its check. Cheaper substitute: advice.
- [ ] Refresh `README.md` and its status table from the stamped sections. Done when: the table test fails when it disagrees with the plan. Cheaper substitute: by memory.
- [ ] Follow the guide in a clean machine image in `tools/guide_check.py`. Done when: it ends with an installed program. Cheaper substitute: reading it.
- [ ] Commit: `docs: the programming guide`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 10. Versions and compatibility

A version that means something.

**Phase:** 6
**Needs:** none
**Surface:** no

**Build order:**

1. Write `tools/api_diff.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Keep one version in `VERSION` and build it in. Done when: `im_version()` and the package agree. Cheaper substitute: several places.
- [ ] Compare the public API with the last release in `tools/api_diff.py`. Done when: a removed or changed name without a major version fails. Cheaper substitute: review.
- [ ] Build every pinned program against the new commit in `tools/check_apps.py`. Done when: a program that breaks is named. Cheaper substitute: the samples.
- [ ] Write `CHANGELOG.md` entries per change that a program can see. Done when: the check refuses an API change without one. Cheaper substitute: commit titles.
- [ ] Commit: `release: versions and compatibility`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 11. Whole-program proof

One program that uses everything, as a user would.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `samples/notes/` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_whole.py`. Done when: the checks fail on a planted fault.

- [ ] Write a notes program with text, lists, tabs, settings, search, pictures and sound in `samples/notes/`. Done when: it works by pointer, keyboard, touch and screen reader alone. Cheaper substitute: the gallery.
- [ ] Run it for an hour of random use in `tools/soak.py`. Done when: memory, handles and windows return to the start. Cheaper substitute: a minute.
- [ ] Package, install, open a file, print, update and remove it in `tests/test_whole.py`. Done when: each step reads back its result. Cheaper substitute: by hand.
- [ ] Compile its portable part with the OS flags in `tests/test_whole.py`. Done when: no undefined symbol beyond the four memory routines. Cheaper substitute: Windows only.
- [ ] Commit: `release: the whole-program proof`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 12. Release archive

Everything a release needs, built by one command.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `tools/release.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_release.py`. Done when: the checks fail on a planted fault.

- [ ] Build the archive of headers, sources, libraries, tools, guide and notices in `tools/release.py`. Done when: two machines give equal bytes. Cheaper substitute: a ZIP by hand.
- [ ] Refuse a dirty tree, a failing test, a wrong version or any open section of phases 1 to 6 in `tools/release.py`. Done when: each planted fault stops it. Cheaper substitute: a warning.
- [ ] Write hashes and a list of contents in `tools/release.py`. Done when: `tools/release.py --check` verifies them. Cheaper substitute: none.
- [ ] Build a program from the archive alone in `tests/test_release.py`. Done when: it runs on Windows. Cheaper substitute: from the repository.
- [ ] Commit: `release: the archive`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 13. First public release

Publishing is the owner's step.

**Phase:** 99
**Needs:** operator
**Surface:** no

**Build order:**

1. Write `tools/release.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Publish the archive of `tools/release.py` with its notes on GitHub. Done when: the download's hash equals the archive's. Cheaper substitute: a draft.
- [ ] Build `samples/notes/` from the published archive on a clean machine. Done when: it runs, and `README.md` names the version. Cheaper substitute: trusting the upload.
- [ ] Commit: `release: first public release`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- The whole-program proof passes from the release archive on a machine without the repository.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
