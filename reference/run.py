#!/usr/bin/env python3
"""Draw every case with real WinUI, on native Windows or from WSL.

    python3 reference/run.py                        capture all reference/cases/*.json into build/reference/out
    python3 reference/run.py --cases path.json      capture a single case file or directory into build/reference/out
    python3 reference/run.py --out <dir>            write fresh output to a specific folder
    python3 reference/run.py --update-store         capture all cases and atomically publish into tests/reference/
"""
import argparse
import base64
import contextlib
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DEFAULT_OUT = ROOT / "build/reference/out"
STORE = ROOT / "tests/reference"
CASES_DIR = HERE / "cases"
PACKAGE_MANIFEST = HERE / "package.json"

FORBIDDEN_ASSET_CONTROLS = {"fonticon", "symbolicon"}
FORBIDDEN_FONT_SUBSTRINGS = ("segoe", "calibri", "cambria", "consolas", "candara", "corbel", "constantia", "microsoft")
OUT_MARKER = ".impossible-reference-out"
STORE_MARKER = ".impossible-reference-store"

# Explicit allowlist of non-text properties safe to persist without a permitted FontFamily.
# Any non-None property outside this allowlist (including reflected displayable content properties like
# OnContent/OffContent/Header/Text/Content, whether string or numeric) requires a permitted non-Microsoft FontFamily.
SAFE_NON_TEXT_PROPERTIES = {
    "name", "width", "height", "minwidth", "minheight", "maxwidth", "maxheight",
    "horizontalalignment", "verticalalignment", "margin", "padding",
    "isenabled", "istabstop", "cornerradius", "background", "foreground",
    "borderbrush", "borderthickness", "opacity", "visibility",
    "ison", "ischecked", "isthreestate", "isopen", "isdropdownopen",
    "isreadonly", "isindeterminate", "isactive", "istextselectionenabled",
    "value", "minimum", "maximum", "smallchange", "largechange",
    "orientation", "flowdirection", "spacing", "data", "selectedindex",
    "fill", "stroke", "strokethickness", "stretch", "zindex",
    "fontfamily", "fontsize", "fontweight",
}


# Displayable content/text properties that render glyphs even when given a numeric or boolean value (e.g. OnContent: 123).
DISPLAYABLE_CONTENT_PROPERTIES = {
    "content", "text", "header", "footer", "oncontent", "offcontent",
    "placeholdertext", "description", "tooltip", "title", "subtitle",
    "message", "label", "caption", "paneheader", "panetitle", "panefooter",
    "displaymemberpath", "selectedvalue", "selecteditem", "accesskey",
    "password", "passwordchar",
}
DISPLAYABLE_CONTENT_SUFFIXES = (
    "text", "content", "header", "footer", "title", "label", "caption", "message", "description", "tooltip",
)


def _is_displayable_content_property(prop_name):
    pl = prop_name.lower()
    return pl in DISPLAYABLE_CONTENT_PROPERTIES or any(pl.endswith(sfx) for sfx in DISPLAYABLE_CONTENT_SUFFIXES)


def is_wsl():
    return os.name != "nt" and shutil.which("wslpath") is not None and shutil.which("pwsh.exe") is not None


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
    norm = normalize_case_spec(case_spec)
    blob = json.dumps(norm, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()


def _reject_duplicate_json_keys(pairs):
    out = {}
    seen = {}
    for k, v in pairs:
        kl = k.lower()
        if kl in seen:
            raise ValueError(f"duplicate JSON key {k!r} (collides with {seen[kl]!r})")
        seen[kl] = k
        out[k] = v
    return out


def load_case_specs(cases_path):
    path = Path(cases_path)
    files = sorted(path.glob("*.json")) if path.is_dir() else [path]
    specs = {}
    seen_lower = {}
    for fp in files:
        data = json.loads(fp.read_text(encoding="utf-8"), object_pairs_hook=_reject_duplicate_json_keys)
        if not isinstance(data, dict):
            raise ValueError(f"{fp}: top-level JSON must be an object")
        top = {}
        for k, v in data.items():
            if not isinstance(k, str):
                raise ValueError(f"{fp}: non-string top-level key {k!r}")
            kl = k.lower()
            if kl != "cases":
                raise ValueError(f"{fp}: unknown top-level key {k!r}")
            if kl in top:
                raise ValueError(f"{fp}: duplicate case-folded top-level key {k!r}")
            top[kl] = v
        raw_cases = top.get("cases", [])
        if not isinstance(raw_cases, list):
            raise ValueError(f"{fp}: 'cases' must be a list")
        for c in raw_cases:
            norm = normalize_case_spec(c)
            cid = norm.get("id")
            if not isinstance(cid, str) or not cid.strip():
                raise ValueError(f"{fp}: case missing non-empty 'id'")
            cid_l = cid.lower()
            if cid_l in seen_lower:
                raise ValueError(f"{fp}: duplicate case id {cid!r}")
            seen_lower[cid_l] = cid
            specs[cid] = norm
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
    "state", "use_transitions", "storyboard_timeout_ms", "frame", "properties", "items", "children",
    "script", "dump_tree", "probe_box", "probe_radii",
    "expected_elements", "expected_states", "expected_setters",
}


