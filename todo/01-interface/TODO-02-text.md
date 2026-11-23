---
schema_version: 1
id: text-engine
domain: 01-interface
status: active
title: "TODO-02 -- Text Engine"
---

# TODO-02 -- Text Engine

> **Goal:** Text of every script that Windows shows is shaped, broken, drawn, measured and selected in portable fixed point, and icons come from our own set.

> **Current state:** Measured 2026-11-23 at `e06ab3a`. No text code exists in `ui/`. `TODO-01-first-controls.md` §5 reads outlines and draws single lines of left-to-right text.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: fixed point, Selawik and Cascadia Code, Segoe only where it is installed.
- `todo/01-interface/TODO-01-first-controls.md` §5: font reading and the rasteriser this file builds on.
- `todo/06-shared/TODO-01-shared-parts.md` §5: Unicode data.

## Outcome

- The published Unicode test sets pass, and the same paragraph breaks at the same places as a WinUI TextBlock.
- `ui/` still calls no system function and uses no floating point.

**Adjacency:** all=not-applicable (a text engine holds no records, settings or reports)

**Adjacency rationale:** Programs own the text; this file draws, measures and breaks it.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Bundled fonts and font lookup | T01 §5, T01 §8 | [ ] |
| 2 | §2 | Glyph cache | §1, T01 §10, T01 §11 | [ ] |
| 3 | §3 | Line, word and grapheme breaking | T01 §5, D06 T01 §5 | [ ] |
| 4 | §4 | Paragraph layout | §1, §3 | [ ] |
| 5 | §5 | Caret, hit testing and selection | §4 | [ ] |
| 6 | §6 | Icon set | T01 §13 | [ ] |
| 7 | §7 | Variable fonts | §1 | [ ] |
| 8 | §8 | Font fallback | §1, D06 T01 §5 | [ ] |
| 9 | §9 | Right-to-left and mixed direction text | §4, §5, D06 T01 §5 | [ ] |
| 10 | §10 | Glyph substitution | §4 | [ ] |
| 11 | §11 | Glyph positioning | §10 | [ ] |
| 12 | §12 | Joining scripts | §9, §10, §11 | [ ] |
| 13 | §13 | Indic and Southeast Asian scripts | §10, §11 | [ ] |
| 14 | §14 | Colour glyphs and emoji | §8, T01 §11, T01 §13 | [ ] |

## 1. Bundled fonts and font lookup

Selawik and Cascadia Code ship with the SDK under their open licences; Segoe is used only where Windows has it.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `fonts/` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Add pinned Selawik and Cascadia Code files with their licences to `fonts/`. Done when: `tools/bootstrap.py` verifies the hashes and the notices are in `licenses/`. Cheaper substitute: a download at run time.
- [ ] Match family, weight, stretch and slope in `ui/fontset.c`. Done when: 200 requests choose the face DirectWrite chooses from the same files. Cheaper substitute: the first face.
- [ ] Ask the host for an installed family in `src/fonts.c`. Done when: Segoe UI Variable is found on Windows 11 and its absence falls back to Selawik. Cheaper substitute: a fixed path.
- [ ] Refuse a damaged font and use the next one, in `ui/fontset.c`. Done when: every prefix of each bundled font ends in the fallback. Cheaper substitute: a crash.
- [ ] Carry the type ramp (caption, body, body strong, body large, subtitle, title, title large, display) in `ui/theme_data.c`. Done when: sizes and line heights equal the WinUI styles. Cheaper substitute: typed-in sizes.
- [ ] Commit: `ui: bundled fonts and lookup`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Glyph cache

Glyphs are drawn once per size and position step and kept within a limit.

**Phase:** 1
**Needs:** none
**Surface:** no

**Build order:**

1. Write `ui/glyphs.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/budgets.json`. Done when: the checks fail on a planted fault.

