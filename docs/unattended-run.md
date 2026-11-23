# Unattended run

An unattended run is one agent session working through `todo/implementation-plan.md` with `process-plan`, in a tmux session that outlives the terminal that started it. This page is for the operator who starts, watches and stops it. The run lives in tmux, so it runs on Linux and in WSL; on Windows a desktop shortcut starts it in WSL.

A run never restarts itself. It starts only when you start it, and `stop`, a crash or a reboot ends it. Nothing in the kit resumes a run at boot, at logon or on a timer, the automations kit refuses a scheduled task that starts one, and `start` refuses to run under a scheduled task.

## Set it up

1. Install the kit after the todo kit: `python forge.py kit <project> unattended`.
2. Set `name`, `model` and `effort` in `.conclave/unattended.json`; the template leaves them `null`, and the loader refuses the profile until they are set, except Antigravity Orchestrator models (`gemini-pro-orchestrator` and `gemini-pro-orchestrator-*`), which manage effort internally, reject `--effort`, and require `"effort": null` (setting any non-null effort is refused). To start the run on another agent as well, add it under `runners` with its own `model` and `effort`, for example `"runners": {"agy": {"model": "gemini-4-carbon-medium", "effort": "medium"}}` or `"runners": {"agy": {"model": "gemini-pro-orchestrator", "effort": null}}` (the launcher omits `--effort` for Orchestrator models on both fresh starts and `--conversation` resumes).
3. Check the machine: `python forge.py doctor <project> unattended`. It reports each kit file, the todo kit, tmux and the `unattended-run` skill in each agent's folder.

## Start it

From a terminal in the project, in WSL or on Linux:

```bash
python3 scripts/unattended-run.py start
python3 scripts/unattended-run.py start --agent agy
python3 scripts/unattended-run.py start --ships 3
```

`--agent` starts the run on one of the profile's runners, with that runner's model and effort; the preflight then checks that agent's own CLI, version and Stop hook. `--ships N` ends the run once it has shipped N sections; the heartbeat enforces it, so a bounded run needs its heartbeat, and a resume keeps the run's own bound.

`start` runs the preflight first (one PASS or FAIL line per precondition) and starts nothing on a FAIL. When the run's tmux session is gone but its marker is still there (a crash), `start` resumes that agent session; after a `stop` it starts a fresh one.

### Shortcuts

For everyday agent and project desktop shortcuts (`.conclave/launchers.json` and `scripts/project-shortcuts.ps1`), see [docs/launchers.md](launchers.md).

On Windows, `pwsh scripts/unattended-shortcut.ps1 -Distro Ubuntu -Icon <the project's icon>` puts a `<Title> <Runner> campaign` shortcut on the desktop for each runner the profile names, plus `<Title> - Watch run` and `<Title> - Stop run` (`-ShortcutDir` puts them elsewhere). The title is the one the project's other links carry, from `.conclave/launchers.json` (the launchers kit's profile, which this kit brings); until that profile sets a title, the shortcuts carry the project folder's name, and a run after the title is set renames them. A link of the project's that a run no longer writes (an earlier title, a runner taken out of the profile) is removed with its icon, and a name held by another project's link is refused before anything is written. A run the window closed on part way is finished by the next run, or by `-Remove`, from the journal every run keeps in `%LOCALAPPDATA%\conclave-unattended`, beside the lock that lets one run of the project change its shortcuts at a time. An icon from before the shortcuts had one name per project and distro goes with the shortcut that shows it, except while another distro has the same Linux path (the old name was that path's in every distro, so the icon may be the other distro's too; WSL is asked) or another shortcut on this desktop, the user's desktop or the public desktop shows it. Each runs PowerShell, which runs `wsl.exe -d <distro> --cd <project in WSL> --exec bash -lc <command>`. A campaign shortcut runs `python3 scripts/unattended-run.py start --agent <runner>` and then `attach`, so its window becomes the run's terminal, with keyboard and mouse; when a run is already going, the start is refused and the window just attaches to it. Watch runs `attach` too (detach with Ctrl-b, then d). Stop asks first and runs `stop` only when you type `y`; once it has started, closing its window does not interrupt it. Their icons are the project's icon with a round badge in the corner: the runner's mark on a campaign shortcut, an eye on Watch, a stop square on Stop. `--exec` keeps the login shell from parsing the command again. When there is no run to attach to (a start refused by the preflight, say), the window says so and waits for Enter, and it does the same when WSL itself fails (a drive gone, a distro removed).

Each run records what it wrote in `build/unattended-shortcuts.json`: its arguments and each link's command. A kit update can change those commands; `python forge.py update` then writes the links again from that record on Windows, and `status` names any link whose command is no longer the current one (`shortcuts: stale: ...`). To write them again yourself, with the arguments they were written with: `pwsh scripts/unattended-shortcut.ps1 -Refresh` (any argument you give wins over the recorded one). The record keeps one set per desktop folder; with links in more than one, give `-ShortcutDir` for each. `status` reads the record, so a link edited or deleted by hand is not seen there: `pwsh scripts/unattended-shortcut.ps1 -Check` reads the links themselves and names each one `missing`, `stale` or `current`, writing nothing. Links written before the kit kept a record are named with the one command that writes them and the record again.

