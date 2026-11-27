## Round 1: astra (`gpt-6.2-astra` via `codex`)

- Writer: claude
- Run: 2026-11-27T08:50:44Z
- Candidate: 7f3664b13c2ecd62469be5ba8502a397c693fc5a
- Effort: medium
- Lenses: adversarial, integration, source-defect, consistency, quality, optimization
- Lines: 40
- Digest: 7aa558fa85f1f7f6

Reviewed commit `7f3664b` against its parent. All candidate line references below are to `docs/specs/2026-11-27-sdk-as-platform-design.md`. OS references are to the local checkout at `180284002`.

## Adversarial: needs-attention

1. **P1 | Design record:63 | The release check cannot enforce the GPU boundary.** **Path:** An app or one of its linked libraries calls an allowed D3D export directly, or calls an unlisted COM method on a device. The release can pass while depending on behavior absent from the OS roadmap. **Evidence: traced** `cmake/ImpossibleSDK.cmake:95` checks the final executable. `tools/check_imports.py:85` compares DLL and export names, which identify neither the calling object nor a COM vtable method. There is already a concrete example of the visibility limit at `src/window.c:212`, where the SDK resolves `DwmSetWindowAttribute` by name at runtime. Specify how the build verifies which component owns platform calls and how it checks method use across the release, including paths the GPU sample never executes.

## Integration: needs-attention

2. **P1 | Design record:93 | The promised GPU fallback can fail before startup.** **Path:** Isotone ships its GPU canvas on an OS that implements the lower tiers but lacks the D3D DLL or a required creation export. With an ordinary import from the statically linked GPU part, the loader must resolve that import before the app can select its CPU path. **Evidence: traced** line 63 assigns the imports to the part, `cmake/ImpossibleSDK.cmake:34` builds SDK parts as a static library, and `tools/check_imports.py:94` rejects delay imports. Define an optional loading contract, including how the OS learns the optional exports, and require the same shipping executable to open a usable canvas with the GPU provider absent.
3. **P2 | Design record:62 | The C++ runtime has no startup or shutdown contract.** **Path:** A toolkit registers a type in a namespace-scope constructor or relies on a static object's destructor at normal exit. A module-only sample can pass while neither lifecycle hook runs. **Evidence: traced** `cmake/ImpossibleSDK.cmake:88` selects `-nostdlib` and `impossible_start`. `src/start.c:5` calls `im_main` and immediately passes its result to `ExitProcess`. Assign initialization and teardown to the SDK entry point and exercise both through the normal app target.
4. **P2 | Design record:73 | Recto's migration omits its engine.** **Path:** Recto finishes the assigned `recto-ui` and Vellum work while `recto-core` and `recto-pdf` still call Windows directly. Calls already on the tier list can pass release CI. **Evidence: traced** OS `todo/11-apps/TODO-05-pdf-viewer.md:204` assigns file mapping, atomic save and the render pool to the seam. Lines 205 and 207 explicitly cover all three libraries and check the platform boundary. The candidate supersedes that section at line 76 but assigns only the UI migration. Carry the engine work and its source-boundary check to named destinations.
5. **P2 | Design record:65 | The ARM64 release has no end-to-end delivery item.** **Path:** Recto produces its existing Windows ARM64 release with the new target and submits it to mandatory SDK import checking. **Evidence: traced** `cmake/toolchain.cmake:10` selects the x86-64 compiler, and `tools/check_imports.py:38` explicitly rejects every machine other than `0x8664`. **Evidence: ran** `git grep -inE 'arm64|aarch64' 7f3664b -- todo` found only the generic host capability in `todo/README.md:73`. Give target selection, import validation and a Windows ARM64 run an actual plan item before retiring Recto's current path.

## Source-defect: needs-attention

