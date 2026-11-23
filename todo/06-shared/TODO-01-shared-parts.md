---
schema_version: 1
id: shared-parts
domain: 06-shared
status: active
title: "TODO-01 -- Shared Parts"
---

# TODO-01 -- Shared Parts

> **Goal:** Code that two programs need exists once, as a part with its own tests.

> **Current state:** Measured 2026-11-23. Notepad, Command and SSH hold some of it as their own code; the rest does not exist. The programs' lists say which part they use.

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
| 1 | §1 | A host's own engine | -- | [ ] |
| 2 | §2 | Deflate and picture writers | -- | [ ] |
| 3 | §3 | Hashes and checksums | -- | [ ] |
| 4 | §4 | Text buffer | -- | [ ] |
| 5 | §5 | Unicode data | -- | [ ] |
| 6 | §6 | Time zones and recurrence | -- | [ ] |
| 7 | §7 | Lossless sound | §3 | [ ] |
| 8 | §8 | Lossy sound | -- | [ ] |
| 9 | §9 | SSH transport | -- | [ ] |

## 1. A host's own engine

Impossible OS keeps its own element tree, layout, states, input and automation engines. The controls reach an engine only through one header, so that they run on this kit's engine on Windows and on the OS's there.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/engine.h`. Done when: a second engine in the tests runs the first controls
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Write the interface in `ui/engine.h` and move the controls to it. Done when: no control includes an engine's own header. Cheaper substitute: two copies.
- [ ] Write a small second engine for the tests. Done when: the first controls pass on both. Cheaper substitute: a mock.
- [ ] Declare what the OS's engine needs beyond it in `abi/engine.json`. Done when: the OS's check names each. Cheaper substitute: a note.
- [ ] Commit: `ui: the engine interface`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Deflate and picture writers

Deflate, PNG and BMP writing, and a scaler in premultiplied colour. Zip, Paint, Stillframe and Icon Editor need them.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/deflate/deflate.c`. Done when: Python's zlib reads it equal
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Deflate at three efforts in `parts/deflate/deflate.c`. Done when: 1,000 random inputs read equal in Python's zlib and zipfile. Cheaper substitute: stored.
- [ ] Write PNG and BMP. Done when: files read equal in the decoder and in Windows. Cheaper substitute: one format.
- [ ] Scale with a filter in `parts/scale/scale.c`. Done when: reference pictures match within 1. Cheaper substitute: nearest pixel.
- [ ] Commit: `parts: deflate and picture writers`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Hashes and checksums

CRC-32, Adler, MD5, SHA-1, SHA-2, SHA-3 and BLAKE3, moved from Command's hash tool with its tests.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/hash/hash.c`. Done when: published vectors pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Move the code and its tests from Command. Done when: Command builds on the part and its tests pass. Cheaper substitute: a copy.
- [ ] Process as a stream of any pieces. Done when: random cuts give the same result. Cheaper substitute: whole files.
- [ ] Use the processor's instructions where a measurement justifies it. Done when: both ways agree on random inputs. Cheaper substitute: always.
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

- [ ] Move Notepad's engine and its 53,430 checks. Done when: Notepad builds on the part. Cheaper substitute: a copy.
- [ ] Keep the interface free of any window. Done when: it compiles alone with the OS flags. Cheaper substitute: one program's needs.
- [ ] Let two programs use it with different undo limits. Done when: both pass their scripts. Cheaper substitute: one setting.
- [ ] Commit: `parts: text buffer`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Unicode data

Categories, breaks, widths, case, normal forms and names from one pinned Unicode version.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/unicode/unicode.c`. Done when: the published test sets pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Generate tables by `tools/gen_unicode.py` from the pinned version. Done when: a rebuild gives the same bytes. Cheaper substitute: hand-typed.
- [ ] Break lines, words, graphemes and order directions. Done when: the published test sets pass. Cheaper substitute: spaces.
- [ ] Normalise and fold case. Done when: the published tests pass. Cheaper substitute: ASCII.
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
- [ ] Convert both ways with gaps and overlaps named. Done when: every change from 1970 to 2100 agrees. Cheaper substitute: the first.
- [ ] Expand recurrence rules with a budget. Done when: 100,000 random rules agree with python-dateutil, as a development check. Cheaper substitute: daily and weekly.
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

- [ ] Read and write WAV and RF64 safely. Done when: every prefix of every sample is refused or whole, and a 120 second native fuzz run finds nothing. Cheaper substitute: 16 bits.
- [ ] Decode FLAC and check its sums. Done when: the published files pass. Cheaper substitute: trusting it.
- [ ] Encode FLAC with a seek table. Done when: 1,000 recordings decode equal in the reference decoder, as a development check. Cheaper substitute: stored.
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

- [ ] Decode MPEG layers II and III in fixed point. Done when: the ISO conformance files are inside the full accuracy limit. Cheaper substitute: floating point.
- [ ] Decode Ogg Vorbis in fixed point. Done when: samples agree with the reference within 1 in 32,768, as a development check. Cheaper substitute: one size.
- [ ] Survive damage. Done when: every prefix of every sample is refused or whole, and a 120 second native fuzz run finds nothing. Cheaper substitute: silence.
- [ ] Commit: `parts: lossy sound`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. SSH transport

The SSH transport, signing in and channels of the SSH program, as a part that Remote Desktop uses too.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/ssh/ssh.c`. Done when: SSH's tests pass on it
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Move the SSH program's engine and its tests when they pass there. Done when: the SSH program builds on the part. Cheaper substitute: a copy.
- [ ] Open a forwarded connection without a terminal. Done when: a local server's script passes. Cheaper substitute: the whole program.
- [ ] Clear every secret after its use. Done when: the cleared bytes are seen zero in the built program. Cheaper substitute: at exit.
- [ ] Commit: `parts: ssh`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- Each part's tests pass, and the programs that used their own copy build on the part
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
