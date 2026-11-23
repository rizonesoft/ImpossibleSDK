#!/usr/bin/env python3
"""Report the CI state of main: the latest completed run of each watched workflow, and runs still in progress.

    python scripts/ci_status.py                  # the watched workflows on main
    python scripts/ci_status.py --json           # the same report as one JSON object
    python scripts/ci_status.py --workflow build.yml --branch main
    python scripts/ci_status.py --since-last     # every run that finished since the last read, each failure named
    python scripts/ci_status.py --rerun <commit> # rerun a stamp candidate's own run a newer push cancelled

For each watched workflow it reads the last 10 runs with `gh run list` and reports the latest completed run (green, red or cancelled), its verdict,
and how many newer runs are still queued or in progress. A cancelled run carries no verdict (a newer push usually
superseded it), so the verdict is the latest completed run that was not cancelled; with none in the last 10 runs the
verdict is `none`. For a red verdict it names the failed jobs from `gh run view <id> --json jobs`.

The watched workflows are the `--workflow` arguments, else the `workflows` list in `.conclave/ci.json` (the ones whose
results gate main), else every `.yml` and `.yaml` file in `.github/workflows/`.

Runs still queued or in progress are named with their commit and URL, older ones included.

`--since-last` lists every run of each watched workflow on the branch created from 35 days before the last read to
today (GitHub reruns a run only within 30 days of its creation, and a run lasts at most 35 days, so any run that can
have finished or been rerun since the last read was created in that range), splitting a range whose list is full down to the second,
(a first read also takes the newest runs wherever they are) and reports each attempt that finished since the last read, by run ID and attempt; `build/ci-status-cursor.json` keeps
per branch and workflow the time of the read and the highest attempt reported for each run. So a failure followed by a
green, a run that finishes after a newer one, and a rerun of any run are each seen once, however many runs came
between, a rerun's earlier attempts read too. The state is written after the read printed everything, and never when
a read failed (exit 3); an unreadable state is said and read as a first read, which reports the latest completed run
and everything after it. The process-phase skill reads it after every push; nothing in it refuses or holds back a
commit or a push.

`--rerun <commit>` reruns, with `gh run rerun`, the latest run of each watched workflow on that commit when a newer
push cancelled it (or, with `--infrastructure`, when it is red from an infrastructure failure: the red-first rule's
rerun), only when GitHub lists nothing of the workflow on the branch queued or running (a busy queue is exit 5: try
again at the next boundary). Where GitHub will not rerun it (older than its 30-day window, or its rerun limit) it
says so (exit 1) and the candidate is written in the run file as open, for the operator; evidence never moves to
another commit.

Exit 0: no watched workflow is red (a repository with no workflow to watch says so and exits 0). Exit 1: at least one is red (the workflow, the commit, the run's URL and its
failed jobs are printed). Exit 2: bad usage. Exit 3: `gh` is missing, not authenticated, or failed, so the state is
unknown; that is never treated as green. The red-first policy that consumes this is in the `process-phase` skill.
Stdlib only; the `gh` runner is injectable for tests.
"""
import argparse
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(".conclave") / "ci.json"
FIELDS = "databaseId,headSha,status,conclusion,url,createdAt,event,attempt"
CURSOR = Path("build") / "ci-status-cursor.json"
PAGES = (10,)   # a first read's newest runs
# A run can still change after a read when it was created at most this long before it: GitHub reruns a run only
# within 30 days of its creation, and a run lasts at most 35 days.
REACH_DAYS = 35
LIST_LIMIT = 1000   # runs per `gh run list` call; a fuller date range is split until each fits
SINCE_PAGES = (10, 50, 200)   # the pages tried before a full list
LIMIT = 10
GREEN = {"success", "neutral", "skipped"}
# A run that was stopped before it judged the commit: no verdict either way.
NO_VERDICT = {"cancelled", "stale"}
MAX_JOBS = 20
MAX_MESSAGE = 300


