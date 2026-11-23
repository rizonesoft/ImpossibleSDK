"""Two programs add to the SDK at once, and what does not pass is not pushed."""
import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
if os.name != "nt":
    import contribute

PASS, FAIL = [sys.executable, "-c", ""], [sys.executable, "-c", "raise SystemExit(1)"]


@unittest.skipIf(os.name == "nt", "the repositories are written in WSL only")
class Contribute(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        clean = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        (self.root / "home").mkdir()
        (self.root / "home/.gitconfig").write_text("[user]\nname = T\nemail = t@example.invalid\n[core]\nhooksPath = /dev/null\n")
        for patch in (mock.patch.dict(os.environ, {**clean, "HOME": str(self.root / "home")}, clear=True),
                      mock.patch.object(contribute, "SDK", self.root / "sdk"), mock.patch.object(contribute, "WORK", self.root / "work")):
            patch.start()
            self.addCleanup(patch.stop)
        self.git(self.root, "init", "-q", "--bare", "-b", "main", "remote.git")
        self.git(self.root, "clone", "-q", "remote.git", "sdk")
        (self.root / "sdk/one").write_text("one")
        self.git(self.root / "sdk", "add", "-A")
        self.git(self.root / "sdk", "commit", "-qm", "one")
        self.git(self.root / "sdk", "push", "-q", "origin", "HEAD:main")
        for name in ("A", "B"):
            (self.root / name).mkdir()
            (self.root / name / "sdk.json").write_text(json.dumps({"sdk": "ImpossibleSDK", "commit": "0" * 40}))

    def git(self, folder, *arguments):
        return subprocess.run(["git", "-C", str(folder), *arguments], capture_output=True, text=True, check=True, timeout=60).stdout.strip()

    def started(self, name, text):
        with mock.patch("builtins.print") as said:
            contribute.start()
        folder = Path(said.call_args[0][0])
        (folder / name).write_text(text)
        self.git(folder, "add", "-A")
        self.git(folder, "commit", "-qm", name)
        return folder

    def main(self):
        return self.git(self.root / "remote.git", "rev-parse", "main")

    def test_both_arrive_and_each_program_is_pinned_to_its_own(self):
        (self.root / "sdk/edited").write_text("the SDK's own run is working here")
        first, second = self.started("a", "a"), self.started("b", "b")
        for name, folder in (("A", first), ("B", second)):
            contribute.finish(folder, self.root / name, PASS)
            self.assertEqual(json.loads((self.root / name / "sdk.json").read_text()), {"sdk": "ImpossibleSDK", "commit": self.main()})
            self.assertFalse(folder.exists())
        self.assertEqual(self.git(self.root / "remote.git", "ls-tree", "--name-only", "main").split(), ["a", "b", "one"])
        self.assertEqual(self.git(self.root / "sdk", "status", "--porcelain"), "?? edited")
        self.git(self.root / "sdk", "cat-file", "-e", self.main())  # the pin can be built at once

    def test_two_at_once_take_turns(self):
        folders = [self.started("a", "a"), self.started("b", "b")]
        slow = [sys.executable, "-c", "import time; time.sleep(0.5)"]
        faults = []

        def run(folder, name):
            try:
                contribute.finish(folder, self.root / name, slow)
            except BaseException as error:
                faults.append(error)

        threads = [threading.Thread(target=run, args=pair) for pair in zip(folders, ("A", "B"))]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(60)
        self.assertEqual(faults, [])
        self.assertEqual(self.git(self.root / "remote.git", "ls-tree", "--name-only", "main").split(), ["a", "b", "one"])

    def test_a_finish_that_stopped_after_the_push_goes_on(self):
        folder = self.started("a", "a")
        real, seen = contribute.git, []

        def stops(where, *arguments):
            seen.append(arguments[0])
            if arguments[0] == "fetch" and "push" in seen:
                raise SystemExit("contribute: git fetch: no network")
            return real(where, *arguments)

        with mock.patch.object(contribute, "git", stops):
            with self.assertRaisesRegex(SystemExit, "no network"):
                contribute.finish(folder, self.root / "A", PASS)
        contribute.finish(folder, self.root / "A", FAIL)  # tested and pushed already
        self.assertEqual(json.loads((self.root / "A/sdk.json").read_text())["commit"], self.main())

    def test_a_commit_made_meanwhile_is_neither_pushed_nor_lost(self):
        folder = self.started("a", "a")
        late = [sys.executable, "-c", f"import subprocess; subprocess.run(['git', '-C', {str(folder)!r}, 'commit', '-q', '--allow-empty', '-m', 'late'], check=True)"]
        with self.assertRaisesRegex(SystemExit, "has a newer commit"):
            contribute.finish(folder, self.root / "A", late)
        self.assertNotIn("late", self.git(self.root / "remote.git", "log", "--format=%s", "main"))
        self.assertIn("late", self.git(folder, "log", "--format=%s"))

    def test_a_merge_is_refused(self):
        folder = self.started("a", "a")
        mine = self.git(folder, "rev-parse", "HEAD")
        self.git(folder, "checkout", "-q", "--detach", "HEAD~1")
        self.git(folder, "commit", "-q", "--allow-empty", "-m", "b")
        self.git(folder, "merge", "-q", "--no-ff", "-m", "merge", mine)
        self.refused(folder, PASS, "make it plain commits")

    def refused(self, folder, test, words, program="A"):
        before, pin = self.main(), (self.root / "A/sdk.json").read_text()
        with self.assertRaisesRegex((SystemExit, subprocess.CalledProcessError), words):
            contribute.finish(folder, self.root / program, test)
        self.assertEqual((self.main(), (self.root / "A/sdk.json").read_text()), (before, pin))

    def test_nothing_is_pushed_that_fails_conflicts_or_is_not_committed(self):
        self.refused(self.started("a", "a"), FAIL, "non-zero exit")
        first, second = self.started("one", "mine"), self.started("one", "theirs")
        contribute.finish(second, self.root / "B", PASS)
        self.refused(first, PASS, "rebase")
        third = self.started("c", "c")
        (third / "c").write_text("changed")
        self.refused(third, PASS, "not committed")
        self.git(third, "checkout", "-q", "c")
        self.git(third, "update-index", "--assume-unchanged", "c")
        (third / "c").write_text("good")  # what is committed is what is tested
        self.refused(third, [sys.executable, "-c", "raise SystemExit(open('c').read() != 'good')"], "non-zero exit")
        other = self.root / "work/foreign"
        self.git(self.root, "init", "-q", "other")
        self.git(self.root / "other", "commit", "-q", "--allow-empty", "-m", "x")
        self.git(self.root / "other", "worktree", "add", "-q", "--detach", str(other))
        self.refused(other, PASS, "not a folder that start made")
        self.refused(self.root / "sdk", PASS, "not a folder that start made")
        self.refused(third, PASS, "is not a program's folder", program="home")


if __name__ == "__main__":
    unittest.main()
