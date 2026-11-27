# Impossible SDK as the platform for Recto, Office and Isotone

Status: approach approved by the owner 2026-11-27 (answers below); this record is the design. Facts about Recto were measured at `274f248`, Isotone at `ff1a42e`, Office at `dd4cbbc`, Impossible OS at `180284002`.

## Why are we doing this?

On 2026-11-22 the owner made Recto, Office and Isotone permanent apps of Impossible OS, under one rule: each is made to work on Windows first, then Impossible OS is made compatible with it (`impossible-os` `todo/11-apps/INDEX.md`). Running them unchanged would make Impossible OS rebuild most of DirectX first. Recto's OS plan instead gives Recto a private platform seam and an Impossible OS backend of its own (`impossible-os` `todo/11-apps/TODO-05-pdf-viewer.md` §12, §14). Doing that three times means three seams and three OS backends.

Impossible SDK already is such a seam for its own programs: a program calls only the Windows functions in `abi/tiers.json`, the build fails on any other import, and every listed function becomes an Impossible OS roadmap section. So the shared seam is Impossible SDK itself.

## Where do the three apps stand?

- **Recto** has code: C++23 on MSVC with modules, exceptions and RTTI, x64 and arm64. `recto-core`, `recto-pdf` and `recto-ui` call Windows directly, and 9 source files include Direct2D, DirectWrite, Direct3D or DirectComposition headers.
- **Isotone** has a C++23 foundation (`isotone::core`, `isotone::ui`) on MSVC and clang-cl with the static runtime, and no DirectX code yet. Its stack design plans Win32, Direct2D, DirectWrite and Direct3D.
- **Office** has no code. Its tech stack record plans C++23 with its own toolkit on Win32, Direct2D and DirectWrite.

## What are the goals?

- Recto, Office and Isotone each call the operating system only through Impossible SDK, and their release builds pass the SDK's checks.
- Each keeps its own toolkit, controls and look: Vellum, `isotone::ui` and Office's toolkit draw on the SDK, not on Direct2D.
- The same source runs on Windows now and on Impossible OS later, with no Impossible OS backend written for any one app.
- Impossible OS gets one finite list of what these apps need, including an exact Direct3D 11 subset.

**Not goals:** running unmodified DirectX programs (tier 9 of `impossible-os` `docs/sdk/win32-compat-matrix.md`), moving any app onto Impossible UI's controls, or merging the three toolkits.

**It has failed if** an app's release depends on a Windows call that is not on the list, Impossible OS needs a special case for one app, any of the three waits for Impossible OS, or a feature is written in two places.

## Which approaches were considered?

| Approach | Verdict |
| --- | --- |
| One shared seam in Impossible SDK (chosen) | One contract, one checker, one OS target |
| A seam per app, as Recto's OS plan has now | Three interfaces and three OS backends to keep |
| Impossible OS runs them unchanged | DirectX on the OS first; these apps run there last |
| All three move to Vellum | Replaces Isotone's working `isotone::ui` |

## Which decisions did the owner make?

- 2026-11-22: the three are permanent Impossible OS apps, Windows first.
- 2026-11-27: one shared seam, built into Impossible SDK ("Can't we build the shared seam into Imposible SDK?").
- 2026-11-27: apps that need the GPU get a Direct3D 11 tier in the SDK.
- 2026-11-27: plan entries go into all the repositories involved.
- 2026-11-27: no double work. Impossible OS uses Impossible SDK for its own interface too, and nothing is built in both.

## What is the shape?

```
 Recto (Vellum)      Office (its toolkit)      Isotone (isotone::ui)
        \                    |                        /
         +--------- Impossible SDK, C interface ----+
         | base: memory, threads, time, files        |
         | window: windows, input, DPI, present      |
         | draw: paths, gradients, images            |
         | text: fonts, shaping, fallback, layout    |
         | gpu: the Direct3D 11 subset (new tier)    |
         | cxx: the C++ runtime (new part)           |
         | parts: png, deflate, unicode and so on    |
         +-------------------------------------------+
                            |  only calls listed in abi/tiers.json
                 +----------+-----------+
              Windows              Impossible OS
           (today, proved)   (each listed call is a roadmap section)
```