6. **P1 | Design record:94 | Impossible OS does not preserve SIMD state per user thread.** **Path:** Two rendering workers in one process retain different vector values across a thread switch. Treating the capability as delivered risks incorrect calculations on the exact workload the record authorizes. **Evidence: traced** OS `docs/research/ai-ml-runtime.md:13` describes one saved vector state per process, and line 47 explicitly says per-thread saves have not shipped. OS `todo/02-kernel-core/TODO-09-x86-64-architecture.md:17` names the outstanding scheduler section. Record per-thread preservation as an OS need and distinguish the x64 baseline from optional AVX.
7. **P1 | Design record:85 | Landing the record is too early to replace Recto's roadmap sections.** **Path:** The OS lane follows step 4 as written and replaces three sections before their destination items exist. Native unwind integration and the OS runtime acceptance tests can disappear even if the Windows sample passes. **Evidence: traced** the governing `docs/specs/2026-11-23-interface-and-apps-design.md:121` requires a destination item and a ledger row with the old text hash for every moved section. OS `todo/11-apps/TODO-05-pdf-viewer.md:187` owns native exception dispatch, and line 192 requires a two-frame catch under the OS launcher. Candidate line 76 calls for import rows for Office and Isotone but gives no equivalent obligation mapping for the three superseded sections. Preserve their addresses and checkpoints until each moved obligation has a destination.

## Consistency: needs-attention

8. **P2 | Design record:64 | The cited plan sections do not deliver the promised text surface.** **Path:** A builder publishes the cited font lookup, variable font, fallback and bidi work, then an external toolkit uses it to lay out joining-script text. The listed sections can all be complete while shaping remains incomplete. **Evidence: traced** `todo/01-interface/TODO-02-text.md:245` begins glyph substitution, line 267 begins glyph positioning, and line 288 begins joining scripts. The font dialog has a separate owner at `todo/05-controls/TODO-03-dialogs.md:246`. The independently buildable `draw` and `text` parts already have a delivery section at `todo/08-release/TODO-01-parts-and-release.md:47`. Reference those owners explicitly.
9. **P3 | Design record:26 | Eleven table rows exceed the repository's 120-character limit.** **Evidence: ran** `git show 7f3664b:docs/specs/2026-11-27-sdk-as-platform-design.md | awk '/^\|/ && length>120 {print NR}'` reported lines 26, 28, 72–76 and 91–94. `AGENTS.md:29` sets the limit. The long repository assignments and risk responses would fit better as prose.

## Quality: needs-attention

10. **P2 | Design record:92 | The proposed C++ proof can pass with broken exception handling.** **Path:** The sample imports a module and returns normally. It builds, runs and passes import checking even if an exception cannot unwind a frame or destroy a live object. **Evidence: traced** candidate line 82 requires a passing import check, and line 92 adds module use. Neither requires a throw. OS `todo/11-apps/TODO-05-pdf-viewer.md:192` supplies a concrete two-frame catch checkpoint. Exercise a cross-frame throw, destructor and catch in the shipping configuration.
11. **P3 | Design record:7 | The app inventory contradicts the proposed measurement method.** Line 7 says all three apps already draw with Direct2D and DirectWrite. Lines 74 and 75 say Isotone has no DirectX code and Office has no code. Line 99 nevertheless delegates the exact D3D11 subset to measurement of current app use. Reading those four lines together reproduces the contradiction. Separate observed calls from planned rendering workloads and identify which sample will establish the latter's method requirements.

## Optimization: needs-attention

12. **P2 | Design record:91 | The performance comparison cannot decide whether a migration passes.** **Path:** Recto's replacement renderer produces a visually identical page but takes longer to render it. Comparing screens can accept the regression, and the record gives no workload or timing tolerance for the decision to move work to the GPU. **Evidence: traced** OS `todo/11-apps/TODO-05-pdf-viewer.md:229` already identifies a concrete workload, opening a 500-page document and rendering page 250 against Recto's target. Carry that workload and its owning target into the migration, and compare timings separately from pixels. No runtime slowdown was measured in this documentation-only change.

LENS adversarial: findings  
LENS integration: findings  
LENS source-defect: findings  
LENS consistency: findings  
LENS quality: findings  
LENS optimization: findings  
HIGHEST: P1  
OVERALL: needs-attention

