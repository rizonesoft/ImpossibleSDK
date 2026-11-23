#!/usr/bin/env python3
"""The launcher profile, `.conclave/launchers.json`: what a project chooses about its desktop links.

    python scripts/lib/launchers.py show [--root <project>]
    python scripts/lib/launchers.py title [--root <project>]

`show` prints the validated profile as one line of JSON, or the refusal on stderr with exit 1. `title` prints the
title the campaign links carry as one line of JSON (`campaign_title`): the title, or `null` when the project has no
profile or has not set its title yet, and the refusal on stderr with exit 1 for any other profile `show` refuses. The root is the project
this script is installed in (two folders above `scripts/lib/`) unless `--root` names another. Standard library only.

The profile holds nothing about one machine (the distro, the desktop folder, the user's name): those are the
shortcut script's parameters. `load_profile` refuses a malformed profile by naming the key, and never fills in a
default the project did not write.
"""

import argparse
import json
import os
import re
import stat
import sys
from pathlib import Path, PurePosixPath, PureWindowsPath

PROFILE = ".conclave/launchers.json"
SCHEMA_VERSION = 1
KEYS = ("schema_version", "title", "icon", "agents", "skip_permissions", "links")
AGENTS = ("claude", "codex", "agy")
AGENT_LABELS = {"claude": "Claude", "codex": "Codex", "agy": "Antigravity"}  # the link each agent gets
MARKS = ("wordpress", "site")
TITLE_LIMIT = 60
LABEL_LIMIT = 30
FORBIDDEN = set('\\/:*?"<>|')  # what a Windows file name cannot hold, since a title and a label name a .lnk file
ICON_SUFFIXES = (".png", ".ico")
_HOST = r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*"
_REST = r"(?::[0-9]{1,5})?(?:[/?#][^\s]*)?"
HTTPS = re.compile(rf"https://{_HOST}{_REST}")
HTTP_LOCAL = re.compile(rf"http://(?:localhost|{_HOST}\.(?:test|site)){_REST}", re.IGNORECASE)


class ProfileError(ValueError):
    """A profile the links cannot be written from, named with the file and the key."""


def slug(label: str) -> str:
    """The name a link's icon file carries: the label in lower case, each run of other characters one hyphen."""
    return re.sub(r"[^a-z0-9]+", "-", label.lower()).strip("-")


def _name_fault(text: str, limit: int) -> str | None:
    """Why `text` cannot name a Windows link file, or None when it can."""
    if not text:
        return "must not be empty"
    if len(text) > limit:
        return f"must be at most {limit} characters, not {len(text)}"
    if not text.strip():
        return "must not be only spaces"
    if text != text.strip():
        return "must not start or end with a space"
    if text.endswith("."):
        return "must not end with a dot"
    bad = sorted({ch for ch in text if ch in FORBIDDEN or ord(ch) < 32 or ord(ch) == 127})
    if bad:
        return f"must not hold {' '.join(repr(ch) for ch in bad)}"
    return None


def _reached_through_link(root: Path, relative: PurePosixPath) -> bool:
    """Whether any folder or the file on the way from `root` to `relative` is a symbolic link or a junction."""
    here = root
    for part in relative.parts:
        here = here / part
        try:
            if here.is_symlink() or (hasattr(os.path, "isjunction") and os.path.isjunction(here)):
                return True
        except OSError:
            return True
    return False