An app's toolkit keeps its element tree, layout, controls and styling. Everything below that (making a window, reading input, rasterising shapes, shaping and drawing text, putting pixels or a GPU surface on screen, files, threads, clipboard) is an SDK call, in the engine as much as in the interface: for Recto that is `recto-core`, `recto-pdf` and `recto-ui` alike. The SDK's Windows layer is the only code that calls Windows.

## How is the boundary checked?

Reading the finished program cannot prove the boundary: the import check sees DLL and function names, not which component made a call, and a COM method is an indirect call that names nothing at all. Review rounds 2 to 4 found new ways past checks of that kind (`docs/reviews/sdk-as-platform-spec.md`), so the boundary is built from three things that close the class together: the link (no Windows name exists outside the Windows layer), the public interface (no Windows handle or pointer ever leaves it) and the source (no Windows header outside it).

1. **No Windows names on the link line.** An app is linked with no system import library (`kernel32`, `user32` and the rest are dropped from `cmake/ImpossibleSDK.cmake`). The SDK generates its own import library from `abi/tiers.json` (with `llvm-dlltool` from the pinned llvm-mingw), in which every function carries a private name such as `im$CreateFileW`. Only the SDK's Windows layer is compiled against that header. Any other object, the app's, a generated one or a prebuilt library, that names `CreateFileW`, `LoadLibraryW` or `GetProcAddress` finds no such symbol and fails to link. Measured 2026-11-27 with the pinned llvm-mingw 20260922: a `.def` entry `im$ExitProcess==ExitProcess` gives an executable whose import table names `kernel32.dll` `ExitProcess`, and an object calling plain `ExitProcess` against the same library fails with an undefined symbol.
2. **Every object is built here, from source.** A shipping program links no prebuilt object or archive: every input is compiled by the program's own build, with the SDK toolchain, from sources that pass rule 4, or is an SDK part built the same way. Third-party libraries (Recto's dependencies, for example) are built from source like the app. The SDK's link step accepts only objects its build recorded, so a vendor archive whose headers cannot be known is refused, not inspected.
3. **Only the Windows layer may hold a private name.** The same link step lists the undefined symbols of every input object (`llvm-nm -u`) and fails when any object outside the SDK's Windows layer refers to a private name.
4. **No Windows header outside the Windows layer.** A source check, run in each app's CI and again when Impossible OS imports the app, fails on any Windows, COM or DirectX header in the app or its dependencies, because a type or constant taken from one leaves no symbol for the link to catch. It is Recto's `--seam-check` (`TODO-05` §12), generalised.
5. **No system handles or pointers in the public interface.** A COM interface or a Windows handle can only come from a Windows call or from the SDK. A header check fails when any header a shipping target exports (`include/impossible/` and every include directory a part exports, such as `ui/` with its `im_host` table in `ui/host.h`) declares a Windows type, handle or interface pointer, in a function, a struct field or a callback argument. Because a handle can hide in a `void *`, an untyped pointer is allowed only in three forms: memory the program owns or asked for (the allocation and byte-buffer functions of `base.h`, such as `im_alloc`, `im_free` and the memory primitives, listed by name in the check), caller-supplied context named `user`, and pointers to SDK types that are only forward-declared (`struct im_*`, opaque). Any other `void *` in a struct field or callback fails, and the check must accept the present `base.h` unchanged. Today `im_window` breaks this: its `native` field is a `void *` holding the live `HWND`, and `cursor` holds an `HCURSOR` (`include/impossible/window.h`); that field is the check's regression case. An SDK item moves the state behind an opaque `im_window` and updates every caller before any app moves: `samples/window/main.c`, `tests/fixtures/reopen.c` and the guide programs in `docs/guide/first-window.md` and `docs/guide/drawing.md` (all build the window by value; reopening after close keeps working, and the guide programs are verified through the site build), and `tests/fixtures/test_host.c` and `tests/fixtures/test_dirty.c` (which read the native window to inject messages and read presented pixels, and, in `test_dirty.c`, the frame timer's tokens; they get test-only accessors for the window and the timer state in the test build path, and the stale-timer regression keeps its assertions).
6. **Portable parts stay portable.** `draw`, `text`, `ui` and the other portable parts are built without the private header and pass the existing no-import, no-undefined-symbol check of Impossible UI, extended to every portable part. Only the Windows layer (including the Windows half of the `gpu` part) may use a private name.
7. **What the Windows layer itself uses is listed.** `abi/tiers.json` keeps three kinds of record per tier: `functions` (DLL exports), `resolved` (names looked up at run time, as `src/window.c` does for `DwmSetWindowAttribute`) and `methods` (COM interface methods, by interface). The private import library is generated from `functions` only, a test fails when the Windows layer looks up a name that is not in `resolved`, and the `gpu` part reaches Direct3D only through a generated header that declares the listed methods and no others.
8. **One effective tier.** A program's tier is the highest of the tier measured from its imports and the tiers its linked parts declare in `parts.json` (the `gpu` part declares the GPU tier, whose calls are run-time lookups the import table cannot show). The build check and `tools/app_build.py` (`D08 T01`) use this one calculation, so a Desktop-tier app that links `gpu` fails, and a correctly declared GPU app passes the build and packaging both.
9. **The existing import check** (`tools/check_imports.py`) still runs on the finished executable, against the effective tier, as the last word.

