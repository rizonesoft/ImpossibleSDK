#!/usr/bin/env python3
"""Draw every case with real WinUI, on native Windows or from WSL.

    python3 reference/run.py                        capture all reference/cases/*.json into build/reference/out
    python3 reference/run.py --cases path.json      capture a single case file or directory into build/reference/out
    python3 reference/run.py --out <dir>            write fresh output to a specific folder
    python3 reference/run.py --update-store         capture all cases and atomically publish into tests/reference/
"""
import argparse
import contextlib
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DEFAULT_OUT = ROOT / "build/reference/out"
STORE = ROOT / "tests/reference"
CASES_DIR = HERE / "cases"

FORBIDDEN_ASSET_CONTROLS = {"fonticon", "symbolicon"}
FORBIDDEN_FONT_SUBSTRINGS = ("segoe", "calibri", "cambria", "consolas", "candara", "corbel", "constantia", "microsoft")
OUT_MARKER = ".impossible-reference-out"
STORE_MARKER = ".impossible-reference-store"

# Explicit allowlist of non-text properties safe to persist without a permitted FontFamily.
# Any property outside this allowlist (including reflected text properties like OnContent/OffContent/Header/Text/Content)
# requires a permitted non-Microsoft FontFamily.
SAFE_NON_TEXT_PROPERTIES = {
    "name", "width", "height", "minwidth", "minheight",
    "horizontalalignment", "verticalalignment", "margin", "padding",
    "isenabled", "istabstop", "cornerradius", "background", "foreground",
    "ison", "ischecked", "value", "minimum", "maximum",
    "orientation", "spacing", "data", "selectedindex",
    "fontfamily", "fontsize",
}


def is_wsl():
    return os.name != "nt"


def _kill_process_tree(proc):
    if os.name == "nt":
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True, timeout=60)
    else:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def run_cmd(*command, cwd=None, capture_output=False, text=False, timeout=1200, check=True):
    kwargs = {
        "cwd": cwd,
        "stdin": subprocess.DEVNULL,
        "text": text,
    }
    if capture_output:
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.PIPE
    if os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW
    else:
        kwargs["start_new_session"] = True
    proc = subprocess.Popen(command, **kwargs)
    try:
        out, err = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        _kill_process_tree(proc)
        proc.communicate()
        raise
    res = subprocess.CompletedProcess(command, proc.returncode, out, err)
    if check and res.returncode != 0:
        raise subprocess.CalledProcessError(res.returncode, command, output=out, stderr=err)
    return res


def to_win_path(path):
    p = str(Path(path).resolve())
    if not is_wsl():
        return p
    return run_cmd("wslpath", "-w", p, capture_output=True, text=True).stdout.strip()


def host_temp_root():
    if not is_wsl():
        return Path(tempfile.gettempdir())
    win_tmp = run_cmd(
        "pwsh.exe", "-NoProfile", "-Command", "[IO.Path]::GetTempPath()",
        capture_output=True, text=True,
    ).stdout.strip()
    return Path(run_cmd("wslpath", "-u", win_tmp, capture_output=True, text=True).stdout.strip())


