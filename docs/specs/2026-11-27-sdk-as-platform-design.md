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

The import check alone cannot prove it: it sees DLL and function names, not which component made a call, nor which COM method was used, nor names the SDK resolves at run time (`src/window.c` already resolves `DwmSetWindowAttribute` that way). So there are three checks, and an app's release passes all three.

1. **Imports.** The existing check (`tools/check_imports.py`) on the finished executable, against the app's tier.
2. **Source.** A new SDK check that an app's sources, and every library it links, include no Windows, COM or DirectX header and declare no Windows function; only `<impossible/...>` headers reach the system. Each app runs it in CI over its own tree.
3. **Inside the SDK.** Every name the SDK resolves at run time and every COM interface method it calls is listed in `abi/tiers.json` like an import. A test of the SDK fails when its code uses a run-time name or a method that is not listed. The `gpu` part reaches Direct3D only through a generated header that exposes the listed methods and nothing else.

## What does the SDK add?

1. **C++ programs.** Public headers compile as C++23 (`extern "C"`, no C-only constructs). A `cxx` part supplies the C++ runtime: LLVM libc++, libc++abi and libunwind, built on the SDK's base and linked statically, with exceptions and RTTI on, no Microsoft runtime DLL and no thread-local storage directory (`thread_local` is emulated). The SDK's entry point runs static constructors before `im_main` and static destructors and `atexit` handlers after it, before `ExitProcess`. It is the runtime Impossible OS already plans for user mode (`TODO-05` §11). An app's shipping build uses the SDK toolchain (`toolchain.json`, clang); an app may keep an MSVC build for its own tools, but that build does not ship. **Proof:** a C++ sample, built like an app, that imports a module, throws an exception two frames up through a live object whose destructor runs, catches it, uses `thread_local` from two threads, and runs a static constructor and destructor, all passing the three checks on Windows.
2. **A GPU tier.** A new tier after Desktop lists the Direct3D 11 and DXGI calls that are allowed, as functions, interfaces and methods: device and swap chain creation, buffers, textures, compiled shaders and the draw calls. Programs never touch `d3d11.dll` or `dxgi.dll`: the `gpu` part loads them at run time with `LoadLibraryW` and `GetProcAddress` (listed in the tier), so a system without them still starts the program. `im_gpu_open` then reports that no GPU is there, and the tier's rule is that every GPU feature has a CPU path. The tier's names are published like any import, so Impossible OS knows them. Shaders are compiled at build time, and the tier fixes their bytecode format. **Proof:** a GPU sample whose same executable draws a canvas on the GPU and, with the GPU forced absent, draws the same canvas on the CPU. The exact method list is set by two workloads: Recto's page tiles (its existing Direct3D and DirectComposition use) and a layered canvas sample shaped like Isotone's planned one. Controls stay on the CPU.
3. **Drawing and text for other toolkits.** `draw` and `text` become public parts with a stable interface that a toolkit outside the SDK can call, at the quality Direct2D and DirectWrite gave these apps. Their owners are the existing sections: paths, arcs and gradients (`D01 T01` §13), clips and layers (`D01 T01` §10), text in fixed point (`D01 T01` §5), and the text engine `D01 T02` §1 to §14, including shaping (§10 glyph substitution, §11 glyph positioning, §12 joining scripts, §13 Indic and Southeast Asian scripts), fallback (§8) and right-to-left text (§9). The font dialog is `D05 T03` §8. Publishing them as separately importable parts is `D08 T01` §1.
4. **arm64 Windows.** Recto ships arm64 today, and the SDK is x64 only (`cmake/toolchain.cmake` picks the x86-64 compiler and `tools/check_imports.py` refuses any other machine). An SDK section adds the arm64 Windows target, teaches the checks the arm64 machine type, and proves a sample on Windows arm64. Recto keeps its present arm64 build until that section is done.
5. **What the toolkits still lack.** Text input with an input method, screen reader support for a toolkit's own element tree, and printing, each as an ordinary SDK section where the existing plan puts it.
6. **A guide.** "Bring your own toolkit": how a C++ toolkit sits on the SDK, with the C++ and GPU samples.

## What does each repository do?

**Impossible SDK** builds the items above, each a section in its own `todo/` tree, and documents the new tier and the checks on the site.

**Recto** adds `sdk.json` and `app.json`, then moves `recto-core`, `recto-pdf`, `recto-ui` and Vellum off direct Windows, Direct2D and DirectComposition calls onto the SDK, one surface at a time, with its Windows tests green after each. Its release is built with the SDK toolchain and passes the three checks in CI.

**Isotone** does the same for `isotone::core` and `isotone::ui`, with its canvas on the `gpu` tier. It has no DirectX code yet, so new drawing starts on the SDK.