**Test programs.** The SDK's own Windows-side test instrumentation (`tests/fixtures/test_host.c` calls `GetWindowLongPtrW`) and the deliberately forbidden fixture (`tests/fixtures/forbidden.c`, which must link so the import check can reject it) get a test-only build path that keeps the system import libraries. That path cannot produce a shipping program, and every shipping target stays under the rules above.

**Planted cases, one per class, on the CPU path,** each asserting its own diagnostic so one rule cannot pass for another: a vendor archive linked prebuilt (rule 2); and, built from admitted source so rule 2 lets them through, a library that calls `CreateFileW` and one that calls `GetProcAddress` (rule 1, undefined symbol), one compiled against the Windows-layer header that refers to a private name (rule 3), and a source file that includes `<windows.h>` only for a type (rule 4); then a public header with a Windows handle field; a public `void *` that is neither opaque SDK type nor `user` (the present `im_window.native`); a callback that would pass an interface pointer; a portable part that uses a private name. Each must fail. **With the first GPU consumer:** a Desktop-tier app linking `gpu` must fail, and a correctly declared GPU program must pass through packaging.

## What does the SDK add?

1. **C++ programs.** Public headers compile as C++23 (`extern "C"`, no C-only constructs). A `cxx` part supplies the C++ runtime: LLVM libc++, libc++abi and libunwind, built on the SDK's base and linked statically, with exceptions and RTTI on, no Microsoft runtime DLL and no thread-local storage directory (`thread_local` is emulated). The SDK's entry point runs static constructors before `im_main` and static destructors and `atexit` handlers after it, before `ExitProcess`. It is the runtime Impossible OS already plans for user mode (`TODO-05` §11). An app's shipping build uses the SDK toolchain (`toolchain.json`, clang); an app may keep an MSVC build for its own tools, but that build does not ship. **Proof:** a C++ sample, built like an app, that imports a module, throws an exception two frames up through a live object whose destructor runs, catches it, uses `thread_local` from two threads, and runs a static constructor and destructor, all built under the link rule and passing the import check on Windows.
2. **A GPU tier.** A new tier after Desktop lists the Direct3D 11 and DXGI calls that are allowed, as functions, interfaces and methods: device and swap chain creation, buffers, textures, compiled shaders and the draw calls. Programs never touch `d3d11.dll` or `dxgi.dll`: the `gpu` part loads them at run time with `LoadLibraryW` and `GetProcAddress` (listed in the tier), so a system without them still starts the program. `im_gpu_open` then reports that no GPU is there, and the tier's rule is that every GPU feature has a CPU path. The tier's `resolved` and `methods` records are published with its functions, so Impossible OS knows them; reading the two new kinds is a change to the OS's `scripts/imports.py --needs`, made in the OS import section of the tier, which must report an unprovided method under its interface. Shaders are compiled at build time, and the tier fixes their bytecode format. Losing the device while running is reported too: the presenting call returns a lost status on the thread that presents, and the program then recreates its surface or carries on with its CPU path. **Proof:** a GPU sample whose same executable draws a canvas on the GPU and, through its ordinary start-up path, reaches the same canvas on the CPU in two failure cases made at the loader boundary: `d3d11.dll` cannot be loaded, and it loads but a required export is missing. A third case removes the device while a canvas is open, through a test hook in the Windows half of the `gpu` part, and the canvas must keep drawing. The exact method list is set by two workloads: Recto's page tiles (its existing Direct3D and DirectComposition use) and a layered canvas sample shaped like Isotone's planned one. Controls stay on the CPU.
3. **Drawing and text for other toolkits.** `draw` and `text` become public parts with a stable interface that a toolkit outside the SDK can call, at the quality Direct2D and DirectWrite gave these apps. Their owners are the existing sections: paths, arcs and gradients (`D01 T01` §13), clips and layers (`D01 T01` §10), text in fixed point (`D01 T01` §5), and the text engine `D01 T02` §1 to §14, including shaping (§10 glyph substitution, §11 glyph positioning, §12 joining scripts, §13 Indic and Southeast Asian scripts), fallback (§8) and right-to-left text (§9). The font dialog is `D05 T03` §8. Publishing them as separately importable parts is `D08 T01` §1.
4. **arm64 Windows.** Recto ships arm64 today, and the SDK is x64 only (`cmake/toolchain.cmake` picks the x86-64 compiler and `tools/check_imports.py` refuses any other machine). An SDK section adds the arm64 Windows target, teaches the checks the arm64 machine type, and proves a sample on Windows arm64. Recto keeps its present arm64 build until that section is done.
5. **What the toolkits still lack.** Text input with an input method, screen reader support for a toolkit's own element tree, and printing, each as an ordinary SDK section where the existing plan puts it.
6. **A guide.** "Bring your own toolkit": how a C++ toolkit sits on the SDK, with the C++ and GPU samples, and the supported C++ subset (`docs/guide/cxx.md`), which is the reference `TODO-05` §11 asked for and Impossible OS links to instead of writing its own.

