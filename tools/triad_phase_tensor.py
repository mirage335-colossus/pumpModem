"""Explicit experimental tensor contraction inside the triad state receiver.

Only the frequency transition product changes.  Every frequency/phase/gain
state remains present, observation storage remains complex64, and the existing
FP64 posterior sum/log evidence and positive phase stencil are retained.
``strict-padded`` is the same-precision organization control.  Other modes
approximate products or their operands and do not inherit an operational
admission guarantee.  In particular, quantizing the fixed transition matrix
perturbs its row sums; no hidden template renormalization is applied.

The CUDA kernel reads the existing [trial, candidate, quadrature, frequency,
phase] layout directly.  It combines all but frequency into wide GEMM columns,
pads only register tiles, and writes the original layout.  Input conversion,
FP16 scaling and component decomposition are inside the measured kernel.
The separately named CPU helpers are diagnostics, never CUDA fallbacks.
"""
from __future__ import annotations

from functools import lru_cache
import math

import torch

from triad_phase_reduction import Fp64ReductionGraphLikelihood


TENSOR_MODES = ("strict-padded", "tf32", "tf32x3", "bf16", "fp16-scaled", "bf16-split2")
BLOCK_COLUMNS = (32, 64, 128, 256)
triton = None
tl = None


def _mode(mode):
    if mode not in TENSOR_MODES:
        raise ValueError(f"tensor_mode must be one of {TENSOR_MODES}")
    return mode


def _shape(values, transition):
    if values.ndim != 5 or any(x < 1 for x in values.shape):
        raise ValueError("frequency values require positive [trial,candidate,quadrature,frequency,phase] shape")
    frequency = values.shape[-2]
    if transition.shape != (frequency, frequency):
        raise ValueError("transition must be [from_frequency,to_frequency]")
    if frequency > 128:
        raise ValueError("tensor experiment supports at most128 frequencies; no states were pruned")
    if values.dtype != torch.float32 or transition.dtype != torch.float32:
        raise ValueError("tensor experiment requires explicit FP32 state and transition storage")
    if not values.is_contiguous() or not transition.is_contiguous():
        raise ValueError("tensor experiment requires contiguous state and transition storage")
    if values.requires_grad or transition.requires_grad:
        raise ValueError("autograd is unsupported by the bounded tensor experiment")
    return frequency, values.shape[-1], math.prod(values.shape[:-2])