## Round 2: astra (`gpt-6.2-astra` via `codex`)

- Writer: claude
- Run: 2026-11-27T08:54:35Z
- Candidate: fa47317aa7c1703874f66a59d5073b6f4ffc3d53
- Effort: medium
- Lenses: adversarial, integration, source-defect, consistency, quality, optimization
- Lines: 35
- Digest: fb2f327a01f4f1c5

Reviewed `7f3664b1..fa47317a`. Candidate line references below are to `docs/specs/2026-11-27-sdk-as-platform-design.md`. OS references are pinned to `180284002`.

## Adversarial: needs-attention

1. **P1 | Design record:72 | A linked dependency can bypass the SDK while all three checks pass.** **Path:** An app links a static library built outside the app checkout. One of its objects calls `CreateFileW` directly. **Evidence: traced** Line 72 scopes CI to the app’s own tree. The final executable check at `tools/check_imports.py:85` sees an allowed import (`abi/tiers.json:13`), and line 73 checks only SDK code. None establishes who made the call. Require the release check to account for every linked object, including generated and external inputs, and exercise rejection with a dependency that directly calls an allowed export.

## Integration: needs-attention

2. **P1 | Design record:73 | A GPU executable can pass with a declared tier below GPU.** **Path:** An app adds the SDK GPU part to an existing Desktop target and keeps its old tier declaration. **Evidence: traced** Line 78 resolves the provider dynamically. Its loader functions are already allowed in tier 0 at `abi/tiers.json:30` and `:38`. `cmake/ImpossibleSDK.cmake:95` checks PE imports against the target tier, while line 73 checks whether a method appears anywhere in the SDK inventory. Both checks can pass, leaving the OS with an understated app requirement. Bind the minimum tier of each linked SDK part to the app declaration and require this mismatched target to fail.
3. **P2 | Design record:78 | The OS roadmap reader cannot distinguish COM methods from DLL exports.** **Path:** The SDK publishes its first GPU method inventory and the OS lane runs its existing needs check. **Evidence: traced** OS `scripts/imports.py:332` recognizes DLL/function pairs, and `scripts/imports.py:371` reads only `tier["functions"]`. A separate methods field is invisible. Putting methods in `functions` instead makes `tools/check_imports.py:21` treat them as allowed DLL exports. Specify distinct records for exports, optional lookups and interface methods, and assign the OS reader update alongside the SDK writer. Verify that an unmapped method is reported under its interface.

## Source-defect: needs-attention

4. **P2 | Design record:98 | Moving Recto’s runtime section can retire OS acceptance obligations prematurely.** **Path:** The OS lane replaces §11 once the SDK runtime item and the OS unwind/import item exist, as line 125 permits. **Evidence: traced** OS `todo/11-apps/TODO-05-pdf-viewer.md:188` requires user-mode thread/mutex and standard-library tests. Line 192 requires all six tests under the launcher on TCG and KVM at one and two CPUs. Candidate line 98 retains the two-frame catch, and line 77 specifies a different Windows sample. Neither destination takes the rest of the OS checkpoint. Preserve the full checkpoint in the OS import item before moving §11.
5. **P2 | Design record:114 | Moving a whole graphics or text section can remove work only the OS can do.** **Path:** The OS lane applies the new ownership rule to a mixed Phase 8 roadmap section while importing SDK text or drawing. **Evidence: traced** The governing `docs/specs/2026-11-23-interface-and-apps-design.md:74` keeps desktop integration, and line 75 splits portable code from driver, Registry and system-service work within each section. There is a concrete split even in the named font manager: OS `include/font_mgr.h:88` loads system fonts at boot, while `include/font_mgr.h:96` exposes glyph drawing. Make the portable obligation the unit of transfer and retain an OS implementation item for the host services. A ledger hash preserves the old wording but does not assign the remaining work.

## Consistency: needs-attention

