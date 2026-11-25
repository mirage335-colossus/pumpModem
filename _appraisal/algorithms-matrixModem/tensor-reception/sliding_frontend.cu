#include "gpu.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace matrix_rx {
namespace {

constexpr int projection_tile_bits = 256;
constexpr int feature_tile_bits = 32;

void cuda_check(cudaError_t e, const char* what) {
    if (e != cudaSuccess) throw std::runtime_error(std::string(what) + ": " + cudaGetErrorString(e));
}

__device__ __forceinline__ int wrap_bin(int k) {
    k %= fft_length;
    return k < 0 ? k + fft_length : k;
}

__device__ __forceinline__ float2 cmul(float2 a, float2 b) {
    return make_float2(a.x*b.x-a.y*b.y, a.x*b.y+a.y*b.x);
}

__device__ __forceinline__ float2 cadd(float2 a, float2 b) {
    return make_float2(a.x+b.x, a.y+b.y);
}

__device__ __forceinline__ float2 csub(float2 a, float2 b) {
    return make_float2(a.x-b.x, a.y-b.y);
}

__device__ __forceinline__ float2 rotate_quadrant(float2 z, int bin) {
    // exp(-i*2*pi*bin*256/1024), exact for integer FFT bins.
    switch (wrap_bin(bin) & 3) {
    case 0: return z;
    case 1: return make_float2(z.y, -z.x);
    case 2: return make_float2(-z.x, -z.y);
    default: return make_float2(-z.y, z.x);
    }
}

// One thread owns a (bit, look, bin) recurrence. It forms one direct 256
// sample seed, then advances the projection by four samples at a time using
// the exact entering/leaving-edge identity for a sliding Fourier window.
__global__ void slide_projections(const Sample* input, const int* centers,
                                 const float2* twiddle, float2* projections,
                                 int first_bit, int bit_count, int starts,
                                 int frequencies, int unique_bins, int first_j) {
    const int seq = blockIdx.x * blockDim.x + threadIdx.x;
    const int count = bit_count * 2 * unique_bins;
    if (seq >= count) return;
    const int bin_index = seq % unique_bins;
    const int t = seq / unique_bins;
    const int look = t & 1;
    const int local_bit = t >> 1;
    const int bit = first_bit + local_bit;
    const int j = first_j + bin_index;
    const int bin = wrap_bin(centers[bit] + j * tone_half_separation);
    const int input_base = 2 * bit * symbol_samples + look * symbol_samples;
    const float2* tw = twiddle;
    float2 value = make_float2(0.0f, 0.0f);
    for (int n = 0; n < symbol_samples; ++n) {
        const Sample x = input[input_base + n];
        const float2 w = tw[size_t(n) * fft_length + bin];
        value = cadd(value, cmul(make_float2(x.re, x.im), w));
    }
    const float2 step = tw[size_t(timing_step) * fft_length + bin];
    const float2 step_forward = make_float2(step.x, -step.y);
    const size_t out_base = ((size_t(local_bit) * 2 + look) * starts) * unique_bins + bin_index;
    projections[out_base] = value;
    for (int start = 1; start < starts; ++start) {
        const int shift = (start - 1) * timing_step;
        float2 leaving = make_float2(0.0f, 0.0f);
        float2 entering = make_float2(0.0f, 0.0f);
        for (int r = 0; r < timing_step; ++r) {
            const float2 w = tw[size_t(r) * fft_length + bin];
            const Sample xo = input[input_base + shift + r];
            const Sample xi = input[input_base + shift + symbol_samples + r];
            leaving = cadd(leaving, cmul(make_float2(xo.re, xo.im), w));
            entering = cadd(entering, cmul(make_float2(xi.re, xi.im), w));
        }
        entering = rotate_quadrant(entering, bin);
        value = cmul(step_forward, cadd(csub(value, leaving), entering));
        projections[out_base + size_t(start) * unique_bins] = value;
    }
}

__device__ __forceinline__ float energy(float2 z) {
    return (z.x*z.x + z.y*z.y) / float(symbol_samples);
}

__global__ void feature_tile(const float2* projections, const int* centers,
                             float* contrast, float* common_parts, float* power_parts,
                             int projection_first_bit, int projection_bit_count,
                             int rows, int starts, int frequencies, int unique_bins, int first_j,
                             float a, float b, float common_constant) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= rows) return;
    const int first_bit = projection_first_bit + int(blockIdx.y) * feature_tile_bits;
    const int remaining_bits = projection_bit_count - int(blockIdx.y) * feature_tile_bits;
    const int bit_count = remaining_bits < feature_tile_bits ? remaining_bits : feature_tile_bits;
    const int tile_index = first_bit / feature_tile_bits;
    const int start = row / frequencies;
    const int frequency = row % frequencies;
    const int base_j = frequency - frequencies / 2;
    const int start_proj = size_t(start) * unique_bins;
    float common_sum = 0.0f, power_sum = 0.0f;
    for (int lb = 0; lb < bit_count; ++lb) {
        const int bit = first_bit + lb;
        const int high_j = base_j + 1;
        const int low_j = base_j - 1;
        const int hi = high_j - first_j, lo = low_j - first_j;
        const int bin_h = wrap_bin(centers[bit] + high_j * tone_half_separation);
        const int bin_l = wrap_bin(centers[bit] + low_j * tone_half_separation);
        const int projection_bit = first_bit + lb - projection_first_bit;
        const size_t bit_base = size_t(projection_bit) * 2 * starts * unique_bins;
        const float2 h1 = projections[bit_base + start_proj + hi];
        const float2 h2 = rotate_quadrant(projections[bit_base + starts * unique_bins + start_proj + hi], bin_h);
        const float2 l1 = projections[bit_base + start_proj + lo];
        const float2 l2 = rotate_quadrant(projections[bit_base + starts * unique_bins + start_proj + lo], bin_l);
        const float eh1 = energy(h1), eh2 = energy(h2), el1 = energy(l1), el2 = energy(l2);
        const float ch = 2.0f * (h1.x*h2.x + h1.y*h2.y) / float(symbol_samples);
        const float cl = 2.0f * (l1.x*l2.x + l1.y*l2.y) / float(symbol_samples);
        const float qh = a*(eh1+eh2) + b*ch - common_constant;
        const float ql = a*(el1+el2) + b*cl - common_constant;
        contrast[size_t(bit)*rows + row] = qh - ql;
        common_sum += qh + ql;
        power_sum += eh1 + eh2 + el1 + el2;
    }
    common_parts[size_t(tile_index)*rows + row] = common_sum;
    power_parts[size_t(tile_index)*rows + row] = power_sum;
}

