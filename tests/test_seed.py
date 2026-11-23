"""The samples run on Windows, and the import check refuses what it must."""
import hashlib
import json
import os
import platform
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("IMPOSSIBLE_BUILD", ROOT / "build" / ("windows" if os.name == "nt" else "linux")))
sys.path.insert(0, str(ROOT / "tools"))
import bootstrap
import check_imports

ON_WINDOWS = os.name == "nt" or "microsoft" in platform.release().lower()


def program(name):
    return (BUILD / f"{name}.exe").read_bytes()


class ImportCheck(unittest.TestCase):
    def test_samples_fit_their_tier(self):
        self.assertEqual(check_imports.problems(program("hello-console"), 0), [])
        self.assertEqual(check_imports.problems(program("hello-window"), 1), [])

    def test_a_function_outside_the_list_is_refused(self):
        self.assertEqual(check_imports.problems(program("forbidden"), 4), ["user32.dll: MessageBoxW is not in tier 4"])

    def test_a_higher_tier_is_refused(self):
        self.assertIn("user32.dll: CreateWindowExW is not in tier 0", check_imports.problems(program("hello-window"), 0))

    def test_an_import_by_ordinal_is_refused(self):
        data = bytearray(program("hello-console"))
        image = check_imports.Image(data)
        table = image.rva(check_imports.IMPORT)
        lookup = struct.unpack_from("<I", image.memory, table)[0]
        struct.pack_into("<Q", data, self.in_file(data, lookup), 1 << 63 | 7)
        self.assertIn("kernel32.dll: imported by ordinal", check_imports.problems(bytes(data), 0))

    def test_a_dll_that_is_loaded_for_nothing_is_refused(self):
        data = bytearray(program("hello-console"))
        image = check_imports.Image(data)
        table = image.rva(check_imports.IMPORT)
        for field in (0, 4):  # the lookup table and the address table
            start = struct.unpack_from("<I", image.memory, table + 4 * field)[0]
            struct.pack_into("<Q", data, self.in_file(data, start), 0)
        self.assertEqual(check_imports.problems(bytes(data), 0), ["kernel32.dll: is loaded for nothing"])

    def test_bound_imports_are_refused(self):
        data = bytearray(program("hello-console"))
        table = check_imports.Image(data).rva(check_imports.IMPORT)
        struct.pack_into("<I", data, self.in_file(data, table) + 4, 0xFFFFFFFF)
        self.assertIn("kernel32.dll: has bound imports", check_imports.problems(bytes(data), 0))

    def test_a_section_outside_the_image_is_refused(self):
        data = bytearray(program("hello-console"))
        struct.pack_into("<I", data, struct.unpack_from("<I", data, 0x3C)[0] + 24 + 56, 0x1000)
        with self.assertRaisesRegex(ValueError, "outside the image"):
            check_imports.problems(bytes(data), 0)

    def test_a_truncated_program_is_refused(self):
        with self.assertRaisesRegex(ValueError, "shorter than its sections"):
            check_imports.problems(program("hello-console")[:0x500], 0)

    @staticmethod
    def in_file(data, rva):
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        count, size = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
        for i in range(count):
            _, address, raw_size, raw = struct.unpack_from("<IIII", data, pe + 24 + size + 40 * i + 8)
            if address <= rva < address + raw_size:
                return raw + rva - address
        raise ValueError(hex(rva))

    def test_delay_loading_and_thread_local_storage_are_refused(self):
        for index, words in ((check_imports.DELAY_IMPORT, "uses delay-loaded DLLs"), (check_imports.TLS, "uses thread-local storage"),
                             (check_imports.BOUND_IMPORT, "has bound imports")):
            data = bytearray(program("hello-console"))
            optional = struct.unpack_from("<I", data, 0x3C)[0] + 24
            struct.pack_into("<I", data, optional + 112 + 8 * index, 0x1000)
            self.assertEqual(check_imports.problems(bytes(data), 0), [words])


@unittest.skipUnless(ON_WINDOWS, "needs Windows, directly or through WSL")
class Samples(unittest.TestCase):
    def run_sample(self, name, *arguments):
        with tempfile.TemporaryDirectory() as folder:
            return subprocess.run([str(BUILD / f"{name}.exe"), *arguments], capture_output=True, timeout=60, cwd=folder)

    def test_console(self):
        result = self.run_sample("hello-console")
        self.assertEqual((result.returncode, result.stdout.decode("utf-8")), (0, "Hello from Impossible SDK: café 🚀\n"))

    def test_base(self):
        self.assertEqual(self.run_sample("selftest", "--first", "not inside quotes", 'a"b', "tail\\", "--last").returncode, 0)

    def test_a_window_can_be_opened_again(self):
        self.assertEqual(self.run_sample("reopen").returncode, 0)

    def test_window_draws_a_frame(self):
        self.assertEqual(self.run_sample("hello-window", "--one-frame").returncode, 0)


class Tools(unittest.TestCase):
    def test_another_program_in_a_tools_place_is_refused(self):
        part = {"name": "demo", "version": "1", "sha256": "0" * 64, "probe": "demo", "runs": {n: hashlib.sha256(b"pinned").hexdigest() for n in ("demo", "wrapper")}}
        with tempfile.TemporaryDirectory() as folder, mock.patch.dict(os.environ, {"IMPOSSIBLE_TOOLS": folder}):
            self.assertFalse(bootstrap.installed(part))
            home = bootstrap.home(part)
            home.mkdir()
            for name in part["runs"]:
                (home / name).write_bytes(b"pinned")
            self.assertTrue(bootstrap.installed(part))
            for name in part["runs"]:
                (home / name).write_bytes(b"other")
                self.assertRaisesRegex(ValueError, "is not the pinned demo", bootstrap.installed, part)
                (home / name).write_bytes(b"pinned")

    def test_every_compiler_program_the_build_names_is_pinned(self):
        text = (ROOT / "cmake/toolchain.cmake").read_text(encoding="utf-8")
        named = set(re.findall(r"/bin/([a-z0-9_-]+)", text))
        self.assertGreater(len(named), 3)
        for part in json.loads((ROOT / "toolchain.json").read_text(encoding="utf-8"))["linux"]:
            if part["name"] == "llvm-mingw":
                self.assertEqual(named - {Path(n).name for n in part["runs"]}, set())


if __name__ == "__main__":
    unittest.main()
