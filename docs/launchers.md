# Project launchers

Windows desktop shortcuts that open this project in a coding agent, run a project command, or open its site in your browser, each with the project's own icon and a corner mark.

## What are project launcher links?

Project launchers are everyday Windows `.lnk` shortcuts written from `.conclave/launchers.json`. Each agent named in `agents` gets a `<Title> (<Agent>)` shortcut that opens a terminal in the project with that agent running, and each entry in `links` gets a `<Title> (<label>)` shortcut that runs a command in the project or opens a URL in your default browser. Every shortcut carries an icon rendered on your machine from the project's icon with a rounded-square corner mark (`claude`, `codex`, `agy`, `wordpress`, or `site`).

## How do I set title, icon, agents and skip_permissions?

A fresh install writes `.conclave/launchers.json` with `"title": null`, `"icon": null`, `"agents": ["claude"]`, `"skip_permissions": false`, and `"links": []`. Both `title` and `icon` must be set before shortcuts can be written.

- `schema_version`: must be `1`.
- `title`: 1 to 60 characters valid in a Windows file name, with no leading or trailing space and no trailing dot.
- `icon`: a project-relative `.png` or `.ico` file inside the project (no `..` and no symlink). Use a square PNG of 256 pixels or more so every icon frame stays crisp. `scripts/lib/launchers.py show` checks only the path, extension, and file metadata (`stat`); it does not open the file's bytes, while `scripts/campaign-icon.py` opens the file and requires an 8-bit, non-interlaced PNG, or an ICO containing a readable PNG frame. If rendering fails even though `show` succeeds, check file read permissions first, and if the file is readable, re-export it as an 8-bit, non-interlaced RGBA square PNG (256x256 or larger).
- `agents`: a non-empty list naming one or more of `claude`, `codex`, and `agy`, each at most once.
- `skip_permissions`: `true` or `false` (default `false`). When `true`, agent shortcuts pass the runner's permission-skip flag (`--dangerously-skip-permissions` for Claude Code, `--dangerously-bypass-approvals-and-sandbox` for Codex) when you open the shortcut.

## How do I declare a run or url link?

Each entry in `links` is an object with `label`, `mark` (`site` or `wordpress`), and either `url` or `run` (never both).

- `label`: 1 to 30 characters under the `title` naming rule, unique case-insensitively and by icon file slug, never an agent name, and never ending in `campaign`.
- `url`: an `https://` address, or an `http://` address on `localhost`, `.test`, or `.site`. The shortcut opens it with `rundll32.exe url.dll,FileProtocolHandler <url>` and no console window.
- `run`: a single-line command. For a project on a Windows drive, the shortcut runs the command in a child PowerShell 7 (`pwsh.exe -EncodedCommand`) in the project directory unless `"wsl": true` is set, in which case it runs through `wsl.exe --cd <project> --exec bash -lc` (requiring `wsl.exe` on `PATH`). For a project in WSL, the shortcut always runs it through `wsl.exe -d <distro> --cd <linux path> --exec bash -lc` in a subshell in the project directory. Either way, if the command fails, the window stays open until Enter.

### Worked example (Laravel application)

```json
{
  "schema_version": 1,
  "title": "Shop App",
  "icon": "public/favicon.png",
  "agents": ["claude", "codex"],
  "skip_permissions": false,
  "links": [
    {"label": "Site", "mark": "site", "url": "https://example.test"},
    {"label": "App Check", "mark": "site", "run": "python -c \"print('laravel-ok')\""}
  ]
}
```

## How do I write the links?

Run `scripts/project-shortcuts.ps1` in PowerShell 7 (`pwsh`) on Windows:

```powershell
pwsh scripts/project-shortcuts.ps1
```

For a project in WSL, invoke the script by its `\\wsl.localhost` path (the distro is read from the path) or pass `-Distro` and `-Project`:

```powershell
pwsh \\wsl.localhost\Ubuntu\home\me\site\scripts\project-shortcuts.ps1
pwsh scripts/project-shortcuts.ps1 -Distro Ubuntu -Project /home/me/site
```

The script always reads `.conclave/launchers.json` and the icon file relative to the script's own installed project folder; `-Project` (with `-Distro` for a Linux path) selects the launch destination and the project's ownership identity. Pass `-ShortcutDir <folder>` to write links to a folder other than your Desktop and `-IconDir <folder>` to store rendered `.ico` files outside `%LOCALAPPDATA%\<project folder>`.