class Unavailable(Exception):
    """gh is missing, unauthenticated or failed: the CI state is unknown (exit 3)."""


def run_gh(args):
    """Run gh with args; returns (exit code, stdout, stderr). Raises Unavailable when gh is not installed."""
    # An explicit path: on Windows a bare which() also searches the working directory, where a planted gh could sit.
    executable = shutil.which("gh", path=os.environ.get("PATH", ""))
    if executable is None:
        raise Unavailable("gh is not installed or not on PATH")
    try:
        done = subprocess.run([executable, *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
                              errors="replace", timeout=120)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise Unavailable(f"gh could not run: {exc}") from None
    return done.returncode, done.stdout, done.stderr


def gh_json(runner, args):
    try:
        code, out, err = runner(args)
    except FileNotFoundError:
        raise Unavailable("gh is not installed or not on PATH") from None
    if code != 0:
        reason = (err or out or "").strip().splitlines()
        raise Unavailable(f"gh {' '.join(args[:2])} exited {code}: {(reason[0] if reason else 'no output')[:MAX_MESSAGE]}")
    try:
        return json.loads(out)
    except ValueError:
        raise Unavailable(f"gh {' '.join(args[:2])} did not return JSON") from None


def watched(root):
    """The workflows .conclave/ci.json names, else every workflow file; a malformed config is a usage error."""
    path = Path(root) / CONFIG
    if path.is_file():
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except ValueError:
            raise ValueError(f"{CONFIG.as_posix()} is not JSON") from None
        names = doc.get("workflows") if isinstance(doc, dict) else None
        if names is not None:
            if not (isinstance(names, list) and all(isinstance(n, str) and n for n in names)):
                raise ValueError(f"{CONFIG.as_posix()} workflows must be a list of workflow file names")
            return list(names)
    directory = Path(root) / ".github" / "workflows"
    if not directory.is_dir():
        return []
    return sorted(p.name for p in directory.iterdir() if p.is_file() and p.suffix in (".yml", ".yaml"))


def verdict_of(run):
    if run is None:
        return "none"
    conclusion = (run.get("conclusion") or "").lower()
    if conclusion in GREEN:
        return "green"
    if conclusion in NO_VERDICT:
        return "cancelled"
    return "red"


def brief(run):
    return None if run is None else {k: run.get(k) for k in ("databaseId", "headSha", "status", "conclusion", "url",
                                                              "createdAt", "event", "attempt")}


def list_runs(runner, workflow, branch, limit=LIMIT):
    runs = gh_json(runner, ["run", "list", "--branch", branch, "--workflow", workflow, "--limit", str(limit),
                            "--json", FIELDS])
    if not isinstance(runs, list) or not all(isinstance(r, dict) for r in runs):
        raise Unavailable(f"gh run list for {workflow} did not return a list of runs")
    return sorted(runs, key=lambda r: str(r.get("createdAt") or ""), reverse=True)


def pending_of(runs):
    """Every run still queued or in progress, newest first, with its commit and URL."""
    return [brief(r) for r in runs if r.get("status") != "completed"]


def workflow_status(runner, workflow, branch):
    runs = list_runs(runner, workflow, branch)
    completed = [r for r in runs if r.get("status") == "completed"]
    latest = completed[0] if completed else None
    decisive = next((r for r in completed if verdict_of(r) != "cancelled"), None)
    newer = [r for r in runs if r.get("status") != "completed"
             and (latest is None or str(r.get("createdAt") or "") > str(latest.get("createdAt") or ""))]
    entry = {"workflow": workflow, "verdict": verdict_of(decisive), "latest_completed": brief(latest),
             "latest_completed_state": verdict_of(latest), "verdict_run": brief(decisive), "in_progress": len(newer),
             "pending": pending_of(runs)}
    if entry["verdict"] == "red":
        entry["failed_jobs"] = failed_jobs(runner, decisive.get("databaseId"))
    return entry


def failed_jobs(runner, run_id):
    """The failed jobs of one run, or a one-line note when they cannot be read (the run is still red)."""
    try:
        view = gh_json(runner, ["run", "view", str(run_id), "--json", "jobs"])
    except Unavailable as exc:
        return [{"name": f"jobs unavailable: {exc}", "conclusion": None, "url": None}]
    jobs = view.get("jobs") if isinstance(view, dict) else None
    if not isinstance(jobs, list):
        return [{"name": "jobs unavailable: gh run view returned no job list", "conclusion": None, "url": None}]
    failed = [j for j in jobs if isinstance(j, dict) and verdict_of(j) == "red" and j.get("status") == "completed"]
    return [{"name": j.get("name"), "conclusion": j.get("conclusion"), "url": j.get("url")} for j in failed[:MAX_JOBS]]


def report(runner, workflows, branch):
    """The report dict and its exit code; Unavailable propagates (exit 3)."""
    entries = [workflow_status(runner, w, branch) for w in workflows]
    code = 1 if any(e["verdict"] == "red" for e in entries) else 0
    return {"branch": branch, "workflows": entries, "exit": code}, code


def describe(entry):
    run = entry["verdict_run"]
    where = f" at {run['headSha'][:12]} ({run['url']})" if run and run.get("headSha") and run.get("url") else ""
    lines = [f"{entry['workflow']}: {entry['verdict']}{where}"]
    if entry["latest_completed_state"] == "cancelled":
        lines.append(f"  latest completed run was cancelled: {entry['latest_completed']['url']}")
    if entry["in_progress"]:
        lines.append(f"  {entry['in_progress']} newer run(s) still queued or in progress")
    for run in entry.get("pending", []):
        lines.append(f"  still running: {(run.get('headSha') or '')[:12]} {run.get('status')} {run.get('url')}")
    for job in entry.get("failed_jobs", []):
        lines.append(f"  failed job: {job['name']} ({job['conclusion']}) {job['url'] or ''}".rstrip())
    return lines


def _write_atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f"{path.name}.{os.getpid()}.tmp")
    try:
        temporary.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        os.replace(temporary, path)
    except OSError:
        temporary.unlink(missing_ok=True)
        raise


