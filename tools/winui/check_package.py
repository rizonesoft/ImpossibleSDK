#!/usr/bin/env python3
"""Verify reference/package.json against Reference.csproj, packages.lock.json, tools/winui/pin.json,
and (when present locally) the installed Microsoft.WindowsAppSDK.WinUI NuGet package and WinUI checkout.

    python3 tools/winui/check_package.py [--root <repo>] [--winui <checkout>] [--nuget-pkg <dir>] [--require-live]
"""
import argparse
import hashlib
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
X_NS = "{http://schemas.microsoft.com/winfx/2006/xaml}"


class PackageDrift(Exception):
    """Raised when reference/package.json disagrees with project files, pin.json, or installed package."""


def _canon_xml(elem):
    parts = [elem.tag.split("}")[-1]]
    for k, v in sorted(elem.attrib.items()):
        parts.append(f"{k.split('}')[-1]}={v}")
    t = (elem.text or "").strip()
    if t:
        parts.append(f"TEXT={t}")
    for c in elem:
        parts.append("(" + _canon_xml(c) + ")")
    return " ".join(parts)


def extract_templates(xaml_path):
    root = ET.fromstring(Path(xaml_path).read_text(encoding="utf-8-sig"))
    out = {}
    for el in root.iter():
        if el.tag.endswith("}Style") or el.tag == "Style":
            key = el.attrib.get(f"{X_NS}Key") or el.attrib.get("TargetType")
            for s in el:
                if (s.tag.endswith("}Setter") or s.tag == "Setter") and s.attrib.get("Property") == "Template":
                    out[key] = _canon_xml(s)
    return out


def extract_target_types(xaml_path):
    root = ET.fromstring(Path(xaml_path).read_text(encoding="utf-8-sig"))
    types = set()
    for el in root.iter():
        if el.tag.endswith("}Style") or el.tag == "Style":
            tt = el.attrib.get("TargetType", "")
            if tt:
                types.add(tt.split(":")[-1])
    return types


def extract_dll_commit(dll_path):
    """Extract the 40-hex source commit from Microsoft.ui.xaml.dll's UTF-16LE FileVersion resource."""
    data = Path(dll_path).read_bytes()
    marker = "FileVersion".encode("utf-16-le") + b"\x00\x00"
    idx = data.find(marker)
    if idx == -1:
        raise PackageDrift(f"{dll_path}: FileVersion resource not found; cannot establish WinUI source revision")
    chunk = data[idx:idx + 320].decode("utf-16-le", errors="ignore")
    m = re.search(r"-([0-9a-f]{40})\)", chunk)
    if not m:
        raise PackageDrift(f"{dll_path}: no 40-hex commit found in FileVersion ({chunk[:80]!r})")
    ver_m = re.search(r"(\d+\.\d+\.\d+\.\d+)", chunk)
    return m.group(1), (ver_m.group(1) if ver_m else "")


def find_nuget_winui_pkg(version):
    candidates = []
    if os.name == "nt":
        home = Path(os.environ.get("USERPROFILE", str(Path.home())))
        candidates.append(home / ".nuget/packages/microsoft.windowsappsdk.winui" / version)
    else:
        for user_dir in sorted(Path("/mnt/c/Users").glob("*")):
            candidates.append(user_dir / ".nuget/packages/microsoft.windowsappsdk.winui" / version)
    for c in candidates:
        if c.is_dir():
            return c
    return None


def compute_template_diffs(pkg_generic_xaml, winui_checkout, pin_data):
    """Return full symmetric template difference: changed, pin_only, and package_only."""
    pkg_t = extract_templates(pkg_generic_xaml)
    pin_t = {}
    base = Path(winui_checkout) / pin_data["base"]
    if base.is_file():
        pin_t.update(extract_templates(base))
    skip = tuple(pin_data.get("skip", []))
    for p in sorted(Path(winui_checkout).glob(pin_data["controls"])):
        if any(s in p.name for s in skip):
            continue
        if p.is_file():
            pin_t.update(extract_templates(p))
    changed = sorted(k for k in (set(pin_t) & set(pkg_t)) if pin_t[k] != pkg_t[k])
    pin_only = sorted(set(pin_t) - set(pkg_t))
    package_only = sorted(set(pkg_t) - set(pin_t))
    return {
        "changed": changed,
        "pin_only": pin_only,
        "package_only": package_only,
    }


