---
schema_version: 1
id: web-readers
domain: 04-web
status: active
title: "TODO-01 -- Web Readers"
---

# TODO-01 -- Web Readers

> **Goal:** Browser, Mail and News Reader read HTML and CSS through one set of safe parts.

> **Current state:** Measured 2026-11-23. No HTML or CSS code exists. Browser's rung 1, Mail and News Reader need it.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: where a feature belongs, and parts.
- The HTML Standard and the CSS Syntax and Selectors specifications.


## Outcome

- The published HTML parser tests all pass.
- Every reader is fuzzed, and none calls the system or uses floating point.


**Adjacency:** all=not-applicable (parts hold no records, settings or reports; programs own those)

**Adjacency rationale:** A part turns text into a tree; the program decides what may act.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | HTML tokenizer | D02 T01 §1 | [ ] |
| 2 | §2 | HTML tree builder | §1 | [ ] |
| 3 | §3 | Safe subset | §2 | [ ] |
| 4 | §4 | CSS syntax and selectors | D02 T01 §1 | [ ] |
| 5 | §5 | Values and cascade | §4 | [ ] |

## 1. HTML tokenizer

The HTML Standard's tokenizer with every state, on text of any encoding already decoded.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/html/token.c`. Done when: the published tokenizer tests pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Write every state in `parts/html/token.c`. Done when: all published tokenizer tests pass, split at every position. Cheaper substitute: well-formed HTML.
- [ ] Keep each token's span in the decoded input in `parts/html/token.c`. Done when: reading that span again gives the token, with character references, CRLF and NUL in it. Cheaper substitute: searching for its text.
- [ ] Survive hostile text in `parts/html/token.c`. Done when: every prefix of every sample is refused or whole, and a 120 second native fuzz run finds nothing. Cheaper substitute: a length limit.
- [ ] Commit: `web: html tokenizer`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. HTML tree builder

The standard's insertion modes as a machine that tells its user what to make, insert, insert before, move and remove, so that each program keeps its own tree.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/html/build.c`. Done when: the published tree tests pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Write every mode in `parts/html/build.c` without recursion. Done when: all published tree construction tests pass. Cheaper substitute: a nesting stack.
- [ ] Give the calls to move nodes and to insert before a table in `parts/html/build.h`. Done when: the misnested formatting and table-text cases pass through a caller's own tree. Cheaper substitute: making and appending.
- [ ] Let the user pause, for scripts later in `parts/html/build.c`. Done when: pausing after every token gives the same tree. Cheaper substitute: at the end.
- [ ] Bound depth and size in `parts/html/build.c`. Done when: documents made to be deep stop at the limit within fixed memory. Cheaper substitute: the stack.
- [ ] Commit: `web: html tree builder`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. Safe subset

What Mail and News Reader may show: structure, text, tables and colours, and nothing that acts or loads.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/html/safe.c`. Done when: hostile samples are harmless
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Allow by list in `parts/html/safe.c`. Done when: a table of 300 hostile samples keeps no address, script, style or form that acts. Cheaper substitute: removing scripts.
- [ ] Keep meaning: headings, lists, tables, quotes, links as text with their target in `parts/html/safe.c`. Done when: the table passes. Cheaper substitute: plain text.
- [ ] Survive hostile text in `parts/html/safe.c`. Done when: every prefix of every sample is refused or whole, and a 120 second native fuzz run finds nothing. Cheaper substitute: a length limit.
- [ ] Commit: `web: safe subset`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. CSS syntax and selectors

Tokenizer, parser and selectors with the standard's error rules.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/css/syntax.c`. Done when: the pinned tests pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Tokenize and parse in `parts/css/syntax.c`. Done when: every prefix of every sample is refused or whole, and a 120 second native fuzz run finds nothing. Cheaper substitute: a subset.
- [ ] Parse and weigh selectors in `parts/css/select.c`. Done when: the pinned selector tests pass. Cheaper substitute: classes.
- [ ] Match through callbacks, so that each program keeps its own tree in `parts/css/syntax.c`. Done when: random trees and selectors agree with a plain matcher. Cheaper substitute: one tree.
- [ ] Commit: `web: css syntax and selectors`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. Values and cascade

Property values in fixed point, shorthands, origins, importance, layers and inheritance.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/css/cascade.c`. Done when: the pinned tests pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Parse values from one table in `parts/css/values.c`. Done when: each property's valid and invalid tests pass. Cheaper substitute: by hand for each.
- [ ] Cascade in `parts/css/cascade.c`. Done when: random sheets agree with a plain model. Cheaper substitute: order only.
- [ ] Compute lengths and colours without floating point in `parts/css/cascade.c`. Done when: the table passes to its stated error. Cheaper substitute: doubles.
- [ ] Commit: `web: values and cascade`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- The published HTML parser tests pass from a built program, and Mail's hostile samples are harmless.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