def _load_state(path):
    """(state, note): the saved state, or {} with why when there is none or it cannot be read."""
    if not path.exists():
        return {}, None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict) or not all(isinstance(v, dict) for v in data.values()):
            raise ValueError("not an object of objects")
        return data, None
    except (OSError, ValueError) as exc:
        return {}, f"{path.name} could not be read ({exc}); read as a first read, and replaced after this read"


def _attempt_conclusion(runner, run_id, attempt):
    view = gh_json(runner, ["api", f"repos/{{owner}}/{{repo}}/actions/runs/{run_id}/attempts/{attempt}"])
    if not isinstance(view, dict):
        raise Unavailable(f"gh api for run {run_id} attempt {attempt} did not return an object")
    return view.get("status"), view.get("conclusion"), view.get("html_url") or view.get("url")


def _now():
    return datetime.now(timezone.utc)


STAMP = "%Y-%m-%dT%H:%M:%SZ"


def list_created(runner, workflow, branch, first, last):
    """Every run of the workflow on the branch created from `first` to `last` (UTC datetimes, both included), whatever
    their number: a range whose list fills LIST_LIMIT is split in two and each half listed, down to one second, which
    would need LIST_LIMIT runs created in the same second to fill."""
    for limit in (*SINCE_PAGES, LIST_LIMIT):   # pages of 10, 50 and 200, then a full list, until one is not full
        runs = gh_json(runner, ["run", "list", "--branch", branch, "--workflow", workflow, "--created",
                                f"{first.strftime(STAMP)}..{last.strftime(STAMP)}", "--limit", str(limit),
                                "--json", FIELDS])
        if not isinstance(runs, list) or not all(isinstance(r, dict) for r in runs):
            raise Unavailable(f"gh run list for {workflow} did not return a list of runs")
        if len(runs) < limit:
            return runs
    if last - first < timedelta(seconds=1):
        raise Unavailable(f"{workflow} has {LIST_LIMIT} or more runs created at {first.strftime(STAMP)}; the cursor is "
                          f"left where it was, so nothing is skipped")
    middle = first + (last - first) / 2
    middle = middle.replace(microsecond=0)
    return list_created(runner, workflow, branch, first, middle) + \
        list_created(runner, workflow, branch, middle + timedelta(seconds=1), last)


