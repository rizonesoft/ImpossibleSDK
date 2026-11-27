---
schema_version: 1
id: gpu-tier
domain: 09-platform
status: active
title: "TODO-03 -- The GPU Tier"
---

# TODO-03 -- The GPU Tier

> **Goal:** A program that needs the GPU gets a Direct3D 11 and DXGI subset through a `gpu` part, listed in a GPU tier as functions, resolved names and methods; it still starts and still draws on the CPU when there is no GPU, and it survives losing the device while running.

> **Current state:** Measured 2026-11-27 at `8d5bf9a`. `abi/tiers.json` has tiers 0 to 5 and no Direct3D or DXGI entry, no `gpu` part exists in `parts.json`, and no code in `src/` or `ui/` touches the GPU. Recto's page tiles use Direct3D and DirectComposition today (9 source files include Direct2D, DirectWrite, Direct3D or DirectComposition headers), and Isotone plans a layered canvas. Impossible OS owns the Direct3D 11 implementation; this file publishes the list it must implement and never implements a call for it.

## Inputs

- `docs/specs/2026-11-27-sdk-as-platform-design.md`: "What does the SDK add?" item 2, rules 7 and 8, and the GPU planted cases.
- `todo/09-platform/TODO-02-boundary.md` §5 (record kinds), §6 (private import library), §8 (private names), §13 (effective tier).
- `src/window.c`: the pattern for a run-time lookup (`LoadLibraryW`, `GetProcAddress`) listed in `resolved`.
- -> XREF: D09 T02 §5 -- the `resolved` and `methods` record kinds this tier uses
- -> XREF: D09 T02 §13 -- the effective tier that makes a Desktop-tier program linking `gpu` fail
- -> XREF: D09 T05 §6 -- the guide that shows the GPU sample
- `impossible-os` `todo/10-platform-services/TODO-13-program-needs.md` §7, §8, §9 -- Impossible OS implements this tier's functions, resolved names and methods, shared with Inkeys; it reads them from `abi/tiers.json` and never edits the list
- Consumers: Recto `todo/00-workspace/TODO-06-impossible-sdk-platform.md` §9 (page tiles), Isotone `todo/01-core/TODO-28-foundation-sdk-platform.md` §2 and §3 (the canvas)

## Outcome

- A GPU sample draws a canvas on the GPU, and the same executable reaches the same canvas on the CPU in three start-up failure cases and two device-loss cases.
- Impossible OS has the tier's functions, resolved names and methods in `abi/tiers.json` and the page that explains them, and a `gpu` call never needs a Windows header.

**Adjacency:** all=not-applicable (a graphics subset holds no records, settings, reports or notices)

**Adjacency rationale:** Programs own their canvases; the part draws and presents and keeps nothing.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | The GPU tier entry in the tier list | D09 T02 §5 | [ ] |
| 2 | §2 | Method list set by the two workloads | §1 | [ ] |
| 3 | §3 | Shader bytecode format and its compiler | §1 | [ ] |
| 4 | §4 | Generated header of the listed methods | §2, D09 T02 §6 | [ ] |
| 5 | §5 | The `gpu` part and its run-time loading | §4, D09 T02 §8 | [ ] |
| 6 | §6 | GPU devices, resources and draw calls | §3, §5 | [ ] |
| 7 | §7 | Lost status on every call that can see it | §6 | [ ] |
| 8 | §8 | Test hook for removing the device | §6 | [ ] |
| 9 | §9 | GPU proof sample and start-up failures | §6, §7, D01 T01 §9 | [ ] |
| 10 | §10 | Device loss at present and on resize | §8, §9 | [ ] |
| 11 | §11 | Effective tier and the GPU planted cases | §5, §10, D09 T02 §13, D09 T02 §14 | [ ] |
| 12 | §12 | GPU tier docs and the OS need | §2, §11 | [ ] |

## 1. The GPU tier entry in the tier list

A new tier after Desktop that lists the Direct3D 11 and DXGI calls allowed. The tier's number is the next free one in `abi/tiers.json`.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the tier to `abi/tiers.json` with empty `functions`, `resolved` and `methods` kinds. Done when: `tools/tiers.py` reads it.
2. Fill the kinds in §2. Done when: the first sample's calls are all listed.

