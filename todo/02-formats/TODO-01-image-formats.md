---
schema_version: 1
id: image-formats
domain: 02-formats
status: active
title: "TODO-01 -- Image Formats"
---

# TODO-01 -- Image Formats

> **Goal:** Programs and the OS share one set of image decoders that cannot be crashed by a file.

> **Current state:** **Groomed 2026-11-23:** §1 moved to phase 1, because every part needs it; §7 to §12 were missing. Measured 2026-11-23. No decoder exists. Photos needs them first.

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
| 1 | §1 | Part layout and image type | -- | [x] |
| 2 | §2 | Inflate and checksums | §1 | [x] |
| 3 | §3 | PNG decoder | §2 | [x] |
| 4 | §4 | JPEG decoder | §1 | [ ] |
| 5 | §5 | BMP decoder | §1 | [x] |
| 6 | §6 | GIF decoder | §1 | [x] |
| 7 | §7 | WebP lossless decoder | §1 | [ ] |
| 8 | §8 | WebP lossy decoder and alpha | §7 | [ ] |
| 9 | §9 | Animated WebP and GIF playback | §6, §8 | [ ] |
| 10 | §10 | JPEG writer | §4 | [ ] |
| 11 | §11 | ICO and CUR, both ways | §3, §5, D06 T01 §10 | [ ] |
| 12 | §12 | Picture orientation and facts | §3, §4, §8 | [ ] |

## 1. Part layout and image type

> **Started:** 2026-11-24T05:13Z

Parts are separate folders with their own tests and dependency list, so Impossible OS can import one without the rest.

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/image/image.h`, `parts/image/image.c`, `parts.json`, and `tools/check_parts.py` (moving nothing yet). Done when: `python3 tools/check_parts.py` passes and refuses an undeclared edge.
2. Add the fixture decoder in `tests/fixtures/image_tests.c`, standalone OS-flag compilation tests in `tests/test_parts.py`, WSL proof wiring in `tools/prove.py`, and reference discovery for `parts/` in `tools/site/build.py`. Done when: `python3 tools/build.py` and `python3 tools/site/build.py --check` pass.

- [x] Define the part list in `parts.json` (name, folder, depends, sources). Done when: `python3 tools/check_parts.py` refuses an include that crosses an undeclared edge. Cheaper substitute: a comment.
- [x] Define the picture type, limits and errors in `parts/image/image.h`. Done when: the fixture decoder in `tests/fixtures/` uses it. Cheaper substitute: one struct per format.
- [x] Compile each part alone with the OS flags in `tests/test_parts.py`. Done when: adding a Windows call to a part fails the test. Cheaper substitute: compiling everything together.
- [x] Commit: `parts: layout, image type and the check`

**Test checkpoint:** `python3 tools/build.py` exits 0 and `python3 tools/check_parts.py` refuses the fixture with a hidden dependency.

> **Verified:** 2026-11-24 | §1 | 4ee5002a8b121d4cd7ae96cea73a7c3166a62a2d
> **Review:** `docs/reviews/d02-t01-s1.json`

## 2. Inflate and checksums

> **Started:** 2026-11-25T12:28Z

Deflate data sits inside PNG, so it comes first. Own code, because the OS part must not need a C library.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/inflate/inflate.c`. Done when: zlib's own test vectors pass.
2. Write CRC-32 and Adler-32 in `parts/checksum/`, the only place they are written. Done when: both equal Python's `zlib`.

- [x] Inflate stored, fixed and dynamic blocks into a bounded buffer in `parts/inflate/inflate.c`. Done when: 10,000 random inputs from Python's `zlib` at every level round trip. Cheaper substitute: dynamic blocks only.
- [x] Take input and give output in any pieces in `parts/inflate/inflate.c`. Done when: input cut at every byte, inside a block too, gives equal output with one fixed window of memory. Cheaper substitute: whole buffers.
- [x] Stop at a caller's output limit in `parts/inflate/inflate.c`. Done when: a 1 GB bomb is refused after the limit without allocating more. Cheaper substitute: trusting the stated size.
- [x] Fuzz `parts/inflate/inflate.c`. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [x] Commit: `parts: inflate and checksums`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the bomb test fails without the limit.