## What does each repository do?

**Impossible SDK** builds the items above, each a section in its own `todo/` tree, and documents the new tier and the checks on the site.

**Recto** adds `sdk.json` and `app.json`, then moves `recto-core`, `recto-pdf`, `recto-ui` and Vellum off direct Windows, Direct2D and DirectComposition calls onto the SDK, one surface at a time, with its Windows tests green after each. Its release is built with the SDK toolchain and passes the link rule and the import check in CI.

**Isotone** does the same for `isotone::core` and `isotone::ui`, with its canvas on the `gpu` tier. It has no DirectX code yet, so new drawing starts on the SDK.

**Office** writes a new dated decision in its tech stack record: its toolkit is built on the SDK from its first line. It has `sdk.json` and `app.json` from its first program.

**Impossible OS** gets import files for Office and Isotone in the shape of `TODO-14-inkeys.md`, with `imports.json` rows, and roadmap sections for the GPU tier, shared with what Inkeys needs. It also files one need this design depends on: vector state is saved per process today, not per thread (`docs/research/ai-ml-runtime.md`; `todo/18-future-research/TODO-05-ai-ml-runtime.md` §2). The x64 baseline (SSE2) must be preserved per user thread before any of these apps runs threaded on Impossible OS, and AVX is optional, detected at run time.

Recto's sections `TODO-05` §11 to §14 are not replaced when this record lands. Under the 2026-11-23 rule, a section moves only when its new item exists, with a ledger row and the hash of its old text. Today the OS ledger cannot record that hash: a moved row in `scripts/imports.py` accepts only `section`, `source` and `to`. So the first transfer is preceded by an OS change that adds the hash field and verifies it, with a test that a changed old text is detected. Their obligations map like this:

- §11 (C++ runtime). Building libc++, libc++abi and libunwind, and the C library pieces they need, moves to the SDK's `cxx` part; Impossible OS does not build its own. What only the OS can do stays there as the import section of the `cxx` part: running the unwinder on Impossible OS's exception dispatch, the cross-reference from `10-platform-services/TODO-09-compiler-sdk.md` §11 to the supported subset, and §11's whole test checkpoint (all six `user/tests/cxx/` tests, threads and mutexes and the standard library included, and the two-frame catch, under the launcher on QEMU TCG and KVM at 1 and 2 CPUs).
- §12 (Recto platform seam). The seam itself moves to Recto's own move onto the SDK. `--seam-check` becomes rule 4 above, still run on import. The behaviour tests stay with Recto's OS import section: the tests of files, memory mapping, atomic save, threads and the render pool pass under the user-mode launcher on Impossible OS, as §12 requires.
- §13 (Recto PDF engine on Impossible OS). It still builds Recto's libraries for a separate user-mode target and a Linux engine build over the seam's host implementation. Under this design the shipping artifact is the one Windows executable built with the SDK toolchain, so §13 is rewritten in the same move: it runs that executable's engine tests under the user-mode launcher, and Recto's host tests and sanitizer corpus run in Recto's own Windows CI, where the SDK toolchain already builds them.
- §14 (Recto UI backend). Superseded by Impossible OS implementing the listed tiers. Its acceptance tests move to Recto's import section.