def canonical_case_hash(case_spec):
    blob = json.dumps(case_spec, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()


def load_case_specs(cases_path):
    path = Path(cases_path)
    files = sorted(path.glob("*.json")) if path.is_dir() else [path]
    specs = {}
    for fp in files:
        data = json.loads(fp.read_text(encoding="utf-8"))
        for c in data.get("cases", []):
            cid = c["id"]
            if cid in specs:
                raise ValueError(f"{fp}: duplicate case id {cid!r}")
            specs[cid] = c
    return specs


def _ci_get(mapping, key, default=None):
    if not isinstance(mapping, dict):
        return default
    kl = key.lower()
    for k, v in mapping.items():
        if isinstance(k, str) and k.lower() == kl:
            return v
    return default


ALLOWED_CASE_KEYS = {
    "id", "case_group", "control", "style", "theme", "scale", "flow_direction",
    "state", "use_transitions", "frame", "properties", "items", "children",
    "script", "dump_tree", "probe_box", "probe_radii",
}


def validate_case_keys(case_spec):
    """Reject unknown, differently cased, or case-colliding keys so Python and C# enforce one exact schema."""
    if not isinstance(case_spec, dict):
        raise ValueError("case specification must be a JSON object")
    seen_lower = set()
    for k in case_spec:
        if not isinstance(k, str) or k not in ALLOWED_CASE_KEYS:
            raise ValueError(f"case {case_spec.get('id', '<unknown>')!r}: unknown or non-canonical key {k!r}")
        kl = k.lower()
        if kl in seen_lower:
            raise ValueError(f"case {case_spec.get('id', '<unknown>')!r}: duplicate case-folded key {k!r}")
        seen_lower.add(kl)
    for child in case_spec.get("children") or []:
        validate_case_keys(child)


def check_no_prohibited_assets(case_spec):
    """Refuse unknown/non-canonical keys, Microsoft fonts, or stock Microsoft glyph controls in committed store cases."""
    validate_case_keys(case_spec)
    cid = case_spec.get("id", "<unknown>")
    ctrl = str(case_spec.get("control", "") or "")
    ctrl_l = ctrl.lower()
    if ctrl_l in FORBIDDEN_ASSET_CONTROLS:
        raise ValueError(f"{cid}: control {ctrl!r} uses a prohibited Microsoft glyph font and cannot be persisted")
    props = case_spec.get("properties") or {}
    items = case_spec.get("items")
    has_non_allowlisted_prop = any(
        isinstance(k, str) and k.lower() not in SAFE_NON_TEXT_PROPERTIES
        for k in props
    )
    has_text = (
        ctrl_l in ("textblock", "textbox")
        or bool(items)
        or has_non_allowlisted_prop
    )
    if has_text:
        ff = str(_ci_get(props, "fontfamily", "") or "").lower()
        if not ff or any(bad in ff for bad in FORBIDDEN_FONT_SUBSTRINGS):
            raise ValueError(
                f"{cid}: text-bearing or unallowlisted property without a permitted non-Microsoft FontFamily cannot be persisted to tests/reference/"
            )
    for child in case_spec.get("children") or []:
        check_no_prohibited_assets(child)


def _is_owned_backup(backup_path, is_store):
    if not backup_path.is_dir():
        return False
    marker = STORE_MARKER if is_store else OUT_MARKER
    if not (backup_path / marker).is_file():
        return False
    for entry in backup_path.iterdir():
        if entry.is_dir():
            return False
        if is_store:
            if entry.name not in ("cases.json", STORE_MARKER) and not entry.name.endswith(".bgra.z"):
                return False
        else:
            if entry.name not in ("cases.json", "error.txt", OUT_MARKER) and entry.suffix != ".bgra":
                return False
    return True


def _recover_dir_unlocked(target_dir, is_store):
    """Recover an owned .<name>.backup sibling if a prior swap was interrupted (caller must hold _store_lock)."""
    target = Path(target_dir)
    backup = target.parent / f".{target.name}.backup"
    if not backup.exists():
        return False
    if not _is_owned_backup(backup, is_store=is_store):
        raise ValueError(f"{backup}: refuses to touch unrelated sibling backup directory")
    if not target.exists():
        backup.rename(target)
        return True
    return False


def read_reference_snapshot(store_dir, filenames=None):
    """Atomically read cases.json and requested decompressed .bgra.z frames under the store lock (with crash recovery)."""
    store = Path(store_dir)
    with _store_lock(store):
        _recover_dir_unlocked(store, is_store=True)
        manifest = json.loads((store / "cases.json").read_text(encoding="utf-8"))
        wanted = set(filenames) if filenames is not None else {c["file"] for c in manifest.get("cases", []) if "file" in c}
        frames = {}
        for fn in wanted:
            frames[fn] = zlib.decompress((store / f"{fn}.z").read_bytes())
        return manifest, frames


def open_reference_manifest(store_dir):
    """Read tests/reference/cases.json under the store lock, recovering an interrupted swap if needed."""
    manifest, _ = read_reference_snapshot(store_dir, filenames=())
    return manifest


def _ensure_safe_capture_dir(out_dir):
    """Refuse to overwrite any existing non-empty output directory or unrelated sibling backup not owned by reference/run.py."""
    out = Path(out_dir)
    with _store_lock(out):
        _recover_dir_unlocked(out, is_store=False)
        if not out.exists():
            return
        if not out.is_dir():
            raise ValueError(f"{out}: output path exists and is not a directory")
        entries = list(out.iterdir())
        if not entries:
            return
        if not (out / OUT_MARKER).is_file():
            raise ValueError(f"{out}: refuses to overwrite existing non-empty directory without {OUT_MARKER} ownership marker")
        for entry in entries:
            if entry.is_dir() or (entry.suffix != ".bgra" and entry.name not in ("cases.json", "error.txt", OUT_MARKER)):
                raise ValueError(f"{out}: refuses to overwrite directory containing unrelated entry {entry.name!r}")


def _publish_capture_output(src_dir, out_dir):
    """Safely publish fresh capture files with ownership marker and crash-recoverable fixed backup under lock."""
    src = Path(src_dir)
    out = Path(out_dir)
    out.parent.mkdir(parents=True, exist_ok=True)
    with _store_lock(out):
        _recover_dir_unlocked(out, is_store=False)
        if out.exists():
            if not out.is_dir():
                raise ValueError(f"{out}: output path exists and is not a directory")
            entries = list(out.iterdir())
            if entries and not (out / OUT_MARKER).is_file():
                raise ValueError(f"{out}: refuses to overwrite existing non-empty directory without {OUT_MARKER} ownership marker")
            for entry in entries:
                if entry.is_dir() or (entry.suffix != ".bgra" and entry.name not in ("cases.json", "error.txt", OUT_MARKER)):
                    raise ValueError(f"{out}: refuses to overwrite directory containing unrelated entry {entry.name!r}")
        stage = out.parent / f".{out.name}.stage-{os.getpid()}"
        backup = out.parent / f".{out.name}.backup"
        shutil.rmtree(stage, ignore_errors=True)
        try:
            shutil.copytree(src, stage)
            (stage / OUT_MARKER).write_text("impossible-sdk-reference-output\n", encoding="utf-8")
            if backup.exists():
                if not _is_owned_backup(backup, is_store=False):
                    raise ValueError(f"{backup}: refuses to delete unrelated sibling backup directory")
                shutil.rmtree(backup)
            had_nonempty_out = False
            if out.exists():
                if not list(out.iterdir()):
                    # Empty destination needs no backup: remove it directly so stage.rename(out) is a single atomic swap.
                    out.rmdir()
                else:
                    had_nonempty_out = True
                    out.rename(backup)
            try:
                stage.rename(out)
            except Exception:
                if had_nonempty_out and backup.exists() and not out.exists():
                    backup.rename(out)
                raise
            if backup.exists():
                shutil.rmtree(backup, ignore_errors=True)
        finally:
            shutil.rmtree(stage, ignore_errors=True)


def validate_store_dir(store_dir, expected_specs=None, _locked=False):
    """Validate that store_dir has a self-consistent index, no missing/extra/stale frames, and valid digests."""
    store = Path(store_dir)
    ctx = contextlib.nullcontext() if _locked else _store_lock(store)
    with ctx:
        _recover_dir_unlocked(store, is_store=True)
        return _validate_store_dir_unlocked(store, expected_specs)


def _validate_store_dir_unlocked(store, expected_specs=None):
    index_path = store / "cases.json"
    if not index_path.is_file():
        raise ValueError(f"{store}: missing cases.json")
    manifest = json.loads(index_path.read_text(encoding="utf-8"))
    cases = manifest.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError(f"{index_path}: cases must be a non-empty list")

    indexed_files = set()
    indexed_ids = {}
    for c in cases:
        cid = c.get("id")
        fname = c.get("file")
        if not cid or not fname:
            raise ValueError(f"{index_path}: case missing id or file")
        if cid in indexed_ids:
            raise ValueError(f"{index_path}: duplicate case id {cid!r}")
        indexed_ids[cid] = c
        zname = f"{fname}.z"
        if zname in indexed_files:
            raise ValueError(f"{index_path}: duplicate frame file {zname!r}")
        indexed_files.add(zname)
        zpath = store / zname
        if not zpath.is_file():
            raise ValueError(f"{store}: missing compressed frame {zname!r}")
        zbytes = zpath.read_bytes()
        if c.get("sha256") != hashlib.sha256(zbytes).hexdigest():
            raise ValueError(f"{store}: stale or corrupted compressed frame {zname!r}")
        raw = zlib.decompress(zbytes)
        expected_len = int(c["width"]) * int(c["height"]) * 4
        if len(raw) != expected_len:
            raise ValueError(f"{store}: frame {zname!r} decoded to {len(raw)} bytes, expected {expected_len}")
        if c.get("raw_sha256") != hashlib.sha256(raw).hexdigest():
            raise ValueError(f"{store}: raw digest mismatch for {zname!r}")

    actual_files = {p.name for p in store.iterdir() if p.is_file()}
    allowed_files = indexed_files | {"cases.json", STORE_MARKER}
    extra = sorted(actual_files - allowed_files)
    if extra:
        raise ValueError(f"{store}: unindexed extra file(s): {', '.join(extra)}")

    if expected_specs is not None:
        missing_ids = sorted(set(expected_specs) - set(indexed_ids))
        extra_ids = sorted(set(indexed_ids) - set(expected_specs))
        if missing_ids or extra_ids:
            raise ValueError(f"{store}: case inventory mismatch (missing={missing_ids}, extra={extra_ids})")
        for cid, spec in expected_specs.items():
            check_no_prohibited_assets(spec)
            want_hash = canonical_case_hash(spec)
            if indexed_ids[cid].get("case_sha256") != want_hash:
                raise ValueError(f"{store}: case {cid!r} is stale (case_sha256 mismatch against reference/cases/)")

    return manifest


@contextlib.contextmanager
def _store_lock(store_dir):
    key = hashlib.sha256(str(Path(store_dir).resolve()).encode("utf-8")).hexdigest()[:16]
    lock_path = Path(tempfile.gettempdir()) / f"impossible-sdk-ref-{key}.lock"
    fp = open(lock_path, "a+", encoding="utf-8")
    try:
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(fp.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(fp.fileno(), fcntl.LOCK_EX)
        yield
    finally:
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(fp.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(fp.fileno(), fcntl.LOCK_UN)
        finally:
            fp.close()


def publish_store(fresh_dir, store_dir, expected_specs):
    """Stage compressed frames + index, validate completely, and atomically swap under an exclusive lock."""
    fresh = Path(fresh_dir)
    store = Path(store_dir)
    manifest = json.loads((fresh / "cases.json").read_text(encoding="utf-8"))

    for spec in expected_specs.values():
        check_no_prohibited_assets(spec)

    with _store_lock(store):
        backup = store.parent / f".{store.name}.backup"
        _recover_dir_unlocked(store, is_store=True)

        stage = store.parent / f".{store.name}.staging-{os.getpid()}"
        shutil.rmtree(stage, ignore_errors=True)
        stage.mkdir(parents=True)

        try:
            for case in manifest["cases"]:
                cid = case["id"]
                if cid not in expected_specs:
                    raise ValueError(f"fresh capture contains unexpected case {cid!r}")
                raw_path = fresh / case["file"]
                raw = raw_path.read_bytes()
                zbytes = zlib.compress(raw, 9)
                (stage / f"{case['file']}.z").write_bytes(zbytes)
                case["sha256"] = hashlib.sha256(zbytes).hexdigest()
                case["raw_sha256"] = hashlib.sha256(raw).hexdigest()
                case["case_sha256"] = canonical_case_hash(expected_specs[cid])

            (stage / "cases.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
            (stage / STORE_MARKER).write_text("impossible-sdk-reference-store\n", encoding="utf-8", newline="\n")
            validate_store_dir(stage, expected_specs, _locked=True)

            if backup.exists():
                if not _is_owned_backup(backup, is_store=True):
                    raise ValueError(f"{backup}: refuses to delete unrelated sibling backup directory")
                shutil.rmtree(backup)
            had_store = store.exists()
            if had_store:
                store.rename(backup)
            try:
                stage.rename(store)
            except Exception:
                if had_store and backup.exists() and not store.exists():
                    backup.rename(store)
                raise
            # Verify the swapped index is readable without redundantly re-decompressing every frame.
            if not (store / "cases.json").is_file():
                raise RuntimeError(f"{store}: cases.json missing after swap")
            if backup.exists():
                shutil.rmtree(backup, ignore_errors=True)
        except Exception:
            shutil.rmtree(stage, ignore_errors=True)
            raise


def build_reference(work_dir):
    work = Path(work_dir)
    dotnet = "dotnet.exe" if is_wsl() else "dotnet"
    shutil.copytree(HERE, work / "reference", ignore=shutil.ignore_patterns("*.py"))
    run_cmd(dotnet, "build", "-c", "Release", "-v", "q", "--nologo", cwd=work / "reference")
    return next((work / "build/reference/bin").rglob("Reference.exe"))


def run_reference(program, out_dir, cases_path=None, work_dir=None, snapshotted_specs=None):
    out = Path(out_dir)
    _ensure_safe_capture_dir(out)
    with tempfile.TemporaryDirectory(dir=host_temp_root(), prefix="impossible-ref-run-") as run_tmp:
        rt = Path(run_tmp)
        win_out = to_win_path(rt / "out")
        cmd = [str(program), win_out]
        if snapshotted_specs is not None:
            staged_cases = rt / "snapshot_cases"
            staged_cases.mkdir(parents=True, exist_ok=True)
            (staged_cases / "snapshot.json").write_text(
                json.dumps({"cases": list(snapshotted_specs.values())}, indent=2),
                encoding="utf-8",
            )
            cmd += ["--cases", to_win_path(staged_cases)]
        elif cases_path is not None:
            src = Path(cases_path).resolve()
            staged_cases = rt / "custom_cases"
            if src.is_dir():
                shutil.copytree(src, staged_cases)
                cmd += ["--cases", to_win_path(staged_cases)]
            else:
                staged_cases.mkdir(parents=True, exist_ok=True)
                dst = staged_cases / src.name
                shutil.copy2(src, dst)
                cmd += ["--cases", to_win_path(dst)]
        else:
            staged_cases = rt / "default_cases"
            shutil.copytree(CASES_DIR, staged_cases)
            cmd += ["--cases", to_win_path(staged_cases)]

        res = run_cmd(*cmd, cwd=work_dir or rt, capture_output=True, text=True, timeout=600, check=False)
        err_file = rt / "out" / "error.txt"
        err_text = err_file.read_text(encoding="utf-8", errors="replace") if err_file.is_file() else ""
        if res.returncode != 0:
            if err_text:
                sys.stderr.write(err_text + "\n")
            raise subprocess.CalledProcessError(res.returncode, cmd, output=res.stdout, stderr=(res.stderr or "") + "\n" + err_text)
        _publish_capture_output(rt / "out", out)
    return out


def capture(out_dir, cases_path=None, snapshotted_specs=None):
    with tempfile.TemporaryDirectory(dir=host_temp_root(), prefix="impossible-reference-") as folder:
        program = build_reference(folder)
        return run_reference(program, out_dir, cases_path=cases_path, work_dir=folder, snapshotted_specs=snapshotted_specs)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, default=None, help="single case JSON file or directory (cannot be combined with --update-store)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output directory for fresh captures")
    parser.add_argument("--update-store", action="store_true", help="atomically publish validated compressed frames into tests/reference/")
    args = parser.parse_args(argv)

    if args.update_store and args.cases is not None:
        print("error: --update-store refuses --cases (partial runs cannot replace the full reference store)", file=sys.stderr)
        return 2

    # Snapshot case specifications BEFORE capture so publication hashes the exact specs rendered by Reference.exe.
    specs = load_case_specs(CASES_DIR) if args.update_store else None
    out = capture(args.out, args.cases, snapshotted_specs=specs)
    frames = list(out.glob("*.bgra"))
    print(f"{len(frames)} frames in {out}")
    if args.update_store:
        publish_store(out, STORE, specs)
        print(f"published {len(specs)} validated compressed frames to {STORE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
