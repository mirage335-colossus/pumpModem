#!/usr/bin/env python3
"""CPU invariants for zero padding and represented tensor-product operands."""
import json
from pathlib import Path
import sys

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
import triad_tensor_probe as probe


def main():
    g = torch.Generator().manual_seed(770051)
    a = torch.complex(torch.randn(2, 127, generator=g, dtype=torch.float64),
                      torch.randn(2, 127, generator=g, dtype=torch.float64))
    b = torch.complex(torch.randn(127, 7, generator=g, dtype=torch.float64),
                      torch.randn(127, 7, generator=g, dtype=torch.float64))
    for mode in probe.MODES:
        encoded = probe.encode(a, b, mode)
        assert encoded.ar.shape == (32, 128)
        assert encoded.br.shape == (128, 16)
        assert not bool(encoded.ar[2:].float().any())
        assert not bool(encoded.ar[:, 127:].float().any())
        assert not bool(encoded.br[127:].float().any())
        assert not bool(encoded.br[:, 7:].float().any())
        ra, rb = encoded.represented()
        full_a = torch.complex(encoded.ar.double(), encoded.ai.double())/encoded.scale_a.double()
        full_b = torch.complex(encoded.br.double(), encoded.bi.double())/encoded.scale_b.double()
        assert torch.allclose((full_a@full_b)[:2, :7], ra@rb, rtol=1e-13, atol=1e-12)
        assert float((ra@rb-a@b).abs().max()) > 0
        if mode == "int8-padded":
            assert encoded.details()["int32_absolute_partial_bound"] < 2**31
            assert torch.equal(encoded.ar[:2, :127],
                               (a.real.float()*encoded.scale_a).round().clamp(-127, 127).to(torch.int8))
        else:
            assert encoded.br.stride() == (1, 128)
        try:
            probe.project(encoded)
        except RuntimeError as error:
            assert "CPU fallback is prohibited" in str(error)
        else:
            raise AssertionError("CPU silently substituted for the requested CUDA native operator")
    zero = probe.encode(torch.zeros_like(a), torch.zeros_like(b), "int8-padded")
    za, zb = zero.represented()
    assert bool(torch.isfinite(za).all()) and bool(torch.isfinite(zb).all())
    assert not bool((za@zb).any())
    try:
        probe.encode(a, b[:-1], "int8-padded")
    except ValueError:
        pass
    else:
        raise AssertionError("shape mismatch accepted")
    assert "not implemented or exercised" in probe.inventory()["fp4_block_scaled_status"]
    print(json.dumps({"status": "passed", "checks": ["zero-padding-complete-dot-product",
        "represented-input-reference", "int32-overflow-bound", "no-cpu-fallback", "zero-input-scaling"]}))


if __name__ == "__main__":
    main()
