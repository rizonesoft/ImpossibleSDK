# Impossible SDK as the platform for Recto, Office and Isotone

Status: approved approach 2026-11-27 (owner answers below); this record is the design. Facts about Recto were measured at `274f248`, Isotone at `ff1a42e`, Office at `dd4cbbc`, Impossible OS at its `main` on 2026-11-27.

## Why are we doing this?

On 2026-11-22 the owner made Recto, Office and Isotone permanent apps of Impossible OS, under one rule: each is made to work on Windows first, then Impossible OS is made compatible with it (`impossible-os` `todo/11-apps/INDEX.md`). All three are C++23 programs that draw with their own toolkit straight on Win32, Direct2D, DirectWrite and, for Recto, DirectComposition. Running them unchanged would make Impossible OS rebuild most of DirectX first. Recto's OS plan instead gives Recto a private platform seam and an Impossible OS backend of its own (`impossible-os` `todo/11-apps/TODO-05-pdf-viewer.md` §12, §14). Doing that three times means three seams and three OS backends.

Impossible SDK already is such a seam for its own programs: a program calls only the Windows functions in `abi/tiers.json`, the build fails on any other import, and every listed function becomes an Impossible OS roadmap section. So the shared seam is Impossible SDK itself.

## What are the goals?

- Recto, Office and Isotone each call the operating system only through Impossible SDK, and their release builds pass the SDK's import check.
- Each keeps its own toolkit, controls and look: Vellum, `isotone::ui` and Office's toolkit draw on the SDK, not on Direct2D.
- The same source runs on Windows now and on Impossible OS later, with no Impossible OS backend written for any one app.
- Impossible OS gets one finite list of what these apps need, including an exact Direct3D 11 subset.

**Not goals:** running unmodified DirectX programs (that is the OS compatibility ladder, tier 9 of `impossible-os` `docs/sdk/win32-compat-matrix.md`), moving any app onto Impossible UI's controls, or merging the three toolkits.

**It has failed if** an app's release build imports a function that is not on the list, Impossible OS needs a special case for one app, any of the three waits for Impossible OS, or a feature is written in two places.

## Which approaches were considered?

| Approach | Verdict |
| --- | --- |
| One shared seam, in Impossible SDK (chosen 2026-11-27) | One contract, one checker, one OS target; the apps move their platform calls once |
| A seam per app, as Recto's OS plan has now | Three interfaces and three OS backends to keep |
| Impossible OS runs them unchanged | Needs Direct2D, DirectWrite, Direct3D and DirectComposition on the OS first; these apps run there last |
| All three move to Vellum | Replaces Isotone's working `isotone::ui`; largest change to the apps |

## Which decisions did the owner make?

- 2026-11-22: the three are permanent Impossible OS apps, Windows first.
- 2026-11-27: one shared seam, and it is built into Impossible SDK ("Can't we build the shared seam into Imposible SDK?").
- 2026-11-27: apps that need the GPU get a Direct3D 11 tier in the SDK.
- 2026-11-27: plan entries go into all the repositories involved.

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
                            |  only functions in abi/tiers.json
                 +----------+-----------+
              Windows              Impossible OS
           (today, proved)   (each function is a roadmap section)
