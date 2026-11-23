---
schema_version: 1
id: shared-parts
domain: 06-shared
status: active
title: "TODO-01 -- Shared Parts"
---

# TODO-01 -- Shared Parts

> **Goal:** Code that two programs need exists once, as a part with its own tests.

> **Current state:** **Groomed 2026-11-23:** §1 moved to phase 2 and §5 to phase 1, because the controls and the text engine need them; §2 held deflate, writers and scaling, which are now §2, §10 and §11. Measured 2026-11-23. Notepad, Command and SSH hold some of it as their own code; the rest does not exist. The programs' lists say which part they use.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: where a feature belongs, and parts.
- `docs/os-roadmap/`: the OS sections whose work moved here.


## Outcome

- Each part compiles alone with the OS flags where it calls no system function.
- No program holds its own copy of a part.


**Adjacency:** all=not-applicable (parts hold no records, settings or reports; programs own those)

**Adjacency rationale:** A part does one thing; the program decides what is kept.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | A host's own engine | D01 T01 §7 | [ ] |
| 2 | §2 | Deflate | D02 T01 §2 | [ ] |
| 3 | §3 | Hashes | D02 T01 §2 | [ ] |
| 4 | §4 | Text buffer | D02 T01 §1 | [ ] |
| 5 | §5 | Unicode data | D02 T01 §1 | [ ] |
| 6 | §6 | Time zones and recurrence | D02 T01 §1 | [ ] |
| 7 | §7 | Lossless sound | §3 | [ ] |
| 8 | §8 | Lossy sound | D02 T01 §1 | [ ] |
| 9 | §9 | SSH transport | D02 T01 §1, D03 T01 §3 | [ ] |
| 10 | §10 | PNG and BMP writers | §2, D02 T01 §3, D02 T01 §5 | [ ] |
| 11 | §11 | Picture scaler | D02 T01 §1 | [ ] |

## 1. A host's own engine

Impossible OS keeps its own element tree, layout, states, input and automation engines. The controls reach an engine only through one header, so that they run on this kit's engine on Windows and on the OS's there.

**Phase:** 2
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/engine.h`. Done when: a second engine in the tests runs the first controls
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Write the interface in `ui/engine.h` and move the controls to it. Done when: no control includes an engine's own header. Cheaper substitute: two copies.
- [ ] Write a small second engine for the tests in `ui/engine.h`. Done when: the first controls pass on both. Cheaper substitute: a mock.
- [ ] Declare what the OS's engine needs beyond it in `abi/engine.json`. Done when: `tests/test_ui.py` builds both engines from the declaration and a missing or extra call fails. The OS reads it when it imports. Cheaper substitute: a note.
- [ ] Commit: `ui: the engine interface`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Deflate

Deflate and the zlib and gzip wrappers. Zip, Paint, Stillframe, Icon Editor, the package builder and PNG writing need it.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/deflate/deflate.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_parts.py`. Done when: the checks fail on a planted fault.

- [ ] Deflate at three efforts in `parts/deflate/deflate.c`. Done when: 1,000 random inputs read equal in Python's zlib and zipfile. Cheaper substitute: stored.
- [ ] Take input and give output in any pieces in `parts/deflate/deflate.c`. Done when: random cuts give the same bytes. Cheaper substitute: whole buffers.
- [ ] Keep to a fixed memory limit in `parts/deflate/deflate.c`. Done when: 10 GB streams through under 1 MB. Cheaper substitute: growing.
- [ ] Reach within 3 percent of zlib's size at its levels 1, 6 and 9 in `parts/deflate/deflate.c`. Done when: `tests/test_parts.py` measures a pinned corpus. Cheaper substitute: any size.
- [ ] Commit: `parts: deflate`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Hashes

