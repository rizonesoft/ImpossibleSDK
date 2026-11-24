## Plan review: astra (`gpt-6.2-astra` via `codex`)

- Writer: agy
- Run: 2026-11-23T23:56:22Z
- Candidate: 3fd677df5ad852935809348e23bcd819a2e55efa
- Effort: medium
- Lenses: plan
- Lines: 12
- Digest: ec932d2c9e0c3072

1. **P1 | §1 lines 60, 68, 71–73, 76. The checkpoint can pass without testing all nine radius and scale combinations.** Path: capture can repeat the host scale under different labels, or regeneration can omit a combination from both the manifest and the fixture output. Reverting the old curve can still fail on an accent button, satisfying the checkpoint. Evidence: traced `reference/App.xaml.cs:83` and `:103`, which render at the current scale and record the host scale, and `tests/test_ui.py:60–65`, which takes its expected count from the manifest being tested. Require the exact case identities independently of the manifest, reject duplicates and omissions, and verify each capture’s pixel dimensions against its logical bounds and requested scale. Demonstrate that removing one probe or relabelling a 100% capture as 150% fails the probe check itself.
2. **P2 | §1 lines 60, 68–69, 73. “Every style” omits the subtle button, and “lower” would loosen the standard button’s existing limit.** Path: an SDK caller draws a hovered subtle button, or a standard corner regresses from 3 to 4. Both can pass the proposed comparison. Evidence: traced `ui/button.c:9–11`, which exposes three styles, `reference/App.xaml.cs:49–54`, which captures two, `tests/fixtures/ui_tests.c:100–104`, which samples no subtle corner, and `tests/test_ui.py:76`, which already enforces 3 for standard. Add subtle reference coverage for its states and themes, or explicitly assign it to a named section and narrow this claim. Retain the standard limit of 3 while tightening accent to 4.
3. **P2 | §1 lines 69, 72. The probes miss a known geometry difference for unequal corner radii.** Path: a caller fills a 40-by-20 rectangle with a top-left radius of 16 and three square corners. The SDK clips that radius to 10 even though it fits along both adjoining edges in WinUI. Every proposed probe uses the same radius at all four corners. Evidence: traced `ui/draw.h:13–16`, `ui/draw.c:43–46`, and the pinned `dxaml/xcp/core/core/elements/geometry.cpp:869–887`, where WinUI fits adjacent radii against their shared edge. Add an asymmetric reference case, correct the fitting rule at the drawing layer, and update the public contract at `ui/draw.h:26`.
4. **P2 | §1 lines 60, 68–69. Recapturing buttons can compare two different accent palettes.** Path: a contributor runs the reference program with a different Windows accent. Evidence: traced `reference/App.xaml.cs:95–104`, which saves the current system accent shades, and `tests/fixtures/control_shots.c:18`, which uses the fixed values in `tests/fixtures/accents.h:1`. Specify that the SDK fixture consumes the palette belonging to the captured buttons, or preserve the existing button captures and their palette when importing probes. Verify that a different host accent does not produce a false corner failure.
5. **P2 | §1 lines 63, 68. The declared prerequisite does not establish that capture can run.** Path: a noninteractive Windows build session can run Python, PowerShell and .NET but cannot activate the reference window. Evidence: traced `tools/windows_host.py:30–44` and `reference/App.xaml.cs:31–36`. Declare `display-session` for capture, as the same TODO already does for the reference program at lines 408–412. Comparing the committed frames can remain independent of a display session.
6. **P2 | §1 lines 73–76. The public comparison guide would publish obsolete results.** Path: an SDK user checks the documented tolerance after the feature ships and reads that accent corners may differ by 27 and that finding the rule is future work. Evidence: traced `docs/guide/button.md:8–24` and the source-linked page review requirement at `AGENTS.md:16`. Update the guide with the verified limits and probe coverage, check its fixture excerpt, and run `python3 tools/site/build.py --check`.
7. **P3 | §1 lines 58, 69, 72. The source attribution conflates path construction with pixel coverage.** The pinned `geometry.cpp:1077–1088` constructs a path through a graphics geometry sink. It does not specify the resulting per-pixel antialiasing weights. Require the `ui/draw.c` comment to identify which geometry comes from the pinned source and which coverage rule was inferred from measured frames.

LENS plan: findings  
HIGHEST: P1  
PLAN: gaps  
OVERALL: needs-attention

- Planned: manifest-independent exact 26 case identities, scale and pixel dimension checks, and negative tests in 92fbb2d
- Planned: retain standard button corner limit of 3, tighten accent to 4, defer subtle reference capture to D01 T01 §7, and test subtle corners in ui_tests.c in 92fbb2d
- Planned: edge-wise proportional corner fitting matching CGeometryBuilder, asymmetric 40x20 probe, and ui/draw.h contract update in 92fbb2d
- Planned: control_shots.c consumes captured accent palette from cases.json in 92fbb2d
- Planned: added display-session to D01 T01 §1 Needs in 92fbb2d
- Planned: update docs/guide/button.md and verify with tools/site/build.py --check in 92fbb2d
- Planned: separate CGeometryBuilder geometry attribution from measured coverage rule in ui/draw.c comment in 92fbb2d