```

An app's toolkit keeps its element tree, layout, controls and styling. Everything below that (making a window, reading input, rasterising shapes, shaping and drawing text, putting pixels or a GPU surface on screen, files, threads, clipboard) is an SDK call. The SDK's Windows layer is the only code that calls Windows.

## What does the SDK add?

1. **C++ programs.** Public headers compile as C++23 (`extern "C"`, no C-only constructs), and a `cxx` part supplies the C++ runtime: LLVM libc++, libc++abi and libunwind, built on the SDK's base and linked statically, with exceptions and RTTI on. It needs no Microsoft runtime DLL and no thread-local storage directory (the import check refuses one), so `thread_local` is emulated. It is the same runtime Impossible OS already plans for user mode (`TODO-05` §11), so the OS imports it instead of building a second one. An app's shipping build uses the SDK toolchain (`toolchain.json`, clang); whether an app also keeps an MSVC build for its own tools is its choice, but that build does not ship.
2. **A GPU tier.** A new tier after Desktop lists the Direct3D 11 and DXGI calls that are allowed: device and swap chain creation, buffers, textures, compiled shaders and the draw calls, as interfaces and methods. Programs never import `d3d11.dll` or `dxgi.dll` themselves: the `gpu` part does, behind a C interface, so the import check still works for apps, and a test of the `gpu` part proves it uses no method that is not listed. Shaders are compiled at build time; their bytecode format is fixed by this tier. Controls stay on the CPU; the GPU is for canvases, page tiles and effects.
3. **Drawing and text for other toolkits.** The `draw` and `text` work already planned for Impossible UI (`D01 T01` §5, §13; `D01 T02` §1, §7 to §9) is published as parts with a stable interface that a toolkit outside the SDK can call, at the quality Direct2D and DirectWrite gave these apps: anti-aliased paths, gradients, image scaling, shaping, fallback, right-to-left text and the font dialog.
4. **What the toolkits still lack.** Text input with an input method, screen reader support for a toolkit's own element tree, printing, and arm64 Windows builds (Recto ships arm64 today), each as an ordinary SDK section in the place the existing plan gives it.
5. **A guide.** "Bring your own toolkit": how a C++ toolkit sits on the SDK, with a sample that draws through `draw`, `text` and `gpu`.

## What does each repository do?

| Repository | Work |
| --- | --- |
| Impossible SDK | The five items above, each a section in its own `todo/` tree; the guide; the tier documented on the site |
| Recto | Add `sdk.json` and `app.json`; move `recto-ui` and Vellum off Direct2D and DirectComposition onto the SDK; build the release with the SDK toolchain; make the import check part of CI |
| Isotone | The same for `isotone::ui`, the canvas on the `gpu` tier; Isotone has no DirectX code yet, so it starts on the SDK |
| Office | A new dated decision in its tech stack record: the toolkit is built on the SDK from its first line (it has no code yet); `sdk.json` and `app.json` from the first program |
| Impossible OS | `TODO-05` §11, §12 and §14 are superseded by importing the SDK's `cxx` part and running Recto as an SDK program; Office and Isotone get import files in the shape of `TODO-14`, with `imports.json` rows; the GPU tier gets its roadmap sections, shared with what Inkeys needs |

None of the apps waits: each is finished when it works on Windows, as the 2026-11-23 design says. An SDK feature an app needs first is built in the SDK by that app's session, then the app moves its `sdk.json` pin.

## In what order?

1. This record, and the SDK's C++ support with a C++ sample that passes the import check.
2. `draw` and `text` as public parts, and the `gpu` tier with its sample.
3. Office starts its toolkit on the SDK; Isotone moves `isotone::ui`; Recto moves `recto-ui`, one surface at a time, keeping its Windows tests green.
4. Impossible OS replaces Recto's seam sections and adds the Office and Isotone import files as soon as this record lands; it implements the tiers when its plan reaches them.

## What can go wrong?

| Risk | Answer |
| --- | --- |
| The SDK's drawing or text is slower or worse than Direct2D and DirectWrite | Each app compares its own pages and screens before and after, and a move lands only when it matches; the GPU tier takes what the CPU cannot |
| C++ modules or exceptions do not build on the SDK toolchain | Item 1 is proved with a module-using C++ sample before any app moves |
| Impossible OS cannot run the Direct3D 11 subset for a long time | The apps still run on Windows; on Impossible OS a GPU-only feature is reported as a need and the app works without it, as the 2026-11-23 rules say |
| The apps' compute code uses floating point and SIMD | Allowed in programs (only Impossible UI is integer-only); Impossible OS saves SSE and AVX state for every user thread, which any x64 Windows program needs |
| Recto loses arm64 | The SDK adds the arm64 Windows target before Recto drops its Direct2D path |

## What is left to the builder?

The names of the parts and their functions, the tier's number and exact method list (measured from what the three apps use), the shader bytecode format, how emulated thread-local storage is done, and the order of surfaces inside each app's move.
