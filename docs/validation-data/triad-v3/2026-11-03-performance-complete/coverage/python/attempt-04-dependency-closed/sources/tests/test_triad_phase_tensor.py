#!/usr/bin/env python3
"""Probability representation checks and optional native tensor/graph checks."""
import argparse
import json
from pathlib import Path
import sys

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_phase_tensor import (TENSOR_MODES, TensorPhaseFrequencyLikelihood,
    cpu_frequency_reference, cpu_frequency_advected_reference, frequency_contract,
    frequency_contract_advected, operand_diagnostics, _cpu_advected_values)
from triad_phase_reduction import Fp64ReductionGraphLikelihood
from triad_phase_frequency import PhaseFrequencyModel


def operands(frequencies=43, phases=65):
    generator = torch.Generator().manual_seed(781401)
    transition = torch.rand((frequencies,frequencies), generator=generator)
    transition /= transition.sum(-1, keepdim=True)
    values = torch.rand((2,2,3,frequencies,phases), generator=generator)
    values /= values.sum((-3,-2,-1), keepdim=True)
    return values, transition


def advection_transition(values, transition, aligned=False):
    frequency,phases=values.shape[-2:]
    generator=torch.Generator().manual_seed(781419)
    # Arbitrary non-affine maps ensure the fusion does not assume a circular
    # shift or adjacent interpolation index.  Physical receiver maps are a
    # supported special case of this full geometry.
    first=torch.stack([torch.randperm(phases,generator=generator) for _ in range(frequency)])
    second=torch.stack([torch.randperm(phases,generator=generator) for _ in range(frequency)])
    fraction=torch.linspace(0.,1.,frequency)
    return {'frequency':transition,'index0':first,'index1':second,
            'fraction':fraction,'optimized_integral':aligned}


def independent_advection(values, transition):
    pieces=[]
    for f in range(values.shape[-2]):
        first=values[...,f,:].index_select(-1,transition['index0'][f])
        if transition['optimized_integral']:pieces.append(first)
        else:
            second=values[...,f,:].index_select(-1,transition['index1'][f])
            fraction=transition['fraction'][f]
            pieces.append(first*(1-fraction)+second*fraction)
    return torch.stack(pieces,-2)