def since_last(runner, workflows, branch, root):
    """The new completed attempts since the last read, the pending runs, the new state and the exit code.

    Every read lists every run of the workflow on the branch created from REACH_DAYS before the last read (before now
    on a first read) to today: any run that can have finished, or been rerun, since the last read was created in
    that range. Each run's attempt is compared with the highest attempt already reported for it (kept per branch and
    workflow, with the time of the read), and each attempt above it that has finished is reported once, a rerun's
    earlier attempts read through `gh api`. So a failure followed by a green, a run that finishes after a newer one
    and a rerun of any run are all seen, however many runs came between. Nothing is cut short: a date range whose
    list is full is split (`list_created`)."""
    path = Path(root) / CURSOR
    state, note = _load_state(path)
    new_state = dict(state)
    report = {"branch": branch, "workflows": [], "note": note}
    failed = False
    now = _now()
    for workflow in workflows:
        key = f"{branch}|{workflow}"
        saved = state.get(key)
        reported = {str(k): int(v) for k, v in (saved.get("reported", {}) if saved else {}).items()}
        def when(field):
            try:
                return datetime.strptime(saved[field], STAMP).replace(tzinfo=timezone.utc)
            except (TypeError, KeyError, ValueError):
                return None
        # The horizon (the creation time of the oldest run unfinished at the last read, else that read's time) and
        # the read's time; a cursor written before `read_at` existed holds only its horizon, which reaches no less far.
        horizon = when("horizon") if saved else None
        read_at = (when("read_at") if saved else None) or horizon or now
        first = min(horizon or read_at, min(read_at, now) - timedelta(days=REACH_DAYS)).replace(microsecond=0)
        runs = list_created(runner, workflow, branch, first, now.replace(microsecond=0))
        if saved is None:
            # A first read (or one after an unreadable state) also takes the newest runs wherever they are, so a
            # repository idle for longer than the reach still reports its latest verdict.
            known = {str(r.get("databaseId")) for r in runs}
            runs += [r for r in list_runs(runner, workflow, branch, PAGES[0]) if str(r.get("databaseId")) not in known]
        runs = sorted(runs, key=lambda r: str(r.get("createdAt") or ""))
        if saved is not None and "reported" not in saved and horizon is not None:
            # A cursor of the earlier format: what was created before its horizon and finished was reported then.
            for r in runs:
                if str(r.get("createdAt") or "") < horizon.strftime(STAMP) and r.get("status") == "completed":
                    reported[str(r.get("databaseId"))] = int(r.get("attempt") or 1)
        if saved is None:
            # A first read: the latest completed run and everything after it; what came before counts as reported.
            completed = [r for r in runs if r.get("status") == "completed"]
            start_at = str(completed[-1].get("createdAt") or "") if completed else ""
            for r in completed:
                if str(r.get("createdAt") or "") < start_at:
                    reported[str(r.get("databaseId"))] = int(r.get("attempt") or 1)
        new = []
        for run in runs:
            run_id = str(run.get("databaseId"))
            attempts = int(run.get("attempt") or 1)
            for n in range(reported.get(run_id, 0) + 1, attempts + 1):
                if n == attempts:
                    if run.get("status") != "completed":
                        break
                    conclusion, url = run.get("conclusion"), run.get("url")
                else:
                    status, conclusion, url = _attempt_conclusion(runner, run_id, n)
                    if status != "completed":
                        break
                verdict = verdict_of({"conclusion": conclusion})
                item = {"run": run.get("databaseId"), "attempt": n, "headSha": run.get("headSha"), "url": url,
                        "verdict": verdict}
                if verdict == "red":
                    failed = True
                    if n == attempts:
                        item["failed_jobs"] = failed_jobs(runner, run_id)
                new.append(item)
                reported[run_id] = n
        # Kept for every run the next read can list (created up to REACH_DAYS before this read).
        keep_from = (now - timedelta(days=REACH_DAYS)).strftime("%Y-%m-%d")
        kept = {str(r.get("databaseId")) for r in runs if str(r.get("createdAt") or "")[:10] >= keep_from}
        unfinished = [str(r.get("createdAt") or "") for r in runs if r.get("status") != "completed"]
        new_state[key] = {"read_at": now.strftime(STAMP), "horizon": min(unfinished) if unfinished else now.strftime(STAMP),
                          "reported": {k: v for k, v in sorted(reported.items()) if k in kept}}
        report["workflows"].append({"workflow": workflow, "new": new,
                                    "pending": pending_of(sorted(runs, key=lambda r: str(r.get("createdAt") or ""),
                                                                 reverse=True))})
    code = 1 if failed else 0
    report["exit"] = code
    return report, new_state, code