@lru_cache(maxsize=1)
def _kernels():
    global triton, tl
    try:
        import triton as compiler
        import triton.language as language
    except ImportError as error:
        raise RuntimeError("tensor frequency contraction requires Triton; no fallback executed") from error
    triton, tl = compiler, language

    @triton.jit
    def contract(source, transition, index0, index1, fraction, output,
                 F: tl.constexpr, P: tl.constexpr,
                 COLUMNS: tl.constexpr, MODE: tl.constexpr,
                 ADVECT: tl.constexpr, ALIGNED: tl.constexpr,
                 BM: tl.constexpr, BN: tl.constexpr, BK: tl.constexpr):
        destination = tl.program_id(0)*BM+tl.arange(0, BM)
        column = tl.program_id(1)*BN+tl.arange(0, BN)
        source_frequency = tl.arange(0, BK)
        # Stored T is [from,to]; GEMM A is its transpose.
        a = tl.load(transition+source_frequency[None, :]*F+destination[:, None],
                    (destination[:, None] < F) & (source_frequency[None, :] < F), other=0.)
        row = column//P
        phase = column % P
        valid_b = (source_frequency[:, None] < F) & (column[None, :] < COLUMNS)
        if ADVECT:
            # The full public index maps are used, including arbitrary
            # non-affine rows.  Interpolation is completed in FP32 before
            # any operand scaling or reduced-precision representation.
            first = tl.load(index0+source_frequency[:, None]*P+phase[None, :],
                            valid_b, other=0)
            b = tl.load(source+row[None, :]*F*P+source_frequency[:, None]*P+first,
                        valid_b, other=0.)
            if not ALIGNED:
                second = tl.load(index1+source_frequency[:, None]*P+phase[None, :],
                                 valid_b, other=0)
                other = tl.load(source+row[None, :]*F*P+source_frequency[:, None]*P+second,
                                valid_b, other=0.)
                part = tl.load(fraction+source_frequency, source_frequency < F, other=0.)
                b = b*(1.-part[:, None])+other*part[:, None]
        else:
            b = tl.load(source+row[None, :]*F*P+source_frequency[:, None]*P+phase[None, :],
                        valid_b, other=0.)
        if MODE == "tf32":
            # Explicit round-to-nearest/even avoids the documented possible
            # truncation bias of an implicit FP32 -> TF32 conversion.  Values
            # in this probability contraction are finite and bounded by one.
            a_bits = a.to(tl.uint32, bitcast=True)
            b_bits = b.to(tl.uint32, bitcast=True)
            a = ((a_bits+4095+((a_bits >> 13) & 1)) & 0xffffe000).to(tl.float32, bitcast=True)
            b = ((b_bits+4095+((b_bits >> 13) & 1)) & 0xffffe000).to(tl.float32, bitcast=True)
            result = tl.dot(a, b, input_precision="tf32", out_dtype=tl.float32)
        elif MODE == "tf32x3":
            result = tl.dot(a, b, input_precision="tf32x3", out_dtype=tl.float32)
        elif MODE == "bf16":
            result = tl.dot(a.to(tl.bfloat16), b.to(tl.bfloat16), out_dtype=tl.float32)
        elif MODE == "fp16-scaled":
            maximum = tl.max(tl.abs(b), axis=0)
            # A normal denominator avoids reciprocal overflow for columns
            # containing only FP32 subnormals.  All-zero columns use one.
            scale = tl.where(maximum > 0., tl.maximum(maximum, 1.1754943508222875e-38), 1.)
            scaled = tl.div_rn(b, scale[None, :])
            result = tl.dot(a.to(tl.float16), scaled.to(tl.float16), out_dtype=tl.float32)
            result = result*scale[None, :]
        elif MODE == "bf16-split2":
            ah = a.to(tl.bfloat16)
            bh = b.to(tl.bfloat16)
            al = (a-ah.to(tl.float32)).to(tl.bfloat16)
            bl = (b-bh.to(tl.float32)).to(tl.bfloat16)
            # All three dot products accumulate into FP32.  The low*low
            # product is deliberately omitted and reported as approximation.
            result = tl.dot(ah, bh, out_dtype=tl.float32)
            result = tl.dot(al, bh, result, out_dtype=tl.float32)
            result = tl.dot(ah, bl, result, out_dtype=tl.float32)
        else:
            result = tl.dot(a, b, input_precision="ieee", out_dtype=tl.float32)
        tl.store(output+row[None, :]*F*P+destination[:, None]*P+phase[None, :], result,
                 (destination[:, None] < F) & (column[None, :] < COLUMNS))

    return contract


def frequency_contract(values, transition, mode="strict-padded", *, block_columns=128,
                       return_kernel=False):
    """CUDA product Y[...,to,phase]=sum_from values[...,from,phase]*T[from,to].

    ``return_kernel`` additionally returns the compiled Triton object for
    outside-timing PTX/metadata inspection.  It must not be used to inspect
    artifacts from inside a CUDA graph.  Hardware counter/ISA verification is
    separate from successfully executing this API.
    """
    _mode(mode)
    if block_columns not in BLOCK_COLUMNS:
        raise ValueError(f"block_columns must be one of {BLOCK_COLUMNS}")
    if values.device.type != "cuda" or transition.device != values.device:
        raise RuntimeError("native CUDA tensors on the same device required; CPU fallback is prohibited")
    frequency, phases, rows = _shape(values, transition)
    if torch.cuda.get_device_capability(values.device)[0] < 8:
        raise RuntimeError("this tensor experiment requires CUDA compute capability8.0 or newer")
    kernel = _kernels()
    output = torch.empty_like(values)
    compiled = kernel[(triton.cdiv(frequency, 64), triton.cdiv(rows*phases, block_columns))](
        values, transition, transition, transition, transition, output,
        F=frequency, P=phases, COLUMNS=rows*phases,
        MODE=mode, ADVECT=False, ALIGNED=True,
        BM=64, BN=block_columns, BK=max(32, triton.next_power_of_2(frequency)),
        num_warps=4, enable_fp_fusion=False)
    return (output, compiled) if return_kernel else output