Changes to `.conclave/launchers.json` (such as renaming `title` or removing a link) take effect when you rerun the command, and a successful rerun deletes stale everyday links of the same project that the profile no longer names.

## How do I remove the links?

Run the same script in PowerShell 7 (`pwsh`) with `-Remove`:

```powershell
pwsh scripts/project-shortcuts.ps1 -Remove
```

Reuse the same `-ShortcutDir`, `-IconDir`, `-Distro`, and `-Project` arguments you used when writing the links so the script matches the same project identity and folders. Because `-Remove` uses only the project identity stored in each shortcut's description, it works even when `.conclave/launchers.json` is invalid, and it leaves shortcuts belonging to other projects untouched. If the project folder itself has already been deleted, run a surviving installed copy of `scripts/project-shortcuts.ps1` (with `scripts/lib/ShellLink.cs` beside it) with `-Remove -Project <original absolute path>` and the original `-Distro`, `-ShortcutDir`, and `-IconDir` arguments.

When the `unattended` kit is installed, `scripts/unattended-shortcut.ps1` owns the campaign (`<Title> <Runner> campaign`), Watch run, and Stop run shortcuts; everyday `-Remove` never touches those three. See [Unattended run shortcuts](unattended-run.md#shortcuts) to create or remove them.

## What does each refusal mean?

Diagnose `.conclave/launchers.json` first with:

```powershell
python scripts/lib/launchers.py show
```

When the profile is valid, `show` prints one line of JSON and exits 0. When it is invalid, `show` exits 1 and names the exact field and reason; fix that field in `.conclave/launchers.json` and rerun `show` until it prints JSON.

- `title: must be set` or `icon: must be set`: the fresh install leaves both `null`; set a valid title and project-relative `.png` or `.ico` path.
- `icon: ... does not exist in the project` / `must stay inside the project` / `reached through a link`: place a real `.png` or `.ico` file inside the project and point `icon` at its relative path.
- `campaign-icon.py` error after `show` succeeds: `show` checks only file metadata, so rendering can still fail if file-read permissions deny reading the icon bytes or if the file is not an 8-bit, non-interlaced PNG or an ICO with a readable PNG frame; check read permissions first, otherwise re-export it as an 8-bit, non-interlaced RGBA square PNG (256x256 or larger).
- `agents: must name at least one of ...` / `is not an agent` / `is named twice`: list distinct values from `claude`, `codex`, `agy`.
- `links[N].label` / `.mark` / `.run` / `.url` errors: keep labels 1 to 30 valid filename characters (not an agent name, not ending in `campaign`, unique by case and icon slug), use mark `site` or `wordpress`, keep `run` to one line, and use `https://` or `http://` on `localhost`, `.test`, or `.site`.
- `project-shortcuts: python is not on PATH` / `wsl.exe is not on PATH`: install Python 3 or WSL and put it on `PATH`.
- `project-shortcuts: the project folder does not exist: '<Project>'`: the Windows `-Project` path was mistyped or does not exist; pass an existing Windows folder path, or omit `-Project` to use the script's own project folder.
- `project-shortcuts: a Linux path needs -Distro` / `the project is in distro ...` / `WSL has no distro ...` / `the project path ... does not exist`: pass a matching `-Distro` and existing Linux path in WSL.
- `project-shortcuts: '<link>' is already there and is not this project's link`: another project or manual shortcut already holds that filename; move it or pick a different `title` or `label`. If an older shortcut written before Unicode support carries a lossy mark, inspect it to verify it is yours, move it aside, and rerun.
- `project-shortcuts: pass either -Remove or -Inspect, not both`: choose one operation at a time.
- `project-shortcuts: missing ...ShellLink.cs (python forge.py kit <project> launchers installs it)`: restore the launchers kit's shortcut helper (`python forge.py kit <project> launchers`).
- `project-shortcuts: could not read '...' to check ownership (close whatever holds it and retry)`: another program holds an exclusive lock on a `.lnk` file in the shortcut folder; close it and rerun.
- `project-shortcuts: the shortcut folder cannot be read: '<ShortcutDir>'`: pass an existing, readable `-ShortcutDir` folder (or fix permissions on the Desktop folder).