- [ ] Cache coverage by face with its resolved axis values, size, glyph and quarter-pixel step in `ui/glyphs.c`. Done when: cached and uncached frames are equal byte for byte, with two sets of axis values drawn in turn. Cheaper substitute: face and size.
- [ ] Keep to a byte limit by least recent use in `ui/glyphs.c`. Done when: a million random requests stay under the limit and are right. Cheaper substitute: growing without end.
- [ ] Draw through clips, layers and transforms in `ui/glyphs.c`. Done when: frames equal the uncached draw. Cheaper substitute: no clip.
- [ ] Measure hits for a fixed workload in `tests/budgets.json`: `tests/fixtures/scroll.txt`, a 4 MB cache, one pass to warm it, then 1,000 lines down and back. Done when: under 1 percent of the glyphs drawn in the second pass are new. Cheaper substitute: any page.
- [ ] Commit: `ui: glyph cache`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Line, word and grapheme breaking

Where a line may break, where a word ends and what counts as one character.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/breaks.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Use the standard breaks of `parts/unicode/` in `ui/breaks.c`. Done when: no break table or rule of UAX 14 or 29 is in `ui/`, checked by `tests/test_text.py`. Cheaper substitute: a second copy.
- [ ] Move by word and grapheme through them in `ui/breaks.c`. Done when: 200 recorded key scripts equal real WinUI. Cheaper substitute: code points.
- [ ] Match WinUI where it differs, with each difference named in `ui/breaks.c`. Done when: 500 paragraphs break at the words of a WinUI TextBlock with the same font. Cheaper substitute: the standard alone.
- [ ] Break text longer than memory allows in pieces in `ui/breaks.c`. Done when: cut text gives the same breaks as whole text. Cheaper substitute: one buffer.
- [ ] Commit: `ui: line, word and grapheme breaking`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. Paragraph layout

Runs of different fonts and sizes become lines with alignment, trimming, tabs and white space rules.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/paragraph.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Lay out styled runs on shared baselines in `ui/paragraph.c`. Done when: line rectangles equal real WinUI's for mixed sizes. Cheaper substitute: one style.
- [ ] Align left, centre, right and justified in `ui/paragraph.c`. Done when: glyph positions equal real WinUI within 1/64 unit. Cheaper substitute: left only.
- [ ] Trim by character and word with an ellipsis and a line limit in `ui/paragraph.c`. Done when: the visible text equals real WinUI's at 100 widths. Cheaper substitute: cutting pixels.
- [ ] Keep, collapse and wrap white space, and place tab stops, in `ui/paragraph.c`. Done when: a table of 60 cases passes. Cheaper substitute: collapsing always.
- [ ] Draw underline, strikethrough and highlight in `ui/paragraph.c`. Done when: their rectangles equal the font's metrics. Cheaper substitute: fixed offsets.
- [ ] Lay out again only what an edit changed, in `ui/paragraph.c`. Done when: random edits equal a full layout, and the counted lines measured stay under 10 for an edit at the end of 10,000. Cheaper substitute: full layout.
- [ ] Commit: `ui: paragraph layout`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Caret, hit testing and selection

From a point to a text position and back.