def load_profile(path, root=None, unset=False) -> dict:
    """The profile at `path`, every key checked. `root` is the project (default: the folder holding `.conclave/`).
    With `unset`, a null `title` or `icon` (the kit's template as installed) passes and every other key is still
    checked. Raises ProfileError naming the file and the key."""
    path = Path(path)
    root = Path(root) if root is not None else path.parent.parent
    where = path.as_posix()

    def fault(key: str, what: str) -> ProfileError:
        return ProfileError(f"{where}: {key}: {what}")

    try:
        raw = path.read_bytes()
    except FileNotFoundError:
        raise ProfileError(f"{where}: no such file") from None
    except IsADirectoryError:
        raise ProfileError(f"{where}: is a folder, not a file") from None
    except OSError as exc:
        if path.is_dir():
            raise ProfileError(f"{where}: is a folder, not a file") from None
        raise ProfileError(f"{where}: cannot be read ({exc.strerror or exc})") from None
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ProfileError(f"{where}: not UTF-8 ({exc.reason} at byte {exc.start})") from None
    try:
        data = json.loads(text)
    except (ValueError, RecursionError) as exc:
        raise ProfileError(f"{where}: not JSON ({exc})") from None
    if not isinstance(data, dict):
        raise ProfileError(f"{where}: a profile is an object with the keys {', '.join(KEYS)}")
    unknown = sorted(set(data) - set(KEYS))
    if unknown:
        raise fault(unknown[0], f"is not a key of the profile; the keys are {', '.join(KEYS)}")
    missing = [key for key in KEYS if key not in data]
    if missing:
        raise fault(missing[0], "is missing")

    version = data["schema_version"]
    if type(version) is not int or version != SCHEMA_VERSION:
        raise fault("schema_version", f"must be the integer {SCHEMA_VERSION}, not {version!r}")

    title = data["title"]
    if title is None and not unset:
        raise fault("title", "must be set: the name the project's links carry on the desktop")
    if title is not None:
        if not isinstance(title, str):
            raise fault("title", f"must be a string, not {type(title).__name__}")
        why = _name_fault(title, TITLE_LIMIT)
        if why:
            raise fault("title", why)

    icon = data["icon"]
    if icon is None and not unset:
        raise fault("icon", "must be set: a .png or .ico file in the project, relative to it")
    if icon is not None:
        if not isinstance(icon, str):
            raise fault("icon", f"must be a string, not {type(icon).__name__}")
        if not icon.strip():
            raise fault("icon", "must not be empty")
        if any(ord(ch) < 32 or ord(ch) == 127 for ch in icon):
            raise fault("icon", f"must not hold a control character, not {icon!r}")
        # Either separator: `\\dir\\icon.png` is rooted on Windows, and on any system it must not leave the project.
        normalized = icon.replace("\\", "/")
        if PurePosixPath(normalized).is_absolute() or PureWindowsPath(icon).anchor:
            raise fault("icon", f"must be relative to the project, not {icon!r}")
        relative = PurePosixPath(normalized)
        if ".." in relative.parts or not relative.parts:
            raise fault("icon", f"must stay inside the project, not {icon!r}")
        if relative.suffix.lower() not in ICON_SUFFIXES:
            raise fault("icon", f"must be a .png or .ico file, not {icon!r}")
        if _reached_through_link(root, relative):
            raise fault("icon", f"{icon} is reached through a link; name the file itself")
        target = root / relative
        try:
            inside = os.path.commonpath([os.path.abspath(root), os.path.abspath(target)]) == os.path.abspath(root)
        except ValueError:  # another drive
            inside = False
        if not inside:
            raise fault("icon", f"must stay inside the project, not {icon!r}")
        try:
            mode = target.stat().st_mode
        except FileNotFoundError:
            raise fault("icon", f"{icon} does not exist in the project") from None
        except (OSError, ValueError) as exc:
            # A ValueError (a path the system cannot name) has no strerror.
            raise fault("icon", f"{icon} cannot be read ({getattr(exc, 'strerror', None) or exc})") from None
        if not stat.S_ISREG(mode):
            raise fault("icon", f"{icon} is not a file")

    agents = data["agents"]
    if not isinstance(agents, list):
        raise fault("agents", f"must be a list, not {type(agents).__name__}")
    if not agents:
        raise fault("agents", f"must name at least one of {', '.join(AGENTS)}")
    for agent in agents:
        if not isinstance(agent, str) or agent not in AGENTS:
            raise fault("agents", f"{agent!r} is not an agent; the agents are {', '.join(AGENTS)}")
    repeated = next((agent for agent in agents if agents.count(agent) > 1), None)
    if repeated:
        raise fault("agents", f"{repeated!r} is named twice")

    skip = data["skip_permissions"]
    if type(skip) is not bool:
        raise fault("skip_permissions", f"must be true or false, not {skip!r}")

    links = data["links"]
    if not isinstance(links, list):
        raise fault("links", f"must be a list, not {type(links).__name__}")
    labels, slugs = {}, {}
    for number, link in enumerate(links, 1):
        here = f"links[{number}]"
        if not isinstance(link, dict):
            raise fault(here, "must be an object with label, mark and one of run or url")
        keys = set(link)
        if keys not in ({"label", "mark", "run"}, {"label", "mark", "run", "wsl"}, {"label", "mark", "url"}):
            extra = sorted(keys - {"label", "mark", "run", "url", "wsl"})
            if extra:
                raise fault(here, f"{extra[0]} is not a key of a link; a link has label, mark and one of run or url")
            if {"run", "url"} <= keys:
                raise fault(here, "has both run and url; a link has one")
            if "wsl" in keys and "url" in keys:
                raise fault(here, "wsl is only for a run link, not a url link")
            lacking = next(key for key in ("label", "mark") if key not in keys) if not {"label", "mark"} <= keys \
                else "run or url"
            raise fault(here, f"is missing {lacking}")
        label = link["label"]
        if not isinstance(label, str):
            raise fault(f"{here}.label", f"must be a string, not {type(label).__name__}")
        why = _name_fault(label, LABEL_LIMIT)
        if why:
            raise fault(f"{here}.label", why)
        if label.lower() in {name.lower() for name in AGENT_LABELS.values()}:
            raise fault(f"{here}.label", f"{label!r} is an agent's link; name the link something else")
        if label.split()[-1].lower().endswith("campaign"):
            raise fault(f"{here}.label", f"{label!r} ends in campaign, which names an unattended run's link")
        if label.lower() in labels:
            raise fault(f"{here}.label", f"{label!r} is used by {labels[label.lower()]} too")
        name = slug(label)
        if not name:
            raise fault(f"{here}.label", f"{label!r} has no letter or digit to name its icon by")
        if name in AGENTS:
            raise fault(f"{here}.label", f"{label!r} names its icon {name!r}, which is an agent's icon")
        if name in slugs:
            raise fault(f"{here}.label", f"{label!r} names its icon {name!r}, as {slugs[name]} does")
        labels[label.lower()] = here
        slugs[name] = here
        mark = link["mark"]
        if not isinstance(mark, str) or mark not in MARKS:
            raise fault(f"{here}.mark", f"{mark!r} is not a mark; the marks are {', '.join(MARKS)}")
        if "run" in link:
            run = link["run"]
            if not isinstance(run, str) or not run.strip():
                raise fault(f"{here}.run", "must be a command on one line")
            if "\n" in run or "\r" in run or "\0" in run:
                raise fault(f"{here}.run", "must be one line")
            if "wsl" in link and type(link["wsl"]) is not bool:
                raise fault(f"{here}.wsl", f"must be true or false, not {link['wsl']!r}")
        else:
            url = link["url"]
            if not isinstance(url, str) or not (HTTPS.fullmatch(url) or HTTP_LOCAL.fullmatch(url)):
                raise fault(f"{here}.url", f"must be an https:// address, or http:// on localhost or a .test or "
                                           f".site name, with no whitespace, not {url!r}")

    return {"schema_version": version, "title": title, "icon": icon, "agents": list(agents),
            "skip_permissions": skip, "links": [dict(link) for link in links]}


