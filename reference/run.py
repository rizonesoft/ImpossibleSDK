#!/usr/bin/env python3
"""Draw every case with real WinUI, on native Windows or from WSL.

    python3 reference/run.py                        capture all reference/cases/*.json into build/reference/out
    python3 reference/run.py --cases path.json      capture a single case file or directory into build/reference/out
    python3 reference/run.py --out <dir>            write fresh output to a specific folder
    python3 reference/run.py --update-store         capture all cases and atomically publish into tests/reference/
"""
import argparse
import hashlib
import json
import os
import shutil
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

FORBIDDEN_ASSET_CONTROLS = {"FontIcon", "SymbolIcon"}
FORBIDDEN_FONT_SUBSTRINGS = ("segoe", "calibri", "cambria", "consolas", "candara", "corbel", "constantia", "microsoft")


def is_wsl():
    return os.name != "nt"


def run_cmd(*command, **more):
    return subprocess.run(command, check=True, timeout=1200, stdin=subprocess.DEVNULL, **more)


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


def check_no_prohibited_assets(case_spec):
    """Refuse Microsoft fonts or stock Microsoft glyph controls in committed store cases."""
    cid = case_spec.get("id", "<unknown>")
    ctrl = case_spec.get("control", "")
    if ctrl in FORBIDDEN_ASSET_CONTROLS:
        raise ValueError(f"{cid}: control {ctrl!r} uses a prohibited Microsoft glyph font and cannot be persisted")
    props = case_spec.get("properties") or {}
    if ctrl in ("TextBlock", "TextBox") or "Content" in props or "Text" in props or "Header" in props:
        ff = str(props.get("FontFamily", "")).lower()
        if not ff or any(bad in ff for bad in FORBIDDEN_FONT_SUBSTRINGS):
            raise ValueError(
                f"{cid}: text-bearing case without a permitted non-Microsoft FontFamily cannot be persisted to tests/reference/"
            )
    for child in case_spec.get("children") or []:
        check_no_prohibited_assets(child)


def validate_store_dir(store_dir, expected_specs=None):
    """Validate that store_dir has a self-consistent index, no missing/extra/stale frames, and valid digests."""
    store = Path(store_dir)
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
    allowed_files = indexed_files | {"cases.json"}
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


def publish_store(fresh_dir, store_dir, expected_specs):
    """Stage compressed frames + index, validate completely, and atomically swap with backup recovery."""
    fresh = Path(fresh_dir)
    store = Path(store_dir)
    manifest = json.loads((fresh / "cases.json").read_text(encoding="utf-8"))

    for spec in expected_specs.values():
        check_no_prohibited_assets(spec)

    stage = store.parent / f".{store.name}.staging-{os.getpid()}"
    backup = store.parent / f".{store.name}.backup-{os.getpid()}"
    shutil.rmtree(stage, ignore_errors=True)
    shutil.rmtree(backup, ignore_errors=True)
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
        validate_store_dir(stage, expected_specs)

        # Atomic swap with recoverable backup.
        had_store = store.exists()
        if had_store:
            store.rename(backup)
        try:
            stage.rename(store)
            validate_store_dir(store, expected_specs)
        except Exception:
            if store.exists():
                shutil.rmtree(store, ignore_errors=True)
            if had_store and backup.exists():
                backup.rename(store)
            raise
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


def run_reference(program, out_dir, cases_path=None, work_dir=None):
    out = Path(out_dir)
    with tempfile.TemporaryDirectory(dir=host_temp_root(), prefix="impossible-ref-run-") as run_tmp:
        rt = Path(run_tmp)
        win_out = to_win_path(rt / "out")
        cmd = [str(program), win_out]
        if cases_path is not None:
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

        res = subprocess.run(cmd, cwd=work_dir or rt, capture_output=True, text=True, timeout=600, stdin=subprocess.DEVNULL)
        err_file = rt / "out" / "error.txt"
        err_text = err_file.read_text(encoding="utf-8", errors="replace") if err_file.is_file() else ""
        if res.returncode != 0:
            if err_text:
                sys.stderr.write(err_text + "\n")
            raise subprocess.CalledProcessError(res.returncode, cmd, output=res.stdout, stderr=res.stderr + "\n" + err_text)
        shutil.rmtree(out, ignore_errors=True)
        shutil.copytree(rt / "out", out)
    return out


def capture(out_dir, cases_path=None):
    with tempfile.TemporaryDirectory(dir=host_temp_root(), prefix="impossible-reference-") as folder:
        program = build_reference(folder)
        return run_reference(program, out_dir, cases_path=cases_path, work_dir=folder)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, default=None, help="single case JSON file or directory (cannot be combined with --update-store)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output directory for fresh captures")
    parser.add_argument("--update-store", action="store_true", help="atomically publish validated compressed frames into tests/reference/")
    args = parser.parse_args(argv)

    if args.update_store and args.cases is not None:
        print("error: --update-store refuses --cases (partial runs cannot replace the full reference store)", file=sys.stderr)
        return 2

    out = capture(args.out, args.cases)
    frames = list(out.glob("*.bgra"))
    print(f"{len(frames)} frames in {out}")
    if args.update_store:
        specs = load_case_specs(CASES_DIR)
        publish_store(out, STORE, specs)
        print(f"published {len(specs)} validated compressed frames to {STORE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
