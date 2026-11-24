"""Parts stay isolated, compile alone with OS flags, and share the portable image type."""
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("IMPOSSIBLE_BUILD", ROOT / "build" / ("windows" if os.name == "nt" else "linux")))
sys.path.insert(0, str(ROOT / "tools"))
import bootstrap

LLVM = bootstrap.home(next(p for p in bootstrap.components() if p["name"] == "llvm-mingw")) / "bin"
EXE = ".exe" if os.name == "nt" else ""
ALLOWED_SYMBOLS = {"memcpy", "memmove", "memset", "memcmp"}
OS_CFLAGS = [
    "--target=x86_64-elf",
    "-std=gnu23",
    "-ffreestanding",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-mno-red-zone",
    "-mno-mmx",
    "-mno-sse",
    "-mno-sse2",
    "-mno-80387",
]


def _dependency_closure(parts, name):
    order = []
    seen = set()

    def visit(cur):
        if cur in seen:
            return
        seen.add(cur)
        for dep in parts[cur]["depends"]:
            visit(dep)
        order.append(cur)

    visit(name)
    return order


def compile_part_isolated(repo_root, parts, part_name):
    """Copy only part_name and its declared dependency closure into a scratch tree, compile with OS flags, and check symbols."""
    closure = _dependency_closure(parts, part_name)
    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp) / "src"
        obj_dir = Path(tmp) / "obj"
        obj_dir.mkdir(parents=True)
        include_dirs = []
        sources_to_compile = []
        for name in closure:
            info = parts[name]
            src_folder = repo_root / info["folder"]
            dst_folder = stage / info["folder"]
            shutil.copytree(src_folder, dst_folder)
            include_dirs.append(dst_folder)
            for s in info["sources"]:
                sources_to_compile.append((name, stage / s))

        objects = []
        for idx, (owner, src_file) in enumerate(sources_to_compile):
            obj = obj_dir / f"{idx}_{owner}_{src_file.stem}.o"
            cmd = [
                str(LLVM / f"clang{EXE}"),
                *OS_CFLAGS,
                *(f"-I{d}" for d in include_dirs),
                "-c",
                str(src_file),
                "-o",
                str(obj),
            ]
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
            if res.returncode != 0:
                return False, f"compile failed for {src_file.name}: {res.stderr.strip()}"
            objects.append(obj)

        if not objects:
            return True, ""

        nm = subprocess.run(
            [str(LLVM / f"llvm-nm{EXE}"), *map(str, objects)],
            capture_output=True,
            text=True,
            check=True,
            timeout=60,
        )
        defined, wanted = set(), set()
        for line in nm.stdout.splitlines():
            words = line.split()
            if len(words) == 2 and words[0] == "U":
                wanted.add(words[1])
            elif len(words) == 3 and words[1].isupper():
                defined.add(words[2])
        forbidden = sorted(wanted - defined - ALLOWED_SYMBOLS)
        if forbidden:
            return False, f"undefined symbols outside freestanding allowlist: {', '.join(forbidden)}"
        return True, ""


def _run_checker(manifest_path):
    return subprocess.run(
        [sys.executable, str(ROOT / "tools/check_parts.py"), str(manifest_path)],
        capture_output=True,
        text=True,
        timeout=60,
    )