def _advection_shape(values, transition):
    """Structural checks only; caller supplies the receiver's validated maps."""
    keys = ("frequency", "index0", "index1", "fraction", "optimized_integral")
    if any(key not in transition for key in keys):
        raise ValueError(f"advection transition requires {keys}")
    frequency, phases, rows = _shape(values, transition["frequency"])
    for key in ("index0", "index1"):
        index = transition[key]
        if (index.shape != (frequency, phases) or index.dtype != torch.int64 or
                index.device != values.device or not index.is_contiguous()):
            raise ValueError("advection index maps require contiguous int64 [frequency,phase] on the state device")
    fraction = transition["fraction"]
    if (fraction.shape != (frequency,) or fraction.dtype != torch.float32 or
            fraction.device != values.device or not fraction.is_contiguous()):
        raise ValueError("advection fractions require contiguous FP32 [frequency] on the state device")
    if not isinstance(transition["optimized_integral"], bool):
        raise ValueError("optimized_integral must be the receiver's prepared boolean")
    return frequency, phases, rows


def frequency_contract_advected(values, transition, mode="strict-padded", *, block_columns=128,
                                return_kernel=False):
    """Fuse first phase advection with the frequency product on raw FP32 state.

    Transition maps come from ``OptimizedPhaseFrequencyLikelihood._transition``
    and must already have valid phase indices and finite [0,1] fractions.
    Those data checks are not synchronized inside the timed/captured kernel.
    Both integral and fractional paths preserve the original FP32 operation
    order; ``enable_fp_fusion=False`` also applies to interpolation.  Tensor
    representation is applied only after that advection.
    """
    _mode(mode)
    if block_columns not in BLOCK_COLUMNS:
        raise ValueError(f"block_columns must be one of {BLOCK_COLUMNS}")
    frequency, phases, rows = _advection_shape(values, transition)
    if values.device.type != "cuda" or transition["frequency"].device != values.device:
        raise RuntimeError("native CUDA tensors on the same device required; CPU fallback is prohibited")
    if torch.cuda.get_device_capability(values.device)[0] < 8:
        raise RuntimeError("this tensor experiment requires CUDA compute capability8.0 or newer")
    kernel = _kernels()
    output = torch.empty_like(values)
    compiled = kernel[(triton.cdiv(frequency, 64), triton.cdiv(rows*phases, block_columns))](
        values, transition["frequency"], transition["index0"], transition["index1"],
        transition["fraction"], output, F=frequency, P=phases, COLUMNS=rows*phases,
        MODE=mode, ADVECT=True, ALIGNED=transition["optimized_integral"],
        BM=64, BN=block_columns, BK=max(32, triton.next_power_of_2(frequency)),
        num_warps=4, enable_fp_fusion=False)
    return (output, compiled) if return_kernel else output


def _cpu_inputs(values, transition, mode):
    _mode(mode)
    if values.device.type != "cpu" or transition.device.type != "cpu":
        raise ValueError("diagnostic reference requires explicit CPU tensors")
    _shape(values, transition)
    if not bool(torch.isfinite(values).all()) or not bool(torch.isfinite(transition).all()):
        raise ValueError("finite diagnostic operands required")
    if bool((values < 0).any()) or bool((transition < 0).any()):
        raise ValueError("frequency probability operands must be nonnegative")


def _tf32_round(values):
    """Explicit RN/even representation for finite nonnegative FP32 values."""
    bits = values.contiguous().view(torch.int32).to(torch.int64)
    rounded = (bits+4095+((bits >> 13) & 1)) & 0xffffe000
    return rounded.to(torch.int32).view(torch.float32)


def _represented(values, transition, mode):
    if mode == "tf32":
        return _tf32_round(values), _tf32_round(transition), None
    if mode == "bf16":
        return values.to(torch.bfloat16).float(), transition.to(torch.bfloat16).float(), None
    if mode == "fp16-scaled":
        maximum = values.amax(dim=-2, keepdim=True)
        scale = torch.where(maximum > 0, maximum.clamp_min(torch.finfo(torch.float32).tiny),
                            torch.ones_like(maximum))
        represented = (values/scale).to(torch.float16).float()
        return represented, transition.to(torch.float16).float(), scale
    return values, transition, None


def cpu_frequency_reference(values, transition, mode="strict-padded"):
    """FP64 sum of represented products; not an emulation of CUDA accumulation.

    For tf32x3 this is the exact FP64 product of original FP32 operands: its
    internal multi-product algorithm is not pretended to be a fixed input
    quantizer.  For split2 this includes exactly its three chosen components.
    """
    _cpu_inputs(values, transition, mode)
    if mode == "bf16-split2":
        vh, th = values.to(torch.bfloat16).float(), transition.to(torch.bfloat16).float()
        vl = (values-vh).to(torch.bfloat16).float()
        tlow = (transition-th).to(torch.bfloat16).float()
        return (torch.matmul(th.double().T, vh.double())+
                torch.matmul(tlow.double().T, vh.double())+
                torch.matmul(th.double().T, vl.double()))
    v, t, scale = _represented(values, transition, mode)
    result = torch.matmul(t.double().T, v.double())
    return result if scale is None else result*scale.double()