def campaign_title(root) -> str | None:
    """The title the unattended kit's campaign links carry (D07 T01 section 7), or None when the project has no
    profile or its `title` is still null, as the kit's template installs it: the unattended kit brings this kit, so
    every install of it holds that template until the owner fills it in, and its campaign links keep the folder's
    name until then. Every other key is still checked (`load_profile` with `unset`), so a truncated or unsupported
    profile is refused with ProfileError, as `show` refuses it."""
    root = Path(root)
    path = root / PROFILE
    if not os.path.lexists(path):
        return None
    if load_profile(path, root, unset=True)["title"] is None:
        return None
    return load_profile(path, root)["title"]  # a profile with its title set is held to every rule, its icon's too


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="launchers.py", description="Read the launcher profile.")
    sub = parser.add_subparsers(dest="command", required=True)
    show = sub.add_parser("show", help="print the validated profile as one line of JSON")
    show.add_argument("--root", help="the project (default: the one this script is installed in)")
    title = sub.add_parser("title", help="print the campaign links' title as JSON, null when none is set")
    title.add_argument("--root", help="the project (default: the one this script is installed in)")
    args = parser.parse_args(argv)
    root = Path(args.root) if args.root else Path(__file__).resolve().parents[2]
    try:
        result = campaign_title(root) if args.command == "title" else load_profile(root / PROFILE, root)
    except ProfileError as exc:
        print(f"launchers: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, separators=(",", ":")))  # ASCII, so any console code page reads it
    return 0


if __name__ == "__main__":
    sys.exit(main())