**Office** writes a new dated decision in its tech stack record: its toolkit is built on the SDK from its first line. It has `sdk.json` and `app.json` from its first program.

**Impossible OS** gets import files for Office and Isotone in the shape of `TODO-14-inkeys.md`, with `imports.json` rows, and roadmap sections for the GPU tier, shared with what Inkeys needs. It also files one need this design depends on: vector state is saved per process today, not per thread (`docs/research/ai-ml-runtime.md`; `todo/18-future-research/TODO-05-ai-ml-runtime.md` §2). The x64 baseline (SSE2) must be preserved per user thread before any of these apps runs threaded on Impossible OS, and AVX is optional, detected at run time.

Recto's sections `TODO-05` §11, §12 and §14 are not replaced when this record lands. Under the 2026-11-23 rule, a section moves only when its new item exists, with a ledger row and the hash of its old text. Their obligations map like this:

- §11 (C++ runtime). Building libc++, libc++abi and libunwind, and the C library pieces they need, moves to the SDK's `cxx` part; Impossible OS does not build its own. What only the OS can do stays there as the import section of the `cxx` part: running the unwinder on Impossible OS's exception dispatch, and the two-frame catch under the OS launcher.
- §12 (Recto platform seam). The seam itself moves to Recto's own move onto the SDK. `--seam-check` becomes the SDK's source check, run by Recto's CI and again on import.
- §14 (Recto UI backend). Superseded by Impossible OS implementing the listed tiers. Its acceptance tests move to Recto's import section.

## Who builds what, once?

Owner, 2026-11-27: "I do not want todo work in the OS repo and in the SDK. No double work." Impossible OS uses Impossible SDK for its own interface (desktop, Explorer, windows, dialogs) as well as for these apps, so every piece has exactly one home.

| Piece | Built once in | Used by |
| --- | --- | --- |
| Controls, layout, drawing, text, image parts | Impossible SDK | The OS shell, SDK programs, the three apps |
| C++ runtime (`cxx`: libc++, libc++abi, libunwind) | Impossible SDK | The three apps and any C++ code in the OS |
| The list of Windows calls (`abi/tiers.json`) | Impossible SDK | Every program; the OS's roadmap reads it |
| The Windows calls themselves, Direct3D 11 included | Impossible OS | Every program, run unchanged |
| Kernel, drivers, GPU, exception dispatch, compositor | Impossible OS | Everything above |

So Impossible OS writes no second drawing library, text engine, control set or C++ runtime, and no per-app backend. Where the OS roadmap still plans one, that section moves to the SDK item that owns it by the 2026-11-23 ledger rule (an `imports.json` row, the old text's hash, an import section left in its place), and is never built twice. The OS's present `gfx.h` and `font_mgr.h` drawing and text keep working until the SDK's parts are imported; they are not grown further. The SDK in turn never implements a Windows call for Impossible OS: what the OS must provide is the list, and only the OS writes it.

## How is a move judged?

Pixels and timings are compared separately, before and after, on the same machine. A move lands only when its pictures match within the app's own tolerance and its timings are no worse than the app's own targets. Recto's workload is its conformance set plus opening a 500-page file and rendering page 250 (`TODO-05` §13), and Isotone's is its `native/benchmarks`. Office sets its targets with its first screens. Work the CPU cannot do within a target goes to the `gpu` tier.

## In what order?

1. This record. Then the SDK's C++ support and its sample, and the source and SDK-internal checks.
2. `draw` and `text` as public parts. The `gpu` tier and its sample. The arm64 target.
3. Office starts its toolkit on the SDK. Isotone moves `isotone::ui`. Recto moves one surface at a time, engine included.
4. Impossible OS files the Office and Isotone imports, the GPU tier sections and the per-thread vector state need as soon as this record lands. It moves Recto's three sections only as each destination item above exists.

## What can go wrong?

| Risk | Answer |
| --- | --- |
| SDK drawing or text is slower or worse than Direct2D | Judged per move as above; the GPU tier takes what the CPU cannot |
| Modules or exceptions fail on the SDK toolchain | The C++ sample proves them before any app moves |
| Impossible OS lacks Direct3D 11 for a long time | GPU features have CPU paths; the program still starts |
| Threads lose vector state on Impossible OS | Filed as an OS need above, before the apps run there |
| Recto loses arm64 | Recto keeps its arm64 build until the SDK's arm64 target is proved |

## What is left to the builder?

The names of the parts and their functions, the tier's number and exact method list (set by the two workloads above), the shader bytecode format, how emulated thread-local storage is done, and the order of surfaces inside each app's move.