def parse_compiled_templates(template_c_path):
    """Extract compiled template names, target control, element counts, and total visual state counts from ui/template_data.c."""
    text = Path(template_c_path).read_text(encoding="utf-8")
    groups_block = text.split("const im_template_group im_template_groups[]", 1)[1].split("};", 1)[0]
    groups = [
        {"name": m.group(1), "state_count": int(m.group(2))}
        for m in re.finditer(r'\{\s*"([^"]+)",\s*\d+,\s*(\d+),\s*\d+,\s*\d+\s*\}', groups_block)
    ]

    tpl_block = text.split("const im_template im_templates[IM_TEMPLATE_COUNT]", 1)[1].split("};", 1)[0]
    out = {}
    for m in re.finditer(
        r'\{\s*"([^"]+)",\s*"([^"]+)",\s*\d+,\s*(\d+),\s*\d+,\s*(\d+),\s*(\d+),\s*(\d+)\s*\}',
        tpl_block,
    ):
        name, target = m.group(1), m.group(2)
        setter_count, elem_count = int(m.group(3)), int(m.group(4))
        group_off, group_count = int(m.group(5)), int(m.group(6))
        state_count = sum(groups[i]["state_count"] for i in range(group_off, group_off + group_count))
        out[name] = {
            "target": target,
            "setter_count": setter_count,
            "elem_count": elem_count,
            "group_count": group_count,
            "state_count": state_count,
        }
    return out


def compute_provenance_sha256(pkg_data):
    """Tamper-evident digest over package provenance and template_diffs."""
    payload = {
        "package": pkg_data.get("package"),
        "version": pkg_data.get("version"),
        "winui_component": pkg_data.get("winui_component"),
        "package_source_commit": pkg_data.get("package_source_commit"),
        "generic_xaml_sha256": pkg_data.get("generic_xaml_sha256"),
        "winui_pin_commit": pkg_data.get("winui_pin_commit"),
        "template_diffs": pkg_data.get("template_diffs"),
    }
    return hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def verify_missing_controls(pkg_data, template_c_path, theme_c_path=None, planned_controls=None, pkg_target_types=None):
    """Reconcile missing_controls entries against package availability and compiled templates."""
    missing = pkg_data.get("missing_controls")
    if not isinstance(missing, list) or not missing:
        raise PackageDrift("reference/package.json: missing_controls must be a non-empty list")
    compiled = parse_compiled_templates(template_c_path)
    by_name = {}
    results = []
    for entry in missing:
        ctrl = entry.get("control")
        status = entry.get("status")
        basis = entry.get("basis")
        tpl = entry.get("compiled_template")
        if not ctrl or status not in ("compiled-template", "not-yet-compiled"):
            raise PackageDrift(f"reference/package.json: invalid missing_controls entry {entry!r}")
        if ctrl in by_name:
            raise PackageDrift(f"reference/package.json: duplicate missing_controls entry {ctrl!r}")
        if pkg_target_types is not None and ctrl in pkg_target_types:
            raise PackageDrift(f"reference/package.json: {ctrl!r} is listed as missing but exists in package generic.xaml")

        record = {"control": ctrl, "status": status, "basis": basis, "compiled_template": tpl}
        if status == "compiled-template":
            if basis != "compiled-template" or not tpl or tpl not in compiled:
                raise PackageDrift(f"reference/package.json: missing_controls {ctrl!r} must name a valid compiled_template")
            info = compiled[tpl]
            if info["target"] != ctrl:
                raise PackageDrift(
                    f"reference/package.json: missing_controls {ctrl!r} points to unrelated template {tpl!r} (target={info['target']!r})"
                )
            exp_elems = entry.get("expected_elements")
            exp_states = entry.get("expected_states")
            exp_setters = entry.get("expected_setters")
            if exp_elems != info["elem_count"]:
                raise PackageDrift(
                    f"reference/package.json: missing_controls {ctrl!r} expected_elements={exp_elems} != compiled {tpl!r} ({info['elem_count']})"
                )
            if exp_states != info["state_count"]:
                raise PackageDrift(
                    f"reference/package.json: missing_controls {ctrl!r} expected_states={exp_states} != compiled {tpl!r} ({info['state_count']})"
                )
            if exp_setters is not None and exp_setters != info["setter_count"]:
                raise PackageDrift(
                    f"reference/package.json: missing_controls {ctrl!r} expected_setters={exp_setters} != compiled {tpl!r} ({info['setter_count']})"
                )
            record["verified_elements"] = info["elem_count"]
            record["verified_states"] = info["state_count"]
            record["verified_setters"] = info["setter_count"]
        else:
            # Honestly marked not-yet-compiled: must not claim a compiled-template basis without a real compiled template.
            if tpl is not None:
                raise PackageDrift(f"reference/package.json: {ctrl!r} has compiled_template={tpl!r} but status='not-yet-compiled'")
            if any(info["target"] == ctrl for info in compiled.values()):
                raise PackageDrift(f"reference/package.json: {ctrl!r} now has a compiled template in ui/template_data.c and must switch to status='compiled-template'")
        by_name[ctrl] = entry
        results.append(record)

    if planned_controls:
        for ctrl in planned_controls:
            if ctrl not in by_name:
                raise PackageDrift(f"reference/package.json: planned absent control {ctrl!r} missing from missing_controls")
    return results


