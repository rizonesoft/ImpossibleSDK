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
            folder_rel = info["folder"]
            if Path(folder_rel).is_absolute() or ".." in Path(folder_rel).parts:
                return False, f"part {name!r} folder must be repository-relative: {folder_rel!r}"
            src_folder = repo_root / folder_rel
            if src_folder.is_symlink():
                return False, f"part {name!r} folder is a symlink: {folder_rel!r}"
            for dirpath, dirnames, filenames in os.walk(src_folder, followlinks=False):
                dp = Path(dirpath)
                for entry in (*dirnames, *filenames):
                    if (dp / entry).is_symlink():
                        return False, f"part {name!r} contains symlink: {(dp / entry).relative_to(repo_root).as_posix()}"
            dst_folder = stage / folder_rel
            shutil.copytree(src_folder, dst_folder, symlinks=True)
            include_dirs.append(dst_folder)
            for s in info["sources"]:
                if Path(s).is_absolute() or ".." in Path(s).parts:
                    return False, f"part {name!r} source must be repository-relative: {s!r}"
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

        defined, wanted = set(), set()
        strong_defs = {}
        strong_types = {"T", "D", "B", "R", "S", "G"}
        for obj in objects:
            nm = subprocess.run(
                [str(LLVM / f"llvm-nm{EXE}"), str(obj)],
                capture_output=True,
                text=True,
                check=True,
                timeout=60,
            )
            for line in nm.stdout.splitlines():
                words = line.split()
                if len(words) == 2 and words[0] == "U":
                    wanted.add(words[1])
                elif len(words) == 3 and words[1].isupper():
                    sym_type, sym_name = words[1], words[2]
                    if sym_type in strong_types:
                        if sym_name in strong_defs:
                            return False, f"duplicate symbol definition: {sym_name} in {strong_defs[sym_name]} and {obj.name}"
                        strong_defs[sym_name] = obj.name
                    defined.add(sym_name)
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

    def test_refuses_duplicate_strong_symbol_across_declared_dependency(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "parts/a").mkdir(parents=True)
            (root / "parts/b").mkdir(parents=True)
            (root / "parts/b/b.c").write_text("int shared_helper(void) { return 1; }\n", encoding="utf-8")
            (root / "parts/a/a.c").write_text("int shared_helper(void) { return 2; }\n", encoding="utf-8")
            parts = {
                "b": {"name": "b", "folder": "parts/b", "depends": [], "sources": ["parts/b/b.c"]},
                "a": {"name": "a", "folder": "parts/a", "depends": ["b"], "sources": ["parts/a/a.c"]},
            }
            ok, msg = compile_part_isolated(root, parts, "a")
            self.assertFalse(ok)
            self.assertIn("duplicate symbol", msg)

    def test_refuses_inc_transitive_include_spliced_include_absolute_path_and_symlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "parts/a").mkdir(parents=True)
            (root / "parts/b").mkdir(parents=True)
            (root / "parts/b/b.h").write_text("#pragma once\nint b_val(void);\n", encoding="utf-8")
            manifest = root / "parts.json"

            # 1. Transitive undeclared include through a non-.c/.h .inc file.
            (root / "parts/a/table.inc").write_text('#include "../b/b.h"\n', encoding="utf-8")
            (root / "parts/a/a.c").write_text('#include "table.inc"\nint f(void) { return 0; }\n', encoding="utf-8")
            manifest.write_text(json.dumps({
                "parts": [
                    {"name": "a", "folder": "parts/a", "depends": [], "sources": ["parts/a/a.c"]},
                    {"name": "b", "folder": "parts/b", "depends": [], "sources": []},
                ]
            }), encoding="utf-8")
            res = _run_checker(manifest)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("undeclared part 'b'", res.stderr)
            (root / "parts/a/table.inc").unlink()

            # 2. C phase-2 backslash-newline spliced #include directive.
            (root / "parts/a/a.c").write_text('#inc\\\nlude "../b/b.h"\nint f(void) { return 0; }\n', encoding="utf-8")
            res = _run_checker(manifest)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("undeclared part 'b'", res.stderr)

            # 3. Absolute source path in manifest must be refused.
            (root / "parts/a/a.c").write_text("int f(void) { return 0; }\n", encoding="utf-8")
            manifest.write_text(json.dumps({
                "parts": [
                    {"name": "a", "folder": "parts/a", "depends": [], "sources": [str((root / "parts/a/a.c").resolve())]},
                ]
            }), encoding="utf-8")
            res = _run_checker(manifest)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("relative", res.stderr)

            # 4. Directory symlink inside part folder must be refused by both checker and isolation.
            manifest.write_text(json.dumps({
                "parts": [
                    {"name": "a", "folder": "parts/a", "depends": [], "sources": ["parts/a/a.c"]},
                ]
            }), encoding="utf-8")
            link = root / "parts/a/loop"
            try:
                os.symlink(root, link, target_is_directory=True)
            except OSError:
                return
            res = _run_checker(manifest)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("symlink", res.stderr)
            ok, msg = compile_part_isolated(
                root,
                {"a": {"name": "a", "folder": "parts/a", "depends": [], "sources": ["parts/a/a.c"]}},
                "a",
            )
            self.assertFalse(ok)
            self.assertIn("symlink", msg)


