"""Exercise the gate with synthetic data, never claim performance results."""
import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "compare_benchmarks.py"


class GateTests(unittest.TestCase):
    def run_case(self, samples, *, mismatch=False, duplicate=False, hot=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            environment = dict.fromkeys(
                ("cpu", "os", "compiler", "build_flags", "affinity", "governor"), "fixture")
            for name in ("base", "candidate"):
                (root / f"{name}.json").write_text(json.dumps(environment), encoding="utf-8")
                values = [100] * 5 if name == "base" else samples
                with (root / f"{name}.csv").open("w", newline="", encoding="utf-8") as stream:
                    writer = csv.writer(stream)
                    header = ("mode", "threads", "entries", "key_bytes", "value_bytes",
                                     "batch", "write_permille", "burst", "reads", "writes",
                                     "repeat", "ops_per_second")
                    writer.writerow(header + (("hot_permille",) if hot is not None else ()))
                    for repeat, value in enumerate(values):
                        row = ("cow", 1, 16, 8, 8, 1, 0, 0, 1000, 0,
                               0 if duplicate else repeat, value)
                        writer.writerow(row + ((hot[0 if name == "base" else 1],)
                                               if hot is not None else ()))
            if mismatch:
                (root / "candidate.json").write_text("{}", encoding="utf-8")
            return subprocess.run([sys.executable, str(SCRIPT),
                "--baseline", str(root / "base.csv"),
                "--candidate", str(root / "candidate.csv"),
                "--baseline-env", str(root / "base.json"),
                "--candidate-env", str(root / "candidate.json")],
                capture_output=True, text=True, check=False).returncode

    def test_stable(self):
        self.assertEqual(self.run_case([99, 100, 101, 100, 100]), 0)

    def test_regression(self):
        self.assertEqual(self.run_case([90] * 5), 1)

    def test_hot_workload(self):
        self.assertEqual(self.run_case([100] * 5, hot=(900, 900)), 0)
        self.assertEqual(self.run_case([100] * 5, hot=(0, 900)), 2)

    def test_unstable(self):
        self.assertEqual(self.run_case([50, 100, 150, 200, 250]), 2)

    def test_invalid(self):
        for values in ([100] * 4, [float("nan")] * 5, [0] * 5):
            self.assertEqual(self.run_case(values), 2)
        self.assertEqual(self.run_case([100] * 5, mismatch=True), 2)
        self.assertEqual(self.run_case([100] * 5, duplicate=True), 2)


if __name__ == "__main__":
    unittest.main()