def describe_since(report):
    lines = [f"ci-status: {report['note']}"] if report.get("note") else []
    for entry in report["workflows"]:
        if not entry["new"] and not entry["pending"]:
            lines.append(f"{entry['workflow']}: nothing new since the last read")
        for item in entry["new"]:
            again = f" (attempt {item['attempt']})" if item["attempt"] > 1 else ""
            lines.append(f"{entry['workflow']}: {item['verdict']}{again} at {(item['headSha'] or '')[:12]} {item['url']}")
            for job in item.get("failed_jobs", []):
                lines.append(f"  failed job: {job['name']} ({job['conclusion']}) {job['url'] or ''}".rstrip())
        for run in entry["pending"]:
            lines.append(f"{entry['workflow']}: still running at {(run.get('headSha') or '')[:12]} {run.get('url')}")
    return lines


def rerun(runner, workflows, branch, commit, infrastructure=False):
    """Rerun the candidate's own latest run per workflow when a push cancelled it, or, with `infrastructure`, when it
    is red from an infrastructure failure (the red-first rule's rerun), with `gh run rerun`, only when nothing of the
    workflow on the branch is queued or running. Where GitHub will not rerun it (older than its 30-day window, or its
    rerun limit), it says so and the candidate is written in the run file as open. Returns (lines, exit code)."""
    lines = []
    code = 0
    for workflow in workflows:
        # Asked for by commit, so no number of newer runs can push the candidate's own run out of reach.
        runs = gh_json(runner, ["run", "list", "--branch", branch, "--workflow", workflow, "--commit", commit,
                                "--limit", "20", "--json", FIELDS])
        if not isinstance(runs, list) or not all(isinstance(r, dict) for r in runs):
            raise Unavailable(f"gh run list for {workflow} did not return a list of runs")
        runs = sorted((r for r in runs if r.get("headSha") == commit), key=lambda r: str(r.get("createdAt") or ""),
                      reverse=True)
        if not runs:
            lines.append(f"{workflow}: no run of {commit[:12]} on {branch}; write it in the run file as open")
            code = max(code, 1)
            continue
        run = runs[0]
        state = verdict_of(run) if run.get("status") == "completed" else "running"
        if state in ("green", "running"):
            lines.append(f"{workflow}: {commit[:12]} is {state} ({run.get('url')}); nothing to rerun")
            continue
        if state == "red" and not infrastructure:
            lines.append(f"{workflow}: {commit[:12]} is red ({run.get('url')}); repair it red first (process-phase "
                         f"step 3; an infrastructure failure is rerun with --rerun <commit> --infrastructure)")
            code = max(code, 1)
            continue
        # Only on an idle queue (asked of GitHub by status): a rerun queued behind another run is the one a push drops.
        busy = []
        for status in ("queued", "waiting", "pending", "requested", "in_progress"):
            listed = gh_json(runner, ["run", "list", "--branch", branch, "--workflow", workflow, "--status", status,
                                      "--limit", "5", "--json", FIELDS])
            busy += listed if isinstance(listed, list) else []
        if busy:
            lines.append(f"{workflow}: {len(busy)} run(s) still queued or running ({busy[0].get('url')}); rerun "
                         f"{commit[:12]} when the queue is idle")
            code = max(code, 5)
            continue
        result, out, err = runner(["run", "rerun", str(run.get("databaseId")), *(["--failed"] if state == "red" else [])])
        if result != 0:
            reason = (err or out or "").strip().splitlines()
            lines.append(f"{workflow}: gh run rerun {run.get('databaseId')} was refused "
                         f"({(reason[0] if reason else 'no output')[:MAX_MESSAGE]}); write {commit[:12]} in the run "
                         f"file as open")
            code = max(code, 1)
            continue
        why = "an infrastructure red" if state == "red" else "a run a push cancelled"
        lines.append(f"{workflow}: reran {run.get('url')} on {commit[:12]} ({why})")
    return lines, code


