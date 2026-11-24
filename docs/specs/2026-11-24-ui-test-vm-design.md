# UI test machine design

**Status:** approved by the operator 2026-11-24.

## Why

The live reference tests (`reference/Script.cs`) bring their window to the front, move the cursor and send real keys. On 2026-11-24 they ran about 24 times in five hours on the operator's desktop, and the operator could not work. Visual testing stays; it must stop using the operator's screen, mouse and keyboard.

## Goals

- Live tests run in a Hyper-V virtual machine and never on the operator's desktop unless the operator starts them there.
- The machine and the inventory machine of Impossible OS (`ios-win11-ref`) take turns, as far as possible, without stalling either run.
- Every live test that is owed is run before the section that owes it closes.
- The machine holds nothing of the SDK, so another project can use it later.

## Not goals

- Moving Bosun's, ScratchPad's or another project's tests now.
- A Bosun that manages virtual machines.
- A change to the pixel checks that open no window (`control-shots.exe` against `tests/reference/`).

## Approaches considered

| Approach | Trade-off | Result |
| --- | --- | --- |
| Bosun queue plus a running-VM check | Small Bosun change, each VM script uses the queue | Picked by the operator |
| Bosun manages the VMs | Central, but large and tied to Hyper-V | Not picked |
| The SDK checks alone | No Bosun change, inventory can start during an SDK run | Not picked |
| GitHub's Windows machines | No local memory, but Windows Server | Operator chose the VM |
| Only when the operator is idle | No VM, but work stalls all day | Operator chose the VM |

## The machine

- A Generation 2 Windows 11 VM, 4 GB static memory, its own name, folder on D:, account and checkpoints. It shares no disk, lock or password with `ios-win11-ref` and has no network adapter.
- Off between runs. A run restores the clean checkpoint, copies its files in, runs in the signed-in desktop session, copies results out and turns the machine off. No window opens on the operator's desktop.
- The caller brings its whole program (the reference program is published with its own .NET), so the checkpoint needs no change for a new project or version.
- The build and run scripts live in this repository first, with no SDK names in them.

## When live tests are owed

- A change to the reference program, its cases or the live tests owes a pass. A change elsewhere owes none.
- A section that owes a pass cannot close without one on its commit or a later one. One pass on the newest commit settles every earlier debt.
- Plain runs refuse real input on the operator's desktop. An explicit setting, which an unattended run never sets, allows it.

## What not now means, and how it completes

A test is never dropped. "Not now" leaves it owed, and the run does other ready work.

1. The run tries the machine at the section's checkpoint. It starts at once if no other VM holds the queue and 5 GB is available.
2. Otherwise the pass is recorded as owed, the section stays open, and the run takes the next ready section that does not depend on it.
3. It tries again after each piece of work.
4. When no other ready work is left, the run waits in the queue instead of skipping.
5. Thirty minutes after the first try, the memory figure no longer holds a pass back: the next try starts the machine with whatever memory Hyper-V accepts. Hyper-V refuses a start it cannot back, and the run tries again every 2 minutes.
6. The plan is not complete while a pass is owed. A failed test is a failed section and is fixed like any other.

The only thing that holds a pass back indefinitely is the inventory machine being in use without a break, or Hyper-V refusing the start each time. Both are recorded in the run file, with the time waited.

## Taking turns

- Both tools run their VM inside Bosun's queue `Bosun.VM`, which has one place. Without Bosun installed, they run directly.
- The SDK holds the place for at most 5 minutes and turns its machine off by then. Inventory waits at most 6 minutes, then starts anyway, so inventory is never refused.
- Until inventory uses the queue, the SDK also refuses to start while `ios-win11-ref` is running. Inventory could still start during an SDK pass, and both would be up for up to 5 minutes. The operator accepted this.
- Inventory's change goes to Impossible OS through Heliograph and its own plan.

## Changes to Bosun

- An option for `bosun run`: when the place or the memory is not available within the wait, exit with a distinct code instead of starting anyway. With it, the memory figure applies to the first place too.
- A job already inside one queue still takes its place in a different queue. Today it passes straight through.

## Order of work

1. Build the machine and run one pass by hand, measuring start time, pass time, pixels against `tests/reference/`, and mouse, keyboard and touch.
2. Change Bosun, with tests.
3. Route the live tests through the machine, add the refusal on the operator's desktop and the owed record.
4. Send inventory's request through Heliograph.

## Open facts, settled by step 1

- A pass that needs more than 5 minutes is split into parts, each with its own turn.
- If the machine's pixels differ, today's baselines stay and pictures are recaptured on the operator's desktop, only when the operator starts it.
- If touch does not work in the machine, the touch cases join that operator-started recapture.
- Section 20 (native 150 percent) may be possible in the machine. It is not part of this work.

## What the machine measured, 2026-11-24

| Fact | Measured |
| --- | --- |
| Windows install to signed-in desktop | 8 minutes |
| Start to a usable session | 6 to 22 seconds |
| Copy 250 MB in | 31 seconds |
| Whole trial, capture and live tests up to the failure | 154 seconds |
| The 36 default cases | all captured, in 29 seconds |
| The scripted button log (mouse, keyboard, touch) | equal to the stored one |
| Frames equal byte for byte | 0 of 36 |
| Ordinary control frames, worst channel difference | 8, and 33 for the icon case |
| Probe frames, worst channel difference | 35 |
| Accent frames (not Disabled) | up to 143: the guest has Windows' default accent |
| Runner killed mid-run | the guest turned itself off at its limit |

- Frames differ, so today's baselines stay and the byte comparison is not asked of the machine.
- The live test stopped at `tests/test_ui.py:3727`: the element `RealLabel` measured 4 pixels narrower than the test's own rectangle at scale 1.5. The commit tested, `f65d416`, has the reference code of `c3d2b55`, whose live tests had not been run on any machine, so whether the machine causes it is open.

## Left to the builder

Names, folders, the owed record's form, and how fonts the live tests look for are supplied to the machine.
