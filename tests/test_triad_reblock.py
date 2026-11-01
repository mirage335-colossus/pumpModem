#!/usr/bin/env python3
"""Exact sufficient-statistic reblocking checks."""
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from triad_state_workspace import BlockWorkspace, WorkspaceError

SPEC = importlib.util.spec_from_file_location("triad_reblock", ROOT/"tools"/"triad-reblock.py")
TRIAD_REBLOCK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRIAD_REBLOCK)
reblock = TRIAD_REBLOCK.reblock


def write_workspace(directory, G, mean, intervals, valid, signal_energy,
                    observation=None, received_energy=None):
    directory = Path(directory)
    b,d,_ = G.shape
    files = {
        "gram.c128": (np.asarray(G,dtype="<c16"), [b,d,d]),
        "mean.c128": (np.asarray(mean,dtype="<c16"), [b,d]),
        "intervals.u64": (np.asarray(intervals,dtype="<u8"), [b,2]),
        "valid_rows.u64": (np.asarray(valid,dtype="<u8"), [b]),
        "signal_energy.f64": (np.asarray(signal_energy,dtype="<f8"), [b]),
    }
    if observation is not None:
        files["observation.c128"]=(np.asarray(observation,dtype="<c16"),[b,d])
    if received_energy is not None:
        files["received_energy.f64"]=(np.asarray(received_energy,dtype="<f8"),[b])
    sizes={}
    for name,(array,_) in files.items():
        array.tofile(directory/name)
        sizes[name]=(directory/name).stat().st_size
    mappings=[[i] for i in range(d)]
    candidates=[{"bit":i%2,"tap_count":1,"arrival_seconds":.01*i,"frequency_hz":0.,
                 "clock_ppm":0.,"frequency_drift_hz_per_second":0.,"taps":[{"delay_seconds":0.,"doppler_hz":0.}]}
                for i in range(d)]
    descriptors=[{"candidate_index":i,"source_bit":i%2,"arrival_seconds":.01*i,
                  "frequency_hz":0.,"clock_ppm":0.,"frequency_drift_hz_per_second":0.,
                  "tap_delay_seconds":0.,"tap_doppler_hz":0.} for i in range(d)]
    manifest={"format":"triad-block-stats-v1","byte_order":"little","complex_type":"complex128",
      "real_type":"float64","integer_type":"uint64","layout":"block,row,column",
      "block_count":b,"dimension":d,"sample_rate":8.,"noise_variance":3.25,
      "sample_count":int(intervals[-1][1]),"block_samples":3,
      "gram_file":"gram.c128","gram_shape":[b,d,d],"mean_file":"mean.c128","mean_shape":[b,d],
      "intervals_file":"intervals.u64","intervals_shape":[b,2],"valid_rows_file":"valid_rows.u64","valid_rows_shape":[b],
      "signal_energy_file":"signal_energy.f64","signal_energy_shape":[b],"column_mapping":mappings,
      "candidate_count":d,"candidates":candidates,"column_descriptors":descriptors,"file_sizes":sizes,
      "stats_noisy":observation is not None,"key_seed":713,"channel_seed":991,"cn0_db_hz":-24,
      "physical_rf_hz":5.8e9,"command_argv":["original","--mode","export-stats"],
      "source_format":{"fine_rate_hz":60,"occupied_bandwidth_hz":225},
      "channel_model":{"name":"independent synthetic test"}}
    if observation is not None:
        manifest.update({"observation_file":"observation.c128","observation_shape":[b,d]})
    if received_energy is not None:
        manifest.update({"received_energy_file":"received_energy.f64","received_energy_shape":[b],
          "received_energy_semantics":"per-block sum |y|^2 of actual noisy samples over valid rows from the same second channel pass; simulation evaluation only"})
    (directory/"manifest.json").write_text(json.dumps(manifest))
    return manifest


