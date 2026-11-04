#pragma once
#include "family_simd.hpp"
#include "pattern_correlator_batch.hpp"
#include <array>
#include <stdexcept>

namespace datapump::modem::detail {
// One original physical sample only. Native vector lanes are independent
// frequency siblings; this is not a temporal/shifted-input matrix. The caller
// packs original projection differences and keeps original per-lane guard order.
struct ScheduledRawSampleScratch {
    static constexpr std::size_t maximum_lanes=16,minimum_simd_lanes=4;
    std::array<std::array<double,maximum_lanes>,6> projection;
    std::array<std::array<std::array<double,maximum_lanes>,6>,2> contribution;
    std::array<std::array<std::uint64_t,maximum_lanes>,2> count;
    std::array<std::size_t,maximum_lanes> member,frequency;
};
// Include bounded descriptor/callee scratch in the reservation. The caller's
// noinline helper owns this stack storage only while the reservation is held.
inline constexpr std::size_t scheduled_raw_sample_scratch_bytes=4096;
inline constexpr std::size_t scheduled_raw_single_frequency_scratch_bytes=768;
// Native GCC14 measured472B helper frame plus outgoing complex-multiply
// support; retain extra headroom. This reservation supplements the separately
// charged common caller frame, rather than claiming a transitive ABI stack cap.
static_assert(2*sizeof(CorrelationFit)+sizeof(CorrelationProjection)+256<=
    scheduled_raw_single_frequency_scratch_bytes);
static_assert(sizeof(ScheduledRawSampleScratch)+sizeof(family_simd::RawFamily)+
    2*sizeof(family_simd::FitPlanes)+4*sizeof(double)+512<=scheduled_raw_sample_scratch_bytes);

inline void scheduled_raw_sample_contributions(ScheduledRawSampleScratch& scratch,
        std::size_t lanes,const std::array<std::complex<double>,2>& phase,
        family_simd::Backend backend=family_simd::Backend::automatic) {
    if(lanes>ScheduledRawSampleScratch::maximum_lanes)
        throw std::invalid_argument("scheduled raw sample exceeds candidate bound");
    backend=family_simd::selected(backend); // Fail before zeroing any output.
    if(backend==family_simd::Backend::scalar) {
        for(std::size_t lane=0;lane<lanes;++lane) {
            const CorrelationProjection p{scratch.projection[0][lane],scratch.projection[1][lane],
                scratch.projection[2][lane],scratch.projection[3][lane],scratch.projection[4][lane],scratch.projection[5][lane]};
            for(unsigned bit=0;bit<2;++bit) {
                CorrelationFit contribution;contribution.add(p,phase[bit],1);
                scratch.contribution[bit][0][lane]=contribution.xc;scratch.contribution[bit][1][lane]=contribution.xs;
                scratch.contribution[bit][2][lane]=contribution.cc;scratch.contribution[bit][3][lane]=contribution.ss;
                scratch.contribution[bit][4][lane]=contribution.cs;scratch.contribution[bit][5][lane]=contribution.energy;
                scratch.count[bit][lane]=contribution.count;
            }
        }
        return;
    }
    family_simd::RawFamily input;input.candidates=lanes;input.samples=1;
    input.projection_row_stride=lanes;input.template_row_stride=0;
    const std::array<std::array<double,2>,2> coefficient{{
        {phase[0].real(),phase[0].imag()},{phase[1].real(),phase[1].imag()}}};
    std::array<family_simd::FitPlanes,2> output;
    for(unsigned field=0;field<6;++field)input.projection[field]={scratch.projection[field].data(),lanes};
    for(unsigned bit=0;bit<2;++bit) {
        for(unsigned part=0;part<2;++part)input.coefficient[bit][part]={&coefficient[bit][part],1};
        for(unsigned field=0;field<6;++field) {
            std::fill_n(scratch.contribution[bit][field].data(),lanes,0.0);
            output[bit].value[field]={scratch.contribution[bit][field].data(),lanes};
        }
        std::fill_n(scratch.count[bit].data(),lanes,0);
        output[bit].count={scratch.count[bit].data(),lanes};
    }
    // The qualified primitive preserves temporary+0 then accumulation order.
    // Zero-initialized output contributes one additional exact +0. No aggregate
    // delta is reused for distinct whole/chip/quarter/differential accumulators.
    family_simd::add_raw_f64(input,output,backend);
}
inline CorrelationProjection scheduled_raw_sample_projection(
        const ScheduledRawSampleScratch& scratch,std::size_t lane) {
    return {scratch.projection[0][lane],scratch.projection[1][lane],
        scratch.projection[2][lane],scratch.projection[3][lane],
        scratch.projection[4][lane],scratch.projection[5][lane]};
}
inline CorrelationFit scheduled_raw_sample_fit(const ScheduledRawSampleScratch& scratch,
        unsigned bit,std::size_t lane) {
    return {scratch.contribution[bit][0][lane],scratch.contribution[bit][1][lane],
        scratch.contribution[bit][2][lane],scratch.contribution[bit][3][lane],
        scratch.contribution[bit][4][lane],scratch.contribution[bit][5][lane],scratch.count[bit][lane]};
}
} // namespace datapump::modem::detail