**Phase:** 1
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `ui/caret.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Give the caret rectangle for a position in `ui/caret.c`. Done when: positions equal real WinUI's in 200 strings. Cheaper substitute: average widths.
- [ ] Give the position for a point in `ui/caret.c`. Done when: clicking every pixel of a line agrees with real WinUI. Cheaper substitute: nearest start.
- [ ] Move by character, word, line, paragraph and page in `ui/caret.c`. Done when: recorded key scripts give equal positions. Cheaper substitute: characters only.
- [ ] Give selection rectangles across lines in `ui/caret.c`. Done when: they equal real WinUI's. Cheaper substitute: one box.
- [ ] Blink at the host's rate and stop while typing, in `ui/caret.c`. Done when: the recording host sees the rate and the pause. Cheaper substitute: a fixed rate.
- [ ] Commit: `ui: caret, hit testing and selection`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 6. Icon set

WinUI templates name Segoe Fluent Icons glyphs. Segoe cannot be redistributed, so each glyph a template or program uses has our own drawing in the same rectangle.

**Phase:** 1
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `tools/winui/icons.py` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] List every glyph the pinned templates name in `tools/winui/icons.py`. Done when: the list is generated and a template naming a new one fails the check. Cheaper substitute: a hand list.
- [ ] Draw our own 16 unit outlines for them in `icons/`. Done when: each fills the same rectangle as the Segoe glyph within 1 unit. Cheaper substitute: Segoe's outlines.
- [ ] Add the common program icons (file, edit, navigation, media, status) to `icons/`. Done when: `icons/index.json` names role, size and mirror rule for each. Cheaper substitute: the WinUI ones only.
- [ ] Compile them to tables in `ui/icon_data.c`. Done when: a changed source without its table fails the drift test. Cheaper substitute: reading SVG at run time.
- [ ] Mirror directional icons right to left in `ui/icon.c`. Done when: arrows turn and clocks do not. Cheaper substitute: mirroring all.
- [ ] Draw a missing icon as a visible box in debug and nothing in release, in `ui/icon.c`. Done when: a test sees both. Cheaper substitute: a wrong icon.
- [ ] Commit: `ui: our own icon set`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 7. Variable fonts

Segoe UI Variable on Windows chooses weight and optical size by axis.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/font_variable.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Read `fvar`, `avar` and `gvar` with bounds checks in `ui/font_variable.c`. Done when: instances equal DirectWrite's outlines within 1 font unit. Cheaper substitute: the default instance.
- [ ] Choose optical size from the text size in `ui/font_variable.c`. Done when: text at 12, 14, 20 and 40 units has DirectWrite's widths. Cheaper substitute: one size.
- [ ] Interpolate advance widths with `HVAR` in `ui/font_variable.c`. Done when: widths are equal for 1,000 glyphs. Cheaper substitute: unchanged widths.
- [ ] Fuzz `ui/font_variable.c`. Done when: a native libFuzzer run of 120 seconds with address checking finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `ui: variable fonts`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 8. Font fallback

A character missing from the chosen font is drawn from another that has it.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/fallback.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Split runs by coverage and script in `ui/fallback.c`. Done when: a mixed line names the same font per run as DirectWrite. Cheaper substitute: per character.
- [ ] Keep a grapheme in one font in `ui/fallback.c`. Done when: combining sequences are never split. Cheaper substitute: splitting them.
- [ ] Read the order from `fonts/fallback.json` and the host's installed fonts. Done when: a missing font is skipped without a gap. Cheaper substitute: a fixed list.
- [ ] Draw the missing glyph box with its code when nothing has it, in `ui/fallback.c`. Done when: a private-use character shows its box. Cheaper substitute: nothing.
- [ ] Commit: `ui: font fallback`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 9. Right-to-left and mixed direction text

UAX 9 order for drawing, the caret and selection.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `ui/bidi.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Use the levels of `parts/unicode/` in `ui/bidi.c`. Done when: 200 mixed paragraphs equal real WinUI's order. Cheaper substitute: a second copy of UAX 9.
- [ ] Order runs and mirror brackets per line in `ui/bidi.c`. Done when: lines equal real WinUI's order. Cheaper substitute: once per paragraph.
- [ ] Move and hit-test the caret at direction changes in `ui/caret.c`. Done when: key scripts equal real WinUI. Cheaper substitute: logical order only.
- [ ] Give selection as several rectangles in `ui/caret.c`. Done when: they equal real WinUI's. Cheaper substitute: one box.
- [ ] Set flow direction per element in `ui/layout.c`. Done when: mirrored control frames match. Cheaper substitute: text only.
- [ ] Commit: `ui: right-to-left text`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 10. Glyph substitution

OpenType GSUB for ligatures, alternates and the shapes other scripts need.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/gsub.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Read every lookup type with bounds checks in `ui/gsub.c`. Done when: the pinned OpenType test fonts give the expected glyphs. Cheaper substitute: single substitution.
- [ ] Choose features by script and language in `ui/gsub.c`. Done when: 300 strings give DirectWrite's glyphs. Cheaper substitute: default features always.
- [ ] Let a program turn features on and off in `ui/paragraph.c`. Done when: Cascadia Code's ligatures appear and disappear. Cheaper substitute: fixed.
- [ ] Keep caret positions inside ligatures in `ui/caret.c`. Done when: the positions equal real WinUI's. Cheaper substitute: before or after only.
- [ ] Fuzz `ui/gsub.c`. Done when: a native run of 120 seconds finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `ui: glyph substitution`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 11. Glyph positioning