- [ ] Add a tier named GPU after Desktop in `abi/tiers.json`. Done when: `tools/check_imports.py --tier` accepts its number and the tiers before it still pass unchanged. Cheaper substitute: adding the calls to Desktop.
- [ ] List `LoadLibraryW` and `GetProcAddress` as the tier's `functions`, and `D3D11CreateDevice`, `CreateDXGIFactory1` and the other entry points in `resolved`. Done when: `tools/tiers.py` reports each under its kind and DLL. Cheaper substitute: importing `d3d11.dll`.
- [ ] State in the tier's `about` that programs never import `d3d11.dll` or `dxgi.dll` and that every GPU feature has a CPU path. Done when: `tools/site/build.py` shows the text. Cheaper substitute: a comment.
- [ ] Test that a program importing `d3d11.dll` fails at every tier in `tests/test_seed.py`. Done when: the diagnostic names the DLL and the tier. Cheaper substitute: reading the table.
- [ ] Commit: `abi: the GPU tier`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the import test fails when `d3d11.dll` is added to the tier's `functions`.

## 2. Method list set by the two workloads

The exact method list is the union of what two workloads need and nothing more: Recto's page tiles (its Direct3D and DirectComposition use) and a layered canvas sample shaped like Isotone's planned one.

**Phase:** 7
**Needs:** none
**Surface:** no

**Build order:**

1. Measure each workload's calls. Done when: `docs/gpu-workloads.md` lists them with a source for each.
2. Write the union into `methods` in `abi/tiers.json`. Done when: every method has a workload that needs it.

- [ ] Record Recto's page tile calls from its pinned source in `docs/gpu-workloads.md` (interface, method, why). Done when: the list cites the file and line of each use at the pinned commit. Cheaper substitute: a guess from the API.
- [ ] Record the layered canvas sample's calls from `samples/gpu/` in `docs/gpu-workloads.md`. Done when: each method is one the sample's source calls. Cheaper substitute: the full API.
- [ ] Write the union into `methods` in `abi/tiers.json`, by interface, with the feature level the tier requires. Done when: `tools/tiers.py` lists each method under its interface and a method with no workload fails the check. Cheaper substitute: all of `ID3D11DeviceContext`.
- [ ] Add a test that a method is dropped from `methods` only when no workload needs it, in `tests/test_seed.py`. Done when: removing a needed method fails the test. Cheaper substitute: review.
- [ ] Commit: `abi: the GPU tier's method list`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the workload test fails when `methods` lists a method no workload calls.

## 3. Shader bytecode format and its compiler

Shaders are compiled when the program is built, and the tier fixes the bytecode format, so no compiler runs on the user's machine and Impossible OS knows what to load.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Choose and record the format in `docs/gpu-workloads.md` and `abi/tiers.json`. Done when: the tier names it.
2. Add `tools/shaders.py` that builds shaders in the program's build. Done when: a `.hlsl` file becomes a checked byte array.

- [ ] Fix the bytecode format and shader model in `abi/tiers.json` `about` and `docs/gpu-workloads.md`. Done when: both state the same format and `tools/tiers.py` exposes it. Cheaper substitute: leaving it to the compiler.
- [ ] Write `tools/shaders.py` that compiles `.hlsl` with the pinned compiler in `toolchain.json` and writes a generated `.c` array. Done when: the output is identical across two builds and `build/objects.json` records it. Cheaper substitute: a prebuilt blob.
- [ ] Add `impossible_add_shader` to `cmake/ImpossibleSDK.cmake`. Done when: the sample builds its shaders through it. Cheaper substitute: running the tool by hand.
- [ ] Refuse a shader whose bytecode is not in the tier's format in `tools/shaders.py`. Done when: a bytecode of another version fails with the version named. Cheaper substitute: loading and seeing.
- [ ] Commit: `tools: build-time shaders in the tier's format`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the shader test fails when the format in `abi/tiers.json` changes.

## 4. Generated header of the listed methods