MD5, SHA-1, SHA-2, SHA-3 and BLAKE3, moved from Command's hash tool with its tests. CRC-32 and Adler-32 are `parts/checksum/` of D02 T01 §2, and Command uses them from there.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/hash/hash.c`. Done when: published vectors pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Move the code and its tests from Command in `parts/hash/hash.c`. Done when: Command builds on the part and its tests pass. Cheaper substitute: a copy.
- [ ] Process as a stream of any pieces in `parts/hash/hash.c`. Done when: random cuts give the same result. Cheaper substitute: whole files.
- [ ] Use the processor's instructions where a measurement justifies it in `parts/hash/hash.c`. Done when: both ways agree on random inputs. Cheaper substitute: always.
- [ ] Commit: `parts: hashes`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Text buffer

The piece tree, encodings, line ends, undo, caret and search of Notepad, as a part for Colophon, WordPad, Nibble and others.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/textbuffer/buffer.c`. Done when: Notepad's tests pass on it
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Move Notepad's engine and its 53,430 checks in `parts/textbuffer/buffer.c`. Done when: Notepad builds on the part. Cheaper substitute: a copy.
- [ ] Keep the interface free of any window in `parts/textbuffer/buffer.c`. Done when: it compiles alone with the OS flags. Cheaper substitute: one program's needs.
- [ ] Let two programs use it with different undo limits in `parts/textbuffer/buffer.c`. Done when: both pass their scripts. Cheaper substitute: one setting.
- [ ] Commit: `parts: text buffer`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Unicode data

Categories, breaks, widths, case, normal forms and names from one pinned Unicode version.

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/unicode/unicode.c`. Done when: the published test sets pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Generate tables by `tools/gen_unicode.py` from the pinned version. Done when: a rebuild gives the same bytes. Cheaper substitute: hand-typed.
- [ ] Break lines, words and graphemes and resolve directions by the standard alone in `parts/unicode/unicode.c`, the only place these are written. Done when: LineBreakTest, WordBreakTest, GraphemeBreakTest, BidiTest and BidiCharacterTest pass. Cheaper substitute: spaces.
- [ ] Normalise and fold case in `parts/unicode/unicode.c`. Done when: the published tests pass. Cheaper substitute: ASCII.
- [ ] Commit: `parts: unicode`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Time zones and recurrence

Civil dates, zone rules from pinned IANA data and iCalendar recurrence, for Clock, Calendar, Weather and Mail.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/time/zones.c`. Done when: every zone agrees with Python's zoneinfo
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Generate tables by `tools/gen_zones.py` from the pinned tzdata. Done when: a rebuild gives the same bytes. Cheaper substitute: fixed offsets.
- [ ] Convert both ways with gaps and overlaps named in `parts/time/zones.c`. Done when: every change from 1970 to 2100 agrees. Cheaper substitute: the first.
- [ ] Expand recurrence rules with a budget in `parts/time/zones.c`. Done when: 100,000 random rules agree with python-dateutil, as a development check. Cheaper substitute: daily and weekly.
- [ ] Commit: `parts: time zones and recurrence`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Lossless sound

WAV and FLAC, reading and writing, exact.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/sound/lossless.c`. Done when: the published FLAC test files decode with equal sums
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Read and write WAV and RF64 safely in `parts/sound/lossless.c`. Done when: every prefix of every sample is refused or whole, and a 120 second native fuzz run finds nothing. Cheaper substitute: 16 bits.
- [ ] Decode FLAC and check its sums in `parts/sound/lossless.c`. Done when: the published files pass. Cheaper substitute: trusting it.
- [ ] Encode FLAC with a seek table in `parts/sound/lossless.c`. Done when: 1,000 recordings decode equal in the reference decoder, as a development check. Cheaper substitute: stored.
- [ ] Answer each item of §3 and §6 of `docs/os-roadmap/10-platform-services-TODO-01-audio-system.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `parts: lossless sound`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. Lossy sound

MP3 with Layer II, and Ogg Vorbis, in fixed point, for Music, Media Player and others.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/sound/lossy.c`. Done when: the conformance limits are met
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Decode MPEG layers II and III in fixed point in `parts/sound/lossy.c`. Done when: the ISO conformance files are inside the full accuracy limit. Cheaper substitute: floating point.
- [ ] Decode Ogg Vorbis in fixed point in `parts/sound/lossy.c`. Done when: samples agree with the reference within 1 in 32,768, as a development check. Cheaper substitute: one size.
- [ ] Survive damage in `parts/sound/lossy.c`. Done when: every prefix of every sample is refused or whole, and a 120 second native fuzz run finds nothing. Cheaper substitute: silence.
- [ ] Answer each item of §4 and §5 of `docs/os-roadmap/10-platform-services-TODO-01-audio-system.md` in `docs/os-roadmap-answers.md`. Done when: each has a test, a declared need or a reason. Cheaper substitute: a summary.
- [ ] Commit: `parts: lossy sound`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. SSH transport