def _cpu_advected_values(values, transition):
    if values.device.type != "cpu":
        raise ValueError("CPU advection reference requires explicit CPU state")
    frequency, phases, _ = _advection_shape(values, transition)
    for key in ("index0", "index1"):
        if int(transition[key].min()) < 0 or int(transition[key].max()) >= phases:
            raise ValueError("CPU advection reference rejects out-of-range phase indices")
    fraction = transition["fraction"]
    if not bool(torch.isfinite(fraction).all()) or bool(((fraction < 0) | (fraction > 1)).any()):
        raise ValueError("CPU advection reference requires finite fractions in[0,1]")
    shape = (1,1,1,frequency,phases)
    first = torch.gather(values,-1,transition["index0"].view(shape).expand_as(values))
    if transition["optimized_integral"]:
        return first
    second = torch.gather(values,-1,transition["index1"].view(shape).expand_as(values))
    part = fraction.view(1,1,1,frequency,1)
    return first*(1-part)+second*part


def cpu_frequency_advected_reference(values, transition, mode="strict-padded"):
    """Original FP32 advection followed by the represented-product FP64 reference."""
    return cpu_frequency_reference(_cpu_advected_values(values,transition),transition["frequency"],mode)


def operand_diagnostics(values, transition, mode="strict-padded"):
    """Outside-timing range/model diagnostics; CUDA inputs are copied to CPU.

    The FP64 represented-product reference measures quantization separately
    from the actual GPU FP32 accumulation.  It does not calibrate an LR tail.
    """
    values = values.detach().to(device="cpu", dtype=torch.float32).contiguous()
    transition = transition.detach().to(device="cpu", dtype=torch.float32).contiguous()
    _cpu_inputs(values, transition, mode)
    represented, template, scale = _represented(values, transition, mode)
    if mode == "bf16-split2":
        high = values.to(torch.bfloat16).float()
        represented = high+(values-high).to(torch.bfloat16).float()
        high_t = transition.to(torch.bfloat16).float()
        template = high_t+(transition-high_t).to(torch.bfloat16).float()
    effective = represented if scale is None else represented.double()*scale.double()
    original_mass = float(values.double().sum())
    lost_mask = (values > 0) & (effective == 0)
    lost_mass = float(values.double()[lost_mask].sum())
    positive = values[values > 0]
    row_error = template.double().sum(-1)-1.
    return {"mode": mode, "state_storage_dtype": "float32",
        "state_min_positive": float(positive.min()) if positive.numel() else None,
        "state_maximum": float(values.max()), "state_original_mass": original_mass,
        "state_zeroed_positive_values": int(lost_mask.sum()),
        "state_zeroed_mass": lost_mass,
        "state_zeroed_mass_fraction": lost_mass/original_mass if original_mass else 0.,
        "state_representation_l1_error": float((effective.double()-values.double()).abs().sum()),
        "state_representation_max_absolute_error": float((effective.double()-values.double()).abs().max()),
        "state_representation_nonfinite_values": int((~torch.isfinite(effective)).sum()),
        "state_representation_above_fp16_limit": int((values > 65504).sum()) if mode == "fp16-scaled" else None,
        "transition_representation_max_absolute_error": float((template-transition).abs().max()),
        "transition_represented_max_absolute_row_sum_error": float(row_error.abs().max()),
        "transition_representation_zeroed_mass": float(transition.double()[(transition > 0) & (template == 0)].sum()),
        "transition_renormalization": "none; existing global predictor normalization retained",
        "scaling": "one positive maximum per quadrature/phase column across all frequencies, floored at FP32 smallest normal; zero columns scale1" if mode == "fp16-scaled" else "none",
        "decomposition": "two BF16 components each, three products; low*low omitted" if mode == "bf16-split2" else None,
        "tf32_rounding": "explicit nearest/even to10 fraction bits before tl.dot" if mode == "tf32" else None,
        "tf32x3_reference_scope": "original FP32 operands; internal multi-product rounding not emulated" if mode == "tf32x3" else None,
        "scope": "representation/model diagnostics only; actual CUDA arithmetic, long evidence and admission require paired checks"}