def main(argv=None, runner=run_gh, root=ROOT):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--json", action="store_true", help="print the report as one JSON object")
    parser.add_argument("--branch", default="main", help="the branch whose runs are read (default main)")
    parser.add_argument("--workflow", action="append", dest="workflows",
                        help="a workflow file in .github/workflows/ (repeatable; default: .conclave/ci.json, else all)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--since-last", action="store_true", help="every run that finished since the last read")
    mode.add_argument("--rerun", metavar="COMMIT", help="rerun this commit's own runs a newer push cancelled")
    parser.add_argument("--infrastructure", action="store_true",
                        help="with --rerun: also rerun the commit's red run, an infrastructure failure (red first)")
    args = parser.parse_args(argv)
    directory = Path(root) / ".github" / "workflows"
    try:
        workflows = args.workflows or watched(root)
    except ValueError as exc:
        parser.error(str(exc))
    missing = [w for w in workflows if "/" in w or "\\" in w or not (directory / w).is_file()]
    if missing or not args.branch.strip():
        parser.error(f"no such workflow in .github/workflows/: {', '.join(missing)}" if missing
                     else "an empty --branch")
    if args.infrastructure and args.rerun is None:
        parser.error("--infrastructure goes with --rerun")
    if args.rerun is not None and not (len(args.rerun) == 40 and all(c in "0123456789abcdef" for c in args.rerun)):
        parser.error("--rerun takes a full 40-character commit")
    if not workflows:
        # A repository without CI (no ci.json list, no workflow file): nothing is watched, so nothing is red.
        note = "no CI workflow in this repository (.conclave/ci.json, .github/workflows/); nothing to watch"
        print(json.dumps({"branch": args.branch, "workflows": [], "note": note, "exit": 0}, indent=2) if args.json
              else f"ci-status: {note}")
        return 0
    try:
        if args.rerun is not None:
            lines, code = rerun(runner, workflows, args.branch, args.rerun, args.infrastructure)
            print("\n".join(lines))
            return code
        if args.since_last:
            result, new_state, code = since_last(runner, workflows, args.branch, root)
            print(json.dumps(result, indent=2) if args.json else "\n".join(describe_since(result)))
            sys.stdout.flush()
            try:
                _write_atomic(Path(root) / CURSOR, new_state)   # only after everything was printed
            except OSError as exc:
                print(f"ci-status: the cursor could not be written ({exc}); the next read reports these runs again")
            return code
        result, code = report(runner, workflows, args.branch)
    except Unavailable as exc:
        if args.json:
            print(json.dumps({"branch": args.branch, "error": str(exc), "exit": 3}))
        else:
            print(f"ci-status unknown: {exc}")
        return 3
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        for entry in result["workflows"]:
            print("\n".join(describe(entry)))
    return code


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
