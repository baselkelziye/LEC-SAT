"""Semantic regressions: solve generated queries with the selected SAT solver."""
import importlib.util
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
spec = importlib.util.spec_from_file_location("bmc", ROOT / "bmc.py")
bmc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bmc)

# Two-bit enabled modulo-4 counter, with deliberately shuffled gate records.
COUNTER = """aag 14 2 2 3 10
2
4
6 15
8 27
6
8
28
28 8 6
26 25 23
24 8 5
22 20 4
20 19 17
18 8 6
16 9 7
14 13 11
12 6 5
10 7 4
i0 clk
i1 enable
l0 q[0]
l1 q[1]
o2 bad
"""


class BmcTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)

    def generate(self, text, depth=0, kind="output", index=0):
        aag = self.path / "test.aag"
        aag.write_text(text)
        prefix = self.path / "query"
        result = subprocess.run([str(ROOT / "build/bmc/bmc_gen"), str(aag), str(depth), kind,
                                 str(index), str(prefix)], capture_output=True, text=True)
        return result, prefix

    def solve(self, text, depth=0, kind="output", index=0, extra=()):
        result, prefix = self.generate(text, depth, kind, index)
        self.assertEqual(result.returncode, 0, result.stderr)
        cnf = prefix.with_suffix(".cnf")
        if extra:
            lines = cnf.read_text().splitlines()
            for i, line in enumerate(lines):
                if line.startswith("p "):
                    words = line.split()
                    words[3] = str(int(words[3]) + len(extra))
                    lines[i] = " ".join(words)
            lines.extend(f"{lit} 0" for lit in extra)
            cnf.write_text("\n".join(lines) + "\n")
        model = prefix.with_suffix(".model")
        solved = bmc.solve(cnf, model, timeout=15)
        self.assertIn(solved.returncode, (10, 20), solved.stderr)
        values = bmc.read_model(model, cnf) if solved.returncode == 10 else None
        return solved.returncode == 10, values, prefix

    def test_counter_depths_and_trace(self):
        for depth in range(4):
            sat, values, prefix = self.solve(COUNTER, depth, index=2)
            self.assertEqual(sat, depth == 3)
        states, enables = {}, {}
        for row in prefix.with_suffix(".frames").read_text().splitlines():
            t, kind, i, lit, name = row.split("\t", 4)
            t, i, lit = int(t), int(i), int(lit)
            value = values[abs(lit)] == (lit > 0)
            if kind == "s": states[t] = states.get(t, 0) + (int(value) << i)
            if kind == "i" and i == 1: enables[t] = int(value)
        self.assertEqual(list(states.values()), [0, 1, 2, 3])
        for t in range(3): self.assertEqual(states[t+1], (states[t] + enables[t]) % 4)

    def test_unknown_and_one_initialization(self):
        for init, expected in [("", False), (" 0", False), (" 1", True), (" 2", True)]:
            sat, _, _ = self.solve(f"aag 1 0 1 1 0\n2 2{init}\n2\n")
            self.assertEqual(sat, expected)
        sat, _, _ = self.solve(COUNTER.replace("6 15\n", "6 15 6\n").replace("8 27\n", "8 27 8\n"), index=2)
        self.assertTrue(sat)

    def test_transition_and_negative_property(self):
        # Toggle initialized to 1; bad is NOT Q. Initialization must apply only once.
        toggle = "aag 1 0 1 1 0\n2 3 1\n3\n"
        for depth, expected in [(0, False), (1, True), (2, False)]:
            self.assertEqual(self.solve(toggle, depth)[0], expected)

    def test_constants_and_final_inputs(self):
        for lit in (0, 1):
            self.assertEqual(self.solve(f"aag 0 0 0 1 0\n{lit}\n")[0], bool(lit))
        self.assertTrue(self.solve("aag 1 1 0 1 0\n2\n3\n", 2)[0])

    def test_inputs_can_change_and_registers_are_connected(self):
        result, prefix = self.generate(COUNTER, 4, index=2)
        self.assertEqual(result.returncode, 0)
        entries = [line.split("\t", 4) for line in prefix.with_suffix(".frames").read_text().splitlines()]
        inputs = {int(t): int(lit) for t, kind, i, lit, name in entries if kind == "i" and i == "1"}
        # Three increments with a pause reaches 3 after four transitions.
        extra = [inputs[0], -inputs[1], inputs[2], inputs[3]]
        self.assertTrue(self.solve(COUNTER, 4, index=2, extra=extra)[0])
        # A q_next=d latch cannot disagree with its previous input.
        dff = "aag 2 1 1 1 0\n2\n4 2 4\n4\n"
        result, prefix = self.generate(dff, 1)
        rows = [x.split("\t", 4) for x in prefix.with_suffix(".frames").read_text().splitlines()]
        d0 = next(int(lit) for t,k,i,lit,n in rows if t == "0" and k == "i")
        self.assertFalse(self.solve(dff, 1, extra=[-d0])[0])

    def test_native_property(self):
        self.assertTrue(self.solve("aag 0 0 0 0 0 1\n1\n", kind="bad")[0])

    @unittest.skipUnless(shutil.which("yosys"), "Yosys not installed")
    def test_verilog_initialization_survives_export(self):
        for initial, expected in [("", True), ("initial q = 0;", False), ("initial q = 1;", True)]:
            (self.path / "init.v").write_text(
                "module init_test(input clk, input d, output reg q);\n"
                + initial + "\nalways @(posedge clk) q <= d;\nendmodule\n")
            result = subprocess.run(["yosys", "-Q", "-T", "-p",
                                     "read_verilog init.v; synth -top init_test -flatten; "
                                     "dffunmap; aigmap; write_aiger -ascii -symbols init.aag"],
                                    cwd=self.path, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            sat, _, _ = self.solve((self.path / "init.aag").read_text())
            self.assertEqual(sat, expected)

    def test_runner_checks_earlier_depth_and_bounded_pass(self):
        aag = self.path / "runner.aag"
        def run(text, bound):
            aag.write_text(text)
            return subprocess.run(["python3", str(ROOT / "bmc.py"), str(aag), "--bound", str(bound),
                                   "--output", "0", "--out-dir", str(self.path / "run")],
                                  capture_output=True, text=True, timeout=15)
        # Q is bad at depth 0, then permanently false. Checking only depth 2 misses it.
        result = run("aag 1 0 1 1 0\n2 0 1\n2\n", 2)
        self.assertEqual(result.returncode, 10, result.stderr)
        self.assertIn("depth 0: SAT", result.stdout)
        result = run("aag 0 0 0 1 0\n0\n", 2)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("bounded result only", result.stdout)
        self.assertFalse((self.path / "run/trace.txt").exists())
        report = (self.path / "run/depth_2.unroll.txt").read_text()
        self.assertIn('Frame 2', report)
        self.assertEqual(report.count('(asserted)'), 1)
        sentinel = self.path / 'run/notes.txt'
        sentinel.write_text('keep user notes')
        result = run("aag 1 0 1 1 0\n2 0 1\n2\n", 2)
        self.assertEqual(result.returncode, 10, result.stderr)
        self.assertFalse((self.path / 'run/depth_2.cnf').exists())
        self.assertFalse((self.path / 'run/depth_2.unroll.txt').exists())
        self.assertEqual(sentinel.read_text(), 'keep user notes')
        result = run("aag 0 0 0 0 0\n", 0)
        self.assertEqual(result.returncode, 2)
        self.assertIn("INCOMPLETE", result.stderr)

    def test_reject_bad_models(self):
        invalid = ["aag 0 0 0 0 0 0 1\n",  # unsupported assumptions
                   "aag 1 0 1 1 0\n2 2 3\n2\n",  # invalid init
                   "aag 1 0 0 1 0\n2\n",  # undefined output
                   "aag 1 0 0 1 1\n2\n2 2 1\n",  # cycle
                   "aag 1 1 0 1 0\n2\n",  # truncated
                   "aag 2 2 0 1 0\n2\n2\n2\n"]  # duplicate
        for text in invalid:
            self.assertNotEqual(self.generate(text)[0].returncode, 0)


if __name__ == "__main__":
    unittest.main()
