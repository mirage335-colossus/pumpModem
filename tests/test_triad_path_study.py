#!/usr/bin/env python3
"""Small independent-path batching checks for triad-path-study."""
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import tempfile
import unittest
import subprocess

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_state_workspace import BlockWorkspace, WorkspaceError
from test_triad_state_workspace import write_export

spec = importlib.util.spec_from_file_location('path_study', Path(__file__).resolve().parents[1]/'tools/triad-path-study.py')
pathstudy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pathstudy)


def workspace(directory, bit, channel_seed, key_seed, mean):
    Path(directory).mkdir(parents=True, exist_ok=True)
    G = np.broadcast_to(np.array([[1., .25j], [-.25j, 1.2]], dtype=np.complex128), (3,2,2)).copy()
    means = np.broadcast_to(np.asarray(mean, dtype=np.complex128), (3,2)).copy()
    manifest = write_export(directory, G, means, block_samples=8)
    manifest.update(signal_bit=bit, channel_seed=channel_seed, key_seed=key_seed, epoch=1800000000+key_seed)
    Path(directory, 'manifest.json').write_text(json.dumps(manifest))
    return BlockWorkspace(directory)


class PathStudyTests(unittest.TestCase):
    def test_stable_path_noise_and_independent_interval(self):
        manifests = [dict(channel_seed=100+i,key_seed=300+i,epoch=1800000000+i) for i in range(6)]
        full = [pathstudy._path_seed(9,'holdout',m) for m in manifests]
        shards = [[pathstudy._path_seed(9,'holdout',m) for m in manifests[:3]],
                  [pathstudy._path_seed(9,'holdout',m) for m in manifests[3:]]]
        self.assertEqual(full, shards[0]+shards[1])
        self.assertEqual(len(set(full)),6)
        self.assertAlmostEqual(pathstudy._chernoff_interval(300,300)[0], .05**(1/300))
        self.assertAlmostEqual(pathstudy._chernoff_interval(0,300)[1], 1-.05**(1/300))

    def test_independent_paths_batch_matches_separate_receivers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            workspaces = [
                workspace(root/'path0', 0, 11, 21, [1+.2j, .3-.5j]),
                workspace(root/'path1', 1, 12, 22, [-.4+.7j, .8+.1j]),
            ]
            ref = pathstudy._validate_calendar(workspaces)
            selected, labels = pathstudy._selected(workspaces, taps=1)
            seeds = [pathstudy._seed(9, 'development', i) for i in range(2)]
            args = SimpleNamespace(device='cpu', precision='fp64', methods=['gaussian','block-energy'],
                                   coherences=[2., 8.], taps=1)
            batched, _, _ = pathstudy._run_one(args, workspaces, ref, selected, labels, -3., seeds,
                                                signal=True)
            separate = []
            for ws, seed in zip(workspaces, seeds):
                labels_one = np.asarray([ws.candidates[i]['bit'] for i in selected])
                blocks = ws.iter_simulator(seed=seed, batch=1, cn0_db_hz=-3., signal=True)
                scores, _, _ = pathstudy._STUDY.evaluate(ws, blocks, args, selected, labels_one, 1)
                separate.append(scores)
            for method in args.methods:
                np.testing.assert_allclose(batched[method], np.concatenate([item[method] for item in separate]),
                                           rtol=2e-13, atol=2e-13)

            joint = list(pathstudy._batched_blocks(workspaces, seeds, -3., signal=False, actual=False))
            self.assertEqual(joint[0].r.shape, (2, 2))
            self.assertEqual(joint[0].gram.shape, (2, 2, 2))
            self.assertFalse(np.array_equal(joint[0].r[0], joint[0].r[1]))
            self.assertEqual(joint[1].dt_seconds, 8/64)
            with_signal = list(pathstudy._batched_blocks(workspaces, seeds, -3., signal=True, actual=False))
            normalized_truth = np.stack([np.asarray(np.memmap(ws.directory/'mean.c128', mode='r',
                                         dtype='<c16', shape=(ws.block_count,ws.dimension))[0]) /
                                         (ws.sample_rate/10**(-3/10)) for ws in workspaces])
            np.testing.assert_allclose(with_signal[0].r-joint[0].r, normalized_truth,
                                       rtol=1e-13, atol=1e-13)
            workspaces[1].iter_simulator = lambda **kwargs: iter(())
            with self.assertRaises(WorkspaceError):
                next(pathstudy._batched_blocks(workspaces, seeds, -3., signal=False, actual=False))

    def test_rejects_imbalanced_or_reused_channel_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            a = workspace(root/'path0', 0, 11, 21, [1j, .5])
            b = workspace(root/'path1', 0, 11, 22, [1., .2j])
            with self.assertRaises(WorkspaceError):
                pathstudy._validate_path_diversity([a,b])
            c = workspace(root/'path2', 1, 13, 21, [.2j, .5j])
            with self.assertRaises(WorkspaceError):
                pathstudy._validate_path_diversity([a,c])
            self.assertEqual(a.candidates[0]['bit'], b.candidates[0]['bit'])
            b.manifest['oscillator_model'] = {'different': True}
            with self.assertRaises(WorkspaceError):
                pathstudy._selected([a,b], taps=1)

    def test_cli_writes_independent_path_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            a = workspace(root/'p0', 0, 11, 21, [1+.1j, .4-.2j])
            b = workspace(root/'p1', 1, 12, 22, [-.3+.7j, .5+.1j])
            output = root/'study.json'
            script = Path(__file__).resolve().parents[1]/'tools/triad-path-study.py'
            command = [sys.executable, str(script), str(a.directory), str(b.directory),
                       '--output', str(output), '--cn0', '-3', '--methods', 'gaussian', 'block-energy', 'phase-frequency',
                       '--coherences', '4', '--taps', '1', '--phase-states', '8',
                       '--relative-phase-states', '2', '--threads', '2']
            run = subprocess.run(command, text=True, capture_output=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stderr)
            report = json.loads(output.read_text())
            summaries = {row['summary']['method']:row['summary'] for row in report['rows']}
            summary = summaries['gaussian']
            self.assertEqual(summary['independent_physical_paths'], 2)
            self.assertEqual(summary['balanced_signal_bits'], {'0':1,'1':1})
            self.assertEqual(len(report['independent_noise_seeds']), 2)
            self.assertEqual(summaries['block-energy']['independent_physical_paths'], 2)
            self.assertEqual(summaries['phase-frequency']['independent_physical_paths'], 2)
            for result in summaries.values():
                self.assertEqual(result['correct_accepted']+result['wrong_accepted']+result['missed'],2)
                self.assertIn('correct_probability_independent_hoeffding95',result)
                self.assertEqual(set(result['per_bit']),{'0','1'})
            self.assertTrue(output.with_suffix('.scores.npz').is_file())


if __name__ == '__main__':
    import torch
    torch.set_num_threads(2)
    unittest.main()
