<!-- page: group="Guide" order=5 sources=docs/specs/2026-11-23-interface-and-apps-design.md,abi/tiers.json reviewed=2026-11-24 -->
# With Impossible OS

How do the two projects fit together? The SDK keeps the list of functions, the controls and the shared parts; [Impossible OS](https://impossibleos.co/) builds every function on that list and imports programs at a pinned commit.

## Does your program need Impossible OS?

No. A program is finished when it works on Windows. No state of the operating system can fail its build, tests or release.

## Does your program run on it today?

Not yet. Impossible OS is building the functions programs call; [what programs need](https://github.com/rizonesoft/impossible-os/blob/main/todo/10-platform-services/TODO-13-program-needs.md) is its list.

## Who owns what?

| Impossible OS | Impossible SDK |
| --- | --- |
| Kernel, drivers and engines | The controls, in portable C |
| Every Windows function on the list | The list itself, in [tiers](site:tiers.html) |
| File types, the desktop, settings | Parts two programs need |
| Importing programs and controls | Tools that build and prove them |

## How does a program say what it needs?

Functions need no declaration: they are measured from the finished program. What imports cannot show goes into `needs` in the program's `app.json`, and becomes a section of the operating system's roadmap. The program never waits for it.

## How does a new function get on the list?

On purpose, as a change to `abi/tiers.json` in the SDK, because someone then has to build it in the operating system. The design is in [the specification](../specs/2026-11-23-interface-and-apps-design.md).