The `gpu` part reaches Direct3D only through a generated header that declares the listed methods and no others.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Extend `tools/gen_imports.py` to emit `src/gpu/im_d3d.h`. Done when: it declares each listed method.
2. Make the header the only declaration the part sees. Done when: the part builds without the Windows SDK headers.

- [ ] Emit the interfaces, vtable layouts and GUIDs of the listed methods into `src/gpu/im_d3d.h` from `abi/tiers.json`. Done when: calling a method not in `methods` does not compile. Cheaper substitute: including `<d3d11.h>`.
- [ ] Check each vtable slot against the real layout in `tests/test_boundary.py` on a Windows host. Done when: every slot index equals the one in the pinned headers. Cheaper substitute: trusting the generator.
- [ ] Keep the header out of every part but the Windows half of `gpu`, in `tools/check_parts.py`. Done when: including it elsewhere fails. Cheaper substitute: a comment.
- [ ] Commit: `gpu: the generated method header`

**Test checkpoint:** `python3 tools/build.py` exits 0 and a call to an unlisted method fails to compile.

## 5. The `gpu` part and its run-time loading

The `gpu` part loads `d3d11.dll` and `dxgi.dll` at run time, so a system without them still starts the program.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `gpu` to `parts.json` with `tier` set to the GPU tier. Done when: `tools/check_parts.py` accepts it.
2. Write `src/gpu/gpu_win.c` and `include/impossible/gpu.h`. Done when: `im_gpu_open` reports no GPU when loading fails.

- [ ] Declare `gpu` in `parts.json` with its tier and the Windows half in `windows_layer`. Done when: `tools/check_parts.py` accepts it and the portable half is checked as portable. Cheaper substitute: one folder.
- [ ] Write `include/impossible/gpu.h` with opaque `im_gpu` and no Windows type, marked as `D09 T02 §10` asks. Done when: `tools/header_check.py` passes. Cheaper substitute: handles in the header.
- [ ] Load `d3d11.dll` and `dxgi.dll` with `LoadLibraryW` and `GetProcAddress` in `src/gpu/gpu_win.c`. Done when: each name is in `resolved` and the lookup test of `D09 T02 §5` passes. Cheaper substitute: import-table linking.
- [ ] Return a no-GPU status from `im_gpu_open` when the DLL, an export or the feature level is missing, in `src/gpu/gpu_win.c`. Done when: each of the three cases returns its own status and the program keeps running. Cheaper substitute: aborting.
- [ ] Commit: `gpu: the part and its run-time loading`

**Test checkpoint:** `python3 tools/build.py` exits 0 and `tools/check_imports.py` on a program linking `gpu` shows no `d3d11.dll` import.

## 6. GPU devices, resources and draw calls

What the workloads use, behind a small C interface in `include/impossible/gpu.h`.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `src/gpu/device.c`. Done when: a device and swap chain are created on a window.
2. Write `src/gpu/resources.c` and `src/gpu/draw.c`. Done when: a textured quad is drawn.

- [ ] Create the device and a swap chain for an `im_window` through its Windows half in `src/gpu/device.c`. Done when: a cleared window presents its colour, read back by pixels. Cheaper substitute: a hidden device.
- [ ] Create vertex, index and constant buffers and 2D textures in `src/gpu/resources.c`. Done when: uploaded bytes read back equal. Cheaper substitute: one buffer kind.
- [ ] Load compiled shaders from the bytes of §3 in `src/gpu/resources.c`. Done when: a shader of the wrong format is refused with a status. Cheaper substitute: running it.
- [ ] Draw and present in `src/gpu/draw.c`. Done when: a textured quad equals its CPU rendering within the sample's tolerance. Cheaper substitute: a clear.
- [ ] Resize the swap chain in `src/gpu/device.c`. Done when: resizing 1,000 times leaves no leaked resource. Cheaper substitute: recreating the window.
- [ ] Commit: `gpu: devices, resources and draw calls`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the pixel-compare test fails when `src/gpu/draw.c` swaps two channels.

## 7. Lost status on every call that can see it

