# The interface and the apps outside the OS repository

Status: waiting for approval. Facts about Impossible OS were measured at `d4a124206`, facts about WinUI in `microsoft/microsoft-ui-xaml` on 2026-11-23.

## Why are we doing this?

The interface is Phase 8 of the OS plan and the plan's next open rows are in Phase 0. Built outside, it is learned from now, on a working Windows desktop. The apps are about as much work as the OS, so they start now too. Each repository stands alone, and Impossible OS uses part of Impossible SDK, so nothing is written twice.

## What are the goals?

- Every Windows 11 control, 1:1, in portable C, usable by Windows programs and by the OS.
- Work starts before the Windows 11 capture ends.
- Apps are developed and released on Windows, each in its own repository.
- The OS always knows what it has to import, from where, at which stage, and fails a check when that knowledge is lost.

**Not goals:** running WinUI apps (that is an OS compatibility tier), shipping Microsoft code as a framework, moving the shell.

**It has failed if** a feature exists in two repositories, a roadmap item disappears in the move, the OS needs a special case for one app, or a repository cannot be built alone.

## Which approaches were considered?

| Approach | Verdict |
| --- | --- |
| Own C engine, WinUI's data (chosen 2026-11-23) | Exact values now, no C++ or DirectX on the OS. We write layout, text, input and drawing |
| Modify WinUI 3 | Needs WinRT, COM, composition and DirectX on the OS before the first button |
| Own engine from the capture only | Controls wait for the capture, and states are measured by eye |

## Which decisions did the owner make?

- The approach above, from the question of 2026-11-23.
- First a thin slice (button, text box, toggle, list view), **and the apps start at the same time**.
- The planned apps move to `D:\ImpossibleOS`; their roadmap files keep the import.
- A repository leaves `repos.md` when it is created or moved.
- Nothing goes to GitHub until the owner asks.

## What is the shape?

```
WinUI source, pinned commit (MIT)          Windows 11 capture (OS repository)
        | templates, states, timings                 | shell layouts, classic controls
        v                                            v
   tools/winui-gen  ------------------------->  C tables (checked in)
                                                     |
                           Impossible UI: portable C, no OS calls
                             |                            |
                      Windows layer                OS layer (in the OS repository)
                             |                            |
                  apps on Windows, gallery      desktop shell, the same apps
```

**Impossible UI** is a library inside Impossible SDK (`ui/`). It holds the element tree, layout, styles and states, input and focus, commands, popups, the software renderer, text, animation, the accessibility tree and the controls.

- It calls no operating system function. Memory, time, files, clipboard, input method and screen reader come through a table of callbacks that the layer fills in.
- It uses integers and 16.16 fixed point only, because OS kernel and user code is compiled without SSE. SIMD versions are optional and must give the same bytes.
- It draws into a pixel buffer and reports dirty rectangles. Transparency effects (Mica, Acrylic) are drawn by the layer, because they need what is behind the window.
- A build check refuses any import or undefined symbol in it, and compiles it with the OS's own flags. This proves on every build that the OS can take it.

**Where do the exact values come from?** `tools/winui-gen` reads the theme resources and templates of a pinned WinUI commit and writes C tables, with the MIT notice kept. Control behaviour (keyboard, states, timing) is ported by hand from the same source and names the file it came from. Real WinUI on Windows is the reference: the test program shows the same control in the same state in both, and compares the pixels outside text and icons. Text and icons must fill the same rectangles.

**Fonts and icons:** Segoe cannot be redistributed. Selawik, Cascadia Code and our own icons are used, and Segoe is used on Windows when it is installed, so text compares exactly there.

## What about the Windows 11 capture?

It continues in the OS repository, in its present order. Controls that exist in WinUI no longer wait for it. It stays the only source for the taskbar, Start, Explorer and Settings layouts and for the classic controls that WinUI does not have. When a capture disagrees with WinUI's value, the difference is a finding in the OS's consistency ledger (`consistency.json`); the SDK changes only through that ledger.

## What moves, and what stays?

| OS roadmap | Now | After |
| --- | --- | --- |
| `08` `TODO-18` §1 to §15, §21, §24 | Engines: tree to navigation, resources, automation | Impossible UI. The OS keeps one import section and its compositor layer |
| `08` `TODO-18` §16 to §20, §22, §23 | Settings platform, shell namespace, app model, shell processes | Stay |
| `08` `TODO-19` (26 sections) | Fluent controls | Impossible UI |
| `08` `TODO-05`, `TODO-06` | Core widgets and dialogs | Impossible UI. Parts that are desktop integration stay |
| `08` `TODO-01` to `TODO-04`, `TODO-07` | Drawing, text, theme, animation, accessibility | Split section by section: portable code moves, driver, Registry and system service work stays |
| `08` `TODO-08` to `TODO-17`, `09` | Window manager, shell, taskbar, Start, capture | Stay |
| `11` `TODO-01` to `TODO-04`, `TODO-06` to `TODO-13` | Apps | Their own repositories, per `repos.md`. The OS keeps an import file each, in the shape of `TODO-14-inkeys.md`. Parts `repos.md` does not cover stay (for example WiFi in `TODO-02`) |
| `11` `TODO-05` (Recto), `TODO-14` (Inkeys) | Bundled apps | Unchanged; they already have homes |

