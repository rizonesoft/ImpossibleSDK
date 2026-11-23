"""The plan's windows-host probe says yes only where Windows can run the proofs."""
import json
import os
import platform
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "tools/windows_host.py"


class Probe(unittest.TestCase):
    def test_it_says_no_without_windows_programs(self):
        with tempfile.TemporaryDirectory() as empty:
            done = subprocess.run([sys.executable, str(PROBE)], env={**os.environ, "PATH": empty}, timeout=60)
        self.assertEqual(done.returncode, 1)

    @unittest.skipUnless("microsoft" in platform.release().lower(), "needs WSL")
    def test_it_says_yes_where_this_suite_proves_on_windows(self):
        self.assertEqual(subprocess.run([sys.executable, str(PROBE)], timeout=60).returncode, 0)

    def test_the_plan_uses_it(self):
        plan = json.loads((ROOT / "todo/plan.json").read_text(encoding="utf-8"))
        self.assertEqual(plan["probes"], {"windows-host": ["python3", "tools/windows_host.py"]})


if __name__ == "__main__":
    unittest.main()