def check_missing_control_case(case_spec, pkg_data, template_c_path):
    """Route a case targeting a missing control to its own compiled template in ui/template_data.c."""
    ctrl = case_spec.get("control")
    missing_by_ctrl = {e.get("control"): e for e in pkg_data.get("missing_controls", []) if isinstance(e, dict)}
    if ctrl not in missing_by_ctrl:
        raise PackageDrift(f"case {case_spec.get('id')!r}: control {ctrl!r} is not listed in reference/package.json missing_controls")
    entry = missing_by_ctrl[ctrl]
    tpl = entry.get("compiled_template")
    if entry.get("status") != "compiled-template" or not tpl:
        raise PackageDrift(f"case {case_spec.get('id')!r}: missing control {ctrl!r} has no compiled template in ui/template_data.c yet")
    compiled = parse_compiled_templates(template_c_path)
    if tpl not in compiled or compiled[tpl]["target"] != ctrl:
        raise PackageDrift(f"case {case_spec.get('id')!r}: compiled template {tpl!r} does not target {ctrl!r}")
    info = compiled[tpl]
    for field, actual in (("expected_elements", info["elem_count"]), ("expected_states", info["state_count"]), ("expected_setters", info["setter_count"])):
        if field in case_spec and case_spec[field] != actual:
            raise PackageDrift(f"case {case_spec.get('id')!r}: {field}={case_spec[field]} != compiled {tpl!r} ({actual})")
    return {
        "id": case_spec.get("id"),
        "control": ctrl,
        "basis": "compiled-template",
        "template": tpl,
        "verified_elements": info["elem_count"],
        "verified_states": info["state_count"],
        "verified_setters": info["setter_count"],
    }