6. **P2 | Design record:123 | The prescribed order makes unrelated SDK work a prerequisite for app work.** **Path:** Office follows step 3 to start its first CPU-rendered screen. Step 2 puts the GPU sample and Recto’s ARM64 target ahead of that start. **Evidence: traced** Candidate lines 122 to 124 present a single ordered sequence. The governing `docs/specs/2026-11-23-interface-and-apps-design.md:107` assigns a missing SDK feature to its first consumer, who builds it and advances the app pin. The revision removed that instruction. Express dependencies per consumer so a GPU or ARM64 milestone gates only the surfaces that require it.
7. **P3 | Design record:114 | The ownership summary assigns the call list to both repositories.** Read the last clause, “what the OS must provide is the list, and only the OS writes it,” against line 110, which assigns `abi/tiers.json` to the SDK. State explicitly that the SDK publishes the list and the OS implements the listed calls.

## Quality: needs-attention

8. **P2 | Design record:78 | Forcing GPU absence can skip the failure path the sample is supposed to prove.** **Path:** The sample implements its test switch by selecting the CPU renderer before `im_gpu_open`. Both image comparisons pass even if startup fails when the provider loads but a required export is unavailable. **Evidence: traced** Line 78 requires the same executable and matching canvas output, but does not specify where absence is forced or require the ordinary initialization path to report the failure. Exercise absence at the loader boundary and a missing required export after load. In both cases, require normal startup to reach the usable CPU canvas.

## Optimization: approve

LENS adversarial: findings  
LENS integration: findings  
LENS source-defect: findings  
LENS consistency: findings  
LENS quality: findings  
LENS optimization: clean  
HIGHEST: P1  
OVERALL: needs-attention

## Round 3: astra (`gpt-6.2-astra` via `codex`)

- Writer: claude
- Run: 2026-11-27T08:58:02Z
- Candidate: 8b8e198fe978f6f717bc1ef3c45a193b7eaecac3
- Effort: medium
- Lenses: adversarial, integration, source-defect, consistency, quality, optimization
- Lines: 36
- Digest: e492ce6ae97491b4

Reviewed `7f3664b1..8b8e198f`. Candidate line numbers below refer to [the design record](/home/derickpayne/impossible/SDK/docs/specs/2026-11-27-sdk-as-platform-design.md) at the tip.

## Adversarial: needs-attention

1. **P1 | `docs/specs/2026-11-27-sdk-as-platform-design.md:72` | The object check cannot establish who made a COM call.** Path: Recto links an optimized external library that invokes a method on a supplied interface. The library can contain an indirect call with no Windows import, interface name or method name. Its source is outside the app CI scan. Evidence: ran `clang++ --target=x86_64-w64-windows-gnu -O2 -S -x c++ - -o -` on a one-method interface and a function calling that method. The call compiled to `movq (%rcx), %rax` and `jmpq *(%rax)`. The proposed `CreateFileW` fixture exercises a named import and cannot expose this failure. Require verifiable source or build provenance for objects admitted by the release check, and cover a dependency that uses a COM method without importing a DLL function.

## Integration: needs-attention

2. **P2 | `docs/specs/2026-11-27-sdk-as-platform-design.md:74` | The GPU tier conflicts with the existing release-tier measurement.** Path: Isotone correctly declares the GPU tier and ships a canvas that loads D3D dynamically. Evidence: traced candidate line 79, which keeps the D3D exports out of the PE import table, to `todo/08-release/TODO-01-parts-and-release.md:83`, which requires `tools/app_build.py` to flag a declaration above or below the tier measured from PE imports. That release therefore passes the proposed part check while failing the planned packaging check. Give both checks the same effective-tier calculation and exercise a correctly declared GPU release through packaging.

