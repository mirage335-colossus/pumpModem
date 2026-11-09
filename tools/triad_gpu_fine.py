"""CUDA bulk basis and block statistics for bounded Robust test observations.

The receiver still owns the public timing lattice, native keyed tile generation,
FP64 Gaussian state, and decisions. The device holds the mapped tile cache and
the intermediate basis, and returns just the block correlations and Gram.
"""

from collections import OrderedDict
import weakref

import numpy as np
import torch
import triton
import triton.language as tl
from triton.language.extra.cuda import libdevice

from triad_continuous import tracking_source_basis


@triton.jit
def _render_mapped(tiles, offsets, coefficients, origins, counts, table, outputs,
                   result, samples: tl.constexpr, tile_samples: tl.constexpr,
                   max_tiles: tl.constexpr, block: tl.constexpr, precise: tl.constexpr):
    lane = tl.program_id(0)
    sample = tl.program_id(1)*block+tl.arange(0, block)
    active = sample < samples
    position = tl.load(offsets+sample, active, other=0)
    fraction = tl.load(coefficients+4*lane)
    scale = tl.load(coefficients+4*lane+1)
    phase = tl.load(coefficients+4*lane+2)
    slope = tl.load(coefficients+4*lane+3)
    relative = fraction+scale*position
    base = tl.floor(relative)
    integer = tl.load(origins+lane)+base.to(tl.int64)
    arithmetic = tl.float64 if precise else tl.float32
    u = (relative-base).to(arithmetic)
    real = tl.full((block,), 0, arithmetic)
    imag = tl.full((block,), 0, arithmetic)
    for i in tl.static_range(4):
        if i == 0:
            weight = -u*(u-1)*(u-2)/6
        elif i == 1:
            weight = (u+1)*(u-1)*(u-2)/2
        elif i == 2:
            weight = -(u+1)*u*(u-2)/2
        else:
            weight = (u+1)*u*(u-1)/6
        point = integer+i-1
        keep = active & (point >= 0) & (point < tl.load(counts+lane)*tile_samples)
        slot = tl.load(table+lane*max_tiles+point//tile_samples, keep, other=0)
        index = 2*(slot*tile_samples+point % tile_samples)
        real += weight*tl.load(tiles+index, keep, other=0)
        imag += weight*tl.load(tiles+index+1, keep, other=0)
    cycles = phase+slope*position
    angle = ((cycles-tl.floor(cycles))*6.283185307179586).to(arithmetic)
    if precise:
        cosine, sine = libdevice.cos(angle), libdevice.sin(angle)
    else:
        cosine, sine = tl.cos(angle), tl.sin(angle)
    output = 2*(tl.load(outputs+lane)*samples+sample)
    tl.store(result+output, real*cosine-imag*sine, active)
    tl.store(result+output+1, real*sine+imag*cosine, active)


class CudaFineBulk:
    """Resident template cache and selected DFT statistics with FP64 coordinates."""

    tile_samples = 8192
    maximum_pinned = 32
    def __init__(self, device, cache_bytes, *, precise=False):
        self.device = torch.device(device)
        if self.device.type != 'cuda' or not torch.cuda.is_available():
            raise ValueError('accelerated fine statistics require an available CUDA device')
        self.precise = precise
        self.dtype = torch.complex128 if precise else torch.complex64
        complex_type = 'complex128' if precise else 'complex64'
        self.arithmetic = dict(backend='cuda-triton-torch',
            template=f'{complex_type} cubic and phase rotation',
            coordinates='FP64 relative with canonical extended-precision origin',
            dft=complex_type, correlation=complex_type, gram='Hermitian complex128',
            precision_selection='precise_tight_clock_grid' if precise else 'fp32_bulk',
            variance_normalization_and_state='CPU complex128',
            native_template_fallback='reported by canonical cache direct_rows')
        self.tile_capacity = max(self.maximum_pinned, cache_bytes//(16*self.tile_samples))
        self.workspace_bytes = (self.tile_capacity*(16 if precise else 8)*self.tile_samples+
                                2*self.maximum_pinned*16*self.tile_samples+512*1024)
        self.tiles = None
        self.slots = OrderedDict()
        self.free = list(range(self.tile_capacity))
        self.uploaded_tiles = self.mapped_batches = self.block_statistics = 0
        self.mapped_values = self.dft_values = self.dc_reduction_input_values = 0
        self.projected_values = self.returned_values = 0

    def empty(self, shape):
        # Entirely out-of-support rows need no mapped kernel launch.
        return torch.zeros(shape, dtype=self.dtype, device=self.device)

    def transfer(self, values):
        return torch.as_tensor(values, dtype=self.dtype, device=self.device)

    def _resident_slots(self, pinned):
        if self.tiles is None:
            self.tiles = torch.empty((self.tile_capacity, self.tile_samples),
                                     dtype=self.dtype, device=self.device)
        protected = {id(tile) for tile in pinned.values()}
        new_slots, new_tiles, by_pointer = [], [], {}
        for tile in pinned.values():
            key = id(tile)
            cached = self.slots.pop(key, None)
            if cached is not None:
                reference, slot = cached
                if reference() is not tile:
                    new_slots.append(slot)
                    new_tiles.append(tile)
            else:
                if self.free:
                    slot = self.free.pop()
                else:
                    evicted = next(other for other in self.slots if other not in protected)
                    _, slot = self.slots.pop(evicted)
                new_slots.append(slot)
                new_tiles.append(tile)
            self.slots[key] = weakref.ref(tile), slot
            by_pointer[tile.ctypes.data] = slot
        if new_tiles:
            indices = torch.as_tensor(new_slots, dtype=torch.int64, device=self.device)
            self.tiles.index_copy_(0, indices, self.transfer(np.stack(new_tiles)))
            self.uploaded_tiles += len(new_tiles)
        return by_pointer

    def render(self, rows, offsets, answer, pinned):
        by_pointer = self._resident_slots(pinned)
        count = len(rows)
        coefficients = np.empty((count, 4), np.float64)
        indices = np.zeros((count, self.maximum_pinned), np.int32)
        origins, counts, outputs = (np.empty(count, dtype) for dtype in (np.int64, np.int32, np.int32))
        for i, (row, pointers) in enumerate(rows):
            coefficients[i] = row.fraction, row.scale, row.phase, row.phase_step
            origins[i], counts[i], outputs[i] = row.origin, row.tile_count, row.output or 0
            indices[i, :row.tile_count] = [by_pointer[pointer] for pointer in pointers]
        arrays = [torch.as_tensor(array, device=self.device) for array in
                  (offsets, coefficients, origins, counts, indices, outputs)]
        block = 128
        _render_mapped[(count, triton.cdiv(len(offsets), block))](
            torch.view_as_real(self.tiles), *arrays, torch.view_as_real(answer),
            len(offsets), self.tile_samples, self.maximum_pinned, block, self.precise,
            enable_fp_fusion=not self.precise)
        self.mapped_batches += 1
        self.mapped_values += count*len(offsets)

    def statistics(self, basis, projection, projected_y, variance):
        if basis.device != self.device or basis.dtype != self.dtype:
            raise ValueError('accelerated basis must remain on its selected CUDA device and precision')
        if not bool(torch.isfinite(basis).all() & (basis[0] == 0).all()):
            raise ValueError('accelerated basis must be finite with an exactly zero H0 lane')
        if (self.precise and projection.kind == 'dft' and
                len(projection.indices) == 1 and projection.indices[0] == 0):
            # The unitary DFT's DC row is a normalized sum. In these
            # narrow profiles it is the only retained mode.
            projected = basis.sum(dim=-1, keepdim=True)/np.sqrt(basis.shape[-1])
            self.dc_reduction_input_values += basis.numel()
        else:
            indices = torch.tensor(projection.indices, dtype=torch.int64, device=self.device)
            if projection.kind == 'dft':
                basis = torch.fft.fft(basis, dim=-1, norm='ortho')
                self.dft_values += basis.numel()
            projected = basis.index_select(-1, indices)
        y = self.transfer(projected_y)
        correlations = (projected.conj()*y).sum(dim=-1)
        # The small projected covariance is used with the strict FP64
        # Gaussian PSD guard. Complex64 matmul can leave an imaginary diagonal
        # or negative roundoff eigenvalues even when the basis is finite.
        flat = projected.reshape(-1, projected.shape[-2], projected.shape[-1]).to(torch.complex128)
        gram = flat.conj() @ flat.transpose(-1, -2)
        gram = (gram+gram.mH)*.5
        small = torch.cat((correlations.reshape(-1), gram.reshape(-1))).cpu()
        small.div_(variance)
        if not bool(torch.isfinite(small).all()):
            raise FloatingPointError('CUDA block statistics are nonfinite')
        split = correlations.numel()
        self.block_statistics += 1
        self.projected_values += projected.numel()
        self.returned_values += small.numel()
        return small[:split].reshape(correlations.shape), small[split:].reshape(
            (*correlations.shape, correlations.shape[-1]))

    def report(self):
        return dict(arithmetic=self.arithmetic, uploaded_tiles=self.uploaded_tiles,
            mapped_batches=self.mapped_batches, mapped_values=self.mapped_values,
            block_statistics=self.block_statistics, dft_values=self.dft_values,
            dc_reduction_input_values=self.dc_reduction_input_values,
            projected_values=self.projected_values, returned_values=self.returned_values,
            workspace_bytes=self.workspace_bytes)


def make_cuda_basis_factory(device, *, precise=False):
    def factory(*args, **kwargs):
        return tracking_source_basis(*args, **kwargs,
            accelerator=CudaFineBulk(device, kwargs['cache_bytes'], precise=precise))
    return factory
