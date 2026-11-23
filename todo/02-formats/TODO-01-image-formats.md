---
schema_version: 1
id: image-formats
domain: 02-formats
status: active
title: "TODO-01 -- Image Formats"
---

# TODO-01 -- Image Formats

> **Goal:** Programs and the OS share one set of image decoders that cannot be crashed by a file.

> **Current state:** Measured 2026-11-23. No decoder exists. Photos needs them first.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: where a feature belongs, and parts.
- The published specifications of each format.


## Outcome

- PNG, JPEG, BMP and GIF decode to 32-bit pixels, each as its own part.
- Every decoder is fuzzed and has no undefined behaviour.


**Adjacency:** all=not-applicable (decoders hold no records, settings or reports)

**Adjacency rationale:** A decoder turns bytes into pixels; programs own files and settings.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Part layout and image type | -- | [ ] |
| 2 | §2 | Inflate and checksums | §1 | [ ] |
| 3 | §3 | PNG decoder | §2 | [ ] |
| 4 | §4 | JPEG decoder | §1 | [ ] |
| 5 | §5 | BMP decoder | §1 | [ ] |
| 6 | §6 | GIF decoder | §1 | [ ] |

## 1. Part layout and image type

Parts are separate folders with their own tests and dependency list, so Impossible OS can import one without the rest.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts.json` and move nothing yet. Done when: `tools/check_parts.py` passes.
2. Write `parts/image/image.h`. Done when: a test part compiles alone with the OS flags.

- [ ] Define the part list in `parts.json` (name, folder, depends, sources). Done when: `python3 tools/check_parts.py` refuses an include that crosses an undeclared edge. Cheaper substitute: a comment.
- [ ] Define the picture type, limits and errors in `parts/image/image.h`. Done when: the fixture decoder in `tests/fixtures/` uses it. Cheaper substitute: one struct per format.
- [ ] Compile each part alone with the OS flags in `tests/test_parts.py`. Done when: adding a Windows call to a part fails the test. Cheaper substitute: compiling everything together.
- [ ] Commit: `parts: layout, image type and the check`

**Test checkpoint:** `python3 tools/build.py` exits 0 and `python3 tools/check_parts.py` refuses the fixture with a hidden dependency.

## 2. Inflate and checksums

Deflate data sits inside PNG, so it comes first. Own code, because the OS part must not need a C library.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/inflate/inflate.c`. Done when: zlib's own test vectors pass.
2. Write CRC-32 and Adler-32 beside it. Done when: both equal Python's `zlib`.

- [ ] Inflate stored, fixed and dynamic blocks into a bounded buffer in `parts/inflate/inflate.c`. Done when: 10,000 random inputs from Python's `zlib` at every level round trip. Cheaper substitute: dynamic blocks only.
- [ ] Stop at a caller's output limit. Done when: a 1 GB bomb is refused after the limit without allocating more. Cheaper substitute: trusting the stated size.
- [ ] Fuzz it. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `parts: inflate and checksums`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the bomb test fails without the limit.

## 3. PNG decoder

All colour types, bit depths, interlacing, palettes, transparency and gamma-free output. The decoder takes bytes and gives 32-bit pixels or a named error, uses no floating point, and never reads outside its input.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/png/png.c` with `png.h`. Done when: every file of the corpus decodes to the same pixels as the reference library.
2. Add `tests/fixtures/png_tests.c` and the corpus list. Done when: `python3 tools/build.py` passes.

- [ ] Parse the header of PNG in `parts/png/png.c` with width, height and size limits checked before any allocation. Done when: a header asking for 1 TB is refused with `IM_IMAGE_TOO_LARGE`. Cheaper substitute: allocating and failing.
- [ ] Decode every variant the specification requires. Done when: all of PngSuite decodes to the pixels of a pinned reference decoder. Cheaper substitute: the common variant only.
- [ ] Return a named error for truncated and damaged data with no partial picture. Done when: every prefix of every corpus file is refused or decodes to the whole picture. Cheaper substitute: a half picture.
- [ ] Fuzz `parts/png/png.c`. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `formats: PNG decoder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when the decoder is reverted.

## 4. JPEG decoder

Baseline and progressive, every subsampling, restart markers, grayscale and the orientation tag. The decoder takes bytes and gives 32-bit pixels or a named error, uses no floating point, and never reads outside its input.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/jpeg/jpeg.c` with `jpeg.h`. Done when: every file of the corpus decodes to the same pixels as the reference library.
2. Add `tests/fixtures/jpeg_tests.c` and the corpus list. Done when: `python3 tools/build.py` passes.

- [ ] Parse the header of JPEG in `parts/jpeg/jpeg.c` with width, height and size limits checked before any allocation. Done when: a header asking for 1 TB is refused with `IM_IMAGE_TOO_LARGE`. Cheaper substitute: allocating and failing.
- [ ] Decode every variant the specification requires. Done when: a pinned corpus is within 1 per channel of the reference decoder in its integer mode. Cheaper substitute: the common variant only.
- [ ] Return a named error for truncated and damaged data with no partial picture. Done when: every prefix of every corpus file is refused or decodes to the whole picture. Cheaper substitute: a half picture.
- [ ] Fuzz `parts/jpeg/jpeg.c`. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `formats: JPEG decoder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when the decoder is reverted.

## 5. BMP decoder

All header versions, 1 to 32 bits, RLE4, RLE8 and bit fields. The decoder takes bytes and gives 32-bit pixels or a named error, uses no floating point, and never reads outside its input.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/bmp/bmp.c` with `bmp.h`. Done when: every file of the corpus decodes to the same pixels as the reference library.
2. Add `tests/fixtures/bmp_tests.c` and the corpus list. Done when: `python3 tools/build.py` passes.

- [ ] Parse the header of BMP in `parts/bmp/bmp.c` with width, height and size limits checked before any allocation. Done when: a header asking for 1 TB is refused with `IM_IMAGE_TOO_LARGE`. Cheaper substitute: allocating and failing.
- [ ] Decode every variant the specification requires. Done when: all of the bmpsuite good files decode to the reference pixels. Cheaper substitute: the common variant only.
- [ ] Return a named error for truncated and damaged data with no partial picture. Done when: every prefix of every corpus file is refused or decodes to the whole picture. Cheaper substitute: a half picture.
- [ ] Fuzz `parts/bmp/bmp.c`. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `formats: BMP decoder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when the decoder is reverted.

## 6. GIF decoder

Animation, local palettes, transparency and disposal. The decoder takes bytes and gives 32-bit pixels or a named error, uses no floating point, and never reads outside its input.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/gif/gif.c` with `gif.h`. Done when: every file of the corpus decodes to the same pixels as the reference library.
2. Add `tests/fixtures/gif_tests.c` and the corpus list. Done when: `python3 tools/build.py` passes.

- [ ] Parse the header of GIF in `parts/gif/gif.c` with width, height and size limits checked before any allocation. Done when: a header asking for 1 TB is refused with `IM_IMAGE_TOO_LARGE`. Cheaper substitute: allocating and failing.
- [ ] Decode every variant the specification requires. Done when: every frame of the pinned corpus equals the reference frames. Cheaper substitute: the common variant only.
- [ ] Return a named error for truncated and damaged data with no partial picture. Done when: every prefix of every corpus file is refused or decodes to the whole picture. Cheaper substitute: a half picture.
- [ ] Fuzz `parts/gif/gif.c`. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `formats: GIF decoder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when the decoder is reverted.

## Verification

- `tests/test_parts.py` builds every part alone with the OS flags.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
