#!/usr/bin/env python3
import sys
from pathlib import Path
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import triad_live_frontend_compare as c


class ExperimentInputGuards(unittest.TestCase):
    def test_stimulus_binding_and_physical_receipt_are_both_required(self):
        plan={'cases':['baseline','edge'],'stimulus_sha256':'input-a'}
        with patch.object(c.campaign,'validate_capture') as check:
            c.validate_experiment_receipt(plan,{'input_sha256':'input-a'},32)
            check.assert_called_once_with({'input_sha256':'input-a'},32)
            with self.assertRaisesRegex(ValueError,'exact transmitted'):
                c.validate_experiment_receipt(plan,{'input_sha256':'input-b'},32)
            with self.assertRaisesRegex(ValueError,'exact transmitted'):
                c.validate_experiment_receipt(plan,{},32)
        with patch.object(c.campaign,'validate_capture',side_effect=ValueError('xrun')):
            with self.assertRaisesRegex(ValueError,'xrun'):
                c.validate_experiment_receipt(plan,{'input_sha256':'input-a'},32)

    def test_unqualified_spread_passband_is_not_silently_accepted(self):
        with patch.object(c.campaign,'validate_capture') as check:
            for cases in ([],['stress'],['baseline','spreading']):
                with self.assertRaisesRegex(ValueError,'flat baseline/edge'):
                    c.validate_experiment_receipt({'cases':cases},{},32)
            check.assert_not_called()


if __name__=='__main__':unittest.main()