__global__ void reduce_parts(const float* common_parts, const float* power_parts,
                             float* common, float* power, int rows, int tiles) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= rows) return;
    float c = 0.0f, p = 0.0f;
    for (int t = 0; t < tiles; ++t) {
        c += common_parts[size_t(t)*rows + row];
        p += power_parts[size_t(t)*rows + row];
    }
    common[row] = c;
    power[row] = p;
}

} // namespace

struct FrontEnd::Impl {
    explicit Impl(const Shape& s)
        : shape(s), rows(s.rows()), tiles((s.bits + feature_tile_bits - 1)/feature_tile_bits),
          unique_bins(s.frequencies + 2), first_j(-s.frequencies/2 - 1) {
        validate_shape(shape);
        try {
            cuda_check(cudaMalloc(&input, size_t(shape.opportunity_samples())*sizeof(Sample)), "allocate input");
            cuda_check(cudaMalloc(&centers, size_t(shape.bits)*sizeof(int)), "allocate centers");
            cuda_check(cudaMalloc(&contrast, size_t(rows)*shape.bits*sizeof(float)), "allocate contrast");
            cuda_check(cudaMalloc(&common, size_t(rows)*sizeof(float)), "allocate common");
            cuda_check(cudaMalloc(&power, size_t(rows)*sizeof(float)), "allocate power");
            cuda_check(cudaMalloc(&common_parts, size_t(rows)*tiles*sizeof(float)), "allocate common partials");
            cuda_check(cudaMalloc(&power_parts, size_t(rows)*tiles*sizeof(float)), "allocate power partials");
            cuda_check(cudaMalloc(&projections, size_t(projection_tile_bits)*2*shape.starts*unique_bins*sizeof(float2)), "allocate sliding projections");
            cuda_check(cudaMalloc(&twiddle, size_t(fft_length)*symbol_samples*sizeof(float2)), "allocate twiddles");
            std::vector<int> hc(shape.bits);
            for (int bit=0; bit<shape.bits; ++bit) hc[bit]=center_bin(bit);
            cuda_check(cudaMemcpy(centers,hc.data(),hc.size()*sizeof(int),cudaMemcpyHostToDevice),"upload centers");
            std::vector<float2> ht(size_t(fft_length)*symbol_samples);
            constexpr double tau=6.283185307179586476925286766559;
            for (int n=0;n<symbol_samples;++n) for (int k=0;k<fft_length;++k) {
                const double p=-tau*double(k)*n/fft_length;
                ht[size_t(n)*fft_length+k]=make_float2(float(std::cos(p)),float(std::sin(p)));
            }
            cuda_check(cudaMemcpy(twiddle,ht.data(),ht.size()*sizeof(float2),cudaMemcpyHostToDevice),"upload twiddles");
        } catch (...) { cleanup(); throw; }
    }
    ~Impl() { cleanup(); }