## Who builds what, once?

Owner, 2026-11-27: "I do not want todo work in the OS repo and in the SDK. No double work." Impossible OS uses Impossible SDK for its own interface (desktop, Explorer, windows, dialogs) as well as for these apps, so every piece has exactly one home.

| Piece | Built once in | Used by |
| --- | --- | --- |
| Controls, layout, drawing, text, image parts | Impossible SDK | The OS shell, SDK programs, the three apps |
| C++ runtime (`cxx`: libc++ and friends) | Impossible SDK | The three apps and any C++ code in the OS |
| The list of Windows calls (`abi/tiers.json`) | Impossible SDK | Every program; the OS's roadmap reads it |
| The Windows calls themselves, Direct3D 11 included | Impossible OS | Every program, run unchanged |
| Kernel, drivers, GPU, exception dispatch, compositor | Impossible OS | Everything above |

So Impossible OS writes no second drawing library, text engine, control set or C++ runtime, and no per-app backend. The unit that moves is the portable obligation, not the whole section: where an OS section mixes portable work with host work, the portable part moves to the SDK item that owns it by the 2026-11-23 ledger rule (an `imports.json` row, the old text's hash, an import section left in its place), and the host work stays as an OS item. For example, `font_mgr.h` both loads the system fonts at boot (host work, stays) and draws glyphs (portable, moves to `text`). The OS's present `gfx.h` and `font_mgr.h` drawing and text keep working until the SDK's parts are imported; they are not grown further. The SDK publishes the list of calls and never implements one for Impossible OS; the OS implements the listed calls and never edits the list.

## How is a move judged?

Pixels and timings are compared separately, before and after, on the same machine. A move lands only when its pictures match within the app's own tolerance and its timings are no worse than the app's own targets. Recto's workload is its conformance set plus opening a 500-page file and rendering page 250 (`TODO-05` §13), and Isotone's is its own canvas target, measured through presentation: panning a 100-megapixel document at 37 degrees with a median frame under 16 ms (`docs/parity/gesso-section-design.md`). Its present `native/benchmarks` time only pixel premultiplication and cannot judge a canvas move. Office sets its targets with its first screens. Work the CPU cannot do within a target goes to the `gpu` tier.

## In what order?

There is no single queue. Each piece waits only for what it uses, and a missing SDK feature is built in the SDK by its first consumer, who then moves the app's `sdk.json` pin (the 2026-11-23 rule).

- **Everyone first:** the SDK's C++ support with its sample, the opaque `im_window`, and the boundary rules with their CPU-path planted cases. Every app move needs them.
- **A CPU screen** needs `window`, `draw` and `text` as public parts, nothing more. Office can start its first screen then, without waiting for the GPU or arm64 work.
- **A GPU surface** (Isotone's canvas, Recto's page tiles) needs the `gpu` tier. Only those surfaces wait for it.
- **Recto's arm64 release** needs the arm64 target. Only dropping Recto's present arm64 build waits for it.
- **Impossible OS** files the Office and Isotone imports, the GPU tier sections and the per-thread vector state need as soon as this record lands. It moves Recto's four sections (§11 to §14) only as each destination item above exists.

## What can go wrong?

| Risk | Answer |
| --- | --- |
| SDK drawing or text is slower than Direct2D | Judged per move as above; the GPU tier takes what the CPU cannot |
| Modules or exceptions fail on the SDK toolchain | The C++ sample proves them before any app moves |
| Impossible OS lacks Direct3D 11 for a long time | GPU features have CPU paths; the program still starts |
| Threads lose vector state on Impossible OS | Filed as an OS need above, before the apps run there |
| Recto loses arm64 | Recto keeps its arm64 build until the SDK's arm64 target is proved |

## What is left to the builder?

The names of the parts and their functions, the tier's number and exact method list (set by the two workloads above), the shader bytecode format, how emulated thread-local storage is done, and the order of surfaces inside each app's move.