`TODO-18` and `TODO-19` say Windows' framework is closed and that nothing is taken from Microsoft's controls. Both are corrected when they move: data and ported behaviour from the MIT source are allowed, Microsoft's fonts, icons, pictures, sounds and wording are not.

## Where does a todo item go?

Ask in this order; the first yes decides.

1. Does it need the kernel, a driver, a system service, Registry defaults or state shared by the whole system? **Impossible OS.**
2. Could a second app use it, and does it work on plain Windows? **Impossible SDK.**
3. Otherwise: **the app's repository.**

Photos as the example:

| Need | Home |
| --- | --- |
| Decode PNG, JPEG, WebP | Impossible SDK |
| Zoom, crop, the film strip | Photos |
| "Photos opens `.png`" | Photos declares it in its `app.json` |
| Reading `app.json` and registering types on Windows | Impossible SDK's installer tool |
| Reading `app.json` and registering types on Impossible OS, the Open With list | Impossible OS |
| Thumbnails in Explorer | Decoder in Impossible SDK, the Explorer part in Impossible OS |

When an app needs something from another repository, the item is filed **there**, and the app's own item names it on a `**Needs:**` line and waits. An app never works around a missing SDK or OS feature. An OS need found by an app becomes an item in the OS import file of that app.

## How are the repositories connected but separate?

Each repository builds, tests and releases alone. Three files connect them.

| File | In | Says |
| --- | --- | --- |
| `sdk.json` | Every app | The Impossible SDK commit it builds with |
| `app.json` | Every app | Name, version, file types, tier and what it needs from the OS |
| `todo/imports.json` | Impossible OS | For each external repository: what it supplies, the OS section that imports it, the stage, the needed tier and the pinned commit (`null` until imported) |

## How does nothing get forgotten?

1. **Nothing moves by deletion.** `imports.json` carries a ledger row for every moved section: its old address, the new repository and item, and a hash of the old text. A section moves only when the new item exists.
2. **An import section stays.** Each moved file keeps its address and one or more import sections in the implementation plan, at the stage where the OS needs it. Other sections that depended on the old one depend on the import section, so the OS plan cannot pass that stage without it.
3. **A check in the OS** (`scripts/imports.py --check`, run by lint) fails when a ledger row has no import section, an import section has no row, a moved section has no new item, or the copied source differs from its pinned commit. When the external checkout is not on the machine, it checks the recorded hashes and says so.
4. **Imports are copies at a pinned commit** under `src/libs/impossible-ui/` and `src/apps/<name>/`, never edited in place and never downloaded during a build. Every import is reviewed, as the OS rule of 2026-11-23 says.
5. **Newest is not automatic.** `imports.py --outdated` lists what is behind. Updating is ordinary work.
6. **The OS cannot drop an import silently:** removing a row needs a reason in `todo/answers.md`.

Public and private: the OS repository is public. It names an external repository only by name and commit until that repository is public too. An import into the public OS repository publishes that source, so the owner is asked before the first one.

## In what order?

Tracks A, B and C run side by side.

**A. Interface**

1. `winui-gen` for the four controls; the comparison program with real WinUI.
2. Impossible UI core: tree, layout, states, renderer, text, input, focus.
3. Button, text box, toggle, list view: every state, light, dark and contrast, 100 to 300 percent.
4. The OS compile check from the first commit.
5. The remaining controls, in the order the apps need them, then the rest of the WinUI Gallery list.

**B. Apps**, starting now. Every app is split into an engine (no window, tested alone) and a window. Engines start at once; windows use Impossible UI as controls arrive.

| App | Engine work that starts now | First window needs |
| --- | --- | --- |
| Calculator | Exact arithmetic, parser, programmer mode, units | Button, text, list |
| Photos | Image decoders (in the SDK), folder model, zoom and crop | List view, command bar |
| Notepad | Text buffer, encodings, find, undo | Text box, tabs, menus |
| Tackle | Whole programs: console tools | Nothing |

**C. The OS connection**

1. `imports.json`, the ledger and `imports.py --check`, with tests.
2. Move one app file as the trial: Calculator.
3. The other app files, one domain at a time, each with its ledger rows.
4. The control and engine files.
5. The first import of Impossible UI, when the OS reaches that stage.

Until C1 exists nothing is taken out of the OS roadmap. The OS run must not build an app or control that has started here, so C1 to C4 come before the OS reaches Phase 8.

## What can go wrong?

| Risk | Answer |
| --- | --- |
| Engine too slow without SSE | Dirty rectangles, and the same SIMD files the OS already builds separately |
| WinUI moves on | The pin moves only on purpose; the generator shows what changed |
| WinUI differs from the shell's own controls | Capture section 14 measures it; the ledger decides |
| The run or the paused section 49 work collides with the move | The move is small commits in a separate worktree on `origin/main` |
| Too many repositories at once | Four start now. Others start when one of them has a release |

## What is left to the builder?

File layout inside `ui/`, the generator's format, the callback table, the names of `imports.json` fields, and the order of controls after the first four.
