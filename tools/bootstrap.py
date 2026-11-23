#!/usr/bin/env python3
"""Install the pinned toolchain once per machine, hash-checked. Stdlib only.

It lands in IMPOSSIBLE_TOOLS when set, otherwise %LOCALAPPDATA%\\ImpossibleSDK on
Windows and ~/.cache/impossible-sdk on Linux, so every program shares one copy.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def host():
    return "windows" if os.name == "nt" else "linux"


def tools_root():
    if os.environ.get("IMPOSSIBLE_TOOLS"):
        return Path(os.environ["IMPOSSIBLE_TOOLS"])
    if host() == "windows":
        return Path(os.environ["LOCALAPPDATA"]) / "ImpossibleSDK"
    return Path.home() / ".cache" / "impossible-sdk"


def components():
    return json.loads((ROOT / "toolchain.json").read_text(encoding="utf-8"))[host()]


def home(part):
    return tools_root() / f"{part['name']}-{part['version']}-{part['sha256'][:8]}"


def sha256(path):
    with open(path, "rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()


def installed(part):
    """Whether the pinned programs are there: another one in a place the build runs is an error, never used."""
    if not (home(part) / part["probe"]).is_file():
        return False
    for name, pinned in part["runs"].items():
        path = home(part) / name
        if not path.is_file() or sha256(path) != pinned:
            raise ValueError(f"{path} is not the pinned {part['name']} {part['version']}: delete {home(part)} and run this again")
    return True


def fetch(part, work, archives):
    name = part["url"].rsplit("/", 1)[1]
    path = archives / name if archives and (archives / name).is_file() else work / name
    if not path.is_file():
        print(f"bootstrap: downloading {name}", flush=True)
        with urllib.request.urlopen(part["url"], timeout=120) as reply, open(path, "wb") as out:
            shutil.copyfileobj(reply, out)
    found = sha256(path)
    if found != part["sha256"]:
        raise ValueError(f"{name}: sha256 is {found}, the pin is {part['sha256']}")
    return path


def install(part, archives):
    target = home(part)
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent) as temp:
        work = Path(temp)
        archive = fetch(part, work, archives)
        out = work / "out"
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as bundle:
                bundle.extractall(out)
                for info in bundle.infolist():
                    mode = info.external_attr >> 16
                    if mode and host() == "linux" and not info.is_dir():
                        (out / info.filename).chmod(mode & 0o777)
        else:
            with tarfile.open(archive) as bundle:
                bundle.extractall(out, filter="data")
        entries = list(out.iterdir())
        top = entries[0] if len(entries) == 1 and entries[0].is_dir() else out
        if not (top / part["probe"]).is_file():
            raise ValueError(f"{part['name']}: the archive has no {part['probe']}")
        try:
            top.rename(target)  # one step, so a toolchain is never half there
        except OSError:
            if target.exists() and not installed(part):
                raise ValueError(f"{target} is damaged: delete it and run this again") from None
            if not installed(part):  # otherwise another run installed it first
                raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archives", type=Path, help="folder of already downloaded archives (still hash-checked)")
    args = parser.parse_args()
    if platform.machine().lower() not in ("x86_64", "amd64"):
        raise ValueError(f"the pinned tools need a 64-bit x86 host, this is {platform.machine()}")
    for part in components():
        if not installed(part):
            install(part, args.archives)
        print(f"bootstrap: {part['name']} {part['version']} ready")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"bootstrap: {error}", file=sys.stderr)
        raise SystemExit(1)