class ReblockTests(unittest.TestCase):
    def _raw(self, directory):
        A=np.array([[1+1j,2-.1j],[.2+2j,-1+.4j],[3-.3j,.8+1j],
                    [-.7+1j,2.3-.5j],[1.2-.8j,.6+2j],[2.2+1.1j,-1.3+.2j],
                    [.1-2j,1.8+.6j],[-1.1+.3j,.2-1.5j]],dtype=np.complex128)
        s=np.array([.2+1j,9+3j,-.1+.8j,1.7-.4j,7-6j,-.8+1.4j,2.1+.3j,99+3j])
        y=s+np.array([.4-.7j,5+2j,-1.3+.2j,.1+.3j,-8+7j,1.1-.9j,.7+.5j,4j])
        mask=np.array([1,0,1,1,1,0,1,0],dtype=bool)
        intervals=np.array([[0,3],[3,6],[6,8]],dtype=np.uint64)
        G=[];mean=[];obs=[];signal=[];received=[];valid=[]
        for first,stop in intervals:
            rows=mask[first:stop]; basis=A[first:stop][rows]
            G.append(basis.conj().T@basis)
            mean.append(basis.conj().T@s[first:stop][rows])
            obs.append(basis.conj().T@y[first:stop][rows])
            signal.append(float(np.sum(np.abs(s[first:stop][rows])**2)))
            received.append(float(np.sum(np.abs(y[first:stop][rows])**2)))
            valid.append(int(rows.sum()))
        write_workspace(directory,np.asarray(G),np.asarray(mean),intervals,np.asarray(valid),
                        np.asarray(signal),np.asarray(obs),np.asarray(received))
        return {"G":np.asarray(G),"mean":np.asarray(mean),"obs":np.asarray(obs),
                "signal":np.asarray(signal),"received":np.asarray(received),"valid":np.asarray(valid)}

    def test_factor_two_matches_independent_raw_sample_sums(self):
        with tempfile.TemporaryDirectory() as temp:
            src=Path(temp)/"source";out=Path(temp)/"reblocked";src.mkdir()
            raw=self._raw(src)
            result=reblock(src,out,factor=2,memory_mib=2)
            self.assertEqual((result["block_count"],result["block_samples"],result["sample_count"]),(2,6,8))
            np.testing.assert_array_equal(np.fromfile(out/"intervals.u64",dtype="<u8").reshape(-1,2),[[0,6],[6,8]])
            ws=BlockWorkspace(out);self.assertEqual(ws.block_count,2)
            checks=(("gram.c128","<c16",(2,2,2),[raw["G"][0]+raw["G"][1],raw["G"][2]]),
                     ("mean.c128","<c16",(2,2),[raw["mean"][0]+raw["mean"][1],raw["mean"][2]]),
                     ("observation.c128","<c16",(2,2),[raw["obs"][0]+raw["obs"][1],raw["obs"][2]]),
                     ("signal_energy.f64","<f8",(2,),[raw["signal"][:2].sum(),raw["signal"][2]]),
                     ("received_energy.f64","<f8",(2,),[raw["received"][:2].sum(),raw["received"][2]]),
                     ("valid_rows.u64","<u8",(2,),[raw["valid"][:2].sum(),raw["valid"][2]]))
            for name,dtype,shape,expected in checks:
                np.testing.assert_allclose(np.fromfile(out/name,dtype=dtype).reshape(shape),expected)
            for key in ("noise_variance","key_seed","channel_seed","physical_rf_hz","command_argv","source_format","channel_model"):
                srcm=json.loads((src/"manifest.json").read_text())
                self.assertEqual(result[key],srcm[key])
            provenance=result["transform_provenance"][-1]
            self.assertEqual(provenance["factor"],2)
            self.assertEqual(provenance["source_block_samples"],3)
            self.assertEqual(set(provenance["source_file_sha256"]),set(srcm["file_sizes"]))

    def test_factor_one_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            src=Path(temp)/"source";out=Path(temp)/"identity";src.mkdir()
            self._raw(src)
            original=json.loads((src/"manifest.json").read_text())
            result=reblock(src,out,factor=1,memory_mib=2)
            for name in original["file_sizes"]:
                self.assertEqual((src/name).read_bytes(),(out/name).read_bytes(),name)
            self.assertEqual(result["sample_count"],original["sample_count"])
            self.assertEqual(result["noise_variance"],original["noise_variance"])
            self.assertEqual(result["source_format"],original["source_format"])
            self.assertEqual(result["channel_model"],original["channel_model"])

    def test_optional_fields_and_fresh_output_guard(self):
        with tempfile.TemporaryDirectory() as temp:
            src=Path(temp)/"source";out=Path(temp)/"out";src.mkdir()
            self._raw(src)
            for name in ("observation.c128","received_energy.f64"):(src/name).unlink()
            m=json.loads((src/"manifest.json").read_text())
            for key in ("observation_file","observation_shape","received_energy_file","received_energy_shape","received_energy_semantics"):
                m.pop(key,None)
            m["stats_noisy"]=False
            m["file_sizes"].pop("observation.c128");m["file_sizes"].pop("received_energy.f64")
            (src/"manifest.json").write_text(json.dumps(m))
            result=reblock(src,out,factor=2,memory_mib=2)
            self.assertNotIn("observation_file",result)
            self.assertNotIn("received_energy_file",result)
            with self.assertRaisesRegex(ValueError,"fresh empty directory"):
                reblock(src,out,factor=2,memory_mib=2)

    def test_memory_guard_and_bad_factors(self):
        with tempfile.TemporaryDirectory() as temp:
            src=Path(temp)/"source";src.mkdir();d=128
            write_workspace(src,np.eye(d,dtype=np.complex128)[None],np.zeros((1,d)),[[0,3]],[3],[1.0])
            with self.assertRaisesRegex(WorkspaceError,"exceeding --memory-mib"):
                reblock(src,Path(temp)/"out",factor=2,memory_mib=1)
            for factor in (0,-1,True):
                with self.assertRaises(ValueError):
                    reblock(src,Path(temp)/f"bad-{factor}",factor=factor,memory_mib=2)


if __name__ == "__main__":
    unittest.main()
