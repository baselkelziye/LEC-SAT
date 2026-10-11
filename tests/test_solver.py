"""Exercise real solver witnesses and adapter failure handling."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import solver


class SolverTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name)
        self.cnf = self.path / 'query.cnf'
        self.model = self.path / 'model.txt'

    def test_sat_unsat_and_stale_model(self):
        self.cnf.write_text('p cnf 3 3\n1 2 0\n-1 3 0\n-2 3 0\n')
        result = solver.solve(self.cnf, self.model)
        self.assertEqual(result.returncode, 10, result.stderr)
        values = solver.read_model(self.model, self.cnf)
        self.assertTrue(values[3])
        self.cnf.write_text('p cnf 1 2\n1 0\n-1 0\n')
        self.assertEqual(solver.solve(self.cnf, self.model).returncode, 20)
        self.assertFalse(self.model.exists())

    def test_unused_variables_and_empty_formula(self):
        for cnf in ('p cnf 5 1\n1 0\n', 'p cnf 3 0\n', 'p cnf 0 0\n'):
            with self.subTest(cnf=cnf):
                self.cnf.write_text(cnf)
                result = solver.solve(self.cnf, self.model)
                self.assertEqual(result.returncode, 10, result.stderr)
                self.assertEqual(len(solver.read_model(self.model, self.cnf)), int(cnf.split()[2]))

    def test_empty_clause_is_unsat(self):
        self.cnf.write_text('p cnf 0 1\n0\n')
        self.assertEqual(solver.solve(self.cnf, self.model).returncode, 20)

    def test_invalid_and_missing_witnesses_are_rejected(self):
        self.cnf.write_text('p cnf 2 2\n1 0\n2 0\n')
        for witness in (None, 'SAT\n-1 2 0\n', 'SAT\n1 -1 2 0\n',
                        'SAT\n1 0\n', 'SAT\n1 2 3 0\n', 'UNSAT\n'):
            with self.subTest(witness=witness):
                self.model.write_text('stale')
                def fake(command, **kwargs):
                    if witness is not None:
                        Path(command[-1]).write_text(witness)
                    return subprocess.CompletedProcess(command, 10, '', '')
                with patch('solver.subprocess.run', side_effect=fake):
                    result = solver.solve(self.cnf, self.model, backend='minisat')
                self.assertEqual(result.returncode, 2)
                self.assertFalse(self.model.exists())

    def test_indeterminate_error_and_timeout_remove_stale_model(self):
        self.cnf.write_text('p cnf 1 1\n1 0\n')
        for status in (0, 1, -9):
            self.model.write_text('stale')
            with patch('solver.subprocess.run', return_value=subprocess.CompletedProcess([], status, '', '')):
                self.assertEqual(solver.solve(self.cnf, self.model).returncode, 2)
            self.assertFalse(self.model.exists())
        self.model.write_text('stale')
        with patch('solver.subprocess.run', side_effect=subprocess.TimeoutExpired('solver', .01)):
            with self.assertRaises(subprocess.TimeoutExpired):
                solver.solve(self.cnf, self.model, timeout=.01)
        self.assertFalse(self.model.exists())

    def test_real_process_timeout(self):
        sleeper = self.path / 'slow.py'
        sleeper.write_text(f'#!{sys.executable}\nimport time\ntime.sleep(10)\n')
        sleeper.chmod(0o755)
        self.cnf.write_text('p cnf 1 1\n1 0\n')
        self.model.write_text('stale')
        with patch.dict(solver.BACKENDS, {'minisat': sleeper}), \
             patch.object(sys, 'argv', ['solver.py', str(self.cnf), '--model', str(self.model),
                                       '--solver', 'minisat', '--timeout', '0.05']):
            self.assertEqual(solver.main(), 2)
        self.assertFalse(self.model.exists())


if __name__ == '__main__':
    unittest.main()
