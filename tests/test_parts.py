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

        # 5. NormalizationTest.txt (100% of vectors across NFD, NFC, NFKD, NFKC).
        norm_lines = []
        for raw in (ver_dir / "NormalizationTest.txt").read_text(encoding="utf-8").splitlines():
            body = raw.split("#", 1)[0].strip()
            if not body or body.startswith("@"):
                continue
            cols = [c.strip() for c in body.split(";")[:5]]
            norm_lines.append("|".join(cols))
        self._assert_suite("--suite-norm", "norm", "\n".join(norm_lines) + "\n", len(norm_lines))

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

        # 7. Properties & Names across all assigned codepoints + unassigned default-bidi ranges.
        data = self.gen.parse_all_ucd(self.pin, self.cache, allow_network=False)
        mirror_map = dict(data["mirrors"])
        bracket_map = {cp: (bt, pair) for cp, pair, bt in data["brackets"]}
        name_map = dict(data["explicit_names"])
        algo = data["algo_prefixes"]
        sample_cps = set(name_map.keys())
        # Add Hangul, CJK, Tangut, Egyptian Hieroglyph algorithmic boundaries, plus unassigned default-bidi ranges.
        for lo, hi, _ in algo:
            sample_cps.update((lo, (lo + hi) // 2, hi))
        sample_cps.update((0xAC00, 0xAC01, 0xD7A3, 0x0378, 0x0590, 0x0600, 0x089F, 0xFB50, 0xFE70, 0x10800, 0x1EE00, 0x10FFFF))
        prop_lines = []
        for cp in sorted(sample_cps):
            nm = name_map.get(cp, "")
            if not nm and 0xAC00 <= cp <= 0xD7A3:
                nm = self.gen.hangul_name(cp)
            elif not nm:
                for lo, hi, pfx in algo:
                    if lo <= cp <= hi:
                        nm = f"{pfx}{cp:04X}"
                        break
            bt, bp = bracket_map.get(cp, (0, 0))
            prop_lines.append(
                f"{cp:X}|{data['gc'][cp]}|{data['eaw'][cp]}|{data['ccc'][cp]}|{data['bidi'][cp]}|"
                f"{data['script_names'][data['script_ids'][cp]]}|{mirror_map.get(cp, 0):X}|{bt}|{bp:X}|{nm}"
            )
        self._assert_suite("--suite-props", "props", "\n".join(prop_lines) + "\n", len(prop_lines))


if __name__ == "__main__":
    unittest.main()
