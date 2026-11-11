#pragma once

#include <ATen/ATen.h>

#include <array>
#include <cstdint>
#include <numbers>

namespace datapump::triad::v3::receiver_detail {

inline constexpr char mapping_c64_arithmetic_contract[] = "fp32-rn4-v1";

// Four eager FP32 products establish the same explicit rounding boundaries as
// the fused CUDA mapper. Materialize every product before updating either
// component; complex multiplication may choose an architecture-dependent FMA.
inline void rotate_mapping_c64(at::Tensor values, const at::Tensor& cosine, const at::Tensor& sine) {
    const auto real = at::real(values);
    const auto imag = at::imag(values);
    auto real_cosine = real * cosine;
    const auto imag_sine = imag * sine;
    auto real_sine = real * sine;
    const auto imag_cosine = imag * cosine;
    real_cosine.sub_(imag_sine);
    real_sine.add_(imag_cosine);
    real.copy_(real_cosine);
    imag.copy_(real_sine);
}

inline void render_aten_mapping(at::Tensor output, const at::Tensor& tile_storage, const at::Tensor& maps,
                               const at::Tensor& indices, const at::Tensor& offsets, at::ScalarType real_type,
                               std::int64_t tile_samples, std::int64_t maximum_pins) {
    const auto relative = maps.narrow(1, 0, 1) + maps.narrow(1, 1, 1) * offsets;
    const auto base = relative.floor();
    const auto index = indices.narrow(1, 0, 1) + base.to(at::kLong);
    const auto extent = indices.narrow(1, 1, 1);
    const auto slots = indices.narrow(1, 3, maximum_pins);
    const auto u = (relative - base).to(real_type);
    const auto less_one = u - 1;
    const auto less_two = u - 2;
    const auto more_one = u + 1;
    const std::array<at::Tensor, 4> weights{
        -u * less_one * less_two / 6, more_one * less_one * less_two / 2,
        -more_one * u * less_two / 2, more_one * u * less_one / 6};
    auto values = at::zeros({maps.size(0), offsets.size(0)}, output.options());
    for (int j = 0; j < 4; ++j) {
        const auto knot = index + (j - 1);
        const auto keep = knot.ge(0) & knot.lt(extent);
        const auto safe = at::where(keep, knot, 0);
        const auto slot = at::gather(slots, 1, at::floor_divide(safe, tile_samples));
        const auto flat = slot * tile_samples + at::remainder(safe, tile_samples);
        const auto samples = tile_storage.view(-1).index_select(0, flat.reshape(-1)).reshape_as(values);
        values.add_(at::where(keep, samples, 0) * weights[j]);
    }
    const auto cycles = maps.narrow(1, 2, 1) + maps.narrow(1, 3, 1) * offsets;
    const auto angle = ((cycles - cycles.floor()) * (2 * std::numbers::pi)).to(real_type);
    if (real_type == at::kFloat) {
        // Named mapping tensors through angle occupy 76 bytes per row/lane;
        // cosine/sine plus the four product arrays add 24. These 100 bytes fit
        // the existing 224-byte mapping reservation. Component views and the
        // in-place sums/copies allocate no additional full-row tensors.
        rotate_mapping_c64(values, angle.cos(), angle.sin());
    } else {
        values.mul_(at::complex(angle.cos(), angle.sin()));
    }
    output.index_copy_(0, indices.select(1, 2), values);
}

} // namespace datapump::triad::v3::receiver_detail
