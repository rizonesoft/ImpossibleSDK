#!/usr/bin/env python3
"""Verify reference/package.json against Reference.csproj, packages.lock.json, tools/winui/pin.json,
and (when present locally) the installed Microsoft.WindowsAppSDK.WinUI NuGet package and WinUI checkout.

    python3 tools/winui/check_package.py [--root <repo>] [--winui <checkout>] [--nuget-pkg <dir>]
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
    common = sorted(set(pin_t) & set(pkg_t))
    return [k for k in common if pin_t[k] != pkg_t[k]]


def parse_compiled_templates(template_c_path):
    """Extract compiled template names and (element_count, state_count) from ui/template_data.c."""
    text = Path(template_c_path).read_text(encoding="utf-8")
    out = {}
    # Match entries in im_templates[]: { "Name", "Target", setter_off, setter_count, elem_off, elem_count, group_off, group_count }
    for m in re.finditer(
        r'\{\s*"([^"]+)",\s*"([^"]+)",\s*\d+,\s*(\d+),\s*\d+,\s*(\d+),\s*\d+,\s*(\d+)\s*\}',
        text,
    ):
        name, target, setter_count, elem_count, group_count = m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5))
        out[name] = {"target": target, "setter_count": setter_count, "elem_count": elem_count, "group_count": group_count}
    return out


def verify_missing_controls(pkg_data, template_c_path, planned_controls=None):
    """Reconcile missing_controls entries against compiled templates and report basis='compiled-template'."""
    missing = pkg_data.get("missing_controls")
    if not isinstance(missing, list) or not missing:
        raise PackageDrift("reference/package.json: missing_controls must be a non-empty list")
    compiled = parse_compiled_templates(template_c_path)
    by_name = {}
    results = []
    for entry in missing:
        ctrl = entry.get("control")
        basis = entry.get("basis")
        tpl = entry.get("fallback_template")
        exp_elems = entry.get("expected_elements")
        if not ctrl or basis != "compiled-template" or not tpl:
            raise PackageDrift(f"reference/package.json: invalid missing_controls entry {entry!r}")
        if ctrl in by_name:
            raise PackageDrift(f"reference/package.json: duplicate missing_controls entry {ctrl!r}")
        if tpl not in compiled:
            raise PackageDrift(f"reference/package.json: missing_controls {ctrl!r} names unknown compiled template {tpl!r}")
        actual_elems = compiled[tpl]["elem_count"]
        if actual_elems != exp_elems:
            raise PackageDrift(
                f"reference/package.json: missing_controls {ctrl!r} expected_elements={exp_elems} != compiled {tpl!r} ({actual_elems})"
            )
        by_name[ctrl] = entry
        results.append({
            "control": ctrl,
            "basis": basis,
            "fallback_template": tpl,
            "verified_elements": actual_elems,
        })
    if planned_controls:
        for ctrl in planned_controls:
            if ctrl not in by_name:
                raise PackageDrift(f"reference/package.json: planned absent control {ctrl!r} missing from missing_controls")
    return results


def verify_package(repo_root, winui_checkout=None, nuget_pkg=None):
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

    # 3. Check tools/winui/pin.json commit matches package.json.
    if pkg.get("winui_pin_commit") != pin.get("commit"):
        raise PackageDrift(f"package.json winui_pin_commit {pkg.get('winui_pin_commit')!r} != pin.json {pin.get('commit')!r}")

    commit = pkg.get("package_source_commit", "")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise PackageDrift(f"package.json package_source_commit {commit!r} is not a 40-hex commit")

    # 4. Verify missing_controls against compiled templates.
    missing_report = verify_missing_controls(pkg, tpl_c, planned_controls=["WrapPanel", "UniformGrid", "DataGrid", "DockPanel"])

    # 5. If installed NuGet WinUI package is available, verify DLL source commit and generic.xaml hash.
    pkg_dir = Path(nuget_pkg) if nuget_pkg else find_nuget_winui_pkg(comp["version"])
    checked_live = False
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

        winui_dir = Path(winui_checkout) if winui_checkout else Path(os.environ.get("IMPOSSIBLE_WINUI", Path.home() / ".cache/impossible-sdk/winui"))
        if (winui_dir / "LICENSE").is_file():
            actual_diffs = compute_template_diffs(gx, winui_dir, pin)
            recorded_diffs = pkg.get("template_diffs")
            if recorded_diffs != actual_diffs:
                raise PackageDrift(
                    f"package.json template_diffs ({len(recorded_diffs or [])}) != computed diffs ({len(actual_diffs)})"
                )
            checked_live = True

    return {
        "version": csproj_ver,
        "winui_component": comp["version"],
        "package_source_commit": commit,
        "template_diffs": len(pkg.get("template_diffs", [])),
        "missing_controls": missing_report,
        "checked_live": checked_live,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--winui", type=Path, default=None)
    parser.add_argument("--nuget-pkg", type=Path, default=None)
    args = parser.parse_args(argv)
    try:
        info = verify_package(args.root, args.winui, args.nuget_pkg)
    except PackageDrift as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    print(json.dumps(info, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