On Linux, the launcher writes a desktop entry per runner:

```bash
python3 scripts/unattended-run.py shortcut
python3 scripts/unattended-run.py shortcut --agent agy
```

To remove them: `pwsh scripts/unattended-shortcut.ps1 -Remove` (with the same `-ShortcutDir` if you used one) and

```bash
python3 scripts/unattended-run.py shortcut --remove
python3 scripts/unattended-run.py shortcut --remove --dir ~/Desktop
```

Both find the project's own shortcuts by a mark they carry, so removal works whatever the profile, the icon or the distro now are, and leaves other projects' shortcuts alone.

## Watch it

```bash
python3 scripts/unattended-run.py status
python3 scripts/unattended-run.py attach
python3 scripts/unattended-run.py attach --watch
```

`status` says whether the run and its heartbeat are live, the run's session and start, its guard, the heartbeat's state, the health file (`build/unattended-health.json`: alive, sections shipped, the ship limit, the last ship and the hours since it) and any stall notice. `attach` gives you the run's terminal with keyboard and mouse: type to the agent, scroll its history with the mouse wheel (or Ctrl-b, then [, and q to leave), and drag to select. `attach --watch` attaches read-only: you can neither type into the run nor scroll its history, with keys or the mouse. Detach with tmux's prefix and `d` (Ctrl-b, then d): the run goes on.

## Attach and stop it

```bash
python3 scripts/unattended-run.py attach
python3 scripts/unattended-run.py stop
```

`attach` gives you the run's terminal; while you are attached with a client that can type and have typed or scrolled in it within the last 15 minutes, the heartbeat does not nudge it. A window left open and untouched for longer does not hold the nudge off. `stop` ends the run's guard, its tmux sessions and every process it started, records the end in the health file, and removes the run's marker, so the next `start` starts fresh. A run cannot stop itself: `stop` from inside the run is refused.

## What the heartbeat does

The heartbeat runs in its own tmux session beside the run. Every `tick_seconds` it:

- nudges the run when its window has shown nothing for `idle_minutes` (not while you are attached with a client that can type and have typed or scrolled in it within the last 15 minutes);
- counts the sections the run has shipped (rows turned `[x]` in each TODO file's Implementation Order table, commit by commit) and writes the health file;
- ends a bounded run once it has shipped its `--ships` sections;
- writes the stall notice `build/unattended-needs-operator.md` when nothing has shipped for `stall_hours`, at most once per `stall_hours`, and nudges the run to find out why;
- records a run whose session has died as ended and stops everything it started. It never restarts it.

## What you may see, and what to do

- `unknown (tmux could not be asked)`: `status` could not reach tmux. Check `tmux ls` in the same WSL distro; nothing about the run is assumed.
- `health: none`: the heartbeat has not written its health file yet (the run started moments ago, or with `--no-heartbeat`). Look again after a tick. A health file whose `updated` is old means one of two things: the heartbeat is not running (`status` shows it `stopped`: start it again, alone, in its own tmux session with `tmux new-session -d -s <name>-heartbeat python3 scripts/unattended-run.py heartbeat`; it supervises the live run and starts nothing), or it is running but cannot write (`status` shows it `live`): its own tmux session says why (`tmux attach -r -t <name>-heartbeat`), most often a full disk (`df -h .`); it keeps supervising and writes again once it can.
- `the preflight refused the run (FAIL lines above); nothing was started`: fix each FAIL line and start again.
- `start refused: it came from the scheduled task`: a scheduled task tried to start the run. Start it yourself, and remove the task.
- `the run's guard was not ended`: the run is stopped but its guard is not; run `stop` again.
- `records it once the disk takes it`: `stop` could not write the end (a full or failing disk); the heartbeat stays and writes it when it can. Free the disk; nothing else is needed.
- `Nothing has shipped for`: the stall notice. Read the run file, `git log` and the gate logs, then attach or stop the run.
- `The run did not start: the reason is above. Press Enter to close.`: a shortcut's start failed; the reason is the lines above it.
- `WSL could not`: then `start the run`, `watch the run` or `stop the run`, by shortcut. WSL itself failed before the run's command (the project's drive is gone, or the distro); fix WSL and try the shortcut again.
- `stale:`, on `status`'s `shortcuts:` line: the Windows links it names were written before a kit update changed their commands, so a window may not take your keys. Run `pwsh scripts/unattended-shortcut.ps1 -Refresh` on Windows.
- `none recorded`, on the same line: no Windows link of this project's has been written by this kit (or `-Remove` took them).