def normalize_case_spec(case_spec):
    """Validate keys case-insensitively, reject unknown or case-colliding keys, and return canonical lowercase dict."""
    if not isinstance(case_spec, dict):
        raise ValueError("case specification must be a JSON object")
    out = {}
    for k, v in case_spec.items():
        if not isinstance(k, str):
            raise ValueError(f"case {case_spec.get('id', '<unknown>')!r}: non-string key {k!r}")
        kl = k.lower()
        if kl not in ALLOWED_CASE_KEYS:
            raise ValueError(f"case {case_spec.get('id', '<unknown>')!r}: unknown key {k!r}")
        if kl in out:
            raise ValueError(f"case {case_spec.get('id', '<unknown>')!r}: duplicate case-folded key {k!r}")
        out[kl] = v
    if isinstance(out.get("frame"), dict):
        norm_frame = {}
        for fk, fv in out["frame"].items():
            if not isinstance(fk, str):
                raise ValueError(f"case {out.get('id', '<unknown>')!r}: non-string frame key {fk!r}")
            fl = fk.lower()
            if fl not in ("width", "height", "background"):
                raise ValueError(f"case {out.get('id', '<unknown>')!r}: unknown frame key {fk!r}")
            if fl in norm_frame:
                raise ValueError(f"case {out.get('id', '<unknown>')!r}: duplicate case-folded frame key {fk!r}")
            norm_frame[fl] = fv
        out["frame"] = norm_frame
    if "children" in out and out["children"] is not None:
        if not isinstance(out["children"], list):
            raise ValueError(f"case {out.get('id', '<unknown>')!r}: 'children' must be a JSON array")
        out["children"] = [normalize_case_spec(ch) for ch in out["children"]]
    return out


def _codepoint_in_ranges(cp, ranges):
    return any(isinstance(r, (list, tuple)) and len(r) == 2 and r[0] <= cp <= r[1] for r in ranges)


def check_no_prohibited_assets(case_spec, require_installed=False, installed_fonts=None, installed_font_ranges=None):
    """Refuse unknown/colliding keys, Microsoft fonts, unverified/uninstalled fonts, or stock Microsoft glyph controls."""
    norm = normalize_case_spec(case_spec)
    cid = norm.get("id", "<unknown>")
    ctrl = str(norm.get("control", "") or "")
    ctrl_l = ctrl.lower()
    if ctrl_l in FORBIDDEN_ASSET_CONTROLS:
        raise ValueError(f"{cid}: control {ctrl!r} uses a prohibited Microsoft glyph font and cannot be persisted")
    props = norm.get("properties") or {}
    items = norm.get("items")
    if ctrl_l == "toggleswitch":
        on_val = _ci_get(props, "oncontent", None)
        off_val = _ci_get(props, "offcontent", None)
        if on_val is None or off_val is None:
            raise ValueError(
                f"{cid}: ToggleSwitch omits OnContent/OffContent and would render stock Microsoft template labels; "
                "set both to '' or provide explicit permitted content with a non-Microsoft FontFamily"
            )
    # Any displayable content/text property (even numeric/bool like OnContent: 123) or any non-scalar/string property
    # outside SAFE_NON_TEXT_PROPERTIES requires a permitted font; empty strings ("") render no glyphs.
    has_non_allowlisted_prop = any(
        isinstance(k, str)
        and v is not None
        and v != ""
        and (
            _is_displayable_content_property(k)
            or (k.lower() not in SAFE_NON_TEXT_PROPERTIES and not isinstance(v, (int, float, bool)))
        )
        for k, v in props.items()
    )
    stock_text_controls = {
        "textblock", "textbox", "passwordbox", "richeditbox", "autosuggestbox",
        "datepicker", "timepicker", "calendarview", "calendardatepicker",
        "numberbox", "ratingcontrol", "navigationview", "breadcrumbbar",
        "tabview", "expander", "infobar", "teachingtip", "contentdialog",
        "menubar", "commandbar",
    }
    has_text = (
        ctrl_l in stock_text_controls
        or bool(items)
        or has_non_allowlisted_prop
    )
    permitted_fonts = {
        "inter", "jetbrains mono", "ibm plex sans", "ibm plex mono",
        "dejavu sans", "dejavu sans mono", "liberation sans", "liberation mono",
        "noto sans", "noto mono",
    }
    if has_text:
        ff = str(_ci_get(props, "fontfamily", "") or "").strip().lower()
        if not ff or ff not in permitted_fonts or any(bad in ff for bad in FORBIDDEN_FONT_SUBSTRINGS):
            raise ValueError(
                f"{cid}: text-bearing property requires a verified permitted non-Microsoft FontFamily (got {ff!r}) to be persisted to tests/reference/"
            )
        if require_installed:
            inst = {str(n).strip().lower() for n in (installed_fonts or []) if isinstance(n, str)}
            if ff not in inst:
                raise ValueError(
                    f"{cid}: permitted FontFamily {ff!r} is not an exact installed font family on the capture host (WinUI would fall back to a system font)"
                )
            norm_ranges = {
                str(k).strip().lower(): v for k, v in (installed_font_ranges or {}).items()
                if isinstance(k, str) and isinstance(v, list)
            }
            cmap_ranges = norm_ranges.get(ff)
            if not cmap_ranges:
                raise ValueError(
                    f"{cid}: no verified cmap glyph coverage recorded for installed FontFamily {ff!r}"
                )
            # Check EVERY rendered text property (including numeric/bool displayable content like OnContent=123,
            # PasswordChar, default PasswordBox mask U+25CF, Text, Content, and items) against the installed font
            # family's intersected non-zero-glyph cmap coverage. Masked PasswordBox.Password is never displayed.
            pw_visible = str(_ci_get(props, "passwordrevealmode", "") or "").lower() == "visible"
            text_values = []
            for k, v in props.items():
                if not isinstance(k, str) or v is None or v == "":
                    continue
                kl = k.lower()
                if ctrl_l == "passwordbox" and kl == "password" and not pw_visible:
                    continue
                if ctrl_l == "passwordbox" and kl == "passwordchar" and pw_visible:
                    continue
                if _is_displayable_content_property(k):
                    text_values.append((k, str(v)))
                elif ctrl_l == "numberbox" and kl in ("value", "minimum", "maximum"):
                    text_values.append((k, str(v)))
                elif isinstance(v, str) and kl not in SAFE_NON_TEXT_PROPERTIES:
                    text_values.append((k, v))
            if ctrl_l == "passwordbox" and not pw_visible and _ci_get(props, "passwordchar", None) is None:
                text_values.append(("PasswordChar(default)", "\u25CF"))
            if ctrl_l == "numberbox":
                text_values.append(("NumberBox(digits)", "0123456789.-+"))
            elif ctrl_l in ("datepicker", "timepicker", "calendarview", "calendardatepicker"):
                text_values.append((f"{ctrl}(formatted)", "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz:-/, "))
            for idx, it in enumerate(items or []):
                if it is not None and str(it) != "":
                    text_values.append((f"items[{idx}]", str(it)))
            for field_name, tv in text_values:
                for ch in tv:
                    cp = ord(ch)
                    if cp < 0x20:
                        continue
                    if not _codepoint_in_ranges(cp, cmap_ranges):
                        raise ValueError(
                            f"{cid}.{field_name}: codepoint U+{cp:04X} is not covered by installed FontFamily {ff!r} cmap (would trigger unverified system font fallback)"
                        )
    for child in norm.get("children") or []:
        check_no_prohibited_assets(
            child, require_installed=require_installed,
            installed_fonts=installed_fonts, installed_font_ranges=installed_font_ranges,
        )