> **Verified:** 2026-11-25 | §2 | 61b2d572ed74fcf6b795eb34f1fa647a6adf78aa
> **Review:** `docs/reviews/d02-t01-s2.json`

## 3. PNG decoder

> **Started:** 2026-11-25T13:04Z

All colour types, bit depths, interlacing, palettes, transparency and gamma-free output. The decoder takes bytes and gives 32-bit pixels or a named error, uses no floating point, and never reads outside its input.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/png/png.c` with `png.h`. Done when: every valid file of the corpus decodes to the same pixels as the pinned reference library and every invalid file returns its named error.
2. Add `tests/fixtures/png_tests.c` and the corpus list. Done when: `python3 tools/build.py` passes.

- [x] Parse the header of PNG in `parts/png/png.c` with width, height and size limits checked before any allocation. Done when: the largest header the format can hold, against a 16 MB limit, is refused with `IM_IMAGE_TOO_LARGE` and no allocation is made. Cheaper substitute: allocating and failing.
- [x] Decode every variant the specification requires in `parts/png/png.c`. Done when: every valid file of PngSuite gives the pixels of a pinned reference decoder, and every invalid one its named error. Cheaper substitute: the common variant only.
- [x] Return a named error for truncated and damaged data with no partial picture in `parts/png/png.c`. Done when: every proper prefix of every corpus file returns a named error and a zeroed image. Cheaper substitute: a half picture.
- [x] Fuzz `parts/png/png.c`. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [x] Commit: `formats: PNG decoder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when the decoder is reverted.

> **Verified:** 2026-11-25 | §3 | cd07fe1d785adff7b0cdce0b2e285bba3170f424
> **Review:** `docs/reviews/d02-t01-s3.json`

## 4. JPEG decoder

Baseline and progressive, every subsampling, restart markers, grayscale and the orientation tag. The decoder takes bytes and gives 32-bit pixels or a named error, uses no floating point, and never reads outside its input.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/jpeg/jpeg.c` with `jpeg.h`. Done when: every file of the corpus decodes to the same pixels as the reference library.
2. Add `tests/fixtures/jpeg_tests.c` and the corpus list. Done when: `python3 tools/build.py` passes.

- [ ] Parse the header of JPEG in `parts/jpeg/jpeg.c` with width, height and size limits checked before any allocation. Done when: the largest header the format can hold, against a 16 MB limit, is refused with `IM_IMAGE_TOO_LARGE` and no allocation is made. Cheaper substitute: allocating and failing.
- [ ] Decode every variant the specification requires in `parts/jpeg/jpeg.c`. Done when: a pinned corpus is within 1 per channel of the reference decoder in its integer mode. Cheaper substitute: the common variant only.
- [ ] Return a named error for truncated and damaged data with no partial picture in `parts/jpeg/jpeg.c`. Done when: every prefix of every corpus file is refused or decodes to the whole picture. Cheaper substitute: a half picture.
- [ ] Fuzz `parts/jpeg/jpeg.c`. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `formats: JPEG decoder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when the decoder is reverted.

## 5. BMP decoder

> **Started:** 2026-11-25T13:49Z

All header versions, 1 to 32 bits, RLE4, RLE8 and bit fields. The decoder takes bytes and gives 32-bit pixels or a named error, uses no floating point, and never reads outside its input.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/bmp/bmp.c` with `bmp.h`. Done when: every accepted file of the corpus decodes to the same pixels as the pinned reference and every refused file returns its named error.
2. Add `tests/fixtures/bmp_tests.c` and the corpus list. Done when: `python3 tools/build.py` passes.

- [x] Parse the header of BMP in `parts/bmp/bmp.c` with width, height and size limits checked before any allocation. Done when: the largest header the format can hold, against a 16 MB limit, is refused with `IM_IMAGE_TOO_LARGE` and no allocation is made. Cheaper substitute: allocating and failing.
- [x] Decode every variant the specification requires in `parts/bmp/bmp.c`. Done when: all of the bmpsuite good files decode to the reference pixels. Cheaper substitute: the common variant only.
- [x] Return a named error for truncated and damaged data with no partial picture in `parts/bmp/bmp.c`. Done when: every prefix of every corpus file is refused or decodes to the whole picture. Cheaper substitute: a half picture.
- [x] Fuzz `parts/bmp/bmp.c`. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [x] Commit: `formats: BMP decoder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when the decoder is reverted.

