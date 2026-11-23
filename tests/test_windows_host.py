"""The plan's windows-host probe says yes only where Windows can run the proofs."""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "tools/windows_host.py"
SIZE = (ROOT / "toolchain.json").stat().st_size
WORKING = {"wslpath": "echo there", "python.exe": f"printf {SIZE}", "pwsh.exe": "exit 0", "dotnet.exe": "echo '10.0.100 [C:\\dotnet]'"}


@unittest.skipIf(os.name == "nt", "the stand-in programs are shell scripts, and Windows needs no probe")
class Probe(unittest.TestCase):
    def probe(self, **changes):
        with tempfile.TemporaryDirectory() as folder:
            for name, does in {**WORKING, **{k.replace("_", "."): v for k, v in changes.items()}}.items():
                if does is not None:
                    path = Path(folder, name)
                    path.write_text(f"#!/bin/sh\n{does}\n", encoding="utf-8")
                    path.chmod(0o755)
            return subprocess.run([sys.executable, str(PROBE)], env={"PATH": folder}, timeout=30).returncode

    def test_yes_when_everything_works(self):
        self.assertEqual(self.probe(), 0)

    def test_no_when_a_program_is_missing_or_fails(self):
        for name in ("wslpath", "python_exe", "pwsh_exe", "dotnet_exe"):
            self.assertEqual(self.probe(**{name: None}), 1, name)
            self.assertEqual(self.probe(**{name: "exit 3"}), 1, name)

    def test_no_when_windows_reads_something_else(self):
        self.assertEqual(self.probe(python_exe="printf 1"), 1)

    def test_no_when_dotnet_is_too_old_for_the_reference(self):
        self.assertEqual(self.probe(dotnet_exe="echo '8.0.400 [C:\\dotnet]'"), 1)
        self.assertEqual(self.probe(dotnet_exe="exit 0"), 1)

    def test_it_answers_before_the_plan_stops_it(self):
        began = time.monotonic()
        self.assertEqual(self.probe(python_exe="exec /bin/sleep 20", dotnet_exe="exec /bin/sleep 20"), 1)
        self.assertLess(time.monotonic() - began, 10)


class Plan(unittest.TestCase):
    def test_the_plan_uses_it(self):
        plan = json.loads((ROOT / "todo/plan.json").read_text(encoding="utf-8"))
        self.assertEqual(plan["probes"], {"windows-host": ["python3", "tools/windows_host.py"]})


if __name__ == "__main__":
    unittest.main()
