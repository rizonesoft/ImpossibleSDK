---
name: project-launchers
description: Use when the operator asks to create, update, inspect, or remove Windows desktop shortcuts for this project, or when the project's title or icon changes.
---

# Project launchers

Follow the installed [docs/launchers.md](../../../docs/launchers.md) for `.conclave/launchers.json` fields, worked examples, refusal diagnosis, and all creation and removal commands. Do not duplicate or invent flags outside that guide.

## Steps

1. When the operator asks only to inspect existing shortcuts, skip creation and removal and go straight to step 4 (read-back works even when `.conclave/launchers.json` is invalid).
2. When creating or updating shortcuts, read `docs/launchers.md`, diagnose `.conclave/launchers.json` with `python scripts/lib/launchers.py show`, run the documented creation command in PowerShell 7 (`pwsh`) on Windows, and continue to step 4.
3. When removing shortcuts, run the documented `-Remove` command from `docs/launchers.md` in PowerShell 7 (`pwsh`) on Windows (reusing `-ShortcutDir`, `-IconDir`, `-Distro`, and `-Project`), verify that the project's owned `.lnk` files and rendered `.ico` files are absent while unrelated shortcuts remain, report what was removed, and stop (do not run step 4 on deleted links).
4. Read this project's shortcuts back in a fresh PowerShell 7 (`pwsh`) process without launching any link. `scripts/project-shortcuts.ps1 -Inspect` shares the script's own project-identity resolver, loads `scripts/lib/ShellLink.cs` with `Add-Type -Path`, calls `[Conclave.ShellLink]::Read` on each `.lnk` directly from disk (no manual path quoting), and surfaces any unreadable `.lnk` filename with its error. Reuse the same `-ShortcutDir`, `-Distro`, and `-Project` flags used for creation:

```powershell
pwsh -NoProfile -File scripts/project-shortcuts.ps1 -Inspect
```

5. Report the verified link names, targets, arguments, and icons (and any unreadable shortcut errors) to the operator.

## Rule

A generated desktop shortcut is only ever started by the operator. Never launch, invoke, or double-click a `.lnk` file from an agent session.
