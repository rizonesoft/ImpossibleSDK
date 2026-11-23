# Contributing

Thank you for looking at Impossible SDK. It is early, so a good bug report or a small, proved fix helps most.

## Build and test

```
python tools/bootstrap.py
python tools/build.py
```

Use Windows or WSL (there the command is `python3`). `python3 tools/prove.py .` in WSL lets Windows run the tests against the committed programs; a change counts when that passes.

## What a change needs

- A test that fails before it and passes after it.
- No new Windows function, unless the change is about that function: each one in `abi/tiers.json` has to be built in [Impossible OS](https://github.com/rizonesoft/impossible-os) as well.
- `ui/` stays portable: no system call, no floating point, no C library.
- Values and behaviour of controls come from the pinned WinUI source or a measurement, never from memory. No Microsoft font, icon, picture, sound or wording.
- A public function has a comment above it, and the [documentation](https://sdk.impossibleos.co/docs/) still says what the code does: `python tools/site/build.py --check` after a build.
- New code of other projects comes with its license and the commit it was taken from.

A feature that two programs need belongs here; one that only one program needs belongs in that program. The design is in [`docs/specs`](docs/specs/2026-11-23-interface-and-apps-design.md).

## Where work is planned

[`todo/`](todo/implementation-plan.md) is the work list. The operating system's side is in the [Impossible OS roadmap](https://github.com/rizonesoft/impossible-os/tree/main/todo). Questions and ideas go to [Discussions](https://github.com/rizonesoft/ImpossibleSDK/discussions).