3. **P2 | `docs/specs/2026-11-27-sdk-as-platform-design.md:115` | The OS ledger cannot validate the required move record.** Path: the OS lane follows line 129 and transfers the first Recto obligation, recording the old text’s hash as required by line 97. Evidence: traced OS `scripts/imports.py:68`, `:79` and `:118` at `180284002`. A moved row accepts exactly `section`, `source` and `to`. A hash field fails shape validation, and a hash embedded in the free-text destination receives no verification. The commit and tree checks at `:277` cover the imported copy. Assign the compatible ledger and checker change before the first transfer, and prove that changing the recorded old-text hash is detected.

## Source-defect: needs-attention

4. **P2 | `docs/specs/2026-11-27-sdk-as-platform-design.md:100` | Recto §12 loses its on-OS behavioral checkpoint.** Path: the OS lane replaces §12 after Recto creates its migration item and the include check passes on import. Evidence: traced OS `todo/11-apps/TODO-05-pdf-viewer.md:204` and `:210`. The section requires the platform implementation’s unit tests to pass under the user-mode launcher, in addition to the include check. The candidate transfers only the check. A broken atomic-save or memory-mapping implementation could satisfy the replacement checkpoint. Assign the behavioral tests to the OS import item and retain the launcher requirement.

## Consistency: needs-attention

5. **P2 | `docs/specs/2026-11-27-sdk-as-platform-design.md:72` | The release rule grants Windows access to every SDK part.** Path: a builder uses an allowed Windows import in the shared text or drawing implementation. Evidence: traced the exemption for “objects built from SDK parts” and the header ban only “outside SDK parts” at line 72 against `AGENTS.md:13` and candidate line 65, which reserve OS calls for the Windows layer. Such an object meets the proposed release rule while breaking the OS shell’s portable import. Scope the exemption to Windows-layer objects, including the Windows GPU adapter, and require an allowed import in a portable SDK part to fail.

## Quality: needs-attention

6. **P2 | `docs/specs/2026-11-27-sdk-as-platform-design.md:79` | Both GPU failure tests can pass while Recto loses a working canvas after startup.** Path: Recto opens a document successfully, then its presentation thread receives a device-reset or device-removed result. Evidence: traced Recto `todo/02-viewer/TODO-02-document-view.md:92` at `274f248`, which explicitly assigns device-loss recovery to the presentation thread. Both candidate failure cases end during DLL or export loading, before a device exists. Define how the SDK reports loss to that thread and exercise recovery or CPU fallback with the document still open.

## Optimization: needs-attention

7. **P2 | `docs/specs/2026-11-27-sdk-as-platform-design.md:119` | Isotone’s selected benchmark cannot measure its canvas target.** Path: the layered canvas migration adds a full-frame copy during presentation. The named benchmark can return identical timings because it never presents a frame. Evidence: traced Isotone `native/benchmarks/main.cpp:32` and `native/benchmarks/imaging/premultiply_bench.cpp:183` at `ff1a42e`. The suite registers only pixel-premultiplication workloads. Isotone already specifies a relevant target at `docs/parity/gesso-section-design.md:557`: panning a 100-megapixel document at 37 degrees must keep the median frame below 16 ms. Measure that interaction through presentation on the same machine. The candidate supplies no frame measurement from which to judge the move.

LENS adversarial: findings  
LENS integration: findings  
LENS source-defect: findings  
LENS consistency: findings  
LENS quality: findings  
LENS optimization: findings  
HIGHEST: P1  
OVERALL: needs-attention

## Round 4: astra (`gpt-6.2-astra` via `codex`)

- Writer: claude
- Run: 2026-11-27T09:01:36Z
- Candidate: 256a32cae980a25c04bd08897dc92e9ca96be365
- Effort: medium
- Lenses: adversarial, integration, source-defect, consistency, quality, optimization
- Lines: 32
- Digest: 2795eb830edfa350

## Adversarial: needs-attention

1. **P1** `docs/specs/2026-11-27-sdk-as-platform-design.md:71` Renaming imports does not restrict which object can reference them. **Path:** An app ships a prebuilt dependency compiled against the SDK's Windows-layer declarations. **Evidence: traced** Line 71 puts the alias library on the final app link. The linker resolves the same private symbol for a dependency object as for an SDK object. Line 73 checks portable SDK parts, while `tools/check_imports.py:87` sees only the permitted DLL and export in the finished executable. The planted cases at line 78 never put a private-name reference in an **app dependency**. Require the release build to establish which input objects reference the private imports and reject a dependency containing such a reference. The reported `ExitProcess` experiment proves that the plain name fails, which does not establish caller ownership.