The SSH transport, signing in and channels, written here first. The SSH program and Remote Desktop move to it; `../SSH/engine` holds no transport today.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `parts/ssh/transport.c`. Done when: a local OpenSSH server completes the key exchange
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Exchange keys and check the host key with current algorithms in `parts/ssh/transport.c`. Done when: a local OpenSSH server accepts it, and a changed host key is refused. Cheaper substitute: one algorithm.
- [ ] Sign in by password, key and keyboard questions in `parts/ssh/auth.c`. Done when: each works with the local server and a wrong one is named. Cheaper substitute: password.
- [ ] Run several channels with flow control in `parts/ssh/channel.c`. Done when: 10 channels carry 1 GB each way whole. Cheaper substitute: one.
- [ ] Refuse damaged and hostile packets in `parts/ssh/transport.c`. Done when: a 120 second native fuzz run finds nothing. Cheaper substitute: well-formed packets.
- [ ] Open a forwarded connection without a terminal in `parts/ssh/ssh.c`. Done when: a local server's script passes. Cheaper substitute: the whole program.
- [ ] Clear every secret after its use in `parts/ssh/ssh.c`. Done when: the cleared bytes are seen zero in the built program. Cheaper substitute: at exit.
- [ ] Commit: `parts: ssh`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 10. PNG and BMP writers

Pictures saved without loss and without hidden facts.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `parts/png/write.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Write PNG at every colour type with the best filter per row in `parts/png/write.c`. Done when: files read equal in our decoder, the reference and Windows. Cheaper substitute: one colour type.
- [ ] Choose palette or grey when it loses nothing in `parts/png/write.c`. Done when: 200 pictures are no larger than the reference encoder's by more than 3 percent. Cheaper substitute: always 32 bit.
- [ ] Write only the needed chunks in `parts/png/write.c`. Done when: the file holds no time, name or program. Cheaper substitute: all chunks.
- [ ] Write BMP with and without transparency in `parts/bmp/write.c`. Done when: Windows shows both right. Cheaper substitute: 24 bit.
- [ ] Commit: `parts: PNG and BMP writers`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 11. Picture scaler

Scaling and turning pictures in premultiplied colour.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/scale/scale.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Scale with box, bilinear and Lanczos filters in fixed point in `parts/scale/scale.c`. Done when: reference pictures match within 1. Cheaper substitute: nearest pixel.
- [ ] Work row by row in `parts/scale/scale.c`. Done when: a 100 megapixel picture scales in under 16 MB of extra memory. Cheaper substitute: a second picture.
- [ ] Turn and flip by right angles in `parts/scale/turn.c`. Done when: four turns give the first picture. Cheaper substitute: resampling.
- [ ] Keep edges free of dark fringes in `parts/scale/scale.c`. Done when: a white shape on transparent stays white. Cheaper substitute: straight colour.
- [ ] Commit: `parts: the picture scaler`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- Each part's tests pass, and the programs that used their own copy build on the part
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
