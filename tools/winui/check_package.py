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
        candidates.append(Path.home() / ".nuget/packages/microsoft.windowsappsdk.winui" / version)
        if Path("/mnt/c/Users").is_dir():
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
    """Extract compiled template names, target control, element counts, and visual states from ui/template_data.c."""
    text = Path(template_c_path).read_text(encoding="utf-8")
    states_block = text.split("const im_template_state im_template_states[]", 1)[1].split("};", 1)[0]
    states_list = [
        m.group(1)
        for m in re.finditer(r'\{\s*"([^"]+)",\s*\d+,\s*\d+,\s*\d+,\s*\d+\s*\}', states_block)
    ]
    groups_block = text.split("const im_template_group im_template_groups[]", 1)[1].split("};", 1)[0]
    groups = [
        {"name": m.group(1), "state_start": int(m.group(2)), "state_count": int(m.group(3))}
        for m in re.finditer(r'\{\s*"([^"]+)",\s*(\d+),\s*(\d+),\s*\d+,\s*\d+\s*\}', groups_block)
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
        tpl_states = set()
        for i in range(group_off, group_off + group_count):
            ss = groups[i]["state_start"]
            sc = groups[i]["state_count"]
            tpl_states.update(states_list[ss:ss + sc])
        out[name] = {
            "target": target,
            "setter_count": setter_count,
            "elem_count": elem_count,
            "group_count": group_count,
            "state_count": state_count,
            "states": tpl_states,
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


def compile_standalone_template(xaml_path):
    """Compile a standalone XAML Style template using tools/winui/templates.py and return its compiled metadata."""
    path = Path(xaml_path)
    if not path.is_file():
        raise PackageDrift(f"missing-control template source not found: {path}")
    winui_dir = str(Path(__file__).resolve().parent)
    if winui_dir not in sys.path:
        sys.path.insert(0, winui_dir)
    import templates as tpl_compiler
    doc = tpl_compiler.parse_xaml(path.read_text(encoding="utf-8"), str(path))
    comp = tpl_compiler.Compiler()
    if doc.tag != "Style":
        raise PackageDrift(f"{path}: standalone missing-control template root must be <Style>, got <{doc.tag}>")
    comp.compile_style(doc)
    tpl_compiler.verify_fully_consumed(doc)
    if not comp.templates:
        raise PackageDrift(f"{path}: compiled zero templates")
    t = comp.templates[0]
    gstart = t["group_start"]
    gcount = t["group_count"]
    state_count = sum(comp.groups[g]["state_count"] for g in range(gstart, gstart + gcount))
    tpl_states = set()
    for g in range(gstart, gstart + gcount):
        ss = comp.groups[g]["state_start"]
        sc = comp.groups[g]["state_count"]
        for s in range(ss, ss + sc):
            tpl_states.add(comp.states[s]["name"])
    estart = t["elem_start"]
    ecount = t["elem_count"]
    elems = [
        {
            "type": comp.elements[i]["kind"],
            "name": comp.elements[i].get("name", ""),
            "parent": comp.elements[i].get("parent", -1),
        }
        for i in range(estart, estart + ecount)
    ]
    border_inset = 0
    for child in doc.children:
        if child.tag == "Setter" and child.attrs.get("Property") == "BorderThickness":
            raw_bt = child.attrs.get("Value", "0").split(",")[0].strip()
            try:
                border_inset = max(0, int(float(raw_bt)))
            except ValueError:
                border_inset = 0
    return {
        t["key"]: {
            "target": t["target_type"],
            "setter_count": t["style_setter_count"],
            "elem_count": ecount,
            "group_count": gcount,
            "state_count": state_count,
            "states": tpl_states,
            "elements": elems,
            "border_inset": border_inset,
        }
    }


def _resolve_missing_control_template(entry, compiled_c, repo_root):
    tpl = entry.get("compiled_template")
    src = entry.get("template_source")
    if src:
        src_path = Path(src)
        if not src_path.is_absolute():
            src_path = Path(repo_root) / src_path
        standalone = compile_standalone_template(src_path)
        if tpl not in standalone:
            raise PackageDrift(
                f"reference/package.json: missing_controls {entry.get('control')!r} template {tpl!r} not found in {src}"
            )
        return standalone[tpl]
    if tpl not in compiled_c:
        raise PackageDrift(
            f"reference/package.json: missing_controls {entry.get('control')!r} names unknown compiled template {tpl!r}"
        )
    return compiled_c[tpl]


def verify_missing_controls(pkg_data, template_c_path, pkg_target_types=None, pin_target_types=None):
    """Verify every missing_controls entry is absent from the package and backed by its own compiled template."""
    missing = pkg_data.get("missing_controls")
    if not isinstance(missing, list):
        raise PackageDrift("reference/package.json: missing_controls must be a list")
    repo_root = Path(template_c_path).resolve().parent.parent
    compiled = parse_compiled_templates(template_c_path)
    by_name = {}
    results = []
    for entry in missing:
        ctrl = entry.get("control")
        basis = entry.get("basis")
        tpl = entry.get("compiled_template")
        if not ctrl or basis != "compiled-template" or not tpl:
            raise PackageDrift(f"reference/package.json: invalid missing_controls entry {entry!r} (must specify compiled_template)")
        if ctrl in by_name:
            raise PackageDrift(f"reference/package.json: duplicate missing_controls entry {ctrl!r}")
        if pkg_target_types is not None and ctrl in pkg_target_types:
            raise PackageDrift(f"reference/package.json: {ctrl!r} is listed as missing but exists in package generic.xaml")
        info = _resolve_missing_control_template(entry, compiled, repo_root)
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
        by_name[ctrl] = entry
        results.append({
            "control": ctrl,
            "basis": basis,
            "compiled_template": tpl,
            "verified_elements": info["elem_count"],
            "verified_states": info["state_count"],
            "verified_setters": info["setter_count"],
        })

    # If package target types are known, ensure no compiled or pinned control is absent from the package without being listed.
    if pkg_target_types is not None:
        required_targets = {info["target"] for info in compiled.values()}
        if pin_target_types is not None:
            required_targets |= set(pin_target_types)
        for target in sorted(required_targets):
            if target not in pkg_target_types and target not in by_name:
                raise PackageDrift(f"reference/package.json: control {target!r} is absent from package and missing from missing_controls")
    return results


def _num_val(x):
    if isinstance(x, bool) or not isinstance(x, (int, float)):
        raise PackageDrift(f"invalid thickness component: {x!r}")
    v = float(x)
    if v != v or v in (float("inf"), float("-inf")) or v < 0:
        raise PackageDrift(f"invalid thickness component: {x!r}")
    return int(v) if v.is_integer() else v


def _parse_thickness_inset(val):
    """Parse an int/float, 1/2/4-element number list, or XAML thickness string into (left, top, right, bottom) numbers."""
    if isinstance(val, (int, float)) and not isinstance(val, bool):
        n = _num_val(val)
        return (n, n, n, n)
    if isinstance(val, (list, tuple)):
        nums = [_num_val(x) for x in val]
        if len(nums) == 1:
            return (nums[0], nums[0], nums[0], nums[0])
        if len(nums) == 2:
            return (nums[0], nums[1], nums[0], nums[1])
        if len(nums) == 4:
            return tuple(nums)
    if isinstance(val, str):
        parts = [p.strip() for p in val.replace(" ", ",").split(",") if p.strip()]
        nums = [_num_val(float(p)) for p in parts]
        if len(nums) == 1:
            return (nums[0], nums[0], nums[0], nums[0])
        if len(nums) == 2:
            return (nums[0], nums[1], nums[0], nums[1])
        if len(nums) == 4:
            return tuple(nums)
    raise PackageDrift(f"invalid thickness value: {val!r}")


def _validate_frame_dict(frame, cid):
    fw, fh = 160, 64
    if frame is None:
        return fw, fh
    if not isinstance(frame, dict):
        raise PackageDrift(f"case {cid!r}: frame must be a JSON object")
    seen = set()
    for fk, fv in frame.items():
        if not isinstance(fk, str):
            raise PackageDrift(f"case {cid!r}: frame has non-string key {fk!r}")
        fl = fk.lower()
        if fl not in ("width", "height", "background"):
            raise PackageDrift(f"case {cid!r}: unknown frame field {fk!r}")
        if fl in seen:
            raise PackageDrift(f"case {cid!r}: duplicate case-folded frame field {fk!r}")
        seen.add(fl)
        if fl == "background":
            if fv is not None and not isinstance(fv, str):
                raise PackageDrift(f"case {cid!r}: frame.{fk} must be a string or null (got {fv!r})")
            continue
        if not isinstance(fv, int) or isinstance(fv, bool) or fv <= 0 or fv > 2147483647:
            raise PackageDrift(f"case {cid!r}: frame.{fk} must be a positive 32-bit integer (got {fv!r})")
        if fl == "width":
            fw = fv
        else:
            fh = fv
    return fw, fh


def check_missing_control_case(case_spec, pkg_data, template_c_path):
    """Route a case targeting a missing control to its own compiled template (ui/template_data.c or reference/templates/)."""
    ctrl = case_spec.get("control")
    missing_by_ctrl = {e.get("control"): e for e in pkg_data.get("missing_controls", []) if isinstance(e, dict)}
    if ctrl not in missing_by_ctrl:
        raise PackageDrift(f"case {case_spec.get('id')!r}: control {ctrl!r} is not listed in reference/package.json missing_controls")
    entry = missing_by_ctrl[ctrl]
    tpl = entry.get("compiled_template")
    if entry.get("basis") != "compiled-template" or not tpl:
        raise PackageDrift(f"case {case_spec.get('id')!r}: missing control {ctrl!r} has no compiled template")
    repo_root = Path(template_c_path).resolve().parent.parent
    compiled = parse_compiled_templates(template_c_path)
    info = _resolve_missing_control_template(entry, compiled, repo_root)
    if info["target"] != ctrl:
        raise PackageDrift(f"case {case_spec.get('id')!r}: compiled template {tpl!r} does not target {ctrl!r}")
    req_state = case_spec.get("state")
    if req_state and req_state not in info.get("states", set()):
        raise PackageDrift(
            f"case {case_spec.get('id')!r}: visual state {req_state!r} does not exist in compiled template {tpl!r}"
        )
    cid = case_spec.get("id")
    allowed_case_keys = {
        "id", "case_group", "control", "style", "theme", "scale", "flow_direction",
        "state", "use_transitions", "frame", "properties", "dump_tree",
        "expected_elements", "expected_states", "expected_setters",
    }
    for ck, cv in case_spec.items():
        if not isinstance(ck, str) or ck.lower() not in allowed_case_keys:
            if cv is not None:
                raise PackageDrift(f"case {cid!r}: compiled-template fallback does not support field {ck!r}")
    for field, actual in (("expected_elements", info["elem_count"]), ("expected_states", info["state_count"]), ("expected_setters", info["setter_count"])):
        if field in case_spec and case_spec[field] != actual:
            raise PackageDrift(f"case {cid!r}: {field}={case_spec[field]} != compiled {tpl!r} ({actual})")
    fw, fh = _validate_frame_dict(case_spec.get("frame"), cid)
    props = case_spec.get("properties") or {}
    if not isinstance(props, dict):
        raise PackageDrift(f"case {cid!r}: properties must be a JSON object")
    is_enabled = True
    def_bt = int(info.get("border_inset", 0))
    bt = (def_bt, def_bt, def_bt, def_bt)
    pad = (0, 0, 0, 0)
    for pk, pv in props.items():
        if not isinstance(pk, str):
            raise PackageDrift(f"case {cid!r}: non-string property key {pk!r}")
        pl = pk.lower()
        if pl == "isenabled":
            if not isinstance(pv, bool):
                raise PackageDrift(f"case {cid!r}: property {pk!r} must be a boolean")
            is_enabled = pv
        elif pl == "borderthickness":
            bt = _parse_thickness_inset(pv)
        elif pl == "padding":
            pad = _parse_thickness_inset(pv)
        else:
            raise PackageDrift(f"case {cid!r}: compiled-template fallback does not support property {pk!r}")
    root_rect = {"X": 0, "Y": 0, "Width": fw, "Height": fh}
    raw_elems = info.get("elements", [])
    children_by_parent = {}
    for idx, e in enumerate(raw_elems):
        children_by_parent.setdefault(e.get("parent", -1), []).append(idx)

    elem_ctrl_types = {
        "IM_ELEM_SCROLL_VIEWER": ("Pane", ["Scroll"]),
        "IM_ELEM_ITEMS_PRESENTER": ("List", []),
        "IM_ELEM_CONTENT_PRESENTER": ("Group", []),
    }

    def _build_nodes(idx, cur_rect):
        e = raw_elems[idx]
        child_rect = cur_rect
        if e["type"] == "IM_ELEM_BORDER":
            il = bt[0] + pad[0]
            it = bt[1] + pad[1]
            ir = bt[2] + pad[2]
            ib = bt[3] + pad[3]
            if il or it or ir or ib:
                nx = cur_rect["X"] + il
                ny = cur_rect["Y"] + it
                nw = max(0, cur_rect["Width"] - il - ir)
                nh = max(0, cur_rect["Height"] - it - ib)
                child_rect = {
                    "X": int(nx) if float(nx).is_integer() else nx,
                    "Y": int(ny) if float(ny).is_integer() else ny,
                    "Width": int(nw) if float(nw).is_integer() else nw,
                    "Height": int(nh) if float(nh).is_integer() else nh,
                }
        child_layouts = []
        child_autos = []
        for c_idx in children_by_parent.get(idx, []):
            cl, ca = _build_nodes(c_idx, child_rect)
            child_layouts.append(cl)
            child_autos.extend(ca)
        layout_node = {
            "Type": e["type"],
            "Name": e["name"],
            "Rect": dict(cur_rect),
            "Children": child_layouts,
        }
        if e["type"] in elem_ctrl_types:
            ctype, pats = elem_ctrl_types[e["type"]]
            auto_nodes = [{
                "ControlType": ctype,
                "Name": e["name"] or ctype,
                "Rect": dict(cur_rect),
                "IsEnabled": is_enabled,
                "HasKeyboardFocus": False,
                "Patterns": pats,
                "Children": child_autos,
            }]
        else:
            auto_nodes = child_autos
        return layout_node, auto_nodes

    elem_children = []
    auto_children = []
    for r_idx in children_by_parent.get(-1, []):
        rl, ra = _build_nodes(r_idx, root_rect)
        elem_children.append(rl)
        auto_children.extend(ra)
    auto_patterns = ["Scroll"] if any(e["type"] == "IM_ELEM_SCROLL_VIEWER" for e in raw_elems) else []
    return {
        "id": case_spec.get("id"),
        "control": ctrl,
        "basis": "compiled-template",
        "template": tpl,
        "verified_elements": info["elem_count"],
        "verified_states": info["state_count"],
        "verified_setters": info["setter_count"],
        "layout": {
            "Type": ctrl,
            "Name": tpl,
            "Basis": "compiled-template",
            "Rect": root_rect,
            "ElementCount": info["elem_count"],
            "States": sorted(info.get("states", set())),
            "Children": elem_children,
        },
        "automation": {
            "ControlType": ctrl,
            "Name": tpl,
            "Basis": "compiled-template",
            "Rect": root_rect,
            "IsEnabled": is_enabled,
            "HasKeyboardFocus": False,
            "Patterns": auto_patterns,
            "Children": auto_children,
        },
    }


def verify_manifest_links(repo_root):
    """Offline check: verify reference/package.json matches Reference.csproj, packages.lock.json, and tools/winui/pin.json."""
    repo = Path(repo_root)
    pkg = json.loads((repo / "reference/package.json").read_text(encoding="utf-8"))
    pin = json.loads((repo / "tools/winui/pin.json").read_text(encoding="utf-8"))
    lock = json.loads((repo / "reference/packages.lock.json").read_text(encoding="utf-8"))
    csproj_text = (repo / "reference/Reference.csproj").read_text(encoding="utf-8")

    m = re.search(r'<PackageReference\s+Include="Microsoft\.WindowsAppSDK"\s+Version="([^"]+)"', csproj_text)
    if not m:
        raise PackageDrift("reference/Reference.csproj: Microsoft.WindowsAppSDK PackageReference not found")
    csproj_ver = m.group(1)
    if pkg.get("package") != "Microsoft.WindowsAppSDK" or pkg.get("version") != csproj_ver:
        raise PackageDrift(f"package.json version {pkg.get('version')!r} != Reference.csproj {csproj_ver!r}")

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
    return pkg, pin, csproj_ver, comp, commit, recorded_diffs


def verify_package(repo_root, winui_checkout=None, nuget_pkg=None, require_live=True):
    repo = Path(repo_root)
    tpl_c = repo / "ui/template_data.c"
    pkg, pin, csproj_ver, comp, commit, recorded_diffs = verify_manifest_links(repo)

    if nuget_pkg is not None:
        pkg_dir = Path(nuget_pkg)
        if not pkg_dir.is_dir():
            raise PackageDrift(f"--nuget-pkg directory not found: {pkg_dir}")
    else:
        pkg_dir = find_nuget_winui_pkg(comp["version"])

    checked_live = False
    pkg_target_types = None
    pin_target_types = None
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
            pin_target_types = extract_target_types(winui_dir / pin["base"])
            for p in sorted(winui_dir.glob(pin["controls"])):
                if not any(s in p.name for s in pin.get("skip", [])):
                    pin_target_types |= extract_target_types(p)
            checked_live = True

    if require_live and not checked_live:
        raise PackageDrift("live WinUI package and checkout verification required but unavailable on this host")

    missing_report = verify_missing_controls(
        pkg, tpl_c, pkg_target_types=pkg_target_types, pin_target_types=pin_target_types,
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
