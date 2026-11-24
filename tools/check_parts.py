#!/usr/bin/env python3
"""Validate parts.json and enforce declared dependency edges across part headers and sources.

    python3 tools/check_parts.py [parts.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Freestanding C headers legal in portable OS/SDK parts.
FREESTANDING_HEADERS = {
    "float.h",
    "iso646.h",
    "limits.h",
    "stdalign.h",
    "stdarg.h",
    "stdatomic.h",
    "stdbool.h",
    "stddef.h",
    "stdint.h",
    "stdnoreturn.h",
}


class PartsError(Exception):
    """Raised when parts.json or a part's includes violate the dependency contract."""


def strip_comments_preserve_lines(text):
    """Replace C // and /* ... */ comments with spaces while keeping exact line numbers."""
    out = []
    i = 0
    n = len(text)
    in_str = None
    while i < n:
        ch = text[i]
        if in_str:
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if ch == in_str:
                in_str = None
            i += 1
        elif ch in ('"', "'"):
            in_str = ch
            out.append(ch)
            i += 1
        elif ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            out.append("  ")
            i += 2
            while i < n:
                if text[i] == "*" and i + 1 < n and text[i + 1] == "/":
                    out.append("  ")
                    i += 2
                    break
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def is_within(child, parent):
    try:
        child.relative_to(parent)
        return True
    except ValueError:
        return False


def load_manifest(manifest_path):
    manifest_path = Path(manifest_path).resolve()
    root = manifest_path.parent
    try:
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as err:
        raise PartsError(f"{manifest_path}: cannot read manifest: {err}") from err
    if not isinstance(data, dict) or not isinstance(data.get("parts"), list):
        raise PartsError(f"{manifest_path}: expected top-level object with 'parts' list")

    parts = {}
    folders = []
    for idx, entry in enumerate(data["parts"]):
        if not isinstance(entry, dict):
            raise PartsError(f"{manifest_path}: part #{idx} must be an object")
        name = entry.get("name")
        folder_rel = entry.get("folder")
        depends = entry.get("depends")
        sources = entry.get("sources")
        if not isinstance(name, str) or not name or not re.fullmatch(r"[a-z][a-z0-9_-]*", name):
            raise PartsError(f"{manifest_path}: invalid part name {name!r}")
        if name in parts:
            raise PartsError(f"{manifest_path}: duplicate part name {name!r}")
        if not isinstance(folder_rel, str) or not folder_rel:
            raise PartsError(f"{manifest_path}: part {name!r} missing folder")
        folder = (root / folder_rel).resolve()
        if not is_within(folder, root) or folder == root:
            raise PartsError(f"{manifest_path}: part {name!r} folder {folder_rel!r} escapes repo root")
        if not folder.is_dir():
            raise PartsError(f"{manifest_path}: part {name!r} folder {folder_rel!r} does not exist")
        if not isinstance(depends, list) or not all(isinstance(d, str) for d in depends):
            raise PartsError(f"{manifest_path}: part {name!r} 'depends' must be a list of strings")
        if not isinstance(sources, list) or not all(isinstance(s, str) for s in sources):
            raise PartsError(f"{manifest_path}: part {name!r} 'sources' must be a list of strings")

        for other_name, other_folder in folders:
            if is_within(folder, other_folder) or is_within(other_folder, folder):
                raise PartsError(f"{manifest_path}: parts {name!r} and {other_name!r} have overlapping folders")
        folders.append((name, folder))

        src_paths = []
        seen_src = set()
        for s in sources:
            sp = (root / s).resolve()
            if not is_within(sp, folder):
                raise PartsError(f"{manifest_path}: source {s!r} is outside part {name!r} folder {folder_rel!r}")
            if not sp.is_file():
                raise PartsError(f"{manifest_path}: source {s!r} in part {name!r} does not exist")
            if sp in seen_src:
                raise PartsError(f"{manifest_path}: duplicate source {s!r} in part {name!r}")
            seen_src.add(sp)
            src_paths.append(sp)

        actual_c = {p.resolve() for p in folder.rglob("*.c")}
        unlisted = sorted(p.relative_to(root).as_posix() for p in (actual_c - seen_src))
        if unlisted:
            raise PartsError(f"{manifest_path}: part {name!r} has unlisted .c source(s): {', '.join(unlisted)}")

        parts[name] = {
            "name": name,
            "folder_rel": folder_rel,
            "folder": folder,
            "depends": list(depends),
            "sources_rel": list(sources),
            "sources": src_paths,
        }

    # Check declared dependencies exist and are acyclic.
    for name, info in parts.items():
        for dep in info["depends"]:
            if dep == name:
                raise PartsError(f"{manifest_path}: part {name!r} cannot depend on itself")
            if dep not in parts:
                raise PartsError(f"{manifest_path}: part {name!r} depends on unknown part {dep!r}")

    visiting = set()
    visited = set()

    def dfs(node, trail):
        if node in visiting:
            cycle = " -> ".join(trail[trail.index(node):] + [node])
            raise PartsError(f"{manifest_path}: dependency cycle detected: {cycle}")
        if node in visited:
            return
        visiting.add(node)
        for dep in parts[node]["depends"]:
            dfs(dep, trail + [dep])
        visiting.remove(node)
        visited.add(node)

    for name in parts:
        dfs(name, [name])

    return root, parts