## Integration: needs-attention

2. **P1** `docs/specs/2026-11-27-sdk-as-platform-design.md:72` The public window interface already hands the application a native handle. **Path:** Every window paint callback receives the caller-owned `im_window`. **Evidence: traced** `include/impossible/window.h:59` exposes `native`, `src/window.c:132` stores the live `HWND` there, and `tests/fixtures/test_host.c:275` retrieves it from a callback. The struct also exposes `HCURSOR` at `include/impossible/window.h:60`. Line 72 relies on opacity without assigning the public API migration. Move host state behind the SDK interface and account for existing callers, including the window sample that constructs `im_window` by value at `samples/window/main.c:55`.

3. **P2** `docs/specs/2026-11-27-sdk-as-platform-design.md:71` Applying the new link rule to the existing program target breaks the SDK's own verification build. **Path:** A developer runs the normal SDK build after removing the system libraries. **Evidence: traced** `CMakeLists.txt:10` builds `test-host` through `impossible_add_program`, but `tests/fixtures/test_host.c:276` calls `GetWindowLongPtrW` directly. Separately, `CMakeLists.txt:34` uses that target for the deliberately forbidden executable. Its `MessageBoxW` call at `tests/fixtures/forbidden.c:5` must link before `tests/test_seed.py:34` can verify the PE checker rejects it. `UNCHECKED` skips only the post-link check at `cmake/ImpossibleSDK.cmake:91`. Give Windows-side test instrumentation and the intentionally invalid PE fixture explicit test-only build paths, while keeping the shipping target subject to the boundary.

## Source-defect: needs-attention

4. **P2** `docs/specs/2026-11-27-sdk-as-platform-design.md:104` Replacing Recto's source check with the link rule loses a distinct OS-import requirement. **Path:** Recto's imported engine retains a Windows header for a type or constant and calls no imported function. **Evidence: traced** At the pinned OS commit `180284002`, `todo/11-apps/TODO-05-pdf-viewer.md:207` requires the imported libraries to exclude Windows headers outside the adapter, and line 210 specifies a zero-include checkpoint. A type-only include creates no unresolved symbol for candidate line 71 to reject. Keep that source check at import or explicitly revise the upstream portability requirement before retiring it.

## Consistency: needs-attention

5. **P2** `docs/specs/2026-11-27-sdk-as-platform-design.md:129` The proposed order still makes GPU packaging a prerequisite for Office's first CPU screen. **Path:** Office follows “Everyone first” while the GPU tier is still being built. **Evidence: traced** Line 129 requires all planted cases before *every* app move. Line 78 includes a correctly declared GPU program passing packaging, which requires the GPU part and its tier metadata at line 75. Line 130 explicitly promises Office can start without the GPU work. Put the GPU-specific cases with the first GPU consumer and keep the shared import-ownership checks on the CPU path.

## Quality: needs-attention

6. **P2** `docs/specs/2026-11-27-sdk-as-platform-design.md:78` The COM fixture can pass for the wrong reason. **Path:** The fixture obtains an interface through an ordinary Windows import and then calls a method. **Evidence: traced** Line 71 requires the link to fail on the acquisition symbol alone. Deleting the COM call leaves the same failure, so the test still passes if the SDK later exposes a live interface through a callback. Exercise the public handle and callback boundary independently of the ordinary-import rejection, including the case where the consumer receives its input without naming a Windows function.

## Optimization: approve

LENS adversarial: findings  
LENS integration: findings  
LENS source-defect: findings  
LENS consistency: findings  
LENS quality: findings  
LENS optimization: clean  
HIGHEST: P1  
OVERALL: needs-attention
