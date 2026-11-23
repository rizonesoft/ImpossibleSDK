"""The public headers, as the pinned compiler reads them and as their comments explain them."""
import json
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path


class Undocumented(Exception):
    pass


@dataclass
class Entry:
    names: list   # what is declared here
    source: str   # the declarations as they are in the header
    text: list    # (name or None, comment): the one above, then those behind a declaration
    line: int


@dataclass
class Header:
    path: str
    title: str    # its first comment
    entries: list = field(default_factory=list)


def declared(clang, root, path, includes):
    """{line: [(name, whether it is a function)]} of what a header declares itself."""
    command = [str(clang), "--target=x86_64-w64-windows-gnu", "-std=gnu23", "-x", "c", "-fsyntax-only", "-Xclang", "-ast-dump=json",
               *(f"-I{Path(root, include)}" for include in includes), str(Path(root, path))]
    tree = json.loads(subprocess.run(command, capture_output=True, text=True, encoding="utf-8", check=True, timeout=120).stdout)
    file, line, found = None, 0, {}

    def follow(value):
        """The dump names a file and line only where they change, in order."""
        nonlocal file, line
        if isinstance(value, list):
            for item in value:
                follow(item)
        elif isinstance(value, dict):
            file, line = value.get("file", file), value.get("line", line)
            for key, item in value.items():
                if key != "includedFrom":
                    follow(item)

    for node in tree["inner"]:
        location = node.get("loc", {})
        location = location.get("expansionLoc", location)
        file, line = location.get("file", file), location.get("line", line)
        own = file is not None and Path(file).resolve() == Path(root, path).resolve()
        at, kind, name = line, node["kind"], node.get("name")
        if own and name and (kind in ("FunctionDecl", "TypedefDecl", "VarDecl") or kind in ("RecordDecl", "EnumDecl")):
            found.setdefault(at, []).append((name, kind == "FunctionDecl"))
        follow({key: value for key, value in node.items() if key != "loc"})
    return found


def sentence(comment):
    lines = [re.sub(r"^\s*\*?\s?", "", line.strip()) for line in comment.strip()[2:-2].strip().splitlines()]
    return " ".join(line for line in lines if line)


def read(clang, root, path, includes):
    """A header's entries in order: declarations that stand together share the comment above them."""
    text = Path(root, path).read_text(encoding="utf-8")
    names = declared(clang, root, path, includes)
    lines, at, depth = text.splitlines(), 0, 0
    first = re.match(r"/\*.*?\*/", text, re.S)
    if not first:
        raise Undocumented(f"{path}: no comment at the top")
    header = Header(path, sentence(first[0]))
    at = text[: first.end()].count("\n") + 1
    comment, start, group = "", None, []

    def close():
        nonlocal comment, start, group
        if start is not None:
            own, texts, used = [], [(None, sentence(comment))] if comment else [], False
            for number, line in enumerate(group, start + 1):
                behind = re.search(r"/\*(.*?)\*/\s*$", line)
                macro = re.match(r"#define (\w+)", line)
                for name, function in [*names.get(number, []), *([(macro[1], False)] if macro else [])]:
                    if name in own:
                        continue
                    if not comment:
                        raise Undocumented(f"{path}:{start + 1}: {name} has no comment above it")
                    if function and used:  # the comment above is the first one's
                        if not behind:
                            raise Undocumented(f"{path}:{number}: {name} has no comment of its own")
                        texts.append((name, behind[1].strip()))
                    used, own = used or function, [*own, name]
            if own:
                header.entries.append(Entry(own, "\n".join(group).rstrip(), texts, start + 1))
        comment, start, group = "", None, []

    while at < len(lines):
        line = lines[at]
        if not line.strip() and depth == 0:
            close()
        elif start is None and line.lstrip().startswith("/*") and depth == 0:
            block = [line]
            while "*/" not in lines[at]:
                at += 1
                block.append(lines[at])
            comment = "\n".join(block)
        elif start is None and line.startswith("#") and not line.startswith("#define"):
            pass
        else:
            if start is None:
                start = at
            group.append(line)
            bare = re.sub(r"/\*.*?\*/", "", line)
            depth += bare.count("{") - bare.count("}")
        at += 1
    close()
    missed = sorted(set(name for found in names.values() for name, _ in found) - {name for entry in header.entries for name in entry.names})
    if missed:
        raise Undocumented(f"{path}: not in the reference: {', '.join(missed)}")
    return header