Any `gpu` call that can see the device lost (presenting, resizing the swap chain, creating a resource) returns a lost status on the calling thread; the device and everything made on it are then invalid.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `IM_GPU_LOST` in `include/impossible/gpu.h`. Done when: it is documented with the recovery rule.
2. Return it from each call. Done when: a table of calls and loss points is covered.

- [ ] Map the device-removed and device-reset results to `IM_GPU_LOST` in `src/gpu/device.c`, `src/gpu/resources.c` and `src/gpu/draw.c`. Done when: each call that can see them returns it on the calling thread. Cheaper substitute: a flag read later.
- [ ] Mark every `im_gpu` object invalid after a loss in `src/gpu/device.c`. Done when: using one after a loss returns `IM_GPU_LOST` and never touches the dead device. Cheaper substitute: undefined behaviour.
- [ ] Add `im_gpu_recreate` in `src/gpu/device.c`. Done when: a new device and resources are made after a loss and the program draws again. Cheaper substitute: restarting the program.
- [ ] Document that the program recreates or carries on with its CPU path, in `include/impossible/gpu.h`. Done when: the site build shows the rule above the function. Cheaper substitute: the design record.
- [ ] Commit: `gpu: a lost status on every call`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the loss test fails when `src/gpu/draw.c` returns success after a removed device.

## 8. Test hook for removing the device