> **Verified:** 2026-11-25 | §5 | 5bd9b67c9fa95406f15d5b221bd6ab10a75ab6fb
> **Review:** `docs/reviews/d02-t01-s5.json`

## 6. GIF decoder

> **Started:** 2026-11-25T14:46Z

Animation, local palettes, transparency and disposal. The decoder takes bytes and gives 32-bit pixels or a named error, uses no floating point, and never reads outside its input.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/gif/gif.c` with `gif.h`. Done when: every valid file of the corpus decodes to the same pixels as the pinned reference and every invalid file returns its named error.
2. Add `tests/fixtures/gif_tests.c` and the corpus list. Done when: `python3 tools/build.py` passes.

- [x] Parse the header of GIF in `parts/gif/gif.c` with width, height and size limits checked before any allocation. Done when: the largest header the format can hold, against a 16 MB limit, is refused with `IM_IMAGE_TOO_LARGE` and no allocation is made. Cheaper substitute: allocating and failing.
- [x] Decode every variant the specification requires in `parts/gif/gif.c`. Done when: every frame of the pinned corpus equals the reference frames. Cheaper substitute: the common variant only.
- [x] Return a named error for truncated and damaged data with no partial picture in `parts/gif/gif.c`. Done when: every prefix of every corpus file is refused or decodes to the whole picture. Cheaper substitute: a half picture.
- [x] Fuzz `parts/gif/gif.c`. Done when: A native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [x] Commit: `formats: GIF decoder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when the decoder is reverted.

> **Verified:** 2026-11-25 | §6 | fa815085dd0c12ba03bc9d379bf323ac60e88160
> **Review:** `docs/reviews/d02-t01-s6.json`

## 7. WebP lossless decoder