def cpu():
    values, transition = operands()
    exact = torch.einsum('...fp,fg->...gp', values.double(), transition.double())
    assert torch.allclose(cpu_frequency_reference(values,transition),exact,rtol=2e-15,atol=1e-16)
    errors = {}
    for mode in TENSOR_MODES:
        actual = cpu_frequency_reference(values,transition,mode)
        assert actual.shape == values.shape and actual.dtype == torch.float64
        assert bool(torch.isfinite(actual).all()) and float(actual.min()) >= 0
        errors[mode] = float((actual-exact).abs().max())
        d = operand_diagnostics(values,transition,mode)
        assert d['state_representation_nonfinite_values'] == 0
    assert errors['bf16-split2'] < errors['bf16']/50
    assert errors['tf32x3'] == errors['strict-padded']
    # Each phase column has its own scale.  A weak quadrature must not borrow
    # a strong quadrature's scale, and all-zero columns must remain finite.
    dynamic = values.clone()
    dynamic[:,:,0] *= 1e-20
    dynamic[:,:,1,:,:,] *= 1e-35
    dynamic[:,:,2,:,:] = 0
    d = operand_diagnostics(dynamic,transition,'fp16-scaled')
    assert d['state_representation_nonfinite_values'] == 0
    assert d['state_zeroed_mass_fraction'] < 1e-5
    scaled = cpu_frequency_reference(dynamic,transition,'fp16-scaled')
    unscaled_half = torch.matmul(transition.half().double().T,dynamic.half().double())
    assert float(scaled[:,:,0].sum()) > 0 and float(unscaled_half[:,:,0].sum()) == 0
    assert bool(torch.isfinite(scaled).all()) and bool((scaled[:,:,2] == 0).all())
    # Uniform and nearly concentrated inputs cover row-mass changes without
    # concealing them through an implicit renormalization in the product.
    concentrated = torch.zeros_like(values);concentrated[...,0,0] = 1.
    for source in (torch.full_like(values,1/values[0,0].numel()),concentrated):
        for mode in TENSOR_MODES:
            actual = cpu_frequency_reference(source,transition,mode)
            assert bool(torch.isfinite(actual).all()) and float(actual.min()) >= 0
    for kwargs in ({'device':'cpu','dtype':torch.float32}, {'device':'cpu','dtype':torch.float64}):
        try: TensorPhaseFrequencyLikelihood(1,2,1,**kwargs)
        except (RuntimeError,ValueError): pass
        else: raise AssertionError('native tensor graph silently ran on CPU')
    try: frequency_contract(values,transition)
    except RuntimeError as error:assert 'CPU fallback is prohibited' in str(error)
    else:raise AssertionError('native tensor product silently fell back toCPU')
    for aligned in (False,True):
        geometry=advection_transition(values,transition,aligned)
        advected=independent_advection(values,geometry)
        assert torch.equal(_cpu_advected_values(values,geometry),advected)
        for mode in TENSOR_MODES:
            assert torch.equal(cpu_frequency_advected_reference(values,geometry,mode),
                               cpu_frequency_reference(advected,transition,mode))
    geometry=advection_transition(values,transition,False)
    after=cpu_frequency_advected_reference(values,geometry,'bf16')
    before=independent_advection(values.bfloat16().float(),geometry)
    before=torch.matmul(transition.bfloat16().double().T,before.double())
    assert float((after-before).abs().max()) > 1e-9
    invalid=dict(geometry);invalid['index0']=geometry['index0'].clone();invalid['index0'][0,0]=-1
    try:cpu_frequency_advected_reference(values,invalid)
    except ValueError:pass
    else:raise AssertionError('invalid advection map accepted')
    # Test property immutability without constructing a CUDA receiver.  Changing
    # either public setting would otherwise leave captured kernels unchanged.
    settings = TensorPhaseFrequencyLikelihood.__new__(TensorPhaseFrequencyLikelihood)
    settings._tensor_mode, settings._tensor_block_columns, settings._tensor_fuse_advection = 'bf16', 128, False
    assert settings.tensor_mode == 'bf16' and settings.tensor_block_columns == 128 and not settings.tensor_fuse_advection
    for name,value in (('tensor_mode','tf32'),('tensor_block_columns',64),('tensor_fuse_advection',True)):
        try: setattr(settings,name,value)
        except AttributeError: pass
        else: raise AssertionError('captured tensor configuration can be mutated')
    return {'status':'passed','checks':10,'representation_max_absolute_errors':errors,'fp16_dynamic_diagnostics':d}