The Windows half of the `gpu` part carries a test hook that removes the device on demand, so loss can be tested at presentation and during a resize.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/gpu/test_hook.c` compiled into test programs only. Done when: a shipping target referencing it fails to link.
2. Use it in `tests/fixtures/test_gpu.c`. Done when: the device is removed on request.

- [ ] Add `im_test_gpu_remove_device(im_gpu *, when)` in `src/gpu/test_hook.c`, built only through `impossible_add_test_program` of `D09 T02 §1`. Done when: a shipping target using it fails to link. Cheaper substitute: an environment variable.
- [ ] Choose `when` among next present, next resize and now, in `src/gpu/test_hook.c`. Done when: each fires its loss at that call and no other. Cheaper substitute: now only.
- [ ] Use the supported removal call of the device, listed in `methods`, in `src/gpu/test_hook.c`. Done when: the hook adds no unlisted method. Cheaper substitute: killing the driver.
- [ ] Commit: `gpu: a test hook to remove the device`

**Test checkpoint:** `python3 tools/build.py` exits 0 and a shipping target that references the hook fails to link.

## 9. GPU proof sample and start-up failures

One executable draws a canvas on the GPU and, through its ordinary start-up path, reaches the same canvas on the CPU in three failure cases made at the start-up boundary.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `samples/gpu/main.c` (a layered canvas) with a CPU path. Done when: both paths draw the same canvas.
2. Add the three failure cases through the same start-up code. Done when: each reaches the CPU canvas.

- [ ] Draw the canvas on the GPU and on the CPU in `samples/gpu/main.c` from one scene description. Done when: the two frames are equal within the stated tolerance. Cheaper substitute: two different scenes.
- [ ] Make the case where `d3d11.dll` cannot be loaded, by a loader hook of the test build in `src/gpu/test_hook.c`. Done when: the sample starts, draws on the CPU and prints which path it took. Cheaper substitute: renaming the DLL on disk.
- [ ] Make the case where it loads but a required export is missing, in the same hook. Done when: the sample starts and draws on the CPU. Cheaper substitute: a fake DLL.
- [ ] Make the case where device creation fails because no adapter offers the required feature level, in the same hook. Done when: the sample starts and draws on the CPU. Cheaper substitute: forcing an error code later in the call.
- [ ] Assert all three in `tests/test_gpu.py` by the sample's printed path and by the canvas bytes. Done when: the canvas equals the CPU canvas in each case and the test fails when the CPU path is removed. Cheaper substitute: checking the exit code.
- [ ] Commit: `samples: the GPU proof sample`

**Test checkpoint:** `python3 tests/test_gpu.py` exits 0 and fails when `samples/gpu/main.c` has no CPU path.

## 10. Device loss at present and on resize

The runtime loss case: with the sample's canvas open, the device is removed, once at presentation and once during a resize, and the canvas must keep drawing.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Add the two cases to `samples/gpu/main.c` and `tests/test_gpu.py`. Done when: each removes the device with §8's hook.
2. Assert the canvas keeps drawing. Done when: the frame after each loss equals the CPU canvas or the recreated GPU canvas.

- [ ] Remove the device at presentation in `tests/test_gpu.py`. Done when: the next frame returns `IM_GPU_LOST`, the sample recreates or falls back, and the canvas bytes equal the expected frame. Cheaper substitute: removing between frames.
- [ ] Remove the device during a resize in `tests/test_gpu.py`. Done when: the same holds and no resource leaks. Cheaper substitute: a resize without loss.
- [ ] Prove the program recreates on a loss in `samples/gpu/main.c`. Done when: after `im_gpu_recreate` succeeds the sample draws on the GPU again. Cheaper substitute: CPU only.
- [ ] Prove the program carries on when recreation fails in `samples/gpu/main.c`. Done when: with creation forced to fail the sample stays on the CPU path. Cheaper substitute: exiting.
- [ ] Commit: `gpu: survive device loss at presentation and resize`

**Test checkpoint:** `python3 tests/test_gpu.py` exits 0 and fails when `samples/gpu/main.c` ignores `IM_GPU_LOST`.

## 11. Effective tier and the GPU planted cases

With the first GPU consumer, a Desktop-tier app linking `gpu` must fail, and a correctly declared GPU program must pass through packaging.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Declare the GPU tier on the `gpu` part in `parts.json`. Done when: `tools/tiers.py` reports it.
2. Add the two cases to `tests/test_boundary.py`. Done when: both are asserted.

- [ ] Plant a Desktop-tier program that links `gpu` in `tests/fixtures/boundary/gpu_desktop.c`. Done when: the build and `tools/app_build.py` both fail with the effective tier and the part named. Cheaper substitute: checking imports only.
- [ ] Add a correctly declared GPU program to `tests/fixtures/boundary/gpu_ok.c`. Done when: it builds, passes `tools/check_imports.py` at the GPU tier and passes packaging. Cheaper substitute: the sample alone.
- [ ] Plant a GPU-part object that names a private name outside the Windows half. Done when: it fails with the private-name diagnostic. Cheaper substitute: the portable-part case.
- [ ] Commit: `tests: the GPU planted cases`

**Test checkpoint:** `python3 tests/test_boundary.py` exits 0 and the Desktop case fails (the planted program is wrongly accepted) when the tier is removed from the `gpu` entry in `parts.json`.

## 12. GPU tier docs and the OS need

The site documents the tier, and the OS gets what it needs to implement.

**Phase:** 7
**Needs:** none
**Surface:** no

**Build order:**

1. Write `docs/guide/gpu.md`. Done when: the site build compiles its programs.
2. Write the OS need in `docs/impossible-os-gpu.md`. Done when: it lists functions, resolved names and methods from `abi/tiers.json`.

- [ ] Write `docs/guide/gpu.md` with the tier, its rule that every feature has a CPU path, the lost status and the sample. Done when: `tools/site/build.py --check` passes. Cheaper substitute: the design record.
- [ ] Generate `docs/impossible-os-gpu.md` from `abi/tiers.json` in `tools/gen_imports.py`. Done when: it lists each function, resolved name and method under its interface and regeneration is clean. Cheaper substitute: a hand list.
- [ ] State in it that reading `resolved` and `methods` is a change to the OS's `scripts/imports.py --needs`, made in the OS import section of the tier and reporting an unprovided method under its interface. Done when: the page says so and the SDK edits nothing in the OS repository. Cheaper substitute: editing the OS repository.
- [ ] Commit: `docs: the GPU tier and the OS need`

**Test checkpoint:** `python3 tools/site/build.py --check` exits 0 and `python3 tools/gen_imports.py --check` fails when `abi/tiers.json` changes without regeneration.

## Verification

- `tests/test_gpu.py` passes: three start-up failure cases and two device-loss cases reach a drawn canvas.
- A Desktop-tier program linking `gpu` fails the build and packaging, and a correctly declared GPU program passes both.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