@unittest.skipUnless(os.name == "nt" or "microsoft" in platform.release().lower(), "needs Windows, directly or through WSL")
class ImageFixture(unittest.TestCase):
    def test_image_fixture_decoder(self):
        res = subprocess.run([str(BUILD / "image-tests.exe")], stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
        self.assertEqual(res.returncode, 0, res.stderr)


class UnicodePart(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(ROOT / "tools"))
        import gen_unicode
        cls.gen = gen_unicode
        cls.pin = gen_unicode.load_pin()
        cls.cache = gen_unicode.DEFAULT_CACHE
        for rel in cls.pin["files"]:
            gen_unicode.ensure_ucd_file(cls.pin, rel, cls.cache, allow_network=True)

    def test_generator_determinism_license_and_refusal_paths(self):
        # 1. Committed tables pass --check --no-network.
        res = subprocess.run(
            [sys.executable, str(ROOT / "tools/gen_unicode.py"), "--check", "--no-network"],
            capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(res.returncode, 0, res.stderr)

        # 2. License notice travels with parts/unicode/ and matches pinned license sha256.
        want_lic = self.pin["license"]["sha256"]
        for p in (ROOT / "licenses/UNICODE-LICENSE-V3.txt", ROOT / "parts/unicode/LICENSE.txt"):
            self.assertTrue(p.is_file(), f"missing {p}")
            self.assertEqual(self.gen.sha256_bytes(p.read_bytes()), want_lic)
        for gen_file in (ROOT / "parts/unicode/unicode_data.h", ROOT / "parts/unicode/unicode_data.c"):
            head = gen_file.read_text(encoding="utf-8")[:600]
            self.assertIn("Do not edit.", head)
            self.assertIn("Unicode License v3", head)
        # 2b. Site API reference discovers unicode.h and omits internal generated unicode_data.h.
        import importlib.util
        spec = importlib.util.spec_from_file_location("impossible_site_build", ROOT / "tools/site/build.py")
        site_build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(site_build)
        site = site_build.Site(ROOT, BUILD, run=False)
        site.generated()
        names = {p.name for p in site.pages}
        self.assertIn("api-unicode", names)
        self.assertNotIn("api-unicode_data", names)

        # 3. Drifted generated output fails --check WITHOUT rewriting the drifted file.
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp) / "out"
            out_dir.mkdir()
            for name in ("unicode_data.h", "unicode_data.c"):
                shutil.copy2(ROOT / "parts/unicode" / name, out_dir / name)
            drifted_h = out_dir / "unicode_data.h"
            drifted_text = drifted_h.read_text(encoding="utf-8") + "\n/* drift */\n"
            drifted_h.write_text(drifted_text, encoding="utf-8")
            chk = subprocess.run(
                [sys.executable, str(ROOT / "tools/gen_unicode.py"), "--check", "--no-network", "--out-dir", str(out_dir)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertNotEqual(chk.returncode, 0)
            self.assertEqual(drifted_h.read_text(encoding="utf-8"), drifted_text)

        # 4. Corrupt cached input is refused before use.
        with tempfile.TemporaryDirectory() as tmp:
            bad_cache = Path(tmp) / "cache"
            ver_dir = bad_cache / self.pin["version"]
            shutil.copytree(self.cache / self.pin["version"], ver_dir)
            (ver_dir / "UnicodeData.txt").write_bytes(b"corrupted")
            bad = subprocess.run(
                [sys.executable, str(ROOT / "tools/gen_unicode.py"), "--check", "--no-network", "--cache-dir", str(bad_cache)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("corrupt cached UCD file", bad.stderr)

    def _run_fixture(self, flag, payload):
        exe = BUILD / "unicode-tests.exe"
        self.assertTrue(exe.is_file(), f"missing {exe}; run python3 tools/build.py")
        args = [str(exe)] + ([flag] if flag else [])
        res = subprocess.run(args, input=payload, capture_output=True, text=True, timeout=180)
        return res

    def _assert_suite(self, flag, label, payload, expected_count):
        res = self._run_fixture(flag, payload)
        self.assertEqual(res.returncode, 0, f"{label} failed (rc={res.returncode}):\n{res.stdout}\n{res.stderr}")
        self.assertIn(f"SUITE {label} executed={expected_count} failed=0", res.stdout, res.stdout)

    @unittest.skipUnless(os.name == "nt" or "microsoft" in platform.release().lower(), "needs Windows, directly or through WSL")
    def test_unicode_self_tests_and_ucd_suites(self):
        # 1. Self-tests: buffers/guards, wrapped bidi L1/L2, every-cut streaming breaks, mixed casefold, errors.
        self_res = self._run_fixture(None, "")
        self.assertEqual(self_res.returncode, 0, f"self-tests failed rc={self_res.returncode}: {self_res.stdout} {self_res.stderr}")
        self.assertIn("SELF_TESTS OK", self_res.stdout)

        ver_dir = self.cache / self.pin["version"]

        # 2. Break suites: GraphemeBreakTest, WordBreakTest, LineBreakTest (100% of vectors, whole + 1-by-1 streaming).
        for flag, label, rel in (
            ("--suite-grapheme", "grapheme", "auxiliary/GraphemeBreakTest.txt"),
            ("--suite-word", "word", "auxiliary/WordBreakTest.txt"),
            ("--suite-line", "line", "auxiliary/LineBreakTest.txt"),
        ):
            lines = []
            for raw in (ver_dir / rel).read_text(encoding="utf-8").splitlines():
                body = raw.split("#", 1)[0].strip()
                if not body:
                    continue
                tokens = body.split()
                cps = []
                brks = []
                for idx, tok in enumerate(tokens):
                    if idx % 2 == 0:
                        brks.append("1" if tok == "÷" else "0")
                    else:
                        cps.append(tok)
                lines.append(f"{' '.join(cps)}|{''.join(brks)}")
            self._assert_suite(flag, label, "\n".join(lines) + "\n", len(lines))

        # 3. BidiTest.txt (all active paragraph modes per bitset).
        bidi_lines = []
        bidi_expected = 0
        cur_levels = []
        cur_order = []
        for raw in (ver_dir / "BidiTest.txt").read_text(encoding="utf-8").splitlines():
            body = raw.split("#", 1)[0].strip()
            if not body:
                continue
            if body.startswith("@Levels:"):
                cur_levels = ["255" if x == "x" else x for x in body[8:].split()]
            elif body.startswith("@Reorder:"):
                cur_order = body[9:].split()
            else:
                cls_part, bit_part = body.split(";", 1)
                bitset = int(bit_part.strip(), 16)
                cls_nums = [str(self.gen.BIDI_MAP[c]) for c in cls_part.split()]
                bidi_lines.append(f"{bitset}|{' '.join(cls_nums)}|{' '.join(cur_levels)}|{' '.join(cur_order)}")
                for m in range(3):
                    if bitset & (1 << m):
                        bidi_expected += 1
        self._assert_suite("--suite-bidi", "bidi", "\n".join(bidi_lines) + "\n", bidi_expected)

        # 4. BidiCharacterTest.txt (100% of vectors).
        bc_lines = []
        for raw in (ver_dir / "BidiCharacterTest.txt").read_text(encoding="utf-8").splitlines():
            body = raw.split("#", 1)[0].strip()
            if not body:
                continue
            parts = [p.strip() for p in body.split(";")]
            req_base = "-1" if parts[1] == "2" else parts[1]
            exp_lvls = ["255" if x == "x" else x for x in parts[3].split()]
            bc_lines.append(f"{req_base}|{parts[0]}|{parts[2]}|{' '.join(exp_lvls)}|{parts[4]}")
        self._assert_suite("--suite-bidi-char", "bidi_char", "\n".join(bc_lines) + "\n", len(bc_lines))

        # 5. NormalizationTest.txt (100% of vectors across NFD, NFC, NFKD, NFKC + Part 1 identity requirement).
        norm_lines = []
        part1_cps = []
        in_part1 = False
        for raw in (ver_dir / "NormalizationTest.txt").read_text(encoding="utf-8").splitlines():
            body = raw.split("#", 1)[0].strip()
            if not body:
                continue
            if body.startswith("@"):
                in_part1 = body.startswith("@Part1")
                continue
            cols = [c.strip() for c in body.split(";")[:5]]
            norm_lines.append("|".join(cols))
            if in_part1 and " " not in cols[0]:
                part1_cps.append(cols[0])
        self._assert_suite("--suite-norm", "norm", "\n".join(norm_lines) + "\n", len(norm_lines))
        self._assert_suite("--suite-norm-id", "norm_id", "\n".join(part1_cps) + "\n", 0x110000 - len(part1_cps))

        # 5b. Fixture rejects malformed non-hex token cleanly without hanging.
        bad_tok = self._run_fixture("--suite-word", "ZZZZ|11\n")
        self.assertNotEqual(bad_tok.returncode, 0)

        # 6. CaseFolding.txt (all C, F, S, T mappings + unlisted identity defaults across all 3 modes).
        cf_map = {}
        for row in self.gen.iter_ucd_rows(ver_dir / "CaseFolding.txt"):
            cf_map.setdefault(int(row[0], 16), {})[row[1]] = row[2]
        cf_lines = []
        for cp, statuses in sorted(cf_map.items()):
            hex_cp = f"{cp:04X}"
            full = statuses.get("C") or statuses.get("F") or hex_cp
            simp = statuses.get("C") or statuses.get("S") or hex_cp
            turk = statuses.get("T") or full
            cf_lines.append(f"0|{hex_cp}|{full}")
            cf_lines.append(f"1|{hex_cp}|{simp}")
            cf_lines.append(f"2|{hex_cp}|{turk}")
        # Add unlisted identity defaults (lowercase, digits, punctuation, CJK, emoji, unassigned).
        for id_cp in (0x0061, 0x0030, 0x0021, 0x4E00, 0x1F600, 0x0378):
            h = f"{id_cp:04X}"
            for mode in (0, 1, 2):
                cf_lines.append(f"{mode}|{h}|{h}")
        self._assert_suite("--suite-casefold", "casefold", "\n".join(cf_lines) + "\n", len(cf_lines))

        # 7. Independent parser of pinned UCD files verifying ALL 1,114,112 codepoints (0x0000..0x10FFFF)
        # using public C enum values from parts/unicode/unicode.h (never reusing gen_unicode's maps).
        test_gc_map = {
            name: i for i, name in enumerate([
                "Cn", "Lu", "Ll", "Lt", "Lm", "Lo",
                "Mn", "Mc", "Me",
                "Nd", "Nl", "No",
                "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po",
                "Sm", "Sc", "Sk", "So",
                "Zs", "Zl", "Zp",
                "Cc", "Cf", "Cs", "Co",
            ])
        }
        test_eaw_map = {name: i for i, name in enumerate(["N", "A", "F", "H", "Na", "W"])}
        test_bidi_map = {
            name: i for i, name in enumerate([
                "L", "R", "AL", "EN", "ES", "ET", "AN", "CS", "NSM", "BN",
                "B", "S", "WS", "ON",
                "LRE", "LRO", "RLE", "RLO", "PDF",
                "LRI", "RLI", "FSI", "PDI",
            ])
        }

        def _parse_rng(tok):
            if ".." in tok:
                a, b = tok.split("..", 1)
                return int(a, 16), int(b, 16)
            v = int(tok, 16)
            return v, v

        def _rows(rel_path):
            for line in (ver_dir / rel_path).read_text(encoding="utf-8").splitlines():
                s = line.split("#", 1)[0].strip()
                if s:
                    yield [f.strip() for f in s.split(";")]

        total_cps = 0x110000
        gc_exp = [0] * total_cps
        eaw_exp = [0] * total_cps
        ccc_exp = [0] * total_cps
        bidi_exp = [0] * total_cps
        script_exp = ["Unknown"] * total_cps
        name_exp = [""] * total_cps
        mirror_exp = {}
        bracket_exp = {}

        for row in _rows("extracted/DerivedGeneralCategory.txt"):
            lo, hi = _parse_rng(row[0])
            val = test_gc_map[row[1]]
            gc_exp[lo : hi + 1] = [val] * (hi - lo + 1)
        for row in _rows("EastAsianWidth.txt"):
            lo, hi = _parse_rng(row[0])
            val = test_eaw_map[row[1]]
            eaw_exp[lo : hi + 1] = [val] * (hi - lo + 1)
        for row in _rows("extracted/DerivedCombiningClass.txt"):
            lo, hi = _parse_rng(row[0])
            val = int(row[1])
            ccc_exp[lo : hi + 1] = [val] * (hi - lo + 1)
        # Independent parse of # @missing: default bidi ranges + explicit DerivedBidiClass rows.
        long_bidi = {"Left_To_Right": "L", "Right_To_Left": "R", "Arabic_Letter": "AL", "European_Terminator": "ET"}
        for line in (ver_dir / "extracted/DerivedBidiClass.txt").read_text(encoding="utf-8").splitlines():
            s = line.strip()
            if s.startswith("# @missing:"):
                parts = [p.strip() for p in s[len("# @missing:"):].split(";")]
                lo, hi = _parse_rng(parts[0])
                val = test_bidi_map[long_bidi.get(parts[1], parts[1])]
                bidi_exp[lo : hi + 1] = [val] * (hi - lo + 1)
        for row in _rows("extracted/DerivedBidiClass.txt"):
            lo, hi = _parse_rng(row[0])
            val = test_bidi_map[row[1]]
            bidi_exp[lo : hi + 1] = [val] * (hi - lo + 1)
        for row in _rows("Scripts.txt"):
            lo, hi = _parse_rng(row[0])
            script_exp[lo : hi + 1] = [row[1]] * (hi - lo + 1)
        for row in _rows("BidiMirroring.txt"):
            mirror_exp[int(row[0], 16)] = int(row[1], 16)
        for row in _rows("BidiBrackets.txt"):
            bracket_exp[int(row[0], 16)] = (1 if row[2] == "o" else 2, int(row[1], 16))
        for row in _rows("extracted/DerivedName.txt"):
            lo, hi = _parse_rng(row[0])
            nm = row[1]
            if nm.endswith("*"):
                pfx = nm[:-1]
                for cp in range(lo, hi + 1):
                    name_exp[cp] = f"{pfx}{cp:04X}"
            else:
                for cp in range(lo, hi + 1):
                    name_exp[cp] = nm

        # Send all 1,114,112 codepoints in 4 chunks so each fixture stdin stays small and fast.
        chunk_size = 0x44000
        checked_total = 0
        for start_cp in range(0, total_cps, chunk_size):
            end_cp = min(total_cps, start_cp + chunk_size)
            prop_lines = []
            for cp in range(start_cp, end_cp):
                bt, bp = bracket_exp.get(cp, (0, 0))
                prop_lines.append(
                    f"{cp:X}|{gc_exp[cp]}|{eaw_exp[cp]}|{ccc_exp[cp]}|{bidi_exp[cp]}|"
                    f"{script_exp[cp]}|{mirror_exp.get(cp, 0):X}|{bt}|{bp:X}|{name_exp[cp]}"
                )
            self._assert_suite("--suite-props", "props", "\n".join(prop_lines) + "\n", end_cp - start_cp)
            checked_total += end_cp - start_cp
        self.assertEqual(checked_total, 1114112)


class InflateAndChecksum(unittest.TestCase):
    def test_inflate_and_checksums_10000_random_streaming_and_1gb_bomb(self):
        import gzip
        import random
        import struct
        import zlib

        exe = BUILD / "inflate-tests.exe"
        self.assertTrue(exe.exists(), f"missing {exe}; run tools/build.py first")

        # 1. Self-contained unit vectors (CRC-32, Adler-32, zlib vector, exact limit, FDICT, truncation, trailing bytes).
        res = subprocess.run([str(exe)], capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=60)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("inflate_tests: ALL PASSED", res.stdout)

        # 2. Persistent batch process for 10,000 random zlib round-trips, byte-by-byte streaming, gzip, and 1 GB bomb!
        proc = subprocess.Popen(
            [str(exe), "--batch"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        killed = False
        try:
            def read_exact(n):
                buf = bytearray()
                while len(buf) < n:
                    chunk = proc.stdout.read(n - len(buf))
                    if not chunk:
                        raise RuntimeError(f"EOF from inflate-tests: {proc.stderr.read().decode('utf-8', 'replace')}")
                    buf.extend(chunk)
                return bytes(buf)

            def check_cksum(data: bytes):
                proc.stdin.write(struct.pack("<II", 1, len(data)) + data)
                proc.stdin.flush()
                crc, adl = struct.unpack("<II", read_exact(8))
                self.assertEqual(crc, zlib.crc32(data) & 0xFFFFFFFF)
                self.assertEqual(adl, zlib.adler32(data) & 0xFFFFFFFF)

            def run_inflate(fmt: int, comp: bytes, out_cap: int, has_limit=False, limit=0, stream_1by1=False):
                flags = (1 if has_limit else 0) | (2 if stream_1by1 else 0)
                proc.stdin.write(struct.pack("<IBBIII", 2, fmt, flags, limit, out_cap, len(comp)) + comp)
                proc.stdin.flush()
                st, iu, olen = struct.unpack("<III", read_exact(12))
                out = read_exact(olen) if olen else b""
                return st, iu, out

            rng = random.Random(20261125)

            # Checksum verification across 100 varied lengths (0 to 32 KB)
            for n in (0, 1, 2, 3, 7, 15, 64, 5551, 5552, 5553, 16384, 32768):
                check_cksum(rng.randbytes(n))
            for _ in range(88):
                check_cksum(rng.randbytes(rng.randint(0, 4096)))

            # 10,000 random inputs compressed at EVERY level 0..9 (100,000 round-trips) across raw/zlib/gzip!
            wbits_for = {0: -15, 1: 15, 2: 31}
            for i in range(10000):
                fmt = i % 3
                kind = i % 4
                if kind == 0:
                    raw = rng.randbytes(rng.randint(0, 32))
                elif kind == 1:
                    raw = bytes([rng.randint(0, 255)]) * rng.randint(1, 120)
                elif kind == 2:
                    pat = rng.randbytes(rng.randint(1, 10))
                    raw = (pat * rng.randint(2, 20))[: rng.randint(1, 180)]
                else:
                    # Larger repetitive+mixed inputs trigger dynamic Huffman blocks (BTYPE=2) across levels!
                    raw = (rng.randbytes(rng.randint(8, 24)) * rng.randint(8, 20)) + rng.randbytes(rng.randint(64, 192))
                for level in range(10):
                    cobj = zlib.compressobj(level=level, wbits=wbits_for[fmt])
                    comp = cobj.compress(raw) + cobj.flush()
                    st, iu, out = run_inflate(fmt, comp, max(len(raw) + 16, 64), has_limit=True, limit=len(raw))
                    self.assertEqual(st, 0, f"case {i} fmt={fmt} level={level} len={len(raw)} st={st}")
                    self.assertEqual(out, raw, f"case {i} level={level} mismatch")
                    self.assertEqual(iu, len(comp), f"case {i} level={level} in_used")

            # Byte-at-a-time streaming (1-byte input AND 1-byte output) across raw, stored, fixed, dynamic,
            # overlapping matches (dist=1, dist=3), 32 KiB window wrap, and multi-member gzip!
            raw_c = zlib.compressobj(9, zlib.DEFLATED, -15)
            raw_stream = raw_c.compress(b"hello " * 100) + raw_c.flush()
            wrap_data = (rng.randbytes(1024) * 40)[:40960]
            stream_cases = [
                (0, raw_stream, b"hello " * 100),
                (1, zlib.compress(b"stored block test " * 20, 0), b"stored block test " * 20),
                (1, zlib.compress(b"A" * 5000, 6), b"A" * 5000),
                (1, zlib.compress(b"ABC" * 2000, 9), b"ABC" * 2000),
                (1, zlib.compress(wrap_data, 6), wrap_data),
                (2, gzip.compress(b"FirstMember!") + gzip.compress(b"SecondMember!"), b"FirstMember!SecondMember!"),
            ]
            for fmt, comp, expected in stream_cases:
                st, iu, out = run_inflate(fmt, comp, len(expected) + 16, stream_1by1=True)
                self.assertEqual(st, 0, f"1-by-1 fmt={fmt} st={st}")
                self.assertEqual(out, expected)
                self.assertEqual(iu, len(comp))

            # Deterministic RFC 1951 degenerate dynamic-block tests:
            # (a) HDIST=32 (with reserved 30/31 zero-length) + empty distance tree on literal-only 'A' -> OK;
            # (b) zlib small advertised window (CINFO=0 -> 256B window) rejecting distance 300 -> BAD_DATA (6).
            def pack_bits(pairs):
                val, cnt = 0, 0
                out_b = bytearray()
                for bits, n in pairs:
                    val |= (bits & ((1 << n) - 1)) << cnt
                    cnt += n
                    while cnt >= 8:
                        out_b.append(val & 0xFF)
                        val >>= 8
                        cnt -= 8
                if cnt:
                    out_b.append(val & 0xFF)
                return bytes(out_b)

            # Build dynamic block: BFINAL=1, BTYPE=2, HLIT=257 (0), HDIST=32 (31), HCLEN=19 (15)
            # clen symbols: 0=len 2 (00), 1=len 2 (01), 18=len 1 (1).
            # Wait: 0 -> len 2 (code 10), 1 -> len 2 (code 11), 18 -> len 1 (code 0): canonical!
            # clen_order: [16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15]
            clens = {18: 1, 0: 2, 1: 2}
            clen_order = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]
            bits = [(1, 1), (2, 2), (0, 5), (31, 5), (15, 4)]
            for sym in clen_order:
                bits.append((clens.get(sym, 0), 3))
            # Emit 257 litlen lengths ('A'=65 has len 1, 256 EOB has len 1; all others 0) + 32 dist lengths (all 0):
            # sym 18 = code 0 (1 bit) + 7 extra bits for 11..138 zeros; sym 0 = code 0b10 (LSB-first: 0b01? No, canonical 10 -> reversed 01!), sym 1 = code 0b11 (2 bits)
            # 65 zeros = sym 18 (extra 54); lit 'A'=1 (0b11); 190 zeros = 138 (extra 127) + 52 (extra 41); EOB 256=1 (0b11); 32 dist zeros = sym 18 (extra 21)
            bits.extend([
                (0, 1), (65 - 11, 7),
                (0b11, 2),
                (0, 1), (138 - 11, 7),
                (0, 1), (52 - 11, 7),
                (0b11, 2),
                (0, 1), (32 - 11, 7),
                # Litlen canonical: 'A' (65) = 0 (1 bit), EOB (256) = 1 (1 bit)
                (0, 1), (1, 1),
            ])
            dyn_hdist32_empty = pack_bits(bits)
            st_d, _, out_d = run_inflate(0, dyn_hdist32_empty, 16)
            self.assertEqual(st_d, 0, f"dynamic HDIST=32 + empty dist tree should return OK, got {st_d}")
            self.assertEqual(out_d, b"A")

            # (b) Dynamic block with empty distance tree attempting a match (len symbol 257) -> IM_INFLATE_BAD_DATA (6)
            # HLIT=258 (1), HDIST=1 (0), HCLEN=19 (15); litlen: 'A'=65 (len 2), EOB=256 (len 2), match=257 (len 1); dist: 0 (len 0)
            clens2 = {18: 1, 1: 2, 2: 2}
            bits2 = [(1, 1), (2, 2), (1, 5), (0, 5), (15, 4)]
            for sym in clen_order:
                bits2.append((clens2.get(sym, 0), 3))
            # clen canonical: 18->0 (1b), 1->10 (2b, LSB-first: 0b01), 2->11 (2b, LSB-first: 0b11)
            # 65 zeros (18: 54), 'A'=len 2 (0b11), 190 zeros (138+52), EOB 256=len 2 (0b11), match 257=len 1 (0b01), 1 dist zero -> need sym 0!
            # Simpler: clens2 = {18:1, 0:2, 1:3, 2:3}: 18=0(1b), 0=10(0b01), 1=110(0b011), 2=111(0b111)
            clens2 = {18: 1, 0: 2, 1: 3, 2: 3}
            bits2 = [(1, 1), (2, 2), (1, 5), (0, 5), (15, 4)]
            for sym in clen_order:
                bits2.append((clens2.get(sym, 0), 3))
            bits2.extend([
                (0, 1), (65 - 11, 7),
                (0b111, 3),            # 'A' (65) len 2
                (0, 1), (138 - 11, 7),
                (0, 1), (52 - 11, 7),
                (0b111, 3),            # EOB (256) len 2
                (0b011, 3),            # match (257) len 1
                (0b01, 2),             # dist 0 len 0 (empty distance tree!)
                # Litlen canonical: 257->0 (1b), 'A'->10 (0b01), EOB->11 (0b11)
                (0b01, 2),             # emit 'A'
                (0, 1),                # emit match 257 -> attempts dist decode on empty dist tree!
            ])
            st_m, _, _ = run_inflate(0, pack_bits(bits2), 16)
            self.assertEqual(st_m, 6, f"match with empty distance tree must return BAD_DATA (6), got {st_m}")

            # Zlib small advertised window (CINFO=0 -> 256 bytes) rejecting a 300-byte back-reference -> BAD_DATA (6)
            big_z = bytearray(zlib.compress(b"X" * 400 + b"Y" * 100 + b"X" * 400, 9))
            # Rewrite CMF from 0x78 (CINFO=7) to 0x08 (CINFO=0, 256B window) with valid FLG check bits
            cmf = 0x08
            flg = (31 - ((cmf * 256) % 31)) % 31
            big_z[0], big_z[1] = cmf, flg
            st_w, _, _ = run_inflate(1, bytes(big_z), 1024)
            self.assertEqual(st_w, 6, f"zlib distance > advertised 256B window must return BAD_DATA (6), got {st_w}")

            # Gzip optional fields (FEXTRA + FNAME + FCOMMENT + FHCRC) split byte-by-byte + bad FHCRC + reserved flag
            body_raw = b"Gzip optional headers work!"
            body_c = zlib.compressobj(6, zlib.DEFLATED, -15)
            defl = body_c.compress(body_raw) + body_c.flush()
            hdr = bytearray([0x1F, 0x8B, 0x08, 0x1E, 0, 0, 0, 0, 0, 255])  # FHCRC|FEXTRA|FNAME|FCOMMENT
            hdr += struct.pack("<H", 4) + b"ABCD"
            hdr += b"file.txt\x00"
            hdr += b"a comment\x00"
            fhcrc = zlib.crc32(hdr) & 0xFFFF
            gz_opt = bytes(hdr) + struct.pack("<H", fhcrc) + defl + struct.pack("<II", zlib.crc32(body_raw) & 0xFFFFFFFF, len(body_raw))
            st, _, out = run_inflate(2, gz_opt, len(body_raw) + 16, stream_1by1=True)
            self.assertEqual(st, 0)
            self.assertEqual(out, body_raw)

            # Bad FHCRC -> IM_INFLATE_BAD_CHECKSUM (7)
            bad_fhcrc = bytearray(gz_opt)
            bad_fhcrc[len(hdr)] ^= 0xFF
            st, _, _ = run_inflate(2, bytes(bad_fhcrc), 64)
            self.assertEqual(st, 7)

            # Reserved gzip flag -> IM_INFLATE_BAD_DATA (6)
            bad_flg = bytearray(gz_opt)
            bad_flg[3] |= 0x20
            st, _, _ = run_inflate(2, bytes(bad_flg), 64)
            self.assertEqual(st, 6)

            # Multi-member gzip: damaged 2nd trailer -> BAD_CHECKSUM (7); 2nd member crossing limit -> LIMIT (3)
            gz2 = gzip.compress(b"12345") + gzip.compress(b"67890")
            gz2_bad = bytearray(gz2)
            gz2_bad[-1] ^= 1
            self.assertEqual(run_inflate(2, bytes(gz2_bad), 32)[0], 7)
            self.assertEqual(run_inflate(2, gz2, 32, has_limit=True, limit=7)[0], 3)

            # 1 GB zip bomb refused at 1 MB output_limit with IM_INFLATE_LIMIT (3) and exact total_out == 1 MB!
            c_bomb = zlib.compressobj(level=9, wbits=15)
            chunk_1mb = b"\x00" * (1024 * 1024)
            parts = [c_bomb.compress(chunk_1mb) for _ in range(1024)]
            parts.append(c_bomb.flush())
            bomb = b"".join(parts)
            self.assertLess(len(bomb), 1500000)
            limit_1mb = 1024 * 1024
            proc.stdin.write(struct.pack("<IBII", 3, 1, limit_1mb, len(bomb)) + bomb)
            proc.stdin.flush()
            st_b, tout_b = struct.unpack("<IQ", read_exact(12))
            self.assertEqual(st_b, 3, f"1 GB bomb must return IM_INFLATE_LIMIT (3), got {st_b}")
            self.assertEqual(tout_b, limit_1mb)
        except Exception:
            killed = True
            proc.kill()
            raise
        finally:
            if proc.stdin:
                proc.stdin.close()
            if proc.stdout:
                proc.stdout.close()
            err_text = proc.stderr.read().decode("utf-8", "replace") if proc.stderr else ""
            if proc.stderr:
                proc.stderr.close()
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
                raise
            if not killed:
                self.assertEqual(proc.returncode, 0, err_text)


class PngDecoder(unittest.TestCase):
    def _ensure_pngsuite(self, manifest: dict) -> Path:
        import hashlib
        import tarfile
        import urllib.request

        cache_dir = Path.home() / ".cache" / "impossible-sdk" / "pngsuite"
        files = manifest["files"]

        def cache_valid() -> bool:
            if not cache_dir.is_dir():
                return False
            for name, meta in files.items():
                p = cache_dir / name
                if not p.is_file():
                    return False
                if hashlib.sha256(p.read_bytes()).hexdigest() != meta["file_sha256"]:
                    return False
            return True

        if not cache_valid():
            cache_dir.mkdir(parents=True, exist_ok=True)
            tgz_path = cache_dir / "PngSuite-2017jul19.tgz"
            urllib.request.urlretrieve(manifest["archive_url"], tgz_path)
            got_sha = hashlib.sha256(tgz_path.read_bytes()).hexdigest()
            self.assertEqual(got_sha, manifest["archive_sha256"], "PngSuite archive SHA-256 mismatch")
            with tarfile.open(tgz_path, "r:gz") as tf:
                tf.extractall(cache_dir, filter="data")
            self.assertTrue(cache_valid(), "PngSuite extracted file SHA-256 verification failed")
        return cache_dir

    def test_png_decoder_pngsuite_synthetic_boundaries_and_all_prefixes(self):
        import hashlib
        import json
        import os
        import struct
        import threading
        import zlib

        exe = BUILD / "png-tests.exe"
        self.assertTrue(exe.exists(), f"missing {exe}; run tools/build.py first")

        # 1. Unit tests (max IHDR 0 allocs, independent limits, 33B probe, NULL limits, RGBA8/BGRA8, OOM, late CRC).
        res = subprocess.run([str(exe)], capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=60)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("png_tests: ALL PASSED", res.stdout)

        # 2. Load pinned cross-host PngSuite manifest and verify every file SHA-256 even on warm cache.
        manifest = json.loads((ROOT / "tests" / "data" / "pngsuite_ref.json").read_text(encoding="utf-8"))
        cache_dir = self._ensure_pngsuite(manifest)

        err_enum = {
            "IM_IMAGE_OK": 0,
            "IM_IMAGE_BAD_ARG": 1,
            "IM_IMAGE_INVALID": 2,
            "IM_IMAGE_TRUNCATED": 3,
            "IM_IMAGE_UNSUPPORTED": 4,
            "IM_IMAGE_TOO_LARGE": 5,
            "IM_IMAGE_NO_MEMORY": 6,
        }

        proc = subprocess.Popen(
            [str(exe), "--batch"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        killed = False
        timed_out = False
        active_op = ["startup"]

        def reap_proc():
            if os.name == "nt":
                subprocess.run(
                    ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    stdin=subprocess.DEVNULL,
                )
            else:
                proc.kill()

        def on_deadline():
            nonlocal timed_out, killed
            timed_out = True
            killed = True
            reap_proc()

        watchdog = threading.Timer(120.0, on_deadline)
        watchdog.daemon = True
        watchdog.start()
        try:
            def read_exact(n: int) -> bytes:
                buf = bytearray()
                while len(buf) < n:
                    chunk = proc.stdout.read(n - len(buf))
                    if not chunk:
                        if timed_out:
                            raise TimeoutError(f"png-tests timed out during {active_op[0]}")
                        raise RuntimeError(f"EOF from png-tests during {active_op[0]}")
                    buf.extend(chunk)
                return bytes(buf)

            def decode_buf(data: bytes, bgra: bool = False, label: str = "decode"):
                active_op[0] = label
                op = 2 if bgra else 1
                proc.stdin.write(struct.pack("<II", op, len(data)) + data)
                proc.stdin.flush()
                err, w, h, bsz, leak = struct.unpack("<IIIII", read_exact(20))
                px = read_exact(bsz) if (err == 0 and bsz > 0) else b""
                return err, w, h, bsz, leak, px

            def check_all_prefixes(data: bytes, label: str = "prefixes"):
                active_op[0] = label
                proc.stdin.write(struct.pack("<II", 3, len(data)) + data)
                proc.stdin.flush()
                return struct.unpack("<II", read_exact(8))

            # 2a. All 176 PngSuite files: 162 valid pixel-for-pixel + 14 invalid named errors.
            valid_count = 0
            invalid_count = 0
            for name, meta in sorted(manifest["files"].items()):
                raw = (cache_dir / name).read_bytes()
                err, w, h, bsz, leak, px = decode_buf(raw, bgra=False, label=f"decode:{name}")
                self.assertEqual(leak, 0, f"{name}: allocator/zero-image invariant failed")
                if meta["valid"]:
                    valid_count += 1
                    self.assertEqual(err, 0, f"{name}: expected IM_IMAGE_OK, got {err}")
                    self.assertEqual(w, meta["width"], f"{name}: width mismatch")
                    self.assertEqual(h, meta["height"], f"{name}: height mismatch")
                    self.assertEqual(bsz, w * h * 4, f"{name}: byte_size mismatch")
                    got_sha = hashlib.sha256(px).hexdigest()
                    self.assertEqual(got_sha, meta["rgba8_sha256"], f"{name}: RGBA8 pixels differ from pinned libpng")
                else:
                    invalid_count += 1
                    want_err = err_enum[meta["expected_error"]]
                    self.assertEqual(err, want_err, f"{name}: expected {meta['expected_error']} ({want_err}), got {err}")

                # Exhaustive proper-prefix test: EVERY prefix 0..len(raw)-1 must fail with zeroed image and 0 leaks.
                bad_cnt, first_bad = check_all_prefixes(raw, label=f"prefixes:{name}")
                self.assertEqual(bad_cnt, 0, f"{name}: {bad_cnt} proper prefixes succeeded or leaked (first={first_bad})")

            self.assertEqual(valid_count, 162)
            self.assertEqual(invalid_count, 14)

            # 2b. Verify BGRA8 swaps R and B while keeping G and A identical on a real color+alpha PngSuite fixture.
            for sample_name in ("tm3n3p02.png", "tbbn2c16.png", "g03n2c08.png"):
                raw = (cache_dir / sample_name).read_bytes()
                e_r, wr, hr, br, lr, px_r = decode_buf(raw, bgra=False)
                e_b, wb, hb, bb, lb, px_b = decode_buf(raw, bgra=True)
                self.assertEqual((e_r, e_b, lr, lb), (0, 0, 0, 0))
                self.assertEqual((wr, hr, br), (wb, hb, bb))
                expected_bgra = bytearray(len(px_r))
                expected_bgra[0::4] = px_r[2::4]
                expected_bgra[1::4] = px_r[1::4]
                expected_bgra[2::4] = px_r[0::4]
                expected_bgra[3::4] = px_r[3::4]
                self.assertEqual(px_b, bytes(expected_bgra), f"{sample_name}: BGRA8 channel order mismatch")

            # 2c. CRC-correct synthetic chunk, ordering, and IDAT boundary fixtures.
            sig = b"\x89PNG\r\n\x1a\n"

            def mk_chunk(ctype: bytes, data: bytes) -> bytes:
                crc = zlib.crc32(ctype + data) & 0xFFFFFFFF
                return struct.pack(">I", len(data)) + ctype + data + struct.pack(">I", crc)

            ihdr_1x1_rgba = mk_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
            ihdr_1x1_rgb = mk_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
            ihdr_1x1_idx1 = mk_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 1, 3, 0, 0, 0))
            iend = mk_chunk(b"IEND", b"")
            z_ok = zlib.compress(b"\x00\x11\x22\x33\x44")
            z_rgb = zlib.compress(b"\x00\x11\x22\x33")
            z_idx = zlib.compress(b"\x00\x00")

            # Indexed PLTE with 3 entries for 1-bit depth (max 2) -> IM_IMAGE_INVALID (2)
            bad_plte_over = sig + ihdr_1x1_idx1 + mk_chunk(b"PLTE", b"\x00" * 9) + mk_chunk(b"IDAT", z_idx) + iend
            self.assertEqual(decode_buf(bad_plte_over)[0], 2)

            # Truecolor PLTE after tRNS -> IM_IMAGE_INVALID (2)
            bad_plte_order = (
                sig + ihdr_1x1_rgb + mk_chunk(b"tRNS", struct.pack(">HHH", 1, 2, 3))
                + mk_chunk(b"PLTE", b"\x01\x02\x03") + mk_chunk(b"IDAT", z_rgb) + iend
            )
            self.assertEqual(decode_buf(bad_plte_order)[0], 2)

            # Non-ASCII chunk-type byte -> IM_IMAGE_INVALID (2)
            bad_ctype = sig + ihdr_1x1_rgba + mk_chunk(b"tE\x01t", b"x") + mk_chunk(b"IDAT", z_ok) + iend
            self.assertEqual(decode_buf(bad_ctype)[0], 2)

            # Unknown critical chunk -> IM_IMAGE_UNSUPPORTED (4)
            unk_crit = sig + ihdr_1x1_rgba + mk_chunk(b"AbCd", b"") + mk_chunk(b"IDAT", z_ok) + iend
            self.assertEqual(decode_buf(unk_crit)[0], 4)

            # Empty IDATs before, between (splitting Adler trailer), and after -> IM_IMAGE_OK (0)
            split_empty = (
                sig + ihdr_1x1_rgba
                + mk_chunk(b"IDAT", b"")
                + mk_chunk(b"IDAT", z_ok[:-2])
                + mk_chunk(b"IDAT", b"")
                + mk_chunk(b"IDAT", z_ok[-2:])
                + mk_chunk(b"IDAT", b"")
                + iend
            )
            e, w, h, _, leak, px = decode_buf(split_empty)
            self.assertEqual((e, w, h, leak, px), (0, 1, 1, 0, b"\x11\x22\x33\x44"))

            # Exact pixels followed by bad Adler-32 checksum -> IM_IMAGE_INVALID (2)
            z_bad_adl = z_ok[:-1] + bytes([z_ok[-1] ^ 1])
            self.assertEqual(decode_buf(sig + ihdr_1x1_rgba + mk_chunk(b"IDAT", z_bad_adl) + iend)[0], 2)

            # 1 filtered byte too few -> IM_IMAGE_INVALID (2)
            z_short = zlib.compress(b"\x00\x11\x22\x33")
            self.assertEqual(decode_buf(sig + ihdr_1x1_rgba + mk_chunk(b"IDAT", z_short) + iend)[0], 2)

            # 1 filtered byte too many -> IM_IMAGE_INVALID (2)
            z_long = zlib.compress(b"\x00\x11\x22\x33\x44\x55")
            self.assertEqual(decode_buf(sig + ihdr_1x1_rgba + mk_chunk(b"IDAT", z_long) + iend)[0], 2)

            # Trailing unused compressed byte in same IDAT -> IM_IMAGE_INVALID (2)
            self.assertEqual(decode_buf(sig + ihdr_1x1_rgba + mk_chunk(b"IDAT", z_ok + b"\x00") + iend)[0], 2)

            # Trailing unused compressed byte in subsequent non-empty IDAT -> IM_IMAGE_INVALID (2)
            self.assertEqual(
                decode_buf(sig + ihdr_1x1_rgba + mk_chunk(b"IDAT", z_ok) + mk_chunk(b"IDAT", b"\x00") + iend)[0], 2
            )

            # Invalid filter byte (5) -> IM_IMAGE_INVALID (2)
            z_bad_filt = zlib.compress(b"\x05\x11\x22\x33\x44")
            self.assertEqual(decode_buf(sig + ihdr_1x1_rgba + mk_chunk(b"IDAT", z_bad_filt) + iend)[0], 2)

            # Trailing byte after IEND -> IM_IMAGE_INVALID (2)
            self.assertEqual(decode_buf(sig + ihdr_1x1_rgba + mk_chunk(b"IDAT", z_ok) + iend + b"\x00")[0], 2)

            # Non-contiguous IDAT -> IM_IMAGE_INVALID (2)
            non_contig = (
                sig + ihdr_1x1_rgba + mk_chunk(b"IDAT", z_ok[:-2])
                + mk_chunk(b"tEXt", b"k\x00v") + mk_chunk(b"IDAT", z_ok[-2:]) + iend
            )
            self.assertEqual(decode_buf(non_contig)[0], 2)

            # Sub-16-bit tRNS key masking (PNG spec 11.3.2.1): unused upper bits must be masked to bit_depth.
            # (a) 8-bit grayscale sample 0x34 with tRNS key 0x0134 -> alpha 0
            ihdr_g8 = mk_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 0, 0, 0, 0))
            png_g8_trns = sig + ihdr_g8 + mk_chunk(b"tRNS", struct.pack(">H", 0x0134)) + mk_chunk(b"IDAT", zlib.compress(b"\x00\x34")) + iend
            e, w, h, _, leak, px = decode_buf(png_g8_trns, label="trns:g8_upper_bits")
            self.assertEqual((e, w, h, leak, px), (0, 1, 1, 0, b"\x34\x34\x34\x00"))

            # (b) 8-bit RGB sample (0x12, 0x34, 0x56) with tRNS key (0x0112, 0x0234, 0x0356) -> alpha 0
            png_rgb8_trns = (
                sig + ihdr_1x1_rgb + mk_chunk(b"tRNS", struct.pack(">HHH", 0x0112, 0x0234, 0x0356))
                + mk_chunk(b"IDAT", zlib.compress(b"\x00\x12\x34\x56")) + iend
            )
            e, w, h, _, leak, px = decode_buf(png_rgb8_trns, label="trns:rgb8_upper_bits")
            self.assertEqual((e, w, h, leak, px), (0, 1, 1, 0, b"\x12\x34\x56\x00"))

            # (c) 16-bit grayscale 2x1: pixel 0 = 0x1200 (opaque), pixel 1 = 0x1201 (matches tRNS 0x1201 -> alpha 0)
            ihdr_g16 = mk_chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 16, 0, 0, 0, 0))
            png_g16_trns = (
                sig + ihdr_g16 + mk_chunk(b"tRNS", struct.pack(">H", 0x1201))
                + mk_chunk(b"IDAT", zlib.compress(b"\x00\x12\x00\x12\x01")) + iend
            )
            e, w, h, _, leak, px = decode_buf(png_g16_trns, label="trns:g16_low_byte")
            self.assertEqual((e, w, h, leak, px), (0, 2, 1, 0, b"\x12\x12\x12\xFF\x12\x12\x12\x00"))
        except Exception:
            killed = True
            reap_proc()
            raise
        finally:
            watchdog.cancel()
            if proc.stdin:
                proc.stdin.close()
            if proc.stdout:
                proc.stdout.close()
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                killed = True
                reap_proc()
                proc.wait(timeout=5)
                raise
            err_text = proc.stderr.read().decode("utf-8", "replace") if proc.stderr else ""
            if proc.stderr:
                proc.stderr.close()
            if not killed:
                self.assertEqual(proc.returncode, 0, err_text)


if __name__ == "__main__":
    unittest.main()