def verify_package(repo_root, winui_checkout=None, nuget_pkg=None, require_live=True):
    repo = Path(repo_root)
    pkg_path = repo / "reference/package.json"
    csproj_path = repo / "reference/Reference.csproj"
    lock_path = repo / "reference/packages.lock.json"
    pin_path = repo / "tools/winui/pin.json"
    tpl_c = repo / "ui/template_data.c"

    pkg = json.loads(pkg_path.read_text(encoding="utf-8"))
    pin = json.loads(pin_path.read_text(encoding="utf-8"))
    lock = json.loads(lock_path.read_text(encoding="utf-8"))

    # 1. Check Reference.csproj PackageReference version matches package.json.
    csproj_text = csproj_path.read_text(encoding="utf-8")
    m = re.search(r'<PackageReference\s+Include="Microsoft\.WindowsAppSDK"\s+Version="([^"]+)"', csproj_text)
    if not m:
        raise PackageDrift(f"{csproj_path}: Microsoft.WindowsAppSDK PackageReference not found")
    csproj_ver = m.group(1)
    if pkg.get("package") != "Microsoft.WindowsAppSDK" or pkg.get("version") != csproj_ver:
        raise PackageDrift(f"package.json version {pkg.get('version')!r} != Reference.csproj {csproj_ver!r}")

    # 2. Check packages.lock.json resolved umbrella + Microsoft.WindowsAppSDK.WinUI component.
    deps = next(iter(lock.get("dependencies", {}).values()), {})
    sdk_lock = deps.get("Microsoft.WindowsAppSDK", {})
    if sdk_lock.get("resolved") != csproj_ver:
        raise PackageDrift(f"packages.lock.json Microsoft.WindowsAppSDK {sdk_lock.get('resolved')!r} != {csproj_ver!r}")

    winui_lock = deps.get("Microsoft.WindowsAppSDK.WinUI", {})
    comp = pkg.get("winui_component") or {}
    if comp.get("name") != "Microsoft.WindowsAppSDK.WinUI":
        raise PackageDrift("package.json: winui_component.name must be 'Microsoft.WindowsAppSDK.WinUI'")
    if comp.get("version") != winui_lock.get("resolved"):
        raise PackageDrift(
            f"package.json winui_component.version {comp.get('version')!r} != packages.lock.json {winui_lock.get('resolved')!r}"
        )
    if comp.get("content_hash") != winui_lock.get("contentHash"):
        raise PackageDrift("package.json winui_component.content_hash != packages.lock.json contentHash")

    # 3. Check tools/winui/pin.json commit and structured template_diffs shape.
    if pkg.get("winui_pin_commit") != pin.get("commit"):
        raise PackageDrift(f"package.json winui_pin_commit {pkg.get('winui_pin_commit')!r} != pin.json {pin.get('commit')!r}")

    commit = pkg.get("package_source_commit", "")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise PackageDrift(f"package.json package_source_commit {commit!r} is not a 40-hex commit")

    recorded_diffs = pkg.get("template_diffs")
    if not isinstance(recorded_diffs, dict) or set(recorded_diffs.keys()) != {"changed", "pin_only", "package_only"}:
        raise PackageDrift("package.json template_diffs must be an object with 'changed', 'pin_only', and 'package_only' lists")
    for bucket in ("changed", "pin_only", "package_only"):
        vals = recorded_diffs[bucket]
        if not isinstance(vals, list) or vals != sorted(set(vals)):
            raise PackageDrift(f"package.json template_diffs.{bucket} must be a sorted unique list")

    expected_seal = compute_provenance_sha256(pkg)
    if pkg.get("provenance_sha256") != expected_seal:
        raise PackageDrift(f"package.json provenance_sha256 mismatch (expected {expected_seal})")

    # 4. Resolve NuGet WinUI package (explicit --nuget-pkg MUST exist).
    if nuget_pkg is not None:
        pkg_dir = Path(nuget_pkg)
        if not pkg_dir.is_dir():
            raise PackageDrift(f"--nuget-pkg directory not found: {pkg_dir}")
    else:
        pkg_dir = find_nuget_winui_pkg(comp["version"])

    checked_live = False
    pkg_target_types = None
    if pkg_dir is not None and pkg_dir.is_dir():
        dll = pkg_dir / "runtimes-framework/win-x64/native/Microsoft.ui.xaml.dll"
        gx = pkg_dir / "lib/native/Microsoft.UI/Themes/generic.xaml"
        if not dll.is_file() or not gx.is_file():
            raise PackageDrift(f"{pkg_dir}: missing Microsoft.ui.xaml.dll or generic.xaml")
        actual_commit, actual_ver = extract_dll_commit(dll)
        if actual_commit != commit:
            raise PackageDrift(f"package.json package_source_commit {commit!r} != DLL commit {actual_commit!r}")
        if comp.get("dll_file_version") and actual_ver and comp["dll_file_version"] != actual_ver:
            raise PackageDrift(f"package.json dll_file_version {comp['dll_file_version']!r} != DLL {actual_ver!r}")
        actual_gx_sha = hashlib.sha256(gx.read_bytes()).hexdigest()
        if pkg.get("generic_xaml_sha256") != actual_gx_sha:
            raise PackageDrift(f"package.json generic_xaml_sha256 != {gx} sha256 ({actual_gx_sha})")
        pkg_target_types = extract_target_types(gx)

        winui_dir = Path(winui_checkout) if winui_checkout else Path(os.environ.get("IMPOSSIBLE_WINUI", Path.home() / ".cache/impossible-sdk/winui"))
        if winui_checkout is not None and not (winui_dir / "LICENSE").is_file():
            raise PackageDrift(f"--winui checkout not found: {winui_dir}")
        if (winui_dir / "LICENSE").is_file():
            actual_diffs = compute_template_diffs(gx, winui_dir, pin)
            if recorded_diffs != actual_diffs:
                raise PackageDrift(
                    "package.json template_diffs mismatch: "
                    f"recorded=({len(recorded_diffs['changed'])}/{len(recorded_diffs['pin_only'])}/{len(recorded_diffs['package_only'])}) "
                    f"!= computed=({len(actual_diffs['changed'])}/{len(actual_diffs['pin_only'])}/{len(actual_diffs['package_only'])})"
                )
            checked_live = True

    if require_live and not checked_live:
        raise PackageDrift("live WinUI package and checkout verification required but unavailable on this host")

    # 5. Verify missing_controls against package availability and compiled template/theme values.
    missing_report = verify_missing_controls(
        pkg, tpl_c,
        theme_c_path=repo / "ui/theme_data.c",
        planned_controls=["WrapPanel", "UniformGrid", "DataGrid", "DockPanel"],
        pkg_target_types=pkg_target_types,
    )

    return {
        "version": csproj_ver,
        "winui_component": comp["version"],
        "package_source_commit": commit,
        "template_diffs": {k: len(v) for k, v in recorded_diffs.items()},
        "missing_controls": missing_report,
        "checked_live": checked_live,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--winui", type=Path, default=None)
    parser.add_argument("--nuget-pkg", type=Path, default=None)
    parser.add_argument("--require-live", action="store_true")
    args = parser.parse_args(argv)
    try:
        info = verify_package(args.root, args.winui, args.nuget_pkg, require_live=args.require_live)
    except PackageDrift as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    print(json.dumps(info, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
