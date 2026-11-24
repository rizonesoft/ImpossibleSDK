"""One live pass inside the test machine (tools/winvm/live.py starts it): every default case captured and held against
tests/reference, then the live tests. Pixels are held only within a limit: the machine draws without a graphics card and
with Windows' default accent. Layout, automation and script logs are held exactly."""
import json
import os
import shutil
import subprocess
import sys
import unittest
import winreg
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIXEL_LIMIT = 40  # a channel's difference; the worst measured on 2026-11-24 was 35, a corner probe
EXACT = ("width", "height", "layout", "automation", "script_log", "scale_mode", "rendered_text")


def register_fonts(folder):
    """The live tests need one permitted font Windows knows of."""
    fonts = Path(os.environ["LOCALAPPDATA"]) / "Microsoft/Windows/Fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    for source in folder.iterdir():
        target = fonts / source.name
        shutil.copy2(source, target)
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts") as key:
            winreg.SetValueEx(key, f"{source.stem} (TrueType)", 0, winreg.REG_SZ, str(target))


def differences(fresh_dir, store):
    fresh = {c["id"]: c for c in json.loads((fresh_dir / "cases.json").read_text(encoding="utf-8"))["cases"]}
    stored = {c["id"]: c for c in json.loads((store / "cases.json").read_text(encoding="utf-8"))["cases"]}
    found = [f"{i}: not captured" for i in stored if i not in fresh]
    for i, old in stored.items():
        if i not in fresh:
            continue
        new = fresh[i]
        found += [f"{i}: {key} differs" for key in EXACT if new.get(key) != old.get(key)]
        if "file" not in old or old["control"] == "accent":
            continue
        name = old["file"] if old["file"].endswith(".z") else old["file"] + ".z"
        want = zlib.decompress((store / name).read_bytes())
        got = (fresh_dir / new["file"]).read_bytes()
        worst = max((abs(a - b) for a, b in zip(want, got)), default=0)
        if len(want) != len(got) or worst > PIXEL_LIMIT:
            found.append(f"{i}: pixels differ by {worst}")
    return found


def main():
    here = ROOT.parent
    exe = here / "exe" / "Reference.exe"
    os.environ["IMPOSSIBLE_DISPLAY"] = "vm"
    os.environ["IMPOSSIBLE_REFERENCE_EXE"] = str(exe)
    register_fonts(here / "fonts")
    sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "reference"), str(ROOT / "tools/winui")]
    import run
    out = Path(r"C:\job\out\frames")
    run.run_reference(exe, out)
    found = differences(out, ROOT / "tests/reference")
    for line in found:
        print(f"capture: {line}")
    print(f"capture: {len(found)} differences")
    import test_live
    tests = unittest.defaultTestLoader.loadTestsFromModule(test_live)
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(tests)
    ran = result.testsRun > 0 and not result.skipped
    return 0 if not found and ran and result.wasSuccessful() else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as error:
        print(error, error.stderr)
        sys.exit(1)
