---
schema_version: 1
id: network
domain: 03-network
status: active
title: "TODO-01 -- Network"
---

# TODO-01 -- Network

> **Goal:** Programs and the OS share one safe way to connect, protect, fetch and keep a secret.

> **Current state:** Measured 2026-11-23. Tier 4 names the functions. No network code exists. FTP, SSH, Mail, Chat, News Reader, Remote Desktop and Weather need it.

## Inputs

- `docs/specs/2026-11-23-interface-and-apps-design.md`: where a feature belongs, and parts.
- `abi/tiers.json`: tier 4.


## Outcome

- One part each for connections, TLS, HTTP and secrets.
- Every reader is fuzzed and tests never leave this machine.


**Adjacency:** all=not-applicable (parts hold no records, settings or reports; programs own those)

**Adjacency rationale:** A part carries bytes; the program decides what is kept.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Random numbers and kept secrets | D02 T01 §1 | [ ] |
| 2 | §2 | Connections | §1 | [ ] |
| 3 | §3 | TLS | §2 | [ ] |
| 4 | §4 | HTTP | §3 | [ ] |
| 5 | §5 | JSON | D02 T01 §1 | [ ] |

## 1. Random numbers and kept secrets

Secure random bytes, constant time comparison, clearing memory, and keeping a secret with the system's own protection.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/secret.c`. Done when: a kept secret returns
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Give random bytes or fail, never weak ones, in `src/random.c`. Done when: a planted failure stops the program's request. Cheaper substitute: a seed.
- [ ] Keep and return secrets in `src/secret.c`. Done when: a kept secret returns for this user and its bytes are not in the file. Cheaper substitute: plain.
- [ ] Clear memory so that the compiler keeps it in `src/secret.c`. Done when: the cleared bytes are seen zero in the built program. Cheaper substitute: a loop.
- [ ] Commit: `network: random numbers and secrets`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 2. Connections

Names, connecting with both address families, waiting, stopping and time limits, without blocking a window.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/connect.c`. Done when: a local server's scripts pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Connect with both families in `src/connect.c`. Done when: a dead first address costs no more than its wait. Cheaper substitute: one family.
- [ ] Stop at any moment in `src/connect.c`. Done when: 100 stops at random moments leave no thread or socket. Cheaper substitute: waiting.
- [ ] Listen on this machine only, for signing in in `src/connect.c`. Done when: another address is not bound. Cheaper substitute: any address.
- [ ] Commit: `network: connections`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 3. TLS

A pinned Mbed TLS built freestanding, TLS 1.2 and 1.3, with a pinned root list and the host name always checked. Licence and provenance recorded.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `parts/tls/tls.c`. Done when: a local server's scripts pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Build it from source with the OS flags, and record its licence and commit in `parts/tls/tls.c`. Done when: the part builds and its notice is in `licenses/`. Cheaper substitute: a binary.
- [ ] Refuse wrong names, old, unknown, revoked by list and weak certificates in `parts/tls/tls.c`. Done when: a table of 40 planted chains is judged right. Cheaper substitute: the chain.
- [ ] Update the root list by a pinned tool in `parts/tls/tls.c`. Done when: a rebuild gives the same bytes. Cheaper substitute: by hand.
- [ ] Commit: `network: tls`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 4. HTTP

HTTP/1.1 as a stream: limits, redirects, ranges, validators, chunks and compression through the inflate part.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/http/http.c`. Done when: recorded and local answers agree
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Read answers in `parts/http/http.c`. Done when: every prefix of every sample is refused or whole, and a 120 second native fuzz run finds nothing. Cheaper substitute: well-formed answers.
- [ ] Refuse what reads two ways in `parts/http/http.c`. Done when: a table of 60 ambiguous answers is refused. Cheaper substitute: the first reading.
- [ ] Follow redirects without carrying secrets to another host in `parts/http/http.c`. Done when: the local servers see none. Cheaper substitute: carrying them.
- [ ] Commit: `network: http`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## 5. JSON

A safe reader and writer of JSON without recursion or floating point.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Write `parts/json/json.c`. Done when: the published tests pass
2. Cover it in `tests/`. Done when: the checks fail on a planted fault.

- [ ] Read in `parts/json/json.c`. Done when: every prefix of every sample is refused or whole, and a 120 second native fuzz run finds nothing. Cheaper substitute: well-formed JSON.
- [ ] Pass the published parsing tests in `parts/json/json.c`. Done when: all must-pass and must-fail files agree. Cheaper substitute: some.
- [ ] Keep numbers as text with exact conversions in `parts/json/json.c`. Done when: the table passes at every limit. Cheaper substitute: rounding.
- [ ] Commit: `network: json`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the new tests fail when their production change is reverted.

## Verification

- A local server's table of answers, faults and certificates passes from a built program.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
