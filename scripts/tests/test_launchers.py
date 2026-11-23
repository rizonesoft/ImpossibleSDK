"""The launcher profile and its loader, `scripts/lib/launchers.py` (D07 T01 section 2)."""

import ast
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
import unittest.mock

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE.parent / "lib" / "launchers.py"
spec = importlib.util.spec_from_file_location("launchers_under_test", SCRIPT)
launchers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launchers)
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


class Fixture(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.project = pathlib.Path(tmp.name).resolve() / "project"
        (self.project / ".conclave").mkdir(parents=True)
        (self.project / "assets").mkdir()
        (self.project / "assets/icon.png").write_bytes(b"\x89PNG\r\n\x1a\n")
        self.path = self.project / launchers.PROFILE

    def valid(self, **changes):
        profile = {"schema_version": 1, "title": "Example Site", "icon": "assets/icon.png",
                   "agents": ["claude", "codex"], "skip_permissions": True,
                   "links": [{"label": "Dashboard", "mark": "wordpress", "run": "python3 scripts/open-dashboard.py"},
                             {"label": "Site", "mark": "site", "url": "https://example.com/"}]}
        profile.update(changes)
        return profile

    def write(self, profile):
        self.path.write_text(json.dumps(profile) if not isinstance(profile, str) else profile, encoding="utf-8")

    def load(self, profile):
        self.write(profile)
        return launchers.load_profile(self.path)

    def refused(self, profile, *words):
        """The refusal of `profile`, which must name the file and each of `words`."""
        self.write(profile)
        with self.assertRaises(launchers.ProfileError) as caught:
            launchers.load_profile(self.path)
        message = str(caught.exception)
        self.assertTrue(message.startswith(self.path.as_posix() + ": "), message)
        for word in words:
            self.assertIn(word, message)
        return message


class FileTests(Fixture):
    def test_a_valid_profile_comes_back_with_every_value_it_holds(self):
        profile = self.valid()
        self.assertEqual(self.load(profile), profile)

    def test_a_missing_folder_unreadable_or_malformed_file_is_refused_by_name(self):
        with self.assertRaises(launchers.ProfileError) as caught:
            launchers.load_profile(self.path)
        self.assertIn("no such file", str(caught.exception))
        self.path.mkdir()
        with self.assertRaises(launchers.ProfileError) as caught:
            launchers.load_profile(self.path)
        self.assertIn("is a folder", str(caught.exception))
        self.path.rmdir()
        self.path.write_bytes(b"\xff\xfe{")
        with self.assertRaises(launchers.ProfileError) as caught:
            launchers.load_profile(self.path)
        self.assertIn("not UTF-8", str(caught.exception))
        self.refused("{not json", "not JSON")
        self.refused("[]", "a profile is an object")

    @unittest.skipIf(os.name == "nt" or (hasattr(os, "geteuid") and os.geteuid() == 0), "needs POSIX permissions")
    def test_an_unreadable_file_is_refused_by_name(self):
        self.write(self.valid())
        self.path.chmod(0)
        self.addCleanup(self.path.chmod, 0o644)
        with self.assertRaises(launchers.ProfileError) as caught:
            launchers.load_profile(self.path)
        self.assertIn("cannot be read", str(caught.exception))


class KeyTests(Fixture):
    def test_an_unknown_or_missing_key_is_refused(self):
        self.refused(self.valid(colour="blue"), "colour: is not a key")
        profile = self.valid()
        del profile["links"]
        self.refused(profile, "links: is missing")

    def test_schema_version_is_the_integer_1(self):
        for value in (2, "1", True, 1.0, None):
            with self.subTest(value=value):
                self.refused(self.valid(schema_version=value), "schema_version:")

    def test_title_rules(self):
        for value, words in ((None, "must be set"), (7, "must be a string"), ("", "must not be empty"),
                             ("x" * 61, "at most 60"), (" Site", "start or end with a space"),
                             ("Site ", "start or end with a space"), ("Site.", "end with a dot"),
                             ("A/B", "'/'"), ("A:B", "':'"), ('A"B', "'\"'"), ("A|B", "'|'"), ("A\x07B", "'\\x07'"),
                             ("A\x7fB", "'\\x7f'"), ("\u00a0", "only spaces"), ("\u00a0Site", "start or end"),
                             ("Site\t", "start or end")):
            with self.subTest(value=value):
                self.refused(self.valid(title=value), "title:", words)
        self.assertEqual(self.load(self.valid(title="x" * 60))["title"], "x" * 60)

    def test_icon_rules(self):
        (self.project / "assets/folder.png").mkdir()
        (self.project / "assets/icon.svg").write_text("<svg/>", encoding="utf-8")
        outside = self.project.parent / "outside.png"
        outside.write_bytes(b"\x89PNG")
        for value, words in ((None, "must be set"), (3, "must be a string"), ("", "must not be empty"),
                             ("/abs/icon.png", "relative to the project"), ("C:/icon.png", "relative to the project"),
                             ("../outside.png", "inside the project"), ("assets/missing.png", "does not exist"),
                             ("assets/icon.svg", ".png or .ico"), ("assets/folder.png", "is not a file"),
                             # Found by this section's round: rooted on Windows, and a NUL that stat() raises on.
                             ("\\outside.png", "relative to the project"), ("\\..\\outside.png", "relative to"),
                             ("assets\\..\\..\\outside.png", "inside the project"),
                             ("bad\u0000.png", "control character"), ("bad\n.png", "control character")):
            with self.subTest(value=value):
                self.refused(self.valid(icon=value), "icon:", words)
        self.assertEqual(self.load(self.valid(icon="assets/icon.png"))["icon"], "assets/icon.png")
        ico = self.project / "assets/ICON.ICO"
        ico.write_bytes(b"\0\0\1\0")
        self.assertEqual(self.load(self.valid(icon="assets/ICON.ICO"))["icon"], "assets/ICON.ICO")

    def test_an_icon_the_system_cannot_name_is_refused_not_raised(self):
        # stat() raises ValueError, which has no strerror, for a path the system cannot name.
        self.write(self.valid())
        real = pathlib.Path.stat

        def stat(path, *args, **kwargs):
            if path.name == "icon.png":
                raise ValueError("the system cannot name it")
            return real(path, *args, **kwargs)

        with unittest.mock.patch.object(pathlib.Path, "stat", stat):
            with self.assertRaises(launchers.ProfileError) as caught:
                launchers.load_profile(self.path)
        self.assertIn("icon: assets/icon.png cannot be read (the system cannot name it)", str(caught.exception))

    def test_an_icon_reached_through_a_link_is_refused(self):
        real = self.project / "real"
        real.mkdir()
        (real / "icon.png").write_bytes(b"\x89PNG")
        try:
            (self.project / "linked").symlink_to(real, target_is_directory=True)
        except OSError as exc:
            self.skipTest(f"this system makes no symbolic link here ({exc})")
        self.refused(self.valid(icon="linked/icon.png"), "icon:", "reached through a link")

    def test_agents_rules(self):
        for value, words in (([], "at least one"), (["gemini"], "'gemini' is not an agent"),
                             (["claude", "claude"], "named twice"), ({"claude": True}, "must be a list"),
                             ("claude", "must be a list"), ([1], "1 is not an agent")):
            with self.subTest(value=value):
                self.refused(self.valid(agents=value), "agents:", words)
        self.assertEqual(self.load(self.valid(agents=["agy", "codex", "claude"]))["agents"], ["agy", "codex", "claude"])

    def test_skip_permissions_is_a_boolean(self):
        for value in (0, 1, "false", None):
            with self.subTest(value=value):
                self.refused(self.valid(skip_permissions=value), "skip_permissions:")
        self.assertIs(self.load(self.valid(skip_permissions=False))["skip_permissions"], False)


class LinkTests(Fixture):
    def link(self, **fields):
        return self.valid(links=[{"label": "Site", "mark": "site", "url": "https://example.com", **fields}])

    def test_links_is_a_list_of_objects_with_label_mark_and_one_of_run_or_url(self):
        self.refused(self.valid(links={"label": "Site"}), "links: must be a list")
        self.refused(self.valid(links=["Site"]), "links[1]: must be an object")
        self.refused(self.valid(links=[{"label": "Site", "mark": "site"}]), "links[1]: is missing run or url")
        self.refused(self.valid(links=[{"label": "Site", "url": "https://example.com"}]), "links[1]: is missing mark")
        self.refused(self.link(run="x"), "links[1]: has both run and url")
        self.refused(self.link(colour="blue"), "links[1]: colour is not a key")

    def test_label_rules(self):
        for value, words in ((5, "must be a string"), ("", "must not be empty"), ("x" * 31, "at most 30"),
                             ("A/B", "'/'"), ("Claude", "an agent's link"), ("codex", "an agent's link"),
                             ("Antigravity", "an agent's link"), ("Nightly campaign", "ends in campaign"),
                             ("Run Campaign", "ends in campaign"), ("Agy", "an agent's icon"),
                             ("---", "no letter or digit"), ("\u00a0", "only spaces")):
            with self.subTest(value=value):
                self.refused(self.link(label=value), "links[1].label:", words)
        self.assertEqual(self.load(self.link(label="x" * 30))["links"][0]["label"], "x" * 30)

    def test_labels_are_unique_by_case_and_by_icon_slug(self):
        two = [{"label": "Admin", "mark": "site", "url": "https://example.com"},
               {"label": "ADMIN", "mark": "site", "url": "https://example.com/b"}]
        self.refused(self.valid(links=two), "links[2].label:", "used by links[1]")
        two[1]["label"] = "Admin Panel"
        two.append({"label": "Admin-Panel", "mark": "wordpress", "run": "x"})
        self.refused(self.valid(links=two), "links[3].label:", "'admin-panel', as links[2] does")

    def test_mark_and_run_rules(self):
        self.refused(self.link(mark="laravel"), "links[1].mark:", "'laravel' is not a mark")
        run = {"label": "Dash", "mark": "wordpress"}
        for value in ("", "   ", "a\nb", "a\rb", 7):
            with self.subTest(value=value):
                self.refused(self.valid(links=[dict(run, run=value)]), "links[1].run:")
        self.assertEqual(self.load(self.valid(links=[dict(run, run="python3 x.py")]))["links"][0]["run"], "python3 x.py")
        for flag in (True, False):
            loaded = self.load(self.valid(links=[dict(run, run="python3 x.py", wsl=flag)]))["links"][0]
            self.assertIs(loaded["wsl"], flag)
        for bad in ("true", 1, None):
            with self.subTest(wsl=bad):
                self.refused(self.valid(links=[dict(run, run="python3 x.py", wsl=bad)]), "links[1].wsl:")
        self.refused(self.link(wsl=True), "links[1]:", "only for a run link")

    def test_url_rules(self):
        for value in ("http://example.com", "ftp://example.com", "https://", "https://exa mple.com",
                      "https://example.com/a b", "https://example.com\n", "example.com", "http://example.test.evil",
                      7):
            with self.subTest(value=value):
                self.refused(self.link(url=value), "links[1].url:")
        for value in ("https://example.com", "https://example.com:8443/wp-admin/?a=1#b", "http://localhost",
                      "http://localhost:8080/x", "http://shop.test", "http://my-app.site/path"):
            with self.subTest(value=value):
                self.assertEqual(self.load(self.link(url=value))["links"][0]["url"], value)


class ShowTests(Fixture):
    def run_show(self, *args, script=SCRIPT, cwd=None):
        return subprocess.run([sys.executable, str(script), "show", *args], capture_output=True, text=True,
                              timeout=60, cwd=cwd, creationflags=NO_WINDOW)

    def test_show_prints_the_profile_as_one_line_of_json(self):
        profile = self.valid(title="Caf\u00e9 Site")
        self.write(profile)
        done = self.run_show("--root", str(self.project))
        self.assertEqual((done.returncode, done.stderr), (0, ""))
        self.assertEqual(len(done.stdout.splitlines()), 1)
        self.assertEqual(json.loads(done.stdout), profile)
        self.assertTrue(done.stdout.isascii())

    def test_an_installed_copy_reads_its_own_project_from_anywhere(self):
        installed = self.project / "scripts/lib/launchers.py"
        installed.parent.mkdir(parents=True)
        shutil.copyfile(SCRIPT, installed)
        profile = self.valid()
        self.write(profile)
        elsewhere = self.project.parent / "elsewhere"
        elsewhere.mkdir()
        done = self.run_show(script=installed, cwd=elsewhere)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(json.loads(done.stdout), profile)

    def test_powershell_reads_the_profile_back_through_convertfrom_json(self):
        # The shortcut script (section 5) reads `show` this way; its values must arrive whole.
        pwsh = shutil.which("pwsh") or shutil.which("powershell")
        if pwsh is None:
            self.skipTest("no PowerShell on this host")
        profile = self.valid(title="Caf\u00e9 Site")
        self.write(profile)
        # PowerShell writes to a pipe in the console's code page unless told otherwise.
        command = ("[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false); "
                   f"& '{sys.executable}' '{SCRIPT}' show --root '{self.project}' | ConvertFrom-Json | "
                   "ConvertTo-Json -Depth 5 -Compress")
        done = subprocess.run([pwsh, "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", command],
                              capture_output=True, text=True, encoding="utf-8", timeout=120, creationflags=NO_WINDOW)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(json.loads(done.stdout), profile)

    @unittest.skipIf(os.name == "nt" or (hasattr(os, "geteuid") and os.geteuid() == 0), "needs POSIX permissions")
    def test_show_refuses_an_unreadable_profile_with_exit_1_and_no_traceback(self):
        self.write(self.valid())
        self.path.chmod(0)
        self.addCleanup(self.path.chmod, 0o644)
        done = self.run_show("--root", str(self.project))
        self.assertEqual((done.returncode, done.stdout), (1, ""))
        self.assertIn("cannot be read", done.stderr)
        self.assertNotIn("Traceback", done.stderr)

    def test_show_refuses_on_stderr_with_exit_1_and_no_traceback(self):
        cases = [("malformed", lambda: self.write(self.valid(title=None)), "title: must be set"),
                 ("folder", lambda: self.path.mkdir(), "is a folder"),
                 ("missing", lambda: None, "no such file")]
        for name, make, words in cases:
            with self.subTest(case=name):
                if self.path.is_dir():
                    self.path.rmdir()
                elif self.path.exists():
                    self.path.unlink()
                make()
                done = self.run_show("--root", str(self.project))
                self.assertEqual((done.returncode, done.stdout), (1, ""))
                self.assertIn(words, done.stderr)
                self.assertNotIn("Traceback", done.stderr)



class TitleTests(Fixture):
    """`title`, which the unattended kit's campaign links read (D07 T01 section 7)."""

    def run_title(self):
        return subprocess.run([sys.executable, str(SCRIPT), "title", "--root", str(self.project)], capture_output=True,
                              text=True, timeout=60, creationflags=NO_WINDOW)

    def test_a_titled_profile_gives_its_title_as_json(self):
        self.write(self.valid(title="Café Site"))
        self.assertEqual(launchers.campaign_title(self.project), "Café Site")
        done = self.run_title()
        self.assertEqual((done.returncode, done.stderr), (0, ""))
        self.assertEqual(json.loads(done.stdout), "Café Site")
        self.assertTrue(done.stdout.isascii())

    def test_no_profile_and_the_installed_template_give_null(self):
        # The kit's own template in the SDK; an installed project's copy may already be filled in, so it gets the
        # template's text as the kit ships it.
        shipped = HERE.parents[1] / ".conclave" / "launchers.json"
        template = shipped.read_text(encoding="utf-8") if HERE.parents[1].name == "templates" else json.dumps(
            {"schema_version": 1, "title": None, "icon": None, "agents": ["claude"], "skip_permissions": False,
             "links": []})
        for name, make in (("missing", lambda: None),
                           ("the kit's template", lambda: self.write(template)),
                           ("title unset, the rest filled in", lambda: self.write(self.valid(title=None)))):
            with self.subTest(case=name):
                self.path.unlink(missing_ok=True)
                make()
                self.assertIsNone(launchers.campaign_title(self.project))
                done = self.run_title()
                self.assertEqual((done.returncode, done.stdout.strip(), done.stderr), (0, "null", ""))

    def test_any_other_refused_profile_is_refused_with_the_loaders_message(self):
        for name, profile, words in (("bad title", self.valid(title=" x"), "title: must not start or end"),
                                     ("titled, icon unset", self.valid(icon=None), "icon: must be set"),
                                     ("not JSON", "{", "not JSON"),
                                     ("title unset, the rest truncated", '{"title": null}', "schema_version: is missing"),
                                     ("title unset, agents wrong", self.valid(title=None, agents=["x"]),
                                      "agents: 'x' is not an agent"),
                                     ("not an object", "[]", "a profile is an object")):
            with self.subTest(case=name):
                self.write(profile)
                with self.assertRaises(launchers.ProfileError):
                    launchers.campaign_title(self.project)
                done = self.run_title()
                self.assertEqual((done.returncode, done.stdout), (1, ""))
                self.assertIn(words, done.stderr)
                self.assertNotIn("Traceback", done.stderr)


class WindowlessTest(unittest.TestCase):
    """Every process the kit starts, from its scripts and from its tests, opens no console window on Windows (the
    owner's rule of 2026-11-12: a launcher test never opens a window). D07 T01 section 3 moved the renderer and its
    tests here, out of the unattended kit's check."""

    SCRIPTS = HERE.parent
    # The kit's own Python files: in an installed project scripts/ also holds the other kits' files, which are theirs.
    KIT_FILES = ("campaign-icon.py", "lib/launchers.py", "tests/test_campaign_icon.py", "tests/test_launchers.py",
                 "tests/test_project_shortcuts.py")

    @staticmethod
    def starts(tree):
        """Each start through subprocess.run or subprocess.Popen, with the call whose keywords configure it: the call
        itself, or the callback registration it is handed to (`addCleanup(subprocess.run, ...)`), or None for an alias
        (`start = subprocess.run`), which hides its starts."""
        parents = {child: node for node in ast.walk(tree) for child in ast.iter_child_nodes(node)}
        for node in ast.walk(tree):
            if not (isinstance(node, ast.Attribute) and node.attr in ("run", "Popen")
                    and isinstance(node.value, ast.Name) and node.value.id == "subprocess"):
                continue
            parent = parents.get(node)
            if isinstance(parent, ast.Call) and parent.func is node:
                yield node.lineno, parent
            elif isinstance(parent, ast.Call) and node in parent.args:
                if getattr(parent.func, "attr", "") in ("addCleanup", "callback", "submit"):
                    yield node.lineno, parent
            elif isinstance(parent, (ast.Assign, ast.AnnAssign, ast.keyword, ast.Return)):
                yield node.lineno, None

    @staticmethod
    def windowless_flag(call) -> bool:
        """Whether a start passes the no-window flag: `creationflags=windowless()` or `creationflags=NO_WINDOW`."""
        flags = [k.value for k in (call.keywords if call else []) if k.arg == "creationflags"]
        if not flags:
            return False
        value = flags[0]
        if isinstance(value, ast.Call):
            return getattr(value.func, "id", "") == "windowless" or getattr(value.func, "attr", "") == "windowless"
        if isinstance(value, ast.BinOp) and isinstance(value.op, ast.BitOr):   # NO_WINDOW | SUSPENDED: a job's child
            return any(isinstance(side, ast.Name) and side.id == "NO_WINDOW" for side in (value.left, value.right))
        return isinstance(value, ast.Name) and value.id == "NO_WINDOW"

    def problems(self, text: str, name: str) -> list[str]:
        lines = text.splitlines()
        found = []
        for line, call in self.starts(ast.parse(text)):
            if "window-exempt:" in lines[line - 1]:
                continue  # the reason is on the line, as the owner's launcher-test hook asks
            if not self.windowless_flag(call):
                found.append(f"{name}:{line} starts a process without the no-window flag")
        return found

    def test_the_kit_file_list_is_complete_in_the_kit_itself(self):
        if self.SCRIPTS.parent.name != "templates":
            self.skipTest("an installed copy: scripts/ holds other kits' files too")
        found = sorted(p.relative_to(self.SCRIPTS).as_posix() for p in self.SCRIPTS.rglob("*.py")
                       if "__pycache__" not in p.parts)
        self.assertEqual(found, sorted(self.KIT_FILES), "a kit file is missing from WindowlessTest.KIT_FILES")

    def test_every_process_the_kit_starts_gets_no_window(self):
        problems, seen = [], 0
        for name in self.KIT_FILES:
            text = (self.SCRIPTS / name).read_text(encoding="utf-8")
            seen += sum(1 for _ in self.starts(ast.parse(text)))
            problems += self.problems(text, name)
        self.assertEqual(problems, [])
        self.assertGreaterEqual(seen, 4)

    def test_the_check_names_a_start_without_the_flag(self):
        for source in ("subprocess.run(['x'], timeout=30)\n", "t.addCleanup(subprocess.run, ['x'])\n",
                       "subprocess.Popen(['x'], creationflags=0)\n", "start = subprocess.run\n"):
            with self.subTest(source=source):
                self.assertEqual(self.problems(source, "sample.py"),
                                 ["sample.py:1 starts a process without the no-window flag"])
        self.assertEqual(self.problems("subprocess.run(['x'], creationflags=NO_WINDOW)\n", "ok.py"), [])
        self.assertEqual(self.problems("subprocess.run(['x'], creationflags=windowless())\n", "ok.py"), [])
        self.assertEqual(self.problems("subprocess.Popen(['x'], creationflags=NO_WINDOW | SUSPENDED)\n", "ok.py"), [])
        self.assertEqual(self.problems("subprocess.Popen(['x'], creationflags=SUSPENDED | 4)\n", "x.py"),
                         ["x.py:1 starts a process without the no-window flag"])


if __name__ == "__main__":
    unittest.main()