    void cleanup() noexcept {
        if (twiddle) cudaFree(twiddle);
        if (projections) cudaFree(projections);
        if (power_parts) cudaFree(power_parts);
        if (common_parts) cudaFree(common_parts);
        if (power) cudaFree(power);
        if (common) cudaFree(common);
        if (contrast) cudaFree(contrast);
        if (centers) cudaFree(centers);
        if (input) cudaFree(input);
        twiddle=nullptr; projections=nullptr; power_parts=nullptr; common_parts=nullptr;
        power=nullptr; common=nullptr; contrast=nullptr; centers=nullptr; input=nullptr;
    }

    DeviceEvidence analyze(const std::vector<Sample>& host, bool history, cudaStream_t stream) {
        if (host.size() < size_t(shape.opportunity_samples())) throw std::invalid_argument("input is shorter than the declared opportunity");
        cuda_check(cudaMemcpyAsync(input,host.data(),size_t(shape.opportunity_samples())*sizeof(Sample),cudaMemcpyHostToDevice,stream),"upload capture");
        const float gamma=history?history_gamma:0.0f;
        const float snr=likelihood_snr(shape);
        const float lp=snr*(1.0f+gamma), lm=snr*(1.0f-gamma);
        const float wp=lp/(1.0f+lp), wm=lm/(1.0f+lm);
        const float a=0.5f*(wp+wm), b=0.5f*(wp-wm);
        const float c=log1pf(lp)+log1pf(lm);
        for (int first=0;first<shape.bits;first+=projection_tile_bits) {
            const int count=std::min(projection_tile_bits,shape.bits-first);
            const int seq=count*2*unique_bins;
            slide_projections<<<(seq+127)/128,128,0,stream>>>(input,centers,twiddle,projections,
                first,count,shape.starts,shape.frequencies,unique_bins,first_j);
            cuda_check(cudaGetLastError(),"launch sliding projections");
            const int feature_slices=(count+feature_tile_bits-1)/feature_tile_bits;
            const dim3 feature_grid((rows+127)/128,feature_slices);
            feature_tile<<<feature_grid,128,0,stream>>>(projections,centers,contrast,common_parts,power_parts,
                first,count,rows,shape.starts,shape.frequencies,unique_bins,first_j,a,b,c);
            cuda_check(cudaGetLastError(),"launch feature extraction");
        }
        reduce_parts<<<(rows+127)/128,128,0,stream>>>(common_parts,power_parts,common,power,rows,tiles);
        cuda_check(cudaGetLastError(),"launch row reduction");
        return {contrast,common,power};
    }

    size_t bytes() const {
        return size_t(shape.opportunity_samples())*sizeof(Sample)+size_t(shape.bits)*sizeof(int)+
               size_t(rows)*shape.bits*sizeof(float)+size_t(rows)*2*sizeof(float)+
               size_t(rows)*tiles*2*sizeof(float)+
               size_t(projection_tile_bits)*2*shape.starts*unique_bins*sizeof(float2)+
               size_t(fft_length)*symbol_samples*sizeof(float2);
    }

    Shape shape;
    int rows,tiles,unique_bins,first_j;
    Sample* input{};
    int* centers{};
    float* contrast{};
    float* common{};
    float* power{};
    float* common_parts{};
    float* power_parts{};
    float2* projections{};
    float2* twiddle{};
};

FrontEnd::FrontEnd(const Shape& shape):impl_(new Impl(shape)){}
FrontEnd::~FrontEnd()=default;
DeviceEvidence FrontEnd::analyze(const std::vector<Sample>& input,bool history,cudaStream_t stream){
    return impl_->analyze(input,history,stream);
}
std::size_t FrontEnd::bytes() const{return impl_->bytes();}

} // namespace matrix_rx