class ManifestAndCheck(unittest.TestCase):
    def test_production_manifest_valid_and_has_ui_and_image(self):
        manifest = ROOT / "parts.json"
        res = _run_checker(manifest)
        self.assertEqual(res.returncode, 0, res.stderr)
        data = json.loads(manifest.read_text(encoding="utf-8"))
        names = {p["name"] for p in data["parts"]}
        self.assertIn("ui", names)
        self.assertIn("image", names)

    def test_refuses_undeclared_cross_part_include_and_accepts_declared(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "parts/a").mkdir(parents=True)
            (root / "parts/b").mkdir(parents=True)
            (root / "parts/b/b.h").write_text("#pragma once\nint b_val(void);\n", encoding="utf-8")
            (root / "parts/b/b.c").write_text('#include "b.h"\nint b_val(void) { return 7; }\n', encoding="utf-8")
            (root / "parts/a/a.c").write_text('#include "../b/b.h"\nint a_val(void) { return b_val(); }\n', encoding="utf-8")
            manifest = root / "parts.json"

            # Without declared edge: must fail naming both parts.
            manifest.write_text(json.dumps({
                "parts": [
                    {"name": "a", "folder": "parts/a", "depends": [], "sources": ["parts/a/a.c"]},
                    {"name": "b", "folder": "parts/b", "depends": [], "sources": ["parts/b/b.c"]},
                ]
            }), encoding="utf-8")
            bad = _run_checker(manifest)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("undeclared part 'b'", bad.stderr)

            # With declared edge: must pass.
            manifest.write_text(json.dumps({
                "parts": [
                    {"name": "a", "folder": "parts/a", "depends": ["b"], "sources": ["parts/a/a.c"]},
                    {"name": "b", "folder": "parts/b", "depends": [], "sources": ["parts/b/b.c"]},
                ]
            }), encoding="utf-8")
            ok = _run_checker(manifest)
            self.assertEqual(ok.returncode, 0, ok.stderr)

    def test_refuses_computed_include_cycle_unlisted_source_and_os_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "parts/a").mkdir(parents=True)
            (root / "parts/b").mkdir(parents=True)
            manifest = root / "parts.json"

            # Computed macro include.
            (root / "parts/a/a.c").write_text('#define H "../b/b.h"\n#include H\n', encoding="utf-8")
            (root / "parts/b/b.h").write_text("#pragma once\n", encoding="utf-8")
            manifest.write_text(json.dumps({
                "parts": [
                    {"name": "a", "folder": "parts/a", "depends": [], "sources": ["parts/a/a.c"]},
                    {"name": "b", "folder": "parts/b", "depends": [], "sources": []},
                ]
            }), encoding="utf-8")
            res = _run_checker(manifest)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("computed #include", res.stderr)

            # Non-freestanding OS header.
            (root / "parts/a/a.c").write_text("#include <windows.h>\nint x(void) { return 0; }\n", encoding="utf-8")
            res = _run_checker(manifest)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("non-freestanding header", res.stderr)

            # Dependency cycle.
            (root / "parts/a/a.c").write_text("int x(void) { return 0; }\n", encoding="utf-8")
            manifest.write_text(json.dumps({
                "parts": [
                    {"name": "a", "folder": "parts/a", "depends": ["b"], "sources": ["parts/a/a.c"]},
                    {"name": "b", "folder": "parts/b", "depends": ["a"], "sources": []},
                ]
            }), encoding="utf-8")
            res = _run_checker(manifest)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("cycle", res.stderr)

            # Unlisted .c source in part folder.
            (root / "parts/a/extra.c").write_text("int y(void) { return 1; }\n", encoding="utf-8")
            manifest.write_text(json.dumps({
                "parts": [
                    {"name": "a", "folder": "parts/a", "depends": [], "sources": ["parts/a/a.c"]},
                ]
            }), encoding="utf-8")
            res = _run_checker(manifest)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("unlisted .c", res.stderr)


