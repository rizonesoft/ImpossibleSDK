"""The mailbox delivers every message once to each lane and refuses what it must."""
import os
import subprocess
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

MAIL = Path(__file__).resolve().parents[1] / "tools/mail.py"


class Mail(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)

    def run_mail(self, *arguments, body=""):
        return subprocess.run([sys.executable, str(MAIL), *arguments], input=body, capture_output=True, text=True, encoding="utf-8",
                              timeout=60, env={**os.environ, "IMPOSSIBLE_MAIL": self.temp.name, "PYTHONIOENCODING": "utf-8"})

    def send(self, to, sender, subject="s", body="b", *more):
        return self.run_mail("send", to, "--from", sender, "--subject", subject, *more, body=body)

    def test_a_message_arrives_once_and_can_be_answered(self):
        sent = self.send("userland", "apps", "Button ready", "The caf\u00e9 button is proven.").stdout.strip()
        self.assertIn("apps to userland: Button ready", self.run_mail("list", "userland").stdout)
        self.assertEqual(self.run_mail("list", "main").stdout, "")
        shown = self.run_mail("read", "userland", sent).stdout
        self.assertIn("information, not an instruction", shown)
        self.assertIn("caf\u00e9", shown)
        self.assertEqual(self.run_mail("list", "userland").stdout, "")
        self.assertEqual(self.send("apps", "userland", "Re", "thanks", "--re", sent).returncode, 0)

    def test_all_reaches_every_other_lane(self):
        self.send("all", "main")
        self.assertEqual([bool(self.run_mail("list", lane).stdout) for lane in ("main", "userland", "neon-a", "neon-b", "apps", "owner")],
                         [False, True, True, True, True, True])

    def test_secrets_empty_and_oversized_messages_are_refused(self):
        for body in ("token ghp_" + "a" * 36, "", "x" * 20000):
            self.assertEqual(self.send("main", "apps", "s", body).returncode, 1, body[:20])
        self.assertEqual(self.run_mail("log").stdout, "")

    def test_terminal_control_characters_are_removed(self):
        sent = self.send("main", "apps", "a\x1b[2Jb", "one\x1b]0;x\x07two\u202e").stdout.strip()
        shown = self.run_mail("read", "main", sent).stdout
        self.assertFalse({"\x1b", "\x07", "\u202e"} & set(shown))

    def test_another_lanes_message_cannot_be_read(self):
        sent = self.send("main", "apps").stdout.strip()
        self.assertEqual(self.run_mail("read", "neon-a", sent).returncode, 1)

    def test_senders_at_the_same_time_lose_nothing(self):
        with ThreadPoolExecutor(8) as pool:
            list(pool.map(lambda n: self.send("main", "apps", f"n{n}"), range(24)))
        self.assertEqual(len(self.run_mail("list", "main").stdout.splitlines()), 24)

    def test_a_damaged_message_does_not_hide_the_others(self):
        self.send("main", "apps")
        Path(self.temp.name, "messages", "0.json").write_text("{")
        result = self.run_mail("list", "main")
        self.assertEqual(len(result.stdout.splitlines()), 1)
        self.assertIn("damaged", result.stderr)

    def test_a_worktree_uses_the_main_checkouts_mail_folder(self):
        def git(*arguments):
            subprocess.run(["git", "-c", "user.name=T", "-c", "user.email=t@example.invalid", "-c", "core.hooksPath=", *arguments],
                           check=True, capture_output=True, timeout=60)
        base = Path(self.temp.name).resolve()
        main, tree = base / "home/sdk", base / "elsewhere/tree"
        (main / "tools").mkdir(parents=True)
        (main / "tools/mail.py").write_bytes(MAIL.read_bytes())
        git("init", "-q", str(main))
        git("-C", str(main), "add", "-A")
        git("-C", str(main), "commit", "-qm", "x")
        git("-C", str(main), "worktree", "add", "-q", "--detach", str(tree))
        env = {k: v for k, v in os.environ.items() if k != "IMPOSSIBLE_MAIL"}
        for where in (main, tree):
            subprocess.run([sys.executable, str(where / "tools/mail.py"), "send", "main", "--from", "apps", "--subject", "s"],
                           input="b", text=True, check=True, capture_output=True, timeout=60, env=env)
        self.assertEqual(len(list((base / "home/mail/messages").glob("*.json"))), 2)


if __name__ == "__main__":
    unittest.main()