class TensorPhaseFrequencyLikelihood(Fp64ReductionGraphLikelihood):
    """Opt-in frequency contraction with original phase/emission/reduction math.

    Mode and tile width are read-only because captured graphs retain the
    compiled arithmetic.  Construct a new receiver to change either setting.
    """

    def __init__(self, *args, tensor_mode="strict-padded", tensor_block_columns=128,
                 tensor_fuse_advection=False, **kwargs):
        self._tensor_mode = _mode(tensor_mode)
        if tensor_block_columns not in BLOCK_COLUMNS:
            raise ValueError(f"tensor_block_columns must be one of {BLOCK_COLUMNS}")
        self._tensor_block_columns = tensor_block_columns
        if not isinstance(tensor_fuse_advection, bool):
            raise ValueError("tensor_fuse_advection must be boolean")
        self._tensor_fuse_advection = tensor_fuse_advection
        if kwargs.get("dtype", torch.float64) != torch.float32:
            raise ValueError("tensor frequency experiment requires explicit dtype=torch.float32; reference FP64 remains separate")
        super().__init__(*args, **kwargs)
        if self.frequency_states > 128:
            raise ValueError("tensor experiment supports at most128 frequency states; none were pruned")
        if torch.cuda.get_device_capability(self.device)[0] < 8:
            raise RuntimeError("tensor experiment requires compute capability8.0 or newer")
        _kernels()

    @property
    def tensor_mode(self):
        return self._tensor_mode

    @property
    def tensor_block_columns(self):
        return self._tensor_block_columns

    @property
    def tensor_fuse_advection(self):
        return self._tensor_fuse_advection

    def _predict(self, weights, dt):
        if dt == 0:
            return weights
        # Global Torch math stays strict for every other operation.  The one
        # experimental contraction specifies its own Triton precision locally.
        if torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != "highest":
            raise ValueError("tensor experiment requires global TF32 disabled and highest Torch precision")
        transition = self._transition(dt)
        if self.tensor_fuse_advection:
            predicted = frequency_contract_advected(weights, transition, self.tensor_mode,
                block_columns=self.tensor_block_columns)
        else:
            predicted = self._advect(weights, transition)
            predicted = frequency_contract(predicted.contiguous(), transition["frequency"],
                self.tensor_mode, block_columns=self.tensor_block_columns)
        predicted = self._second_advection_and_phase(predicted, transition)
        # Identical to the current graph predictor.  The separate posterior
        # normalizer and accumulated log evidence remain FP64 in the parent.
        return predicted/predicted.sum(dim=(-3, -2, -1), keepdim=True)

    def diagnostics(self):
        result = super().diagnostics()
        templates = {}
        for dt, transition in self._transition_cache.items():
            t = transition["frequency"].detach().cpu()
            dummy = torch.zeros((1,1,1,self.frequency_states,1), dtype=torch.float32)
            diagnostic = operand_diagnostics(dummy, t, self.tensor_mode)
            templates[str(dt)] = {key:value for key,value in diagnostic.items()
                                  if key.startswith("transition_")}
        result.update({"implementation": "experimental_graph_tensor_frequency_v1",
            "tensor_mode": self.tensor_mode, "tensor_block_columns": self.tensor_block_columns,
            "tensor_fuse_advection": self.tensor_fuse_advection,
            "first_advection": "FP32 gather/interpolation inside frequency kernel" if self.tensor_fuse_advection else "original separate FP32 gather/interpolation",
            "advection_geometry": "full public index0/index1 maps; no affine or integral timing assumption",
            "frequency_contraction": "shared_T_transpose_times_flattened_trial_candidate_quadrature_phase_columns",
            "frequency_input_storage_dtype": "float32", "frequency_output_dtype": "float32",
            "frequency_accumulation_dtype": "float32",
            "frequency_product_mode": self.tensor_mode,
            "frequency_padding": "register tiles only; no state pruning or padded global input",
            "frequency_transition_template_diagnostics_by_dt": templates,
            "predictor_normalizer_dtype": "torch.float32 (same as current graph receiver)",
            "global_torch_tf32_allowed": torch.backends.cuda.matmul.allow_tf32,
            "tf32_allowed": self.tensor_mode in ("tf32", "tf32x3"),
            "frequency_tensor_instruction_verified": False,
            "tensor_mode_scope": "experimental organization/product/representation change; does not inherit operational admission calibration"})
        return result