@unittest.skipUnless((LLVM / f"clang{EXE}").is_file(), f"no pinned clang at {LLVM}")
class PortableParts(unittest.TestCase):
    def test_every_part_compiles_alone_with_os_flags(self):
        data = json.loads((ROOT / "parts.json").read_text(encoding="utf-8"))
        parts = {p["name"]: p for p in data["parts"]}
        for name in parts:
            ok, msg = compile_part_isolated(ROOT, parts, name)
            self.assertTrue(ok, f"part {name!r} failed standalone OS build: {msg}")

    def test_windows_call_fails_standalone_os_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            shutil.copytree(ROOT / "parts/image", root / "parts/image")
            src = root / "parts/image/image.c"
            src.write_text(
                src.read_text(encoding="utf-8")
                + "\nextern unsigned long GetLastError(void);\nunsigned long leak_win(void) { return GetLastError(); }\n",
                encoding="utf-8",
            )
            parts = {"image": {"name": "image", "folder": "parts/image", "depends": [], "sources": ["parts/image/image.c"]}}
            ok, msg = compile_part_isolated(root, parts, "image")
            self.assertFalse(ok)
            self.assertIn("GetLastError", msg)

    def test_macro_relative_include_fails_checker_and_isolation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "parts/a").mkdir(parents=True)
            (root / "parts/b").mkdir(parents=True)
            (root / "parts/b/secret.h").write_text("#pragma once\n#define SECRET_NUM 42\n", encoding="utf-8")
            (root / "parts/a/a.c").write_text('#define HIDDEN "../b/secret.h"\n#include HIDDEN\nint f(void) { return SECRET_NUM; }\n', encoding="utf-8")
            parts = {
                "a": {"name": "a", "folder": "parts/a", "depends": [], "sources": ["parts/a/a.c"]},
                "b": {"name": "b", "folder": "parts/b", "depends": [], "sources": []},
            }
            manifest = root / "parts.json"
            manifest.write_text(json.dumps({"parts": list(parts.values())}), encoding="utf-8")

            # Confirm it would compile in the full checkout where parts/b sits alongside parts/a.
            full_obj = root / "full.o"
            full_res = subprocess.run(
                [str(LLVM / f"clang{EXE}"), *OS_CFLAGS, "-c", str(root / "parts/a/a.c"), "-o", str(full_obj)],
                capture_output=True,
                text=True,
                timeout=120,
            )
            self.assertEqual(full_res.returncode, 0, full_res.stderr)

            # Both check_parts.py and isolated scratch compilation must refuse it.
            chk = _run_checker(manifest)
            self.assertNotEqual(chk.returncode, 0)
            ok, msg = compile_part_isolated(root, parts, "a")
            self.assertFalse(ok)
            self.assertIn("compile failed", msg)

    def test_declared_dependency_call_passes_with_edge_and_fails_without(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            shutil.copytree(ROOT / "parts/image", root / "parts/image")
            (root / "parts/consumer").mkdir(parents=True)
            (root / "parts/consumer/consumer.c").write_text(
                '#include "image.h"\n'
                "im_image_error probe_call(uint32_t w, uint32_t h, size_t *out) {\n"
                "    return im_image_check_limits(w, h, NULL, out);\n"
                "}\n",
                encoding="utf-8",
            )
            with_edge = {
                "image": {"name": "image", "folder": "parts/image", "depends": [], "sources": ["parts/image/image.c"]},
                "consumer": {"name": "consumer", "folder": "parts/consumer", "depends": ["image"], "sources": ["parts/consumer/consumer.c"]},
            }
            ok, msg = compile_part_isolated(root, with_edge, "consumer")
            self.assertTrue(ok, msg)

            without_edge = {
                "image": {"name": "image", "folder": "parts/image", "depends": [], "sources": ["parts/image/image.c"]},
                "consumer": {"name": "consumer", "folder": "parts/consumer", "depends": [], "sources": ["parts/consumer/consumer.c"]},
            }
            ok_no, _ = compile_part_isolated(root, without_edge, "consumer")
            self.assertFalse(ok_no)


@unittest.skipUnless(os.name == "nt" or "microsoft" in platform.release().lower(), "needs Windows, directly or through WSL")
class ImageFixture(unittest.TestCase):
    def test_image_fixture_decoder(self):
        res = subprocess.run([str(BUILD / "image-tests.exe")], stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
        self.assertEqual(res.returncode, 0, res.stderr)


if __name__ == "__main__":
    unittest.main()