OpenType GPOS and the older kerning table.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/gpos.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Read every lookup type with bounds checks in `ui/gpos.c`. Done when: the pinned test fonts give the expected positions. Cheaper substitute: pair kerning.
- [ ] Attach marks to bases, ligatures and marks in `ui/gpos.c`. Done when: 300 strings give DirectWrite's positions. Cheaper substitute: stacking by height.
- [ ] Use the `kern` table where there is no GPOS, in `ui/gpos.c`. Done when: widths equal DirectWrite's. Cheaper substitute: no kerning.
- [ ] Fuzz `ui/gpos.c`. Done when: a native run of 120 seconds finds nothing. Cheaper substitute: a few broken files.
- [ ] Commit: `ui: glyph positioning`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 12. Joining scripts

Arabic, Syriac, N'Ko and the other scripts whose letters join.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/shape_joining.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Compute joining forms in `ui/shape_joining.c`. Done when: the shaping tests of the pinned corpus pass. Cheaper substitute: isolated forms.
- [ ] Justify with kashida where WinUI does in `ui/paragraph.c`. Done when: line frames match. Cheaper substitute: spaces.
- [ ] Place marks in the right order in `ui/shape_joining.c`. Done when: 200 vowelled words give DirectWrite's positions. Cheaper substitute: input order.
- [ ] Shape across style changes in `ui/shape_joining.c`. Done when: a bold letter inside a word stays joined. Cheaper substitute: breaking the word.
- [ ] Commit: `ui: joining scripts`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 13. Indic and Southeast Asian scripts

Syllable clusters, reordering and line breaks for scripts that need them.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/shape_indic.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Find syllables and reorder for the Indic scripts in `ui/shape_indic.c`. Done when: the pinned corpus for nine scripts passes. Cheaper substitute: no reordering.
- [ ] Shape Thai, Lao, Khmer and Myanmar in `ui/shape_seasian.c`. Done when: the pinned corpus passes. Cheaper substitute: Thai only.
- [ ] Break Thai, Lao, Khmer and Myanmar lines by a pinned word list in `ui/breaks_dictionary.c`. Done when: 500 sentences break where the pinned reference breaks. Cheaper substitute: between any letters.
- [ ] Fall back to the universal cluster rules for other scripts in `ui/shape_universal.c`. Done when: a table of 10 scripts passes. Cheaper substitute: raw glyphs.
- [ ] Commit: `ui: Indic and Southeast Asian scripts`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 14. Colour glyphs and emoji

COLR glyphs from the installed emoji font, with an open font as fallback.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `ui/font_color.c` with its first item. Done when: the first item below passes.
2. Cover it in `tests/test_ui.py`. Done when: the checks fail on a planted fault.

- [ ] Read COLR version 0 and 1 and CPAL with limits in `ui/font_color.c`. Done when: frames of 200 emoji match within 4 of 255. Cheaper substitute: one colour.
- [ ] Choose text or emoji form by the variation selector in `ui/fallback.c`. Done when: a table of 50 sequences passes. Cheaper substitute: emoji always.
- [ ] Join sequences with skin tones, flags and families in `ui/gsub.c`. Done when: each published sequence the font has is one glyph. Cheaper substitute: separate glyphs.
- [ ] Draw bitmap emoji at any scale in `ui/font_color.c`. Done when: the pinned font draws within its cell. Cheaper substitute: leaving it out.
- [ ] Bound depth and cost of a colour glyph in `ui/font_color.c`. Done when: a planted endless paint graph is refused. Cheaper substitute: trusting the font.
- [ ] Commit: `ui: colour glyphs and emoji`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- The Unicode test sets and the script corpora pass in `tests/test_text.py`.
- A page of mixed Arabic, Hindi, Japanese, English and emoji draws, selects and edits in the gallery.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
