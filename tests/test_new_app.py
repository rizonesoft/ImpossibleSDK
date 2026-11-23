"""A refused start leaves nothing behind, so it can be repeated."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "tools/new_app.py"


class Refused(unittest.TestCase):
    def test_nothing_is_created(self):
        with tempfile.TemporaryDirectory() as folder:
            for extra, reason in ((["--kits", folder, "--goal", "A goal."], "not a Conclave-SDK checkout"),
                                  (["--kits", folder], "go together"), (["--goal", "A goal."], "go together")):
                done = subprocess.run([sys.executable, str(TOOL), str(Path(folder, "Demo")), "--label", "Demo", "--tier", "1", *extra],
                                      capture_output=True, text=True, timeout=60)
                self.assertNotEqual(done.returncode, 0)
                self.assertIn(reason, done.stderr)
                self.assertFalse(Path(folder, "Demo").exists(), extra)


if __name__ == "__main__":
    unittest.main()