def cuda(device):
    torch.cuda.set_device(device)
    results=[]
    for f,p in ((3,17),(43,65),(43,512),(73,17)):
        values,transition = operands(f,p)
        for mode in TENSOR_MODES:
            reference = cpu_frequency_reference(values,transition,mode)
            actual,kernel = frequency_contract(values.to(device),transition.to(device),mode,return_kernel=True)
            error = float((actual.cpu().double()-reference).abs().max())
            # TF32x3 retains original FP32 operands but changes the product
            # organization.  All modes compare the represented-input reference.
            assert torch.allclose(actual.cpu().double(),reference,rtol=2e-5,atol=5e-8), (f,p,mode,error)
            results.append({'shape':list(values.shape),'mode':mode,'maximum_represented_product_error':error,
                            'registers':getattr(kernel,'n_regs',None),'shared_bytes':getattr(kernel.metadata,'shared',None)})
    fused=[]
    for f,p in ((3,17),(43,65),(43,512),(73,17)):
        values,transition=operands(f,p);native=values.to(device)
        for aligned in (False,True):
            geometry=advection_transition(values,transition,aligned)
            geometry={key:value.to(device) if isinstance(value,torch.Tensor) else value
                      for key,value in geometry.items()}
            separate=independent_advection(native,geometry)
            for mode in ('tf32x3','bf16-split2'):
                expected=frequency_contract(separate,geometry['frequency'],mode)
                actual=frequency_contract_advected(native,geometry,mode)
                error=float((actual-expected).abs().max())
                assert torch.equal(actual,expected),(f,p,aligned,mode,error)
                fused.append({'shape':list(values.shape),'mode':mode,'aligned':aligned,
                              'maximum_fused_separate_error':error,'bit_identical':True})
    model = PhaseFrequencyModel(phase_states=32,relative_phase_states=2,
        frequency_grid_hz=(-.03125,0.,.03125),nominal_dt_seconds=4.,
        phase_diffusion_rad_per_sqrt_second=.03)
    generator=torch.Generator().manual_seed(781411)
    observations=torch.complex(torch.randn((17,2,2,2),generator=generator),
                               torch.randn((17,2,2,2),generator=generator))*.12
    gram=torch.eye(2,dtype=torch.complex64,device=device).expand(2,2,2)*.12
    graph=[]
    for mode in TENSOR_MODES:
        ref=Fp64ReductionGraphLikelihood(2,2,2,model,device=device,dtype=torch.float32,validate_inputs=False)
        receiver=TensorPhaseFrequencyLikelihood(2,2,2,model,device=device,dtype=torch.float32,
            validate_inputs=False,tensor_mode=mode)
        fused_receiver=(TensorPhaseFrequencyLikelihood(2,2,2,model,device=device,dtype=torch.float32,
            validate_inputs=False,tensor_mode=mode,tensor_fuse_advection=True)
            if mode in ('tf32x3','bf16-split2') else None)
        maximum=0.
        fused_maximum=0.
        for i,observation in enumerate(observations):
            if i==9:
                ref.reset();receiver.reset()
                if fused_receiver is not None:fused_receiver.reset()
            r=observation.to(device);g=gram
            if i==5:r=torch.zeros_like(r);g=torch.zeros_like(gram)
            dt=0. if i in (0,9) else (3.7 if i%3 else 4.)
            ref.update(r,g,dt);receiver.update(r,g,dt)
            if fused_receiver is not None:
                fused_receiver.update(r,g,dt)
                fused_maximum=max(fused_maximum,float((receiver.scores()-fused_receiver.scores()).abs().max()))
                assert torch.equal(receiver.weights,fused_receiver.weights),(mode,i,fused_maximum)
                assert torch.equal(receiver.scores(),fused_receiver.scores()),(mode,i,fused_maximum)
            maximum=max(maximum,float((ref.scores()-receiver.scores()).abs().max()))
        assert bool(torch.isfinite(receiver.weights).all())
        assert float(receiver.weights.min()) >= 0
        assert torch.allclose(receiver.weights.sum((-3,-2,-1)),torch.ones((2,2),device=device),atol=2e-6)
        if mode in ('strict-padded','tf32x3','bf16-split2'):assert maximum < 2e-3,(mode,maximum)
        graph.append({'mode':mode,'max_score_difference':maximum,'max_fused_separate_score_error':fused_maximum if fused_receiver is not None else None,
                      'diagnostics':receiver.diagnostics()})
    return {'status':'passed','products':results,'fused_products':fused,'graphs':graph}


def main():
    p=argparse.ArgumentParser();p.add_argument('--device',default='cpu');a=p.parse_args()
    torch.set_num_threads(2);torch.set_float32_matmul_precision('highest');torch.backends.cuda.matmul.allow_tf32=False
    result={'cpu':cpu(),'cuda':{'status':'unexecuted','reason':'native CUDA checks require --device cuda:N'}}
    if a.device.startswith('cuda'):result['cuda']=cuda(torch.device(a.device))
    print(json.dumps(result))


if __name__=='__main__':main()