def _is_owned_backup(backup_path, is_store):
    if not backup_path.is_dir():
        return False
    marker = STORE_MARKER if is_store else OUT_MARKER
    if not (backup_path / marker).is_file():
        return False
    indexed_z = None
    if is_store:
        idx_path = backup_path / "cases.json"
        if not idx_path.is_file():
            return False
        try:
            manifest = json.loads(idx_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return False
        cases = manifest.get("cases")
        if not isinstance(cases, list):
            return False
        indexed_z = {f"{c['file']}.z" for c in cases if isinstance(c, dict) and isinstance(c.get("file"), str)}
    for entry in backup_path.iterdir():
        if entry.is_dir():
            return False
        if is_store:
            if entry.name not in ("cases.json", STORE_MARKER) and entry.name not in indexed_z:
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


def read_reference_snapshot(store_dir, filenames=None, check_provenance=True):
    """Atomically read cases.json and requested decompressed .bgra.z frames under the store lock (with crash recovery)."""
    store = Path(store_dir)
    with _store_lock(store):
        _recover_dir_unlocked(store, is_store=True)
        manifest = json.loads((store / "cases.json").read_text(encoding="utf-8"))
        if check_provenance:
            want_prov = compute_package_fingerprint()
            if manifest.get("provenance") != want_prov:
                raise ValueError(
                    f"{store / 'cases.json'}: package/template provenance mismatch (store is stale relative to reference/package.json, pin.json, or templates)"
                )
        wanted = set(filenames) if filenames is not None else {c["file"] for c in manifest.get("cases", []) if "file" in c}
        frames = {}
        for fn in wanted:
            frames[fn] = zlib.decompress((store / f"{fn}.z").read_bytes())
        return manifest, frames


def open_reference_manifest(store_dir, check_provenance=True):
    """Read tests/reference/cases.json under the store lock, recovering an interrupted swap if needed."""
    manifest, _ = read_reference_snapshot(store_dir, filenames=(), check_provenance=check_provenance)
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
        stage = Path(tempfile.mkdtemp(dir=out.parent, prefix=f".{out.name}.stage-"))
        backup = out.parent / f".{out.name}.backup"
        try:
            shutil.copytree(src, stage, dirs_exist_ok=True)
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


def compute_package_fingerprint(pkg_path=None, pin_path=None, template_c_path=None, templates_dir=None):
    """Compute a deterministic provenance fingerprint binding stored frames to the WinUI package and template inputs."""
    p_pkg = Path(pkg_path) if pkg_path else (HERE / "package.json")
    p_pin = Path(pin_path) if pin_path else (ROOT / "tools/winui/pin.json")
    p_tpl = Path(template_c_path) if template_c_path else (ROOT / "ui/template_data.c")
    p_tdir = Path(templates_dir) if templates_dir else (HERE / "templates")
    pkg = json.loads(p_pkg.read_text(encoding="utf-8"))
    tpl_h = hashlib.sha256()
    if p_tdir.is_dir():
        for xf in sorted(p_tdir.glob("*.xaml")):
            tpl_h.update(xf.name.encode("utf-8") + b"\0" + xf.read_bytes() + b"\0")
    return {
        "version": pkg.get("version"),
        "winui_component": pkg.get("winui_component"),
        "package_source_commit": pkg.get("package_source_commit"),
        "package_sha256": hashlib.sha256(p_pkg.read_bytes()).hexdigest(),
        "pin_sha256": hashlib.sha256(p_pin.read_bytes()).hexdigest(),
        "template_data_sha256": hashlib.sha256(p_tpl.read_bytes()).hexdigest(),
        "templates_sha256": tpl_h.hexdigest(),
    }


LEGACY_BASELINE_IDS = (
    "accent-Disabled-Dark", "accent-Disabled-Light",
    "accent-Normal-Dark", "accent-Normal-Light",
    "accent-PointerOver-Dark", "accent-PointerOver-Light",
    "accent-Pressed-Dark", "accent-Pressed-Light",
    "button-Disabled-Dark", "button-Disabled-Light",
    "button-Normal-Dark", "button-Normal-Light",
    "button-PointerOver-Dark", "button-PointerOver-Light",
    "button-Pressed-Dark", "button-Pressed-Light",
    "probe-asym-s1",
    "probe-r4-s1", "probe-r4-s15", "probe-r4-s2",
    "probe-r8-s1", "probe-r8-s15", "probe-r8-s2",
    "probe-r16-s1", "probe-r16-s15", "probe-r16-s2",
)


def compare_baseline_frames(fresh_dir, baseline_store_dir, baseline_ids=None):
    """Compare fresh capture frames against a baseline store by ID, dimensions, and decoded BGRA pixels."""
    fresh = Path(fresh_dir)
    fresh_manifest = json.loads((fresh / "cases.json").read_text(encoding="utf-8"))
    fresh_by_id = {c["id"]: c for c in fresh_manifest.get("cases", []) if "id" in c}
    base_index = open_reference_manifest(baseline_store_dir, check_provenance=False)
    base_by_id = {c["id"]: c for c in base_index.get("cases", []) if "id" in c and "file" in c}
    ids = list(baseline_ids) if baseline_ids is not None else list(LEGACY_BASELINE_IDS)
    if not ids:
        raise ValueError("baseline comparison has no frame IDs to verify")
    missing_base = [cid for cid in ids if cid not in base_by_id]
    if missing_base:
        raise ValueError(f"baseline store missing expected frame {missing_base[0]!r}")
    wanted_files = [base_by_id[cid]["file"] for cid in ids]
    _, base_frames = read_reference_snapshot(baseline_store_dir, filenames=wanted_files, check_provenance=False)
    for cid in ids:
        if cid not in fresh_by_id:
            raise ValueError(f"fresh capture missing baseline frame {cid!r}")
        bc = base_by_id[cid]
        fc = fresh_by_id[cid]
        if (fc.get("width"), fc.get("height")) != (bc.get("width"), bc.get("height")):
            raise ValueError(
                f"baseline frame {cid!r} dimension mismatch: fresh=({fc.get('width')},{fc.get('height')}) "
                f"vs baseline=({bc.get('width')},{bc.get('height')})"
            )
        fraw = (fresh / fc["file"]).read_bytes()
        braw = base_frames[bc["file"]]
        if fraw != braw:
            raise ValueError(f"baseline frame {cid!r} pixel mismatch against {baseline_store_dir}")
    return len(ids)


def validate_store_dir(store_dir, expected_specs=None, _locked=False, expected_provenance=None, check_provenance=True):
    """Validate that store_dir has a self-consistent index, no missing/extra/stale frames, and valid digests."""
    store = Path(store_dir)
    ctx = contextlib.nullcontext() if _locked else _store_lock(store)
    with ctx:
        _recover_dir_unlocked(store, is_store=True)
        return _validate_store_dir_unlocked(
            store, expected_specs, expected_provenance=expected_provenance, check_provenance=check_provenance
        )


def _validate_store_dir_unlocked(store, expected_specs=None, expected_provenance=None, check_provenance=True):
    index_path = store / "cases.json"
    if not index_path.is_file():
        raise ValueError(f"{store}: missing cases.json")
    manifest = json.loads(index_path.read_text(encoding="utf-8"))
    if check_provenance:
        want_prov = expected_provenance if expected_provenance is not None else compute_package_fingerprint()
        if manifest.get("provenance") != want_prov:
            raise ValueError(
                f"{index_path}: package/template provenance mismatch (store is stale relative to reference/package.json, pin.json, or templates)"
            )
    cases = manifest.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError(f"{index_path}: cases must be a non-empty list")

    indexed_files = set()
    indexed_ids = {}
    for c in cases:
        cid = c.get("id")
        if not cid:
            raise ValueError(f"{index_path}: case missing id")
        if cid in indexed_ids:
            raise ValueError(f"{index_path}: duplicate case id {cid!r}")
        indexed_ids[cid] = c
        if c.get("basis") == "compiled-template":
            if not c.get("template") or "verified_elements" not in c or "verified_states" not in c:
                raise ValueError(f"{index_path}: compiled-template case {cid!r} missing template or verified counts")
            continue
        fname = c.get("file")
        if not fname:
            raise ValueError(f"{index_path}: case {cid!r} missing file")
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


def _pid_is_alive_same_host(pid):
    if not isinstance(pid, int) or pid <= 0:
        return False
    if os.name == "nt":
        import ctypes
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        ERROR_INVALID_PARAMETER = 87
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            # ERROR_INVALID_PARAMETER (87) proves the PID does not exist; ERROR_ACCESS_DENIED (5) means it is alive.
            return ctypes.get_last_error() != ERROR_INVALID_PARAMETER
        try:
            code = ctypes.c_ulong(0)
            if kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
                return code.value == 259  # STILL_ACTIVE
            return True  # conservatively assume alive if GetExitCodeProcess fails
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def _pid_is_alive_cross_host(pid, owner_host, owner_distro=""):
    """Check whether a lock owner PID is alive on the same host or across the WSL/Windows boundary."""
    if not isinstance(pid, int) or pid <= 0:
        return False
    cur_host = "nt" if os.name == "nt" else "posix"
    if owner_host == cur_host:
        if cur_host == "posix":
            cur_distro = os.environ.get("WSL_DISTRO_NAME", "")
            if (cur_distro or owner_distro) and owner_distro != cur_distro:
                return True  # different or unknown WSL distro; do not guess
        return _pid_is_alive_same_host(pid)
    if cur_host == "posix" and owner_host == "nt" and shutil.which("tasklist.exe"):
        res = subprocess.run(
            ["tasklist.exe", "/FI", f"PID eq {pid}", "/NH", "/FO", "CSV"],
            capture_output=True, text=True, timeout=10,
        )
        if res.returncode == 0 and f'"{pid}"' not in res.stdout and "INFO:" in res.stdout:
            return False
        return True  # conservatively assume alive unless Windows explicitly confirms no matching task
    if cur_host == "nt" and owner_host == "posix" and owner_distro and shutil.which("wsl.exe"):
        res = subprocess.run(
            ["wsl.exe", "-d", owner_distro, "--", "sh", "-c", 'test -d "/proc/$1" && echo ALIVE || echo DEAD', "_", str(pid)],
            capture_output=True, text=True, timeout=10,
        )
        if res.returncode == 0 and res.stdout.strip() == "DEAD":
            return False
        return True  # conservatively assume alive on any distro/interop error
    # Unknown foreign host without exact distro/interop: never steal a potentially live lock.
    return True


def _encode_distro(distro):
    if not distro:
        return "0"
    return base64.urlsafe_b64encode(distro.encode("utf-8")).decode("ascii").rstrip("=")


def _decode_distro(tag):
    if not tag or tag == "0":
        return ""
    pad = "=" * (-len(tag) % 4)
    try:
        return base64.urlsafe_b64decode((tag + pad).encode("ascii")).decode("utf-8")
    except (ValueError, UnicodeDecodeError):
        return ""


def _resolve_legacy_distro_tag(tag):
    """Resolve a b78e066 hyphen-to-underscore distro tag to an exact distro name if unambiguous, else None."""
    if not tag:
        return ""
    if tag == "none":
        if os.name != "nt" and os.environ.get("WSL_DISTRO_NAME", "") == "none":
            return "none"
        return ""
    if "_" not in tag:
        return tag
    # Both WSL and Windows can query wsl.exe -l -q to prove whether tag maps to a unique installed WSL distro.
    if shutil.which("wsl.exe"):
        res = subprocess.run(["wsl.exe", "-l", "-q"], capture_output=True, timeout=10)
        if res.returncode == 0:
            text = res.stdout.decode("utf-16-le", errors="ignore").replace("\x00", "")
            matches = [d.strip() for d in text.splitlines() if d.strip() and d.strip().replace("-", "_") == tag]
            if len(matches) == 1:
                return matches[0]
    return None


def _parse_token_identity(token):
    parts = token.split(".")
    if len(parts) == 4 and parts[0] in ("nt", "posix") and parts[1].isdigit():
        return parts[0], int(parts[1]), _decode_distro(parts[2])
    # Also recognize legacy b78e066 hyphenated token format: <host>-<pid>-<distro_tag>-<ns>
    hparts = token.split("-")
    if len(hparts) >= 4 and hparts[0] in ("nt", "posix") and hparts[1].isdigit():
        fhost, fpid = hparts[0], int(hparts[1])
        if fhost == "nt":
            return fhost, fpid, ""
        resolved = _resolve_legacy_distro_tag(hparts[2])
        if resolved is None:
            return "__ambiguous__", fpid, ""
        return fhost, fpid, resolved
    return None, None, ""


def _rename_dir_noreplace(src, dst):
    """Atomically rename src -> dst, failing with FileExistsError if dst exists (even if dst is an empty directory)."""
    if os.name == "nt":
        os.rename(src, dst)
        return
    if sys.platform.startswith("linux"):
        import ctypes
        libc = ctypes.CDLL("libc.so.6", use_errno=True)
        AT_FDCWD = -100
        RENAME_NOREPLACE = 1
        rc = libc.renameat2(AT_FDCWD, os.fsencode(src), AT_FDCWD, os.fsencode(dst), RENAME_NOREPLACE)
        if rc == 0:
            return
        err = ctypes.get_errno()
        raise OSError(err, os.strerror(err), str(src), None, str(dst))
    if Path(dst).exists():
        raise FileExistsError(str(dst))
    os.rename(src, dst)


def _rename_dir_with_retry(src, dst, attempts=5):
    for i in range(attempts):
        try:
            os.rename(src, dst)
            return True
        except OSError:
            if i + 1 < attempts:
                time.sleep(0.02)
    return False


def _try_clear_dead_lock(lock_dir, my_token):
    """Atomically clear a provably dead lockdir (current tok-*, legacy b78e066 tok-*, two-file tok+owner.json, or owner.json)."""
    try:
        entries = list(lock_dir.iterdir())
    except OSError:
        return False
    if not entries:
        # .lockv2 is never created empty by a live owner (only an interrupted b78e066 release leaves it empty).
        # .lockdir is held empty for the entire critical section by a live c5ae204 mkdir-only publisher, so
        # never remove an empty .lockdir regardless of age (current publishers never create or leave .lockdir empty).
        if lock_dir.name.endswith(".lockv2"):
            try:
                os.rmdir(lock_dir)
                return True
            except OSError:
                return False
        return False

    owner_file = lock_dir / "owner.json"
    # Case 1: current-format tok-<token>, interrupted .reclaimed-<token>, legacy b78e066 tok-*, or two-file tok-* + owner.json.
    tok_files = [p for p in entries if p.is_file() and (p.name.startswith("tok-") or p.name.startswith(".reclaimed-"))]
    if tok_files:
        target = tok_files[0]
        raw_tok = target.name[4:] if target.name.startswith("tok-") else target.name[11:]
        host = pid = None
        distro = ""
        # Prefer the complete JSON record inside target when its token matches the filename (preserves exact hyphenated distro).
        try:
            owner = json.loads(target.read_text(encoding="utf-8"))
            if isinstance(owner, dict) and owner.get("token") == raw_tok:
                host, pid, distro = owner.get("host"), owner.get("pid"), owner.get("distro", "")
        except (OSError, ValueError):
            pass
        if host is None:
            host, pid, distro = _parse_token_identity(raw_tok)
        if host == "__ambiguous__":
            return False
        if host is None and owner_file.is_file():
            try:
                owner = json.loads(owner_file.read_text(encoding="utf-8"))
                host, pid, distro = owner.get("host"), owner.get("pid"), owner.get("distro", "")
            except (OSError, ValueError):
                return False
        if host is None:
            return False
        if _pid_is_alive_cross_host(pid, host, distro):
            return False
        # CAS claim via rename on the exact dead marker filename; roll back if directory rename fails.
        claimed = lock_dir / f".reclaimed-{my_token}"
        try:
            os.rename(target, claimed)
        except OSError:
            return False
        trash = lock_dir.parent / f"{lock_dir.name}.trash-{my_token}"
        if not _rename_dir_with_retry(lock_dir, trash):
            try:
                os.rename(claimed, target)
            except OSError:
                pass
            return False
        shutil.rmtree(trash, ignore_errors=True)
        return True

    # Case 2: legacy owner.json without tok-* (new locks never create owner.json, so CAS rename cannot touch a new lock).
    if owner_file.is_file():
        try:
            owner = json.loads(owner_file.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return False
        if _pid_is_alive_cross_host(owner.get("pid"), owner.get("host"), owner.get("distro", "")):
            return False
        claimed = lock_dir / f".reclaimed-{my_token}"
        try:
            os.rename(owner_file, claimed)
        except OSError:
            return False
        trash = lock_dir.parent / f"{lock_dir.name}.trash-{my_token}"
        if not _rename_dir_with_retry(lock_dir, trash):
            try:
                os.rename(claimed, owner_file)
            except OSError:
                pass
            return False
        shutil.rmtree(trash, ignore_errors=True)
        return True

    # Case 3: orphaned b78e066 .tok-*.tmp takeover residue in .lockv2 with no tok-* or owner.json remaining.
    if lock_dir.name.endswith(".lockv2") and all(
        p.is_file() and p.name.startswith(".tok-") and p.name.endswith(".tmp") for p in entries
    ):
        for p in entries:
            try:
                p.unlink()
            except OSError:
                return False
        try:
            os.rmdir(lock_dir)
            return True
        except OSError:
            return False

    return False


def _acquire_single_lock_dir(lock_root, lock_dir, key, host, distro, token, deadline):
    prep = Path(tempfile.mkdtemp(dir=lock_root, prefix=f".ref-{key}.prep-"))
    try:
        (prep / f"tok-{token}").write_text(
            json.dumps({"pid": os.getpid(), "host": host, "distro": distro, "token": token, "created": time.time()}),
            encoding="utf-8",
        )
        while True:
            try:
                _rename_dir_noreplace(prep, lock_dir)
                return
            except OSError:
                if lock_dir.is_dir() and _try_clear_dead_lock(lock_dir, token):
                    continue
                if time.monotonic() >= deadline:
                    raise TimeoutError(f"timed out waiting for store lock {lock_dir}")
                time.sleep(0.02)
    finally:
        if prep.exists():
            shutil.rmtree(prep, ignore_errors=True)


def _release_single_lock_dir(lock_root, lock_dir, token):
    if not (lock_dir / f"tok-{token}").is_file():
        return
    trash = lock_root / f"{lock_dir.name}.rel-{token}"
    if _rename_dir_with_retry(lock_dir, trash):
        shutil.rmtree(trash, ignore_errors=True)
    else:
        # Never unlink tok-<token> while leaving lock_dir behind (which would strand an empty directory);
        # raise immediately so the caller observes the release failure and tok-<token> stays recoverable.
        raise OSError(f"failed to release store lock {lock_dir} after retries")


@contextlib.contextmanager
def _store_lock(store_dir, timeout=120.0):
    """Cross-host exclusive lock holding both .lockdir and legacy .lockv2 in fixed order."""
    resolved = Path(store_dir).resolve()
    try:
        rel = resolved.relative_to(ROOT.resolve()).as_posix()
        # Shared repo-relative lock dirs under build/ (gitignored) so WSL and Windows hit the exact same VFS rename.
        key = hashlib.sha256(rel.encode("utf-8")).hexdigest()[:16]
        lock_root = ROOT / "build"
        lock_root.mkdir(parents=True, exist_ok=True)
        lock_dir = lock_root / f".ref-{key}.lockdir"
        legacy_v2_dir = lock_root / f".ref-{key}.lockv2"
    except ValueError:
        key = hashlib.sha256(str(resolved).encode("utf-8")).hexdigest()[:16]
        lock_root = Path(tempfile.gettempdir())
        lock_dir = lock_root / f"impossible-sdk-ref-{key}.lockdir"
        legacy_v2_dir = lock_root / f"impossible-sdk-ref-{key}.lockv2"

    host = "nt" if os.name == "nt" else "posix"
    distro = os.environ.get("WSL_DISTRO_NAME", "") if host == "posix" else ""
    token = f"{host}.{os.getpid()}.{_encode_distro(distro)}.{time.time_ns()}"
    deadline = time.monotonic() + timeout
    acquired_main = False
    acquired_v2 = False
    try:
        _acquire_single_lock_dir(lock_root, lock_dir, key, host, distro, token, deadline)
        acquired_main = True
        _acquire_single_lock_dir(lock_root, legacy_v2_dir, key, host, distro, token, deadline)
        acquired_v2 = True
        yield
    finally:
        first_err = None
        if acquired_v2:
            try:
                _release_single_lock_dir(lock_root, legacy_v2_dir, token)
            except Exception as err:
                first_err = err
        if acquired_main:
            try:
                _release_single_lock_dir(lock_root, lock_dir, token)
            except Exception as err:
                if first_err is None:
                    first_err = err
        if first_err is not None:
            raise first_err


def publish_store(fresh_dir, store_dir, expected_specs):
    """Stage compressed frames + index, validate completely, and atomically swap under an exclusive lock."""
    fresh = Path(fresh_dir)
    store = Path(store_dir)
    manifest = json.loads((fresh / "cases.json").read_text(encoding="utf-8"))

    installed_fonts = manifest.get("installed_fonts")
    installed_font_ranges = manifest.get("installed_font_ranges")
    for spec in expected_specs.values():
        check_no_prohibited_assets(
            spec, require_installed=True,
            installed_fonts=installed_fonts, installed_font_ranges=installed_font_ranges,
        )

    with _store_lock(store):
        backup = store.parent / f".{store.name}.backup"
        _recover_dir_unlocked(store, is_store=True)
        if store.exists():
            if not store.is_dir():
                raise ValueError(f"{store}: store path exists and is not a directory")
            if any(store.iterdir()) and not _is_owned_backup(store, is_store=True):
                raise ValueError(f"{store}: refuses to replace non-empty store containing unowned or unrelated files")

        stage = Path(tempfile.mkdtemp(dir=store.parent, prefix=f".{store.name}.staging-"))
        try:
            for case in manifest["cases"]:
                cid = case["id"]
                if cid not in expected_specs:
                    raise ValueError(f"fresh capture contains unexpected case {cid!r}")
                if case.get("basis") == "compiled-template":
                    case["case_sha256"] = canonical_case_hash(expected_specs[cid])
                    continue
                raw_path = fresh / case["file"]
                raw = raw_path.read_bytes()
                zbytes = zlib.compress(raw, 9)
                (stage / f"{case['file']}.z").write_bytes(zbytes)
                case["sha256"] = hashlib.sha256(zbytes).hexdigest()
                case["raw_sha256"] = hashlib.sha256(raw).hexdigest()
                case["case_sha256"] = canonical_case_hash(expected_specs[cid])

            prov = compute_package_fingerprint()
            manifest["provenance"] = prov
            (stage / "cases.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
            (stage / STORE_MARKER).write_text("impossible-sdk-reference-store\n", encoding="utf-8", newline="\n")
            validate_store_dir(stage, expected_specs, _locked=True, expected_provenance=prov)

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
            frame_count = sum(1 for c in manifest["cases"] if c.get("basis") != "compiled-template")
            fallback_count = sum(1 for c in manifest["cases"] if c.get("basis") == "compiled-template")
            return frame_count, fallback_count
        except Exception:
            shutil.rmtree(stage, ignore_errors=True)
            raise


def build_reference(work_dir):
    work = Path(work_dir)
    dotnet = "dotnet.exe" if is_wsl() else "dotnet"
    shutil.copytree(HERE, work / "reference", ignore=shutil.ignore_patterns("*.py"))
    run_cmd(dotnet, "build", "-c", "Release", "-v", "q", "--nologo", cwd=work / "reference")
    return next((work / "build/reference/bin").rglob("Reference.exe"))


def _load_missing_control_checker():
    winui_dir = str(ROOT / "tools/winui")
    if winui_dir not in sys.path:
        sys.path.insert(0, winui_dir)
    import check_package
    return check_package


def run_reference(program, out_dir, cases_path=None, work_dir=None, snapshotted_specs=None,
                  store_dir=None, pkg_manifest_path=None, compare_baseline=None):
    out = Path(out_dir)
    _ensure_safe_capture_dir(out)
    pkg_path = Path(pkg_manifest_path) if pkg_manifest_path is not None else PACKAGE_MANIFEST
    pkg_data = json.loads(pkg_path.read_text(encoding="utf-8")) if pkg_path.is_file() else {}
    missing_controls = {
        e.get("control") for e in pkg_data.get("missing_controls", []) if isinstance(e, dict) and e.get("control")
    }

    # Partition missing-control cases so controls lacking in the WinUI package route to compiled-template checks.
    missing_results = []
    live_specs = None
    if missing_controls:
        specs_to_partition = snapshotted_specs
        if specs_to_partition is None:
            specs_to_partition = load_case_specs(cases_path if cases_path is not None else CASES_DIR)
        check_pkg = _load_missing_control_checker()
        tpl_c = ROOT / "ui/template_data.c"
        def _check_no_nested_missing(node, root_id):
            for ch in node.get("children") or []:
                ch_ctrl = ch.get("control")
                if ch_ctrl in missing_controls:
                    raise ValueError(
                        f"case {root_id!r}: nested missing-package control {ch_ctrl!r} inside live tree is not supported by compiled-template fallback (see D01 T01 §16 / D01 T01 §7)"
                    )
                _check_no_nested_missing(ch, root_id)

        live_list = []
        for c in specs_to_partition.values():
            if c.get("control") in missing_controls:
                missing_results.append(check_pkg.check_missing_control_case(c, pkg_data, tpl_c))
            else:
                _check_no_nested_missing(c, c.get("id", "<unknown>"))
                live_list.append(c)
        live_specs = live_list

    with tempfile.TemporaryDirectory(dir=host_temp_root(), prefix="impossible-ref-run-") as run_tmp:
        rt = Path(run_tmp)
        rt_out = rt / "out"
        if live_specs is not None and not live_specs:
            # All cases were missing-control fallbacks; emit cases.json directly without launching Reference.exe.
            rt_out.mkdir(parents=True, exist_ok=True)
            (rt_out / "cases.json").write_text(
                json.dumps({"accents": {}, "cases": missing_results}, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )
        else:
            win_out = to_win_path(rt_out)
            cmd = [str(program), win_out]
            if live_specs is not None:
                staged_cases = rt / "snapshot_cases"
                staged_cases.mkdir(parents=True, exist_ok=True)
                (staged_cases / "snapshot.json").write_text(
                    json.dumps({"cases": live_specs}, indent=2),
                    encoding="utf-8",
                )
                cmd += ["--cases", to_win_path(staged_cases)]
            elif snapshotted_specs is not None:
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
            err_file = rt_out / "error.txt"
            err_text = err_file.read_text(encoding="utf-8", errors="replace") if err_file.is_file() else ""
            if res.returncode != 0:
                if err_text:
                    sys.stderr.write(err_text + "\n")
                raise subprocess.CalledProcessError(res.returncode, cmd, output=res.stdout, stderr=(res.stderr or "") + "\n" + err_text)
            if missing_results:
                mpath = rt_out / "cases.json"
                manifest = json.loads(mpath.read_text(encoding="utf-8"))
                manifest.setdefault("cases", []).extend(missing_results)
                mpath.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")

        # Compare fresh 26 legacy frames against baseline BEFORE any store update if requested.
        if compare_baseline is not None:
            compare_baseline_frames(rt_out, compare_baseline)

        # Publish store directly from the private per-run capture directory so concurrent runs touching `out`
        # can never mix another run's pixels with this run's snapshotted case hashes.
        publish_counts = None
        if store_dir is not None:
            publish_counts = publish_store(rt_out, store_dir, snapshotted_specs)
        _publish_capture_output(rt_out, out)
    if store_dir is not None:
        return out, publish_counts
    return out


def capture(out_dir, cases_path=None, snapshotted_specs=None, store_dir=None, pkg_manifest_path=None, compare_baseline=None):
    with tempfile.TemporaryDirectory(dir=host_temp_root(), prefix="impossible-reference-") as folder:
        program = build_reference(folder)
        return run_reference(
            program, out_dir, cases_path=cases_path, work_dir=folder,
            snapshotted_specs=snapshotted_specs, store_dir=store_dir,
            pkg_manifest_path=pkg_manifest_path,
            compare_baseline=compare_baseline,
        )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, default=None, help="single case JSON file or directory (cannot be combined with --update-store)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output directory for fresh captures")
    parser.add_argument("--update-store", action="store_true", help="atomically publish validated compressed frames into tests/reference/")
    parser.add_argument("--compare-baseline", type=Path, default=None, help="compare fresh 26 legacy frames against a baseline store before publishing (defaults to tests/reference on --update-store)")
    parser.add_argument("--skip-baseline", action="store_true", help="skip the automatic 26-frame baseline check during --update-store (only for intentional WinUI upgrades)")
    args = parser.parse_args(argv)

    if args.update_store and args.cases is not None:
        print("error: --update-store refuses --cases (partial runs cannot replace the full reference store)", file=sys.stderr)
        return 2

    baseline = args.compare_baseline
    if args.update_store and baseline is None and not args.skip_baseline and STORE.is_dir():
        baseline = STORE

    # Snapshot case specifications BEFORE capture so publication hashes the exact private capture rendered by Reference.exe.
    specs = load_case_specs(CASES_DIR) if args.update_store else None
    result = capture(
        args.out,
        args.cases,
        snapshotted_specs=specs,
        store_dir=STORE if args.update_store else None,
        compare_baseline=baseline,
    )
    if baseline is not None:
        print(f"baseline comparison passed ({len(LEGACY_BASELINE_IDS)} legacy frames matched {baseline})")
    if args.update_store:
        out, (frame_count, fallback_count) = result
    else:
        out = result
    frames = list(out.glob("*.bgra"))
    print(f"{len(frames)} frames in {out}")
    if args.update_store:
        if fallback_count:
            print(f"published {frame_count} validated compressed frames and {fallback_count} compiled-template fallbacks to {STORE}")
        else:
            print(f"published {frame_count} validated compressed frames to {STORE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
