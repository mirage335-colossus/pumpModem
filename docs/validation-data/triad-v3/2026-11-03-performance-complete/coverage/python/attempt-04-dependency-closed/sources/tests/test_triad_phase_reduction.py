#!/usr/bin/env python3
"""FP64 reducer invariants and optional native CUDA/graph comparisons."""
import argparse
import json
from pathlib import Path
import sys

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_phase_reduction import (Fp64ReductionGraphLikelihood, chunked_cpu_reference,
                                   reduction_layout, sum_last_three_fp64)
from triad_phase_optimized import GraphPhaseFrequencyLikelihood
from triad_phase_frequency import PhaseFrequencyModel


def cpu():
    generator = torch.Generator().manual_seed(771309)
    for dtype in (torch.float32, torch.float64):
        for shape in ((2, 3, 7), (2, 3, 2, 17, 33), (1, 2, 1, 1, 1025)):
            x = torch.rand(shape, generator=generator, dtype=dtype)
            expected = x.sum(dim=(-3,-2,-1), keepdim=True, dtype=torch.float64)
            actual = chunked_cpu_reference(x, block_size=256)
            assert actual.dtype == torch.float64 and actual.shape == expected.shape
            assert torch.allclose(actual, expected, rtol=3e-15, atol=3e-13)
    x = torch.zeros((1, 2, 1, 1, 4097), dtype=torch.float32)
    x[..., 0] = 1.
    x[..., 1:] = 2.**-30
    expected = 1.+4096*2.**-30
    assert torch.equal(chunked_cpu_reference(x), torch.full((1,2,1,1,1), expected, dtype=torch.float64))
    assert not torch.equal(x.sum((-3,-2,-1), keepdim=True).double(), chunked_cpu_reference(x))
    layout = reduction_layout((8, 2, 24, 43, 512))
    assert layout["scratch_fp64_bytes"] == 8*2*516*8
    assert layout["avoided_full_fp64_intermediate_bytes"] == 8*2*24*43*512*8
    assert layout["logical_intermediate_write_and_read_bytes_avoided"] == 8*2*24*43*512*16
    assert layout["baseline_full_fp64_conversion_copy_expected"] is True
    fp64_layout = reduction_layout((8,2,24,43,512), input_dtype=torch.float64)
    assert fp64_layout["avoided_full_fp64_intermediate_bytes"] == 0
    assert fp64_layout["logical_intermediate_write_and_read_bytes_avoided"] == 0
    assert fp64_layout["baseline_full_fp64_conversion_copy_expected"] is False
    assert fp64_layout["scratch_fp64_bytes"] == layout["scratch_fp64_bytes"]
    assert "not measured" in layout["byte_accounting_scope"]
    try:
        sum_last_three_fp64(x)
    except RuntimeError as e:
        assert "CPU fallback is prohibited" in str(e)
    else:
        raise AssertionError("native CUDA reduction silently substituted CPU")
    for shape in ((0,2,3), (2,3)):
        try: reduction_layout(shape)
        except ValueError: pass
        else: raise AssertionError("invalid shape accepted")
    return {"status":"passed", "checks":7, "production_layout":layout, "fp64_layout":fp64_layout}


def cuda(device):
    torch.cuda.set_device(device)
    generator = torch.Generator().manual_seed(771309)
    reduced = []
    for dtype in (torch.float32, torch.float64):
        for shape in ((2, 3, 7), (2,3,2,17,33), (8,2,24,43,512)):
            x = torch.rand(shape, generator=generator, dtype=dtype).to(device)
            ref = x.sum((-3,-2,-1), keepdim=True, dtype=torch.float64)
            actual = sum_last_three_fp64(x)
            error = float((actual-ref).abs().max())
            assert torch.allclose(actual, ref, rtol=4e-15, atol=2e-11)
            reduced.append({"dtype":str(dtype), "shape":shape, "max_error":error})
    model = PhaseFrequencyModel(phase_states=32, relative_phase_states=4,
        frequency_grid_hz=(-.03125,0.,.03125), nominal_dt_seconds=4.,
        phase_diffusion_rad_per_sqrt_second=.03)
    paired=[]
    for dtype in (torch.float32, torch.float64):
        ref = GraphPhaseFrequencyLikelihood(3,2,2,model,device=device,dtype=dtype,validate_inputs=False)
        actual = Fp64ReductionGraphLikelihood(3,2,2,model,device=device,dtype=dtype,validate_inputs=False)
        gram=torch.eye(2,device=device,dtype=ref.complex_dtype).expand(2,2,2)*.1
        observations=torch.complex(torch.randn((35,3,2,2),generator=generator,dtype=torch.float64),
                                   torch.randn((35,3,2,2),generator=generator,dtype=torch.float64))*.1
        error=0.
        for i,r in enumerate(observations):
            if i==17: ref.reset();actual.reset()
            r=r.to(device);g=gram
            if i in (8,9):r=torch.zeros_like(r);g=torch.zeros_like(gram)
            dt=0. if i in (0,17) else (3.7 if i%3 else 4.)
            ref.update(r,g,dt);actual.update(r,g,dt)
            error=max(error,float((ref.scores()-actual.scores()).abs().max()))
        assert error < (2e-5 if dtype==torch.float32 else 2e-11)
        assert torch.equal(ref.scores().argmax(-1),actual.scores().argmax(-1))
        paired.append({"dtype":str(dtype),"max_score_error":error,"diagnostics":actual.diagnostics()})
    return {"status":"passed","reducers":reduced,"graph_pairs":paired}


def main():
    p=argparse.ArgumentParser();p.add_argument('--device',default='cpu');a=p.parse_args()
    torch.set_num_threads(2);torch.set_float32_matmul_precision('highest')
    torch.backends.cuda.matmul.allow_tf32=False
    result={"cpu":cpu(),"cuda":{"status":"unexecuted","reason":"CPU invocation; native CUDA and graph checks require --device cuda:N"}}
    if a.device.startswith('cuda'):result['cuda']=cuda(torch.device(a.device))
    print(json.dumps(result))


if __name__=='__main__':main()
