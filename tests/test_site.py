"""The site's own checks see what they are there to see."""
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import bootstrap

BUILD = Path(os.environ.get("IMPOSSIBLE_BUILD", ROOT / "build" / bootstrap.host()))
CLANG = next(bootstrap.home(p) for p in bootstrap.components() if p["name"] == "llvm-mingw") / "bin" / ("clang.exe" if os.name == "nt" else "clang")
KEEP = ("abi", "brand", "cmake", "docs", "include", "reference", "samples", "scripts", "site", "src", "tests", "todo", "tools", "ui", ".conclave", "licenses")


@unittest.skipUnless(CLANG.is_file() and not str(ROOT).startswith("\\\\"), "needs the pinned compiler: the build in WSL or on Windows runs this")
class Site(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        cls.copy = Path(cls.folder.name, "sdk")
        # Clone local history (isolated from ROOT, no hardlinks for cross-drive Windows temp dirs) so stamped receipt SHAs resolve.
        subprocess.run(["git", "-c", "safe.directory=*", "clone", "-q", "--no-hardlinks", "--no-checkout", str(ROOT), str(cls.copy)],
                       check=True, capture_output=True, timeout=60)
        skip = shutil.ignore_patterns("__pycache__", "node_modules", "build")
        for name in KEEP:
            shutil.copytree(ROOT / name, cls.copy / name, ignore=skip)
        for path in ROOT.iterdir():
            if path.is_file() and path.name != ".git":  # a worktree's .git leads to the real repository
                shutil.copy2(path, cls.copy)
        cls.git("config", "user.name", "test")
        cls.git("config", "user.email", "test@example.invalid")
        cls.git("config", "core.autocrlf", "false")
        cls.commit()

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    @classmethod
    def git(cls, *command):
        subprocess.run(["git", "-c", "core.hooksPath=/nowhere", *command], cwd=cls.copy, check=True, capture_output=True, timeout=60)

    @classmethod
    def commit(cls):
        cls.git("add", "-A")
        cls.git("commit", "-q", "--allow-empty", "-m", "x")

    def build(self, *more, examples=False):
        command = [sys.executable, "-B", str(ROOT / "tools/site/build.py"), "--root", str(self.copy), "--no-run", *(() if examples else ("--no-examples",)), *(more or ("--check",))]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", start_new_session=os.name != "nt",
                                   env={**os.environ, "IMPOSSIBLE_BUILD": str(BUILD), "PYTHONIOENCODING": "utf-8"})
        try:
            out, err = process.communicate(timeout=600)
        except subprocess.TimeoutExpired:  # the compiler it started goes too
            if os.name == "nt":
                subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True, timeout=60)
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.communicate()
            raise
        return subprocess.CompletedProcess(command, process.returncode, out, err)

    def change(self, path, old, new):
        file = self.copy / path
        text = file.read_text(encoding="utf-8")
        self.assertIn(old, text, path)
        self.addCleanup(file.write_text, text, encoding="utf-8", newline="\n")
        file.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")

    def refused(self, says, **how):
        done = self.build(**how)
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertIn(says, done.stderr)

    def test_as_it_is_it_passes_twice_alike(self):
        sites = []
        for name in ("one", "two"):
            out = Path(self.folder.name, name)
            done = self.build("--out", str(out))
            self.assertEqual(done.returncode, 0, done.stderr)
            sites.append({p.relative_to(out).as_posix(): p.read_bytes() for p in out.rglob("*") if p.is_file()})
        self.assertEqual(sites[0], sites[1])
        self.assertIn("docs/api-file.html", sites[0])

    def test_an_example_that_does_not_build(self):
        self.change("docs/guide/first-window.md", "im_window_run(&window, false)", "im_window_open(&window)")
        self.refused("an example does not build", examples=True)

    def test_an_example_above_its_tier(self):
        self.change("docs/guide/first-window.md", "window tier=1", "window tier=0")
        self.refused("an example does not build", examples=True)

    def test_code_that_no_longer_is_in_its_file(self):
        self.change("samples/window/main.c", "if (event->kind != IM_POINTER_MOVE) return false;", "if (event->kind == IM_KEY_DOWN) return false;")
        self.refused("this part is not in samples/window/main.c")

    def test_code_nobody_checks(self):
        self.change("docs/guide/first-window.md", "```c program=first_window window tier=1", "```c")
        self.refused("say program=, project= or from=")

    def test_a_function_without_a_comment(self):
        self.change("include/impossible/file.h", "/* Deletes a file, not a folder. */\n", "")
        self.refused("im_delete has no comment above it")

    def test_a_function_does_not_take_its_neighbours_comment(self):
        self.change("include/impossible/file.h", "im_result im_delete(const char *path);", "im_result im_delete(const char *path);\nim_result im_delete_all(const char *path);")
        self.refused("im_delete_all has no comment of its own")

    def test_a_new_header_is_read(self):
        path = self.copy / "include/impossible/extra.h"
        self.addCleanup(path.unlink)
        path.write_text("/* Impossible SDK: more. Tier 0. */\n#pragma once\n\nvoid im_more(void);\n", encoding="utf-8", newline="\n")
        self.refused("im_more has no comment above it")

    def test_a_value_without_a_comment(self):
        self.change("include/impossible/file.h", "/* Deletes a file, not a folder. */", "#define IM_MOST 4\n\n/* Deletes a file, not a folder. */")
        self.refused("IM_MOST has no comment above it")

    def test_indented_code_is_refused(self):
        self.change("docs/guide/adding.md", "## What does a change need?", "Text.\n\n    int wrong code\n\n## What does a change need?")
        self.refused("indented code is not checked")

    def test_an_example_twice_or_with_a_path_for_a_name(self):
        self.change("docs/guide/drawing.md", "program=drawn_button", "program=first_window")
        self.refused("a second program called first_window", examples=True)
        self.change("docs/guide/text-and-files.md", "program=copy_file", "program=../copy_file")
        self.refused("not a name for an example")

    def test_a_quoted_file_is_named_as_a_source(self):
        self.change("docs/guide/button.md", ",tests/fixtures/control_shots.c", "")
        self.refused("quotes tests/fixtures/control_shots.c and does not name it")

    def test_a_link_written_another_way(self):
        self.change("site/home.html", '<a class="btn" href="docs/api-base.html">', "<a class='btn' href='docs/api-bases.html'>")
        self.refused("leads nowhere: docs/api-bases.html")

    def test_whole_addresses_of_our_own(self):
        self.change("docs/guide/adding.md", "(site:contributing.html)", "(https://sdk.impossibleos.co/docs/contribute.html)")
        self.refused("leads nowhere: /docs/contribute.html")
        self.change("docs/guide/drawing.md", "(site:controls.html)", "(https://github.com/rizonesoft/ImpossibleSDK/blob/main/ui/none.h)")
        self.refused("no such file: ui/none.h")

    def test_a_link_that_runs_something(self):
        self.change("site/status.json", "(include/impossible/window.h)", "(javascript:alert)")
        self.refused("no link a page may have")

    def test_a_row_without_a_proof(self):
        self.change("site/status.json", '["tests.test_seed.Samples.test_console", "tests.test_seed.Samples.test_base"]', "[]")
        self.refused("a row has an area, what you get, and either tests or work lists")

    def test_the_readme_has_another_size_or_start(self):
        self.change("README.md", "The console sample is 3 KB", "The console sample is 300 KB")
        self.refused("README.md: the small part")
        self.change("README.md", "cd ImpossibleSDK", "cd impossible")
        self.refused("README.md: the start part")

    def test_a_partial_site_is_not_written(self):
        done = subprocess.run([sys.executable, "-B", str(ROOT / "tools/site/build.py"), "--no-run", "--out", str(Path(self.folder.name, "part"))],
                              capture_output=True, text=True, encoding="utf-8", timeout=60)
        self.assertEqual(done.returncode, 2)

    def test_nothing_built_is_not_the_live_site(self):
        done = subprocess.run([sys.executable, "-B", str(ROOT / "tools/site/live.py"), "same", str(Path(self.folder.name, "nowhere")), "--base", "http://127.0.0.1:9"],
                              capture_output=True, text=True, encoding="utf-8", timeout=60)
        self.assertIn("holds no built site", done.stderr)

    def test_a_new_function_reaches_its_page(self):
        self.change("include/impossible/file.h", "/* Deletes a file, not a folder. */", "/* Says hello. */\nvoid im_hello(void);\n\n/* Deletes a file, not a folder. */")
        out = Path(self.folder.name, "new")
        self.assertEqual(self.build("--out", str(out)).returncode, 0)
        self.assertIn('id="im_hello"', (out / "docs/api-file.html").read_text(encoding="utf-8"))

    def test_a_link_to_a_function_that_is_gone(self):
        self.change("ui/draw.h", "void im_fill(", "void im_paint(")
        self.refused("no such part: api-draw.html#im_fill")

    def test_links_that_lead_nowhere(self):
        self.change("docs/guide/drawing.md", "(site:controls.html)", "(site:control.html)")
        self.refused("leads nowhere: control.html")

    def test_a_link_to_a_file_that_is_gone(self):
        self.change("docs/guide/impossible-os.md", "interface-and-apps-design.md)", "interface-design.md)")
        self.refused("no such file")

    def test_a_command_or_option_that_is_gone(self):
        self.change("docs/guide/own-program.md", "--label Hello", "--title Hello")
        self.refused("tools/new_app.py has no --title")
        self.change("docs/guide/install.md", "python tools/bootstrap.py", "python tools/setup.py")
        self.refused("no such command: tools/setup.py")

    def test_the_readme_says_something_else(self):
        self.change("project.json", "Sound out and in", "Sound in and out")
        self.refused("README.md: the tiers part is not what the code says")
        self.assertEqual(self.build("--sync").returncode, 0)
        self.addCleanup(self.build, "--sync")
        self.assertEqual(self.build().returncode, 0)

    def test_a_row_that_works_names_a_test_that_is_there(self):
        self.change("site/status.json", "Samples.test_console", "Samples.test_consoles")
        self.refused("no such test: tests.test_seed.Samples.test_consoles")

    def test_a_described_file_that_is_gone(self):
        self.change("docs/guide/testing.md", "sources=tools/prove.py", "sources=tools/proof.py")
        self.refused("describes a file that is not there")

    def test_a_page_left_behind_is_listed_and_published(self):
        out = Path(self.folder.name, "behind")
        self.change("tools/contribute.py", "import argparse", "import argparse  # changed")
        self.assertEqual(self.build("--out", str(out)).returncode, 0)
        page = (out / "docs/adding.html").read_text(encoding="utf-8")
        self.assertIn("Review needed", page)
        self.assertIn("Review needed", (out / "docs/status.html").read_text(encoding="utf-8"))
        self.assertNotIn("Review needed", (out / "docs/drawing.html").read_text(encoding="utf-8"))

    def test_a_folder_that_is_no_site_is_left_alone(self):
        out = Path(self.folder.name, "mine")
        out.mkdir()
        (out / "keep.txt").write_text("x")
        (out / "CNAME").write_text("example.invalid\n")
        self.assertEqual(self.build("--out", str(out)).returncode, 1)
        self.assertTrue((out / "keep.txt").is_file())


if __name__ == "__main__":
    unittest.main()