The VP8L bitstream inside its RIFF container.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/webp/lossless.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Parse the header in `parts/webp/lossless.c` with size limits checked before any allocation. Done when: the largest header the format can hold, against a 16 MB limit, is refused with `IM_IMAGE_TOO_LARGE` before any allocation. Cheaper substitute: allocating and failing.
- [ ] Read the container with the order and number each chunk kind allows in `parts/webp/container.c`. Done when: two `ANMF` frames are read, and a second `VP8X`, a chunk out of order and an overlong one are refused by name. Cheaper substitute: the first chunk.
- [ ] Decode prefix codes, the colour cache and back references in `parts/webp/lossless.c`. Done when: the pinned corpus equals the reference decoder's pixels. Cheaper substitute: no colour cache.
- [ ] Undo the four transforms in `parts/webp/lossless.c`. Done when: a file for each combination is equal. Cheaper substitute: predictor only.
- [ ] Return a named error for truncated and damaged data in `parts/webp/lossless.c`. Done when: every prefix of every corpus file is refused or decodes to the whole picture. Cheaper substitute: a half picture.
- [ ] Fuzz `parts/webp/lossless.c`. Done when: a native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `formats: webp lossless decoder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. WebP lossy decoder and alpha

VP8 key frames with their alpha plane, in integers.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/webp/lossy.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Parse the header in `parts/webp/lossy.c` with size limits checked before any allocation. Done when: the largest header the format can hold, against a 16 MB limit, is refused with `IM_IMAGE_TOO_LARGE` before any allocation. Cheaper substitute: allocating and failing.
- [ ] Decode the bool coder, coefficients and prediction in `parts/webp/lossy.c`. Done when: the published VP8 test vectors are equal. Cheaper substitute: one partition.
- [ ] Filter edges both ways in `parts/webp/lossy.c`. Done when: the vectors with each filter are equal. Cheaper substitute: no filter.
- [ ] Join the alpha plane and convert colours in `parts/webp/lossy.c`. Done when: the pinned corpus equals the reference decoder in its integer mode within 1. Cheaper substitute: opaque.
- [ ] Return a named error for truncated and damaged data in `parts/webp/lossy.c`. Done when: every prefix of every corpus file is refused or decodes to the whole picture. Cheaper substitute: a half picture.
- [ ] Fuzz `parts/webp/lossy.c`. Done when: a native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `formats: webp lossy decoder and alpha`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. Animated WebP and GIF playback

Frames with time, disposal and blending, from either format, through one reader.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/image/animation.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Read frames in order with bounded memory in `parts/image/animation.c`. Done when: every frame of both corpora equals the reference. Cheaper substitute: all frames at once.
- [ ] Apply disposal and blending in `parts/image/animation.c`. Done when: a table of 24 combinations passes. Cheaper substitute: overwriting.
- [ ] Seek and loop in `parts/image/animation.c`. Done when: any frame reached by seeking equals the one reached in order. Cheaper substitute: playing only.
- [ ] Refuse an endless or huge animation by the caller's limits in `parts/image/animation.c`. Done when: planted files stop within fixed memory. Cheaper substitute: trusting counts.
- [ ] Commit: `formats: animation`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 10. JPEG writer

Baseline and progressive JPEG from a picture, in integers, for Paint and Photos.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `parts/jpeg/write.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_parts.py`. Done when: the checks fail on a planted fault.

- [ ] Write baseline files with quality 1 to 100 in `parts/jpeg/write.c`. Done when: Windows and the reference decoder read each. Cheaper substitute: one quality.
- [ ] Subsample colour by choice in `parts/jpeg/write.c`. Done when: 4:4:4, 4:2:2 and 4:2:0 read back within the reference encoder's error. Cheaper substitute: 4:2:0 only.
- [ ] Build the best Huffman tables and progressive scans in `parts/jpeg/write.c`. Done when: files are no larger than the reference encoder's by more than 2 percent. Cheaper substitute: fixed tables.
- [ ] Turn and flip without loss in `parts/jpeg/transform.c`. Done when: one turn and each flip of an asymmetric picture give the size and the coefficient places worked out by hand, partial edge blocks included. Cheaper substitute: four turns giving the first picture.
- [ ] Write the same bytes on every system in `tests/test_parts.py`. Done when: Windows and the portable build are equal. Cheaper substitute: close.
- [ ] Commit: `formats: JPEG writer`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 11. ICO and CUR, both ways

> **Started:** 2026-11-25T18:18Z

Icons and cursors with every size and depth, for Icon Editor and programs.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `parts/ico/read.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [x] Read every entry, PNG or bitmap with mask, in `parts/ico/read.c`. Done when: the icons of 50 Windows programs are equal to Windows' own drawing. Cheaper substitute: the largest entry.
- [x] Choose the entry for a size and scale in `parts/ico/read.c`. Done when: the choice equals Windows' for 100 sizes. Cheaper substitute: the nearest.
- [x] Write all entries with hot spots in `parts/ico/write.c`. Done when: Windows shows each size and cursor right. Cheaper substitute: PNG only.
- [x] Refuse entries that overlap or leave the file in `parts/ico/read.c`. Done when: a table of 40 planted files is refused. Cheaper substitute: trusting offsets.
- [x] Fuzz `parts/ico/read.c`. Done when: a native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [x] Commit: `formats: ICO and CUR`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 12. Picture orientation and facts

EXIF orientation, size, date and camera facts, read safely and removable.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/exif/exif.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Read EXIF from JPEG, PNG and WebP in `parts/exif/exif.c`. Done when: a pinned corpus equals the reference reader. Cheaper substitute: JPEG only.
- [ ] Apply all eight orientations in `parts/exif/exif.c`. Done when: each test picture stands upright. Cheaper substitute: rotation only.
- [ ] Remove facts without touching pixels in `parts/exif/strip.c`. Done when: the pixel data's bytes are equal and no fact remains. Cheaper substitute: encoding again.
- [ ] Fuzz `parts/exif/exif.c`. Done when: a native Linux libFuzzer run with address and undefined-behaviour checking for 120 seconds at `-rss_limit_mb=800` finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `formats: orientation and facts`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- `tests/test_parts.py` builds every part alone with the OS flags.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