def owner_of(path, parts):
    for name, info in parts.items():
        if is_within(path, info["folder"]):
            return name
    return None


def resolve_include(inc_text, file_path, part_info, root, parts):
    """Resolve an include path against the file's dir, the part's folder, declared deps, and repo root."""
    candidates = [
        file_path.parent / inc_text,
        part_info["folder"] / inc_text,
        *(parts[d]["folder"] / inc_text for d in part_info["depends"]),
        root / inc_text,
    ]
    for cand in candidates:
        resolved = cand.resolve()
        if resolved.is_file():
            return resolved
    return None


def check_parts(manifest_path):
    root, parts = load_manifest(manifest_path)
    inc_re = re.compile(r"^\s*#\s*include(?:_next)?\s*(.*)$")

    for name, info in parts.items():
        allowed_deps = set(info["depends"])
        scan_files = sorted(
            p.resolve() for p in info["folder"].rglob("*")
            if p.is_file() and p.suffix in (".c", ".h")
        )
        for fp in scan_files:
            rel = fp.relative_to(root).as_posix()
            cleaned = strip_comments_preserve_lines(fp.read_text(encoding="utf-8"))
            for lineno, line in enumerate(cleaned.splitlines(), 1):
                m = inc_re.match(line)
                if not m:
                    continue
                operand = m.group(1).strip()
                if operand.startswith('"'):
                    end = operand.find('"', 1)
                    if end == -1:
                        raise PartsError(f"{rel}:{lineno}: malformed #include directive")
                    inc = operand[1:end]
                    target = resolve_include(inc, fp, info, root, parts)
                    if target is None:
                        raise PartsError(f"{rel}:{lineno}: unresolved #include {inc!r} in part {name!r}")
                    if not is_within(target, root):
                        raise PartsError(f"{rel}:{lineno}: #include {inc!r} escapes repository root")
                    target_owner = owner_of(target, parts)
                    if target_owner == name:
                        continue
                    if target_owner is not None:
                        if target_owner not in allowed_deps:
                            raise PartsError(
                                f"{rel}:{lineno}: part {name!r} includes {inc!r} from undeclared part {target_owner!r}"
                            )
                        continue
                    raise PartsError(
                        f"{rel}:{lineno}: part {name!r} includes {inc!r} outside any declared part"
                    )
                elif operand.startswith("<"):
                    end = operand.find(">", 1)
                    if end == -1:
                        raise PartsError(f"{rel}:{lineno}: malformed #include directive")
                    inc = operand[1:end]
                    if inc in FREESTANDING_HEADERS:
                        continue
                    target = resolve_include(inc, fp, info, root, parts)
                    if target is not None and is_within(target, root):
                        target_owner = owner_of(target, parts)
                        if target_owner == name or (target_owner is not None and target_owner in allowed_deps):
                            continue
                        if target_owner is not None:
                            raise PartsError(
                                f"{rel}:{lineno}: part {name!r} includes <{inc}> from undeclared part {target_owner!r}"
                            )
                    raise PartsError(
                        f"{rel}:{lineno}: part {name!r} includes non-freestanding header <{inc}>"
                    )
                else:
                    raise PartsError(
                        f"{rel}:{lineno}: computed #include {operand!r} is forbidden in part {name!r}"
                    )

    return parts


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", nargs="?", type=Path, default=ROOT / "parts.json", help="path to parts.json")
    args = parser.parse_args(argv)
    try:
        parts = check_parts(args.manifest)
    except PartsError as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    print(f"parts: {len(parts)} parts ok ({', '.join(sorted(parts))})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
