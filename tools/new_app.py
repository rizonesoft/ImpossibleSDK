#!/usr/bin/env python3
"""Start a program's repository: python tools/new_app.py <folder> --label <name> [--window] --tier <n>

The folder's name is the program's name. It becomes a git repository with one
commit, pinned to this SDK's commit. Nothing is sent anywhere.
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

SDK = Path(__file__).resolve().parents[1]

CMAKE = """cmake_minimum_required(VERSION 4.0)
project({name} LANGUAGES C)
include("${{IMPOSSIBLE_SDK_DIR}}/cmake/ImpossibleSDK.cmake")
include_directories(engine)
set(ENGINE engine/engine.c)

impossible_add_program({name} {kind} TIER {tier} SOURCES src/main.c ${{ENGINE}})
impossible_add_program(engine-tests CONSOLE TIER 0 SOURCES tests/engine_tests.c ${{ENGINE}})
"""
TESTS = '''"""The engine's own tests run as a Windows console program."""
import os
import platform
import subprocess
import unittest
from pathlib import Path

BUILD = Path(os.environ["IMPOSSIBLE_BUILD"])


@unittest.skipUnless(os.name == "nt" or "microsoft" in platform.release().lower(), "needs Windows, directly or through WSL")
class Engine(unittest.TestCase):
    def test_engine(self):
        result = subprocess.run([str(BUILD / "engine-tests.exe")], capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout.decode("utf-8", "replace"))


if __name__ == "__main__":
    unittest.main()
'''
README = """# {label}

A program for Windows and, later, Impossible OS, built on Impossible SDK. Status: started, no release yet.

## Build

Clone Impossible SDK beside this folder at the commit in `sdk.json`, then run this in Windows or WSL:

```
python ../ImpossibleSDK/tools/app_build.py .
```

`engine/` has no window and calls no Windows function. `src/` is the program around it.

## License

GPL-3.0-only. See [LICENSE](LICENSE).
"""


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--tier", type=int, required=True)
    parser.add_argument("--window", action="store_true")
    parser.add_argument("--license", type=Path, required=True, help="a GPL-3.0 LICENSE file to copy")
    args = parser.parse_args()
    root = args.folder.resolve()
    name = root.name
    if not re.fullmatch(r"[A-Z][A-Za-z0-9]{1,39}", name):
        raise SystemExit(f"error: {name} is not a program name (letters and digits, capital first)")
    if root.exists():
        raise SystemExit(f"error: {root} already exists")
    commit = subprocess.run(["git", "-C", str(SDK), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()

    write(root / "CMakeLists.txt", CMAKE.format(name=name, kind="WINDOW" if args.window else "CONSOLE", tier=args.tier))
    write(root / "sdk.json", json.dumps({"sdk": "ImpossibleSDK", "commit": commit}, indent=2) + "\n")
    write(root / "app.json", json.dumps({"name": name, "label": args.label, "version": "0.0.0", "tier": args.tier,
                                         "opens": [], "needs": []}, indent=2) + "\n")
    write(root / "engine/engine.h", "#pragma once\n#include <stdint.h>\n\n/* Replace this with the program's engine. */\nint32_t engine_version(void);\n")
    write(root / "engine/engine.c", '#include "engine.h"\n\nint32_t engine_version(void) {\n    return 0;\n}\n')
    write(root / "src/main.c", '#include <impossible/base.h>\n#include "engine.h"\n\nint im_main(void) {\n    return engine_version();\n}\n')
    write(root / "tests/engine_tests.c", '/* Exits 0, or with the number of the first check that failed. */\n#include <impossible/base.h>\n#include "engine.h"\n\nint im_main(void) {\n    return engine_version() == 0 ? 0 : 1;\n}\n')
    write(root / "tests/test_engine.py", TESTS)
    write(root / "README.md", README.format(label=args.label))
    write(root / ".gitignore", "build/\n__pycache__/\n")
    write(root / ".gitattributes", (SDK / ".gitattributes").read_text(encoding="utf-8"))
    shutil.copyfile(args.license, root / "LICENSE")
    for command in (["init", "-q", "-b", "main"], ["add", "-A"], ["commit", "-qm", f"{args.label}: an empty program on Impossible SDK"]):
        subprocess.run(["git", "-C", str(root), *command], check=True)
    print(f"{root} is ready")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
