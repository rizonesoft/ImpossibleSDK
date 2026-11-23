"""Impossible UI draws what it should, and stays something the OS can take."""
import json
import os
import platform
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("IMPOSSIBLE_BUILD", ROOT / "build" / ("windows" if os.name == "nt" else "linux")))
sys.path.insert(0, str(ROOT / "tools"))
import bootstrap

LLVM = next(bootstrap.home(p) for p in bootstrap.components() if p["name"] == "llvm-mingw") / "bin"
EXE = ".exe" if os.name == "nt" else ""
WINUI = Path(os.environ.get("IMPOSSIBLE_WINUI", Path.home() / ".cache/impossible-sdk/winui"))


class Portable(unittest.TestCase):
    def test_it_builds_as_the_os_builds_and_calls_nothing(self):
        allowed = {"memcpy", "memmove", "memset", "memcmp"}
        with tempfile.TemporaryDirectory() as temp:
            objects = []
            for source in sorted((ROOT / "ui").glob("*.c")):
                objects.append(Path(temp, source.stem + ".o"))
                subprocess.run([str(LLVM / f"clang{EXE}"), "--target=x86_64-elf", "-std=gnu23", "-ffreestanding", "-O2",
                                "-Wall", "-Wextra", "-Werror", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2", "-mno-80387",
                                "-c", str(source), "-o", str(objects[-1])], check=True, timeout=120)
            defined, wanted = set(), set()
            for line in subprocess.run([str(LLVM / f"llvm-nm{EXE}"), *map(str, objects)], capture_output=True, text=True,
                                       check=True, timeout=60).stdout.splitlines():
                words = line.split()
                if len(words) == 2 and words[0] == "U":
                    wanted.add(words[1])
                elif len(words) == 3:
                    defined.add(words[2])
            self.assertEqual(wanted - defined - allowed, set())


@unittest.skipUnless((WINUI / "LICENSE").is_file(), f"no WinUI checkout at {WINUI}")
class Generated(unittest.TestCase):
    def test_the_theme_data_is_what_the_pinned_winui_gives(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/winui/resources.py"), str(WINUI), "--check"],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)


@unittest.skipUnless(os.name == "nt" or "microsoft" in platform.release().lower(), "needs Windows, directly or through WSL")
class Drawing(unittest.TestCase):
    def test_drawing(self):
        self.assertEqual(subprocess.run([str(BUILD / "ui-tests.exe")], timeout=60).returncode, 0)

    def test_buttons_match_real_winui(self):
        """Straight edges and fills may differ by rounding only; corners have their own limit."""
        folder = ROOT / "tests/reference"
        cases = json.loads((folder / "cases.json").read_text(encoding="utf-8"))["cases"]
        ours = subprocess.run([str(BUILD / "control-shots.exe")], capture_output=True, timeout=60, check=True).stdout
        size = 160 * 64 * 4
        self.assertEqual(len(ours), size * len(cases))
        for index, case in enumerate(cases):
            mine, real = ours[index * size:(index + 1) * size], zlib.decompress((folder / f"{case['file']}.z").read_bytes())
            edge = corner = 0
            for p in range(160 * 64):
                x, y = p % 160, p // 160
                off = max(abs(mine[p * 4 + k] - real[p * 4 + k]) for k in range(3))
                if (x < 25 or x >= 135) and (y < 21 or y >= 43):
                    corner = max(corner, off)
                else:
                    edge = max(edge, off)
            name = f"{case['control']} {case['state']} {case['theme']}"
            self.assertLessEqual(edge, 1, name)
            self.assertLessEqual(corner, 27 if case["control"] == "accent" else 3, name)


if __name__ == "__main__":
    unittest.main()
