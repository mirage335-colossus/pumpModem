// Matrix Modem FSK lattice: GPU stage benchmarks (transform and exhaustive decode).
//
// transform   One 0.4 s dwell = 1024 active complex samples at 3200 Hz. Wanted: the energy of every
//             3.125 Hz bin in the 2.2 kHz band (704 bins) as 4-bit evidence. Routes:
//               fft     cuFFT C2C FP32, batch of 1024-point transforms, then energy of the bins
//               dft16   one matrix product, FP16 operands, FP32 accumulation and output
//               dft16h  the same with FP16 output
//               dft8    one matrix product, INT8 operands holding 4-bit samples (15 levels) and
//                       8-bit or 4-bit (15-level) cos/sin coefficients, exact INT32 accumulation
//             The matrix product computes [Re; Im] of the 704 bins from [Re; Im] of the samples:
//             C[1408 x B] = D[1408 x 2048] * X[2048 x B].
// decode      Exhaustive maximum-likelihood decode of a 24-bit block: 24 symbols of 64 tones, the
//             tone index of symbol j is a keyed linear map of the message, G_j c = Ghi_j c_hi xor
//             Glo_j c_lo with c = (c_hi, c_lo) of 12 bits each. Score[c] = sum_j T_j[G_j c] with
//             4-bit symbol tables T_j. Routes:
//               gemm8   Score = P[4096 x 1536] * W[1536 x 4096], P one-hot in (j, Ghi_j c_hi),
//                       W[(j,t), c_lo] = T_j[t xor Glo_j c_lo]; INT8 operands, INT32 result
//               gemm16  the same with FP16 operands and FP32 result (exact: sums <= 360)
//               direct  CUDA-core kernel that sums the 24 lookups for every c and keeps the maximum
//               fwht    scatter of the tables' Walsh spectra and a 2^24-point fast Walsh-Hadamard
//                       transform on CUDA cores
//             A hypotheses (timing, frequency, drift, spread) are decoded per call.
// simulate    Time-domain simulation of B dwells per call: complex Gaussian noise for every sample
//             (cuRAND), one keyed tone per dwell with a per-dwell Rayleigh gain, then the dft16h
//             or fft route to 4-bit evidence. Reports each stage and the total.
// chunks      Transform routes against the batch size (dwells per call), including a CPU FFT route
//             (host threads, evidence uploaded), to find the smallest efficient portion of work.
// contend     The transform routes alone and while a second host thread runs the block decode on
//             its own stream (direct kernel on CUDA cores, or the FP16 matrix product on Tensor
//             Cores), then a probe-and-exploit race: every cycle each route gets 0.5 s, the
//             fastest runs for 4 s, under no load, direct-search load and matrix-search load.
// schedule    A running scheduler. Queued blocks need transform portions (32,768 dwells) and then
//             decode portions (16 hypotheses). A GPU executor and a CPU executor each run one
//             portion at a time. Fixed rules are compared with a cost-based rule that measures
//             the time of every portion, keeps a running cost per stage, route and executor,
//             explores other routes on about 5% of portions, gives each free executor the stage
//             in which it is least disadvantaged, and leaves a portion to the other executor
//             when that one would clear everything sooner. Scenarios: decode-heavy, dwell-heavy,
//             mixed, mixed with a second GPU load, and a GPU slowed fifty-fold by added waits.
// sustain R S Repeat route R for S seconds (for device telemetry).
//
// Build: nvcc -O3 -std=c++17 -arch=sm_120 -Xcompiler -pthread lattice_gpu.cu -lcufft -lcublas -lcublasLt -lcurand -o lattice_gpu
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cufft.h>
#include <cublas_v2.h>
#include <cublasLt.h>
#include <curand.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#define CK(x) do { cudaError_t e_ = (x); if (e_ != cudaSuccess) { fprintf(stderr, "CUDA error %s at %s:%d\n", cudaGetErrorString(e_), __FILE__, __LINE__); exit(2); } } while (0)
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static const double PI = 3.14159265358979323846;

// ---------------------------------------------------------------- transform
static const int NS = 1024, K2 = 2 * NS;       // samples per dwell, real inputs per dwell
static const int BIN0 = 160;                   // first in-band fine bin of the 1024-point grid

__global__ void tile_c(const cufftComplex* src, cufftComplex* dst, int unique, long total) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < total) dst[i] = src[i % ((long)unique * NS)];
}
__global__ void to_half(const cufftComplex* x, __half* h, long total) {  // total = B*NS
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < total) { h[2 * i] = __float2half(x[i].x); h[2 * i + 1] = __float2half(x[i].y); }
}
// 15-level sample code: round(x / step) clamped to -7..7, a component beyond 7.5 steps erases the sample
__global__ void to_code(const cufftComplex* x, int8_t* q, long total, float step) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < total) {
        float a = x[i].x / step, b = x[i].y / step;
        bool erase = fabsf(a) > 7.5f || fabsf(b) > 7.5f;
        q[2 * i] = erase ? 0 : (int8_t)lrintf(a);
        q[2 * i + 1] = erase ? 0 : (int8_t)lrintf(b);
    }
}
__global__ void f2h(const float* f, __half* h, long n) { long i = blockIdx.x * (long)blockDim.x + threadIdx.x; if (i < n) h[i] = __float2half(f[i]); }
// evidence = min(15, floor(energy / (0.5 * noise_mean)))
__global__ void ev_fft(const cufftComplex* y, uint8_t* e, int nb, long B, float scale) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < nb * B) { long d = i / nb; int b = (int)(i % nb); cufftComplex v = y[d * NS + BIN0 + b]; float g = (v.x * v.x + v.y * v.y) * scale; e[i] = (uint8_t)fminf(15.f, floorf(g)); }
}
__global__ void ev_f32(const float* c, uint8_t* e, int nb, long B, float scale) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < nb * B) { float re = c[2 * i], im = c[2 * i + 1]; e[i] = (uint8_t)fminf(15.f, floorf((re * re + im * im) * scale)); }
}
__global__ void ev_h16(const __half* c, uint8_t* e, int nb, long B, float scale) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < nb * B) { float re = __half2float(c[2 * i]), im = __half2float(c[2 * i + 1]); e[i] = (uint8_t)fminf(15.f, floorf((re * re + im * im) * scale)); }
}
__global__ void ev_i32(const int* c, uint8_t* e, int nb, long B, float scale) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < nb * B) { float re = (float)c[2 * i], im = (float)c[2 * i + 1]; e[i] = (uint8_t)fminf(15.f, floorf((re * re + im * im) * scale)); }
}

// one keyed tone per dwell: bin and complex gain from a counter hash; phasor table of NS entries
__device__ __forceinline__ unsigned hash32(unsigned x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
__global__ void add_tone(cufftComplex* x, const cufftComplex* table, long total, int nb, float amp, unsigned seed) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < total) {
        unsigned d = (unsigned)(i / NS); int n = (int)(i % NS);
        unsigned h = hash32(d * 2654435761U + seed);
        int bin = BIN0 + 2 * (int)(h % (unsigned)(nb / 2));
        // Rayleigh gain: two uniforms -> complex Gaussian (Box-Muller), constant over the dwell
        float u1 = (hash32(h + 1) + 0.5f) * 2.3283064e-10f, u2 = (hash32(h + 2) + 0.5f) * 2.3283064e-10f;
        float r = amp * sqrtf(-logf(u1)); int ph = (int)(u2 * NS);
        cufftComplex t = table[(int)(((long)bin * n + ph) % NS)];
        x[i].x += r * t.x; x[i].y += r * t.y;
    }
}

struct Lt {  // cuBLASLt INT8 x INT8 -> INT32 product, column-major, no transposes
    cublasLtHandle_t h = nullptr; cublasLtMatmulDesc_t op = nullptr; cublasLtMatrixLayout_t la = nullptr, lb = nullptr, lc = nullptr;
    void* ws = nullptr; size_t wsz = 256u << 20; bool ok = false; int status = 0;
    bool init(int m, int n, int k) {
        if (cublasLtCreate(&h) != CUBLAS_STATUS_SUCCESS) return false;
        if (cublasLtMatmulDescCreate(&op, CUBLAS_COMPUTE_32I, CUDA_R_32I) != CUBLAS_STATUS_SUCCESS) return false;
        if (cublasLtMatrixLayoutCreate(&la, CUDA_R_8I, m, k, m) != CUBLAS_STATUS_SUCCESS) return false;
        if (cublasLtMatrixLayoutCreate(&lb, CUDA_R_8I, k, n, k) != CUBLAS_STATUS_SUCCESS) return false;
        if (cublasLtMatrixLayoutCreate(&lc, CUDA_R_32I, m, n, m) != CUBLAS_STATUS_SUCCESS) return false;
        CK(cudaMalloc(&ws, wsz));
        ok = true; return true;
    }
    int run(const int8_t* A, const int8_t* B, int* C) {
        int alpha = 1, beta = 0;
        status = (int)cublasLtMatmul(h, op, &alpha, A, la, B, lb, &beta, C, lc, C, lc, nullptr, ws, wsz, 0);
        return status;
    }
};

struct Transform {
    int nb, m; long B;
    cufftComplex *xc = nullptr, *yc = nullptr; __half *x16 = nullptr, *d16 = nullptr, *c16 = nullptr; int8_t *x8 = nullptr, *d8a = nullptr, *d8b = nullptr;
    float* c32 = nullptr; int* ci = nullptr; uint8_t* ev = nullptr;
    cufftHandle plan = 0; cublasHandle_t blas = nullptr; Lt lt;
    std::vector<std::complex<float>> host;  // unique dwells
    int unique = 256; float step = 0;
    int gemm8_mode = -1;  // 0 cublasGemmEx, 1 cublasLt, -1 unavailable
    cudaStream_t strm = 0;  // 0 = default stream
    void use_stream(cudaStream_t s) { strm = s; cufftSetStream(plan, s); cublasSetStream(blas, s); }
    void sync() { if (strm) CK(cudaStreamSynchronize(strm)); else CK(cudaDeviceSynchronize()); }

    Transform(int nb_, long B_) : nb(nb_), m(2 * nb_), B(B_) {
        std::mt19937_64 rng(12345); std::normal_distribution<float> nd(0.f, std::sqrt(0.5f));
        host.resize((size_t)unique * NS);
        for (int d = 0; d < unique; ++d) {
            int bin = BIN0 + 2 * ((d * 37) % (nb / 2)); float amp = d % 4 == 0 ? 0.25f : 0.03f, ph = 0.7f * d;
            for (int n = 0; n < NS; ++n) host[(size_t)d * NS + n] = std::complex<float>(nd(rng), nd(rng)) + amp * std::polar(1.f, (float)(2 * PI * bin * n / NS) + ph);
        }
        step = 0.45f * std::sqrt(0.5f);
        cufftComplex* hx; CK(cudaMalloc(&hx, sizeof(cufftComplex) * unique * NS));
        CK(cudaMemcpy(hx, host.data(), sizeof(cufftComplex) * unique * NS, cudaMemcpyHostToDevice));
        long total = B * NS; int thr = 256; unsigned blocks = (unsigned)((total + thr - 1) / thr);
        CK(cudaMalloc(&xc, sizeof(cufftComplex) * total)); CK(cudaMalloc(&yc, sizeof(cufftComplex) * total));
        CK(cudaMalloc(&x16, sizeof(__half) * 2 * total)); CK(cudaMalloc(&x8, 2 * total));
        tile_c<<<blocks, thr>>>(hx, xc, unique, total);
        to_half<<<blocks, thr>>>(xc, x16, total);
        to_code<<<blocks, thr>>>(xc, x8, total, step);
        CK(cudaDeviceSynchronize()); CK(cudaFree(hx));
        // DFT matrices, column-major [m x 2048]: row 2b = Re bin b, row 2b+1 = Im bin b; column 2n = Re x[n], 2n+1 = Im x[n]
        std::vector<float> df((size_t)m * K2); std::vector<int8_t> da((size_t)m * K2), db((size_t)m * K2);
        for (int b = 0; b < nb; ++b) for (int n = 0; n < NS; ++n) {
            double th = 2 * PI * (double)(((long)(BIN0 + b) * n) % NS) / NS, c = std::cos(th), s = std::sin(th);
            double v[4] = {c, s, -s, c};  // (2b,2n) (2b,2n+1) (2b+1,2n) (2b+1,2n+1)
            size_t idx[4] = {(size_t)(2 * n) * m + 2 * b, (size_t)(2 * n + 1) * m + 2 * b, (size_t)(2 * n) * m + 2 * b + 1, (size_t)(2 * n + 1) * m + 2 * b + 1};
            for (int q = 0; q < 4; ++q) { df[idx[q]] = (float)v[q]; da[idx[q]] = (int8_t)std::lrint(127 * v[q]); db[idx[q]] = (int8_t)std::lrint(7 * v[q]); }
        }
        float* dfd; CK(cudaMalloc(&dfd, sizeof(float) * df.size())); CK(cudaMemcpy(dfd, df.data(), sizeof(float) * df.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&d16, sizeof(__half) * df.size()));
        f2h<<<(unsigned)((df.size() + 255) / 256), 256>>>(dfd, d16, (long)df.size()); CK(cudaDeviceSynchronize()); CK(cudaFree(dfd));
        CK(cudaMalloc(&d8a, da.size())); CK(cudaMemcpy(d8a, da.data(), da.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&d8b, db.size())); CK(cudaMemcpy(d8b, db.data(), db.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&c32, sizeof(float) * (size_t)m * B)); CK(cudaMalloc(&c16, sizeof(__half) * (size_t)m * B)); CK(cudaMalloc(&ci, sizeof(int) * (size_t)m * B));
        CK(cudaMalloc(&ev, (size_t)nb * B));
        int n1[1] = {NS};
        if (cufftPlanMany(&plan, 1, n1, nullptr, 1, NS, nullptr, 1, NS, CUFFT_C2C, (int)B) != CUFFT_SUCCESS) { fprintf(stderr, "cufft plan failed\n"); exit(2); }
        if (cublasCreate(&blas) != CUBLAS_STATUS_SUCCESS) { fprintf(stderr, "cublas create failed\n"); exit(2); }
        // probe the INT8 route
        int ia = 1, ib = 0;
        cublasStatus_t st = cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, m, (int)B, K2, &ia, d8a, CUDA_R_8I, m, x8, CUDA_R_8I, K2, &ib, ci, CUDA_R_32I, m, CUBLAS_COMPUTE_32I, CUBLAS_GEMM_DEFAULT);
        cudaDeviceSynchronize();
        printf("probe cublasGemmEx 8I,8I->32I: status %d\n", (int)st);
        if (st == CUBLAS_STATUS_SUCCESS) gemm8_mode = 0;
        else if (lt.init(m, (int)B, K2)) { int s2 = lt.run(d8a, x8, ci); cudaDeviceSynchronize(); printf("probe cublasLtMatmul 8I,8I->32I: status %d\n", s2); if (s2 == 0) gemm8_mode = 1; }
    }
    unsigned eb() const { return (unsigned)(((long)nb * B + 255) / 256); }
    void fft(bool evidence) {
        if (cufftExecC2C(plan, xc, yc, CUFFT_FORWARD) != CUFFT_SUCCESS) { fprintf(stderr, "cufft exec failed\n"); exit(2); }
        if (evidence) ev_fft<<<eb(), 256, 0, strm>>>(yc, ev, nb, B, 1.0f / (NS * 0.5f));
    }
    void dft16(bool evidence) {
        float a = 1, b = 0;
        cublasStatus_t st = cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, m, (int)B, K2, &a, d16, CUDA_R_16F, m, x16, CUDA_R_16F, K2, &b, c32, CUDA_R_32F, m, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT);
        if (st != CUBLAS_STATUS_SUCCESS) { fprintf(stderr, "dft16 gemm status %d\n", (int)st); exit(2); }
        if (evidence) ev_f32<<<eb(), 256, 0, strm>>>(c32, ev, nb, B, 1.0f / (NS * 0.5f));
    }
    bool dft16h(bool evidence) {
        __half a = __float2half(1.f), b = __float2half(0.f);
        cublasStatus_t st = cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, m, (int)B, K2, &a, d16, CUDA_R_16F, m, x16, CUDA_R_16F, K2, &b, c16, CUDA_R_16F, m, CUBLAS_COMPUTE_16F, CUBLAS_GEMM_DEFAULT);
        if (st != CUBLAS_STATUS_SUCCESS) return false;
        if (evidence) ev_h16<<<eb(), 256, 0, strm>>>(c16, ev, nb, B, 1.0f / (NS * 0.5f));
        return true;
    }
    bool dft8(bool evidence, bool coef4) {
        const int8_t* d = coef4 ? d8b : d8a;
        if (gemm8_mode == 0) { int a = 1, b = 0; if (cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, m, (int)B, K2, &a, d, CUDA_R_8I, m, x8, CUDA_R_8I, K2, &b, ci, CUDA_R_32I, m, CUBLAS_COMPUTE_32I, CUBLAS_GEMM_DEFAULT) != CUBLAS_STATUS_SUCCESS) return false; }
        else if (gemm8_mode == 1) { if (lt.run(d, x8, ci) != 0) return false; }
        else return false;
        // sample step and coefficient scale back to noise-mean units
        float cs = coef4 ? 7.f : 127.f, sc = step * step / (cs * cs) / (NS * 0.5f);
        if (evidence) ev_i32<<<eb(), 256, 0, strm>>>(ci, ev, nb, B, sc);
        return true;
    }
};

template <class F> static double timeit(F f, double min_seconds = 2.0) {
    f(); CK(cudaDeviceSynchronize());
    int it = 0; double t0 = now(), t;
    do { f(); ++it; if ((it & 3) == 0) CK(cudaDeviceSynchronize()); t = now() - t0; } while (t < min_seconds);
    CK(cudaDeviceSynchronize());
    return (now() - t0) / it;
}

static void run_transform(long B, int nb) {
    Transform T(nb, B);
    printf("transform: B=%ld dwells per call (%.1f h of signal), %d bins, matrix product %d x %d x %ld = %.1f GFLOP per call\n", B, B * 0.4 / 3600, nb, T.m, K2, B, 2.0 * T.m * K2 * B / 1e9);
    // accuracy on the first dwells against a host double-precision DFT
    {
        int nd = 8; std::vector<float> c32((size_t)T.m * nd); std::vector<int> ci((size_t)T.m * nd); std::vector<cufftComplex> yc((size_t)NS * nd); std::vector<int8_t> x8((size_t)K2 * nd);
        T.fft(false); T.dft16(false); CK(cudaDeviceSynchronize());
        CK(cudaMemcpy(yc.data(), T.yc, sizeof(cufftComplex) * yc.size(), cudaMemcpyDeviceToHost));
        CK(cudaMemcpy(c32.data(), T.c32, sizeof(float) * c32.size(), cudaMemcpyDeviceToHost));
        CK(cudaMemcpy(x8.data(), T.x8, x8.size(), cudaMemcpyDeviceToHost));
        double ef = 0, e16 = 0, ref = 0, eq = 0;
        std::vector<std::complex<double>> exact((size_t)nb * nd), exq((size_t)nb * nd);
        for (int d = 0; d < nd; ++d) for (int b = 0; b < nb; ++b) {
            std::complex<double> a = 0, q = 0;
            for (int n = 0; n < NS; ++n) { std::complex<double> k = std::polar(1.0, -2 * PI * (double)(((long)(BIN0 + b) * n) % NS) / NS); a += std::complex<double>(T.host[(size_t)d * NS + n]) * k; q += std::complex<double>(x8[(size_t)d * K2 + 2 * n], x8[(size_t)d * K2 + 2 * n + 1]) * k; }
            exact[(size_t)d * nb + b] = a; exq[(size_t)d * nb + b] = q; ref += std::norm(a);
            ef += std::norm(a - std::complex<double>(yc[(size_t)d * NS + BIN0 + b].x, yc[(size_t)d * NS + BIN0 + b].y));
            e16 += std::norm(a - std::complex<double>(c32[(size_t)d * T.m + 2 * b], c32[(size_t)d * T.m + 2 * b + 1]));
            eq += std::norm(a - q * (double)T.step);
        }
        printf("accuracy (error power / signal power): cuFFT %.1f dB, dft16 %.1f dB, 4-bit sample code alone %.1f dB\n", 10 * std::log10(ef / ref), 10 * std::log10(e16 / ref), 10 * std::log10(eq / ref));
        for (int coef4 = 0; coef4 < 2; ++coef4) {
            if (!T.dft8(false, coef4)) { printf("dft8: INT8 matrix product not available on this library/device\n"); break; }
            CK(cudaDeviceSynchronize()); CK(cudaMemcpy(ci.data(), T.ci, sizeof(int) * ci.size(), cudaMemcpyDeviceToHost));
            double e = 0, r = 0, cs = coef4 ? 7.0 : 127.0;
            for (int d = 0; d < nd; ++d) for (int b = 0; b < nb; ++b) { std::complex<double> g(ci[(size_t)d * T.m + 2 * b] / cs, ci[(size_t)d * T.m + 2 * b + 1] / cs); e += std::norm(g - exq[(size_t)d * nb + b]); r += std::norm(exq[(size_t)d * nb + b]); }
            printf("accuracy dft8 with %s coefficients against the exact transform of the same 4-bit samples: %.1f dB\n", coef4 ? "15-level (4-bit)" : "8-bit", 10 * std::log10(e / r));
        }
    }
    auto report = [&](const char* name, double sec) { printf("%-28s %9.3f ms/call  %12.0f dwells/s  %10.0f x real time  %8.1f nominal TFLOP/s\n", name, sec * 1e3, B / sec, B * 0.4 / sec, 2.0 * T.m * K2 * B / sec / 1e12); fflush(stdout); };
    report("fft (transform only)", timeit([&] { T.fft(false); }));
    report("fft + evidence", timeit([&] { T.fft(true); }));
    report("dft16 (product only)", timeit([&] { T.dft16(false); }));
    report("dft16 + evidence", timeit([&] { T.dft16(true); }));
    if (T.dft16h(false)) { report("dft16h (product only)", timeit([&] { T.dft16h(false); })); report("dft16h + evidence", timeit([&] { T.dft16h(true); })); } else printf("dft16h not supported\n");
    if (T.gemm8_mode >= 0) {
        report("dft8 8-bit coef (product)", timeit([&] { T.dft8(false, false); }));
        report("dft8 8-bit coef + evidence", timeit([&] { T.dft8(true, false); }));
        report("dft8 4-bit coef + evidence", timeit([&] { T.dft8(true, true); }));
    }
}

static void run_simulate(long B) {
    Transform T(704, B);
    curandGenerator_t gen;
    if (curandCreateGenerator(&gen, CURAND_RNG_PSEUDO_PHILOX4_32_10) != CURAND_STATUS_SUCCESS) { fprintf(stderr, "curand create failed\n"); exit(2); }
    curandSetPseudoRandomGeneratorSeed(gen, 20261126ULL);
    std::vector<cufftComplex> tab(NS); for (int k = 0; k < NS; ++k) { tab[k].x = (float)std::cos(2 * PI * k / NS); tab[k].y = (float)std::sin(2 * PI * k / NS); }
    cufftComplex* dtab; CK(cudaMalloc(&dtab, sizeof(cufftComplex) * NS)); CK(cudaMemcpy(dtab, tab.data(), sizeof(cufftComplex) * NS, cudaMemcpyHostToDevice));
    long total = B * NS; unsigned blocks = (unsigned)((total + 255) / 256); unsigned seed = 1;
    float amp = std::sqrt(0.25f / NS);  // -6 dB per dwell
    auto noise = [&] { if (curandGenerateNormal(gen, (float*)T.xc, (size_t)2 * total, 0.f, std::sqrt(0.5f)) != CURAND_STATUS_SUCCESS) { fprintf(stderr, "curand failed\n"); exit(2); } };
    auto tone = [&] { add_tone<<<blocks, 256>>>(T.xc, dtab, total, 704, amp, seed++); };
    auto half = [&] { to_half<<<blocks, 256>>>(T.xc, T.x16, total); };
    auto code = [&] { to_code<<<blocks, 256>>>(T.xc, T.x8, total, T.step); };
    auto report = [&](const char* name, double sec) { printf("%-44s %9.3f ms/call  %12.0f dwells/s  %10.0f x real time\n", name, sec * 1e3, B / sec, B * 0.4 / sec); fflush(stdout); };
    printf("simulate: B=%ld dwells per call (%.1f h of signal), 1024 complex samples each\n", B, B * 0.4 / 3600);
    report("noise (cuRAND Philox normal)", timeit(noise));
    report("tone with Rayleigh gain", timeit(tone));
    report("samples to FP16", timeit(half));
    report("samples to 4-bit code", timeit(code));
    report("dft16h + evidence", timeit([&] { T.dft16h(true); }));
    report("fft + evidence", timeit([&] { T.fft(true); }));
    report("total: noise, tone, FP16, dft16h, evidence", timeit([&] { noise(); tone(); half(); T.dft16h(true); }));
    report("total: noise, tone, fft, evidence", timeit([&] { noise(); tone(); T.fft(true); }));
    report("total: noise, tone, 4-bit code, dft8, evid.", timeit([&] { noise(); tone(); code(); T.dft8(true, true); }));
    // sanity: mean evidence level and the fraction of dwells whose tone bin holds the top level
    noise(); tone(); half(); T.dft16h(true); CK(cudaDeviceSynchronize());
    std::vector<uint8_t> e((size_t)704 * 4096); CK(cudaMemcpy(e.data(), T.ev, e.size(), cudaMemcpyDeviceToHost));
    double m = 0; for (auto v : e) m += v; printf("mean 4-bit evidence level %.3f (noise only would be about 1.54)\n", m / e.size());
}

// ---------------------------------------------------------------- decode
static const int NSYM = 24, MT = 64, KD = NSYM * MT, HB = 12, NH = 1 << HB;  // 24 symbols, 64 tones, 12+12 bits

__global__ void build_w8(const uint8_t* tq, const uint8_t* glo, int8_t* w, int A) {  // w[KD x NH*A]
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    long total = (long)KD * NH * A;
    if (i < total) { int row = (int)(i % KD); long col = i / KD; int clo = (int)(col % NH), a = (int)(col / NH), j = row / MT, t = row % MT; w[i] = (int8_t)tq[(size_t)a * KD + j * MT + (t ^ glo[j * NH + clo])]; }
}
__global__ void build_w16(const uint8_t* tq, const uint8_t* glo, __half* w, int A) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    long total = (long)KD * NH * A;
    if (i < total) { int row = (int)(i % KD); long col = i / KD; int clo = (int)(col % NH), a = (int)(col / NH), j = row / MT, t = row % MT; w[i] = __float2half((float)tq[(size_t)a * KD + j * MT + (t ^ glo[j * NH + clo])]); }
}
// column maxima of a column-major [NH x cols] matrix; one block per column
template <class T> __global__ void colmax(const T* s, int* best, int* arg, long cols) {
    __shared__ int sv[256]; __shared__ int si[256];
    long col = blockIdx.x; int tid = threadIdx.x; int bv = -2147483647, bi = 0;
    for (int r = tid; r < NH; r += 256) { int v = (int)s[col * NH + r]; if (v > bv) { bv = v; bi = r; } }
    sv[tid] = bv; si[tid] = bi; __syncthreads();
    for (int h = 128; h > 0; h >>= 1) { if (tid < h && sv[tid + h] > sv[tid]) { sv[tid] = sv[tid + h]; si[tid] = si[tid + h]; } __syncthreads(); }
    if (tid == 0) { best[col] = sv[0]; arg[col] = si[0]; }
}
// direct: one block per (hypothesis, c_hi); threads sweep c_lo
__global__ void direct_k(const uint8_t* tq, const uint8_t* ghi, const uint8_t* glo, int* best, int* arg) {
    __shared__ uint8_t t[KD]; __shared__ uint8_t gh[NSYM]; __shared__ int sv[256]; __shared__ int si[256];
    int a = blockIdx.x / NH, chi = blockIdx.x % NH, tid = threadIdx.x;
    for (int i = tid; i < KD; i += 256) t[i] = tq[(size_t)a * KD + i];
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    int bv = -1, bi = 0;
    for (int clo = tid; clo < NH; clo += 256) { int s = 0; for (int j = 0; j < NSYM; ++j) s += t[j * MT + (gh[j] ^ glo[j * NH + clo])]; if (s > bv) { bv = s; bi = clo; } }
    sv[tid] = bv; si[tid] = bi; __syncthreads();
    for (int h = 128; h > 0; h >>= 1) { if (tid < h && sv[tid + h] > sv[tid]) { sv[tid] = sv[tid + h]; si[tid] = si[tid + h]; } __syncthreads(); }
    if (tid == 0) { best[blockIdx.x] = sv[0]; arg[blockIdx.x] = si[0]; }
}
__global__ void fwht_pass(int* x, long n, long h) {
    long p = blockIdx.x * (long)blockDim.x + threadIdx.x;  // pair index 0..n/2-1
    if (p < n / 2) { long i = ((p & ~(h - 1)) << 1) | (p & (h - 1)); int a = x[i], b = x[i | h]; x[i] = a + b; x[i | h] = a - b; }
}
__global__ void scatter_k(int* x, const int* idx, const int* val, int n) { int i = blockIdx.x * blockDim.x + threadIdx.x; if (i < n) atomicAdd(&x[idx[i]], val[i]); }
__global__ void argmax_all(const int* x, long n, int* best, int* arg) {  // one block per 2^12 chunk
    __shared__ int sv[256]; __shared__ int si[256];
    long base = (long)blockIdx.x * NH; int tid = threadIdx.x; int bv = -2147483647, bi = 0;
    for (int r = tid; r < NH; r += 256) { int v = x[base + r]; if (v > bv) { bv = v; bi = r; } }
    sv[tid] = bv; si[tid] = bi; __syncthreads();
    for (int h = 128; h > 0; h >>= 1) { if (tid < h && sv[tid + h] > sv[tid]) { sv[tid] = sv[tid + h]; si[tid] = si[tid + h]; } __syncthreads(); }
    if (tid == 0) { best[blockIdx.x] = sv[0]; arg[blockIdx.x] = si[0]; }
}

struct Decode {
    int A; std::vector<uint32_t> G;  // G[j*6 + bit] = 24-bit row mask
    std::vector<uint8_t> ghi, glo, tq; std::vector<int> planted;
    uint8_t *dtq = nullptr, *dghi = nullptr, *dglo = nullptr; int8_t *p8 = nullptr, *w8 = nullptr; __half *p16 = nullptr, *w16 = nullptr;
    int *s32 = nullptr, *best = nullptr, *arg = nullptr, *wh = nullptr, *sidx = nullptr, *sval = nullptr; float* sf = nullptr;
    cublasHandle_t blas = nullptr; Lt lt; int gemm8_mode = -1;
    cudaStream_t strm = 0;
    void use_stream(cudaStream_t s) { strm = s; cublasSetStream(blas, s); }
    void sync() { if (strm) CK(cudaStreamSynchronize(strm)); else CK(cudaDeviceSynchronize()); }
    static int par(uint32_t v) { return __builtin_parity(v); }
    int tone(int j, uint32_t c) const { int t = 0; for (int b = 0; b < 6; ++b) t |= par(G[j * 6 + b] & c) << b; return t; }

    explicit Decode(int A_) : A(A_) {
        std::mt19937_64 rng(777);
        G.resize(NSYM * 6); for (auto& g : G) g = (uint32_t)(rng() & 0xFFFFFF);
        ghi.resize(NSYM * NH); glo.resize(NSYM * NH);
        for (int j = 0; j < NSYM; ++j) for (int v = 0; v < NH; ++v) { ghi[j * NH + v] = (uint8_t)tone(j, (uint32_t)v << HB); glo[j * NH + v] = (uint8_t)tone(j, (uint32_t)v); }
        tq.resize((size_t)A * KD); planted.resize(A);
        for (int a = 0; a < A; ++a) {
            for (int i = 0; i < KD; ++i) { double e = -std::log((rng() >> 11) * (1.0 / 9007199254740992.0) + 1e-300); tq[(size_t)a * KD + i] = (uint8_t)std::min(15.0, std::floor(e / 0.5)); }
            planted[a] = (int)(rng() & 0xFFFFFF);
            for (int j = 0; j < NSYM; ++j) { uint8_t& v = tq[(size_t)a * KD + j * MT + tone(j, planted[a])]; v = (uint8_t)std::min(15, v + 9); }
        }
        CK(cudaMalloc(&dtq, tq.size())); CK(cudaMemcpy(dtq, tq.data(), tq.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&dghi, ghi.size())); CK(cudaMemcpy(dghi, ghi.data(), ghi.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&dglo, glo.size())); CK(cudaMemcpy(dglo, glo.data(), glo.size(), cudaMemcpyHostToDevice));
        std::vector<int8_t> p((size_t)NH * KD, 0); std::vector<float> pf((size_t)NH * KD, 0.f);
        for (int chi = 0; chi < NH; ++chi) for (int j = 0; j < NSYM; ++j) { size_t i = (size_t)(j * MT + ghi[j * NH + chi]) * NH + chi; p[i] = 1; pf[i] = 1.f; }
        CK(cudaMalloc(&p8, p.size())); CK(cudaMemcpy(p8, p.data(), p.size(), cudaMemcpyHostToDevice));
        float* pfd; CK(cudaMalloc(&pfd, sizeof(float) * pf.size())); CK(cudaMemcpy(pfd, pf.data(), sizeof(float) * pf.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&p16, sizeof(__half) * pf.size())); f2h<<<(unsigned)((pf.size() + 255) / 256), 256>>>(pfd, p16, (long)pf.size()); CK(cudaDeviceSynchronize()); CK(cudaFree(pfd));
        size_t wn = (size_t)KD * NH * A, sn = (size_t)NH * NH * A;
        CK(cudaMalloc(&w8, wn)); CK(cudaMalloc(&w16, sizeof(__half) * wn)); CK(cudaMalloc(&s32, sizeof(int) * sn)); CK(cudaMalloc(&sf, sizeof(float) * sn));
        CK(cudaMalloc(&best, sizeof(int) * NH * A)); CK(cudaMalloc(&arg, sizeof(int) * NH * A));
        CK(cudaMalloc(&wh, sizeof(int) * (size_t)NH * NH)); CK(cudaMalloc(&sidx, sizeof(int) * KD)); CK(cudaMalloc(&sval, sizeof(int) * KD));
        if (cublasCreate(&blas) != CUBLAS_STATUS_SUCCESS) { fprintf(stderr, "cublas create failed\n"); exit(2); }
        build_w8<<<(unsigned)((wn + 255) / 256), 256>>>(dtq, dglo, w8, A); CK(cudaDeviceSynchronize());
        int ia = 1, ib = 0;
        cublasStatus_t st = cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, NH, NH * A, KD, &ia, p8, CUDA_R_8I, NH, w8, CUDA_R_8I, KD, &ib, s32, CUDA_R_32I, NH, CUBLAS_COMPUTE_32I, CUBLAS_GEMM_DEFAULT);
        cudaDeviceSynchronize(); printf("probe cublasGemmEx 8I,8I->32I: status %d\n", (int)st);
        if (st == CUBLAS_STATUS_SUCCESS) gemm8_mode = 0;
        else if (lt.init(NH, NH * A, KD)) { int s2 = lt.run(p8, w8, s32); cudaDeviceSynchronize(); printf("probe cublasLtMatmul 8I,8I->32I: status %d\n", s2); if (s2 == 0) gemm8_mode = 1; }
    }
    struct Out { std::vector<int> word, score; };
    Out collect() {  // best/arg hold one entry per (hypothesis, 4096-chunk); the chunk index is the other half of the word
        std::vector<int> b(NH * A), g(NH * A); CK(cudaMemcpy(b.data(), best, sizeof(int) * b.size(), cudaMemcpyDeviceToHost)); CK(cudaMemcpy(g.data(), arg, sizeof(int) * g.size(), cudaMemcpyDeviceToHost));
        Out o; o.word.assign(A, 0); o.score.assign(A, -1);
        for (int a = 0; a < A; ++a) for (int c = 0; c < NH; ++c) if (b[a * NH + c] > o.score[a]) { o.score[a] = b[a * NH + c]; o.word[a] = (c << 16) | g[a * NH + c]; }
        return o;
    }
    bool gemm8(bool reduce) {
        build_w8<<<(unsigned)(((size_t)KD * NH * A + 255) / 256), 256, 0, strm>>>(dtq, dglo, w8, A);
        if (gemm8_mode == 0) { int a = 1, b = 0; if (cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, NH, NH * A, KD, &a, p8, CUDA_R_8I, NH, w8, CUDA_R_8I, KD, &b, s32, CUDA_R_32I, NH, CUBLAS_COMPUTE_32I, CUBLAS_GEMM_DEFAULT) != CUBLAS_STATUS_SUCCESS) return false; }
        else if (gemm8_mode == 1) { if (lt.run(p8, w8, s32) != 0) return false; }
        else return false;
        if (reduce) colmax<int><<<NH * A, 256, 0, strm>>>(s32, best, arg, (long)NH * A);  // column = (hypothesis, c_lo), row = c_hi
        return true;
    }
    void gemm16(bool reduce) {
        build_w16<<<(unsigned)(((size_t)KD * NH * A + 255) / 256), 256, 0, strm>>>(dtq, dglo, w16, A);
        float a = 1, b = 0;
        cublasStatus_t st = cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, NH, NH * A, KD, &a, p16, CUDA_R_16F, NH, w16, CUDA_R_16F, KD, &b, sf, CUDA_R_32F, NH, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT);
        if (st != CUBLAS_STATUS_SUCCESS) { fprintf(stderr, "gemm16 status %d\n", (int)st); exit(2); }
        if (reduce) colmax<float><<<NH * A, 256, 0, strm>>>(sf, best, arg, (long)NH * A);
    }
    void direct() { direct_k<<<NH * A, 256, 0, strm>>>(dtq, dghi, dglo, best, arg); }
    // fwht for hypothesis a: results in best/arg rows [a*NH, (a+1)*NH) with chunk = high 12 bits of the word
    void fwht_one(int a, const std::vector<int>& idx, const std::vector<int>& val) {
        long n = (long)NH * NH;
        CK(cudaMemsetAsync(wh, 0, sizeof(int) * n));
        CK(cudaMemcpyAsync(sidx, idx.data(), sizeof(int) * KD, cudaMemcpyHostToDevice)); CK(cudaMemcpyAsync(sval, val.data() + (size_t)a * KD, sizeof(int) * KD, cudaMemcpyHostToDevice));
        scatter_k<<<(KD + 255) / 256, 256>>>(wh, sidx, sval, KD);
        for (long h = 1; h < n; h <<= 1) fwht_pass<<<(unsigned)((n / 2 + 255) / 256), 256>>>(wh, n, h);
        argmax_all<<<NH, 256>>>(wh, n, best + a * NH, arg + a * NH);
    }
};

static void run_decode(int A) {
    Decode D(A);
    printf("decode: %d hypotheses per call, 2^24 messages each; matrix product %d x %d x %d = %.1f GFLOP per call\n", A, NH, KD, NH * A, 2.0 * NH * KD * NH * A / 1e9);
    // Walsh spectra of the tables for the fwht route: index v = G_j^T u, value That_j[u]
    std::vector<int> idx(KD), val((size_t)A * KD);
    for (int j = 0; j < NSYM; ++j) for (int u = 0; u < MT; ++u) { uint32_t v = 0; for (int b = 0; b < 6; ++b) if (u >> b & 1) v ^= D.G[j * 6 + b]; idx[j * MT + u] = (int)v; }
    for (int a = 0; a < A; ++a) for (int j = 0; j < NSYM; ++j) for (int u = 0; u < MT; ++u) { int s = 0; for (int t = 0; t < MT; ++t) s += (__builtin_parity(u & t) ? -1 : 1) * D.tq[(size_t)a * KD + j * MT + t]; val[(size_t)a * KD + j * MT + u] = s; }
    auto fw = [&] { for (int a = 0; a < A; ++a) D.fwht_one(a, idx, val); };
    // verification: every route must return the same best score; the planted word should win
    D.direct(); CK(cudaDeviceSynchronize()); auto od = D.collect();
    // direct: chunk = c_hi, arg = c_lo -> word = (chi << 16 | clo) in collect; convert to 24-bit
    auto word_direct = [&](int w) { return ((w >> 16) << HB) | (w & 0xFFFF); };
    auto word_gemm = [&](int w) { return ((w & 0xFFFF) << HB) | (w >> 16); };  // column index = c_lo (chunk), row = c_hi (arg)
    int okd = 0; for (int a = 0; a < A; ++a) okd += word_direct(od.word[a]) == D.planted[a];
    printf("verify direct: planted word found in %d of %d hypotheses, best score of the first %d\n", okd, A, od.score[0]);
    D.gemm16(true); CK(cudaDeviceSynchronize()); auto og = D.collect();
    int same = 0; for (int a = 0; a < A; ++a) same += og.score[a] == od.score[a] && word_gemm(og.word[a]) == word_direct(od.word[a]);
    printf("verify gemm16: %d of %d hypotheses agree with direct (score and word)\n", same, A);
    if (D.gemm8(true)) { CK(cudaDeviceSynchronize()); auto o8 = D.collect(); int s8 = 0; for (int a = 0; a < A; ++a) s8 += o8.score[a] == od.score[a] && word_gemm(o8.word[a]) == word_direct(od.word[a]); printf("verify gemm8: %d of %d hypotheses agree with direct\n", s8, A); }
    else printf("gemm8: INT8 matrix product not available\n");
    fw(); CK(cudaDeviceSynchronize()); auto of = D.collect();
    int sf = 0; for (int a = 0; a < A; ++a) sf += of.score[a] == od.score[a] * MT && word_direct(of.word[a]) == word_direct(od.word[a]);
    printf("verify fwht: %d of %d hypotheses agree with direct (score x 64, word)\n", sf, A);
    auto report = [&](const char* name, double sec) { printf("%-30s %9.3f ms/call  %10.1f hypotheses/s  %8.1f nominal TFLOP/s (dense product count)\n", name, sec * 1e3, A / sec, 2.0 * NH * KD * NH * A / sec / 1e12); fflush(stdout); };
    report("direct (CUDA cores)", timeit([&] { D.direct(); }));
    report("fwht (CUDA cores)", timeit(fw));
    report("gemm16 (product only)", timeit([&] { D.gemm16(false); }));
    report("gemm16 + maxima", timeit([&] { D.gemm16(true); }));
    if (D.gemm8_mode >= 0) { report("gemm8 (product only)", timeit([&] { D.gemm8(false); })); report("gemm8 + maxima", timeit([&] { D.gemm8(true); })); }
}

// ---------------------------------------------------------------- CPU route, chunks, contention
struct CpuFft {  // radix-2 complex FP32, plain C++ (not a tuned library)
    int n; std::vector<std::complex<float>> tw; std::vector<int> rev;
    explicit CpuFft(int n_) : n(n_), tw(n_ / 2), rev(n_) {
        for (int i = 0; i < n / 2; ++i) tw[i] = std::polar(1.f, (float)(-2 * PI * i / n));
        int lg = 0; while ((1 << lg) < n) ++lg;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < lg; ++b) if (i >> b & 1) r |= 1 << (lg - 1 - b); rev[i] = r; }
    }
    void run(std::complex<float>* a) const {
        for (int i = 0; i < n; ++i) if (i < rev[i]) std::swap(a[i], a[rev[i]]);
        for (int len = 2; len <= n; len <<= 1) { int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len) for (int j = 0; j < half; ++j) { std::complex<float> u = a[i + j], v = a[i + j + half] * tw[j * step]; a[i + j] = u + v; a[i + j + half] = u - v; } }
    }
};
struct CpuRoute {  // FFT and 4-bit evidence on host threads, evidence uploaded to the device
    Transform& T; int threads; CpuFft plan; uint8_t* hev = nullptr; uint8_t* dst = nullptr; cudaStream_t ds = 0; bool own = false;
    CpuRoute(Transform& t, int th) : T(t), threads(th), plan(NS) { CK(cudaMallocHost(&hev, (size_t)T.nb * T.B)); }
    void own_target(cudaStream_t s) { CK(cudaMalloc(&dst, (size_t)T.nb * T.B)); ds = s; own = true; }
    ~CpuRoute() { cudaFreeHost(hev); }
    void run() {
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) pool.emplace_back([&, t] {
            std::vector<std::complex<float>> buf(NS); const float sc = 1.0f / (NS * 0.5f);
            for (long d = T.B * t / threads; d < T.B * (t + 1) / threads; ++d) {
                std::memcpy(buf.data(), &T.host[(size_t)(d % T.unique) * NS], sizeof(std::complex<float>) * NS);
                plan.run(buf.data());
                for (int b = 0; b < T.nb; ++b) hev[(size_t)d * T.nb + b] = (uint8_t)std::min(15.f, std::floor(std::norm(buf[BIN0 + b]) * sc));
            }
        });
        for (auto& th : pool) th.join();
        if (own) { CK(cudaMemcpyAsync(dst, hev, (size_t)T.nb * T.B, cudaMemcpyHostToDevice, ds)); CK(cudaStreamSynchronize(ds)); }
        else CK(cudaMemcpyAsync(T.ev, hev, (size_t)T.nb * T.B, cudaMemcpyHostToDevice, T.strm));
    }
};
template <class F> static double rate(Transform& T, F f, double seconds) {  // dwells per second on T's stream
    f(); T.sync();
    long it = 0; double t0 = now();
    do { f(); ++it; if ((it & 3) == 0) T.sync(); } while (now() - t0 < seconds);
    T.sync();
    return it * (double)T.B / (now() - t0);
}
static void run_chunks(int cpu_threads) {
    printf("chunks: 352 bins, dwells per call against route; CPU route uses %d host threads (plain radix-2 FFT)\n", cpu_threads);
    printf("%10s %14s %14s %14s %14s   (dwells/s)\n", "B", "fft", "dft16h", "dft8", "cpu");
    for (long B : {64L, 512L, 4096L, 32768L}) {
        Transform T(352, B); cudaStream_t s; CK(cudaStreamCreate(&s)); T.use_stream(s);
        CpuRoute C(T, cpu_threads);
        double a = rate(T, [&] { T.fft(true); }, 1.0), b = rate(T, [&] { T.dft16h(true); }, 1.0), c = T.gemm8_mode >= 0 ? rate(T, [&] { T.dft8(true, true); }, 1.0) : 0, d = rate(T, [&] { C.run(); }, 1.0);
        printf("%10ld %14.0f %14.0f %14.0f %14.0f\n", B, a, b, c, d); fflush(stdout);
    }
}
static void run_contend(int cpu_threads) {
    cudaStream_t sa, sb; CK(cudaStreamCreate(&sa)); CK(cudaStreamCreate(&sb));
    Transform T(352, 32768); T.use_stream(sa);
    CpuRoute C(T, cpu_threads);
    Decode D(16); D.use_stream(sb);
    std::atomic<int> load(0); std::atomic<long> decodes(0); std::atomic<bool> stop(false);
    std::thread bg([&] {
        long it = 0;
        while (!stop.load()) {
            int m = load.load();
            if (m == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
            if (m == 1) D.direct(); else D.gemm16(true);
            if ((++it & 1) == 0) D.sync();
            decodes.fetch_add(16);
        }
        D.sync();
    });
    const char* rn[4] = {"fft", "dft16h", "dft8", "cpu"};
    auto call = [&](int r) { if (r == 0) T.fft(true); else if (r == 1) T.dft16h(true); else if (r == 2) T.dft8(true, true); else C.run(); };
    const char* ln[3] = {"no search", "direct search (CUDA cores)", "matrix search (Tensor Cores)"};
    printf("contend: 352 bins, 32768 dwells per call; decode of 16 hypotheses per call on a second stream; CPU route %d threads\n", cpu_threads);
    printf("%-30s %-8s %16s %18s\n", "search load", "route", "dwells/s", "decodes/s meanwhile");
    double alone[3] = {0, 0, 0};
    for (int m = 1; m < 3; ++m) { load = m; std::this_thread::sleep_for(std::chrono::milliseconds(500)); long c0 = decodes.load(); double t0 = now(); std::this_thread::sleep_for(std::chrono::seconds(3)); alone[m] = (decodes.load() - c0) / (now() - t0); }
    printf("%-30s %-8s %16s %18.0f\n%-30s %-8s %16s %18.0f\n", ln[1], "(none)", "-", alone[1], ln[2], "(none)", "-", alone[2]);
    for (int m = 0; m < 3; ++m) {
        load = m; std::this_thread::sleep_for(std::chrono::milliseconds(500));
        for (int r = 0; r < 4; ++r) {
            if (r == 2 && T.gemm8_mode < 0) continue;
            long c0 = decodes.load(); double t0 = now();
            double dps = rate(T, [&] { call(r); }, 3.0);
            double hps = (decodes.load() - c0) / (now() - t0);
            printf("%-30s %-8s %16.0f %18.0f\n", ln[m], rn[r], dps, hps); fflush(stdout);
        }
    }
    printf("\nrace: probe every route for 0.5 s, run the fastest for 4 s, two cycles per load\n");
    printf("%-30s %-44s %-8s %16s %18s\n", "search load", "probe dwells/s (fft dft16h dft8 cpu)", "winner", "dwells/s", "decodes/s meanwhile");
    for (int m = 0; m < 3; ++m) {
        load = m; std::this_thread::sleep_for(std::chrono::milliseconds(500));
        for (int cyc = 0; cyc < 2; ++cyc) {
            double p[4] = {0, 0, 0, 0}; int w = 0;
            for (int r = 0; r < 4; ++r) { if (r == 2 && T.gemm8_mode < 0) continue; p[r] = rate(T, [&] { call(r); }, 0.5); if (p[r] > p[w]) w = r; }
            long c0 = decodes.load(); double t0 = now();
            double dps = rate(T, [&] { call(w); }, 4.0);
            double hps = (decodes.load() - c0) / (now() - t0);
            char pb[96]; snprintf(pb, sizeof pb, "%.1fM %.1fM %.1fM %.1fM", p[0] / 1e6, p[1] / 1e6, p[2] / 1e6, p[3] / 1e6);
            printf("%-30s %-44s %-8s %16.0f %18.0f\n", ln[m], pb, rn[w], dps, hps); fflush(stdout);
        }
    }
    stop = true; bg.join();
}

// ---------------------------------------------------------------- running scheduler test
struct CpuDecode {  // the direct decode on host threads
    Decode& D; int threads; std::vector<uint8_t> W; std::vector<int> word, score;
    CpuDecode(Decode& d, int th) : D(d), threads(th), W((size_t)NSYM * MT * NH), word(d.A), score(d.A) {}
    void run() {
        for (int a = 0; a < D.A; ++a) {
            { std::vector<std::thread> pool;
              for (int t = 0; t < threads; ++t) pool.emplace_back([&, t] {
                  for (int j = t; j < NSYM; j += threads) for (int tp = 0; tp < MT; ++tp) {
                      uint8_t* row = &W[((size_t)j * MT + tp) * NH]; const uint8_t* tq = &D.tq[(size_t)a * KD + j * MT]; const uint8_t* gl = &D.glo[(size_t)j * NH];
                      for (int c = 0; c < NH; ++c) row[c] = tq[tp ^ gl[c]]; } });
              for (auto& th : pool) th.join(); }
            std::vector<int> bs(threads, -1), bw(threads, 0); std::vector<std::thread> pool;
            for (int t = 0; t < threads; ++t) pool.emplace_back([&, t] {
                std::vector<uint16_t> acc(NH);
                for (int chi = NH * t / threads; chi < NH * (t + 1) / threads; ++chi) {
                    std::fill(acc.begin(), acc.end(), (uint16_t)0);
                    for (int j = 0; j < NSYM; ++j) { const uint8_t* row = &W[((size_t)j * MT + D.ghi[(size_t)j * NH + chi]) * NH]; for (int c = 0; c < NH; ++c) acc[c] = (uint16_t)(acc[c] + row[c]); }
                    for (int c = 0; c < NH; ++c) if ((int)acc[c] > bs[t]) { bs[t] = acc[c]; bw[t] = (chi << HB) | c; }
                } });
            for (auto& th : pool) th.join();
            int b = 0; for (int t = 1; t < threads; ++t) if (bs[t] > bs[b]) b = t;
            score[a] = bs[b]; word[a] = bw[b];
        }
    }
};
struct Policy { const char* name; bool adaptive, gpu_tr, gpu_dec, cpu_tr, cpu_dec; int gtr, gdec; };
struct Block { int ntr, ndec; };
static void run_schedule(int cpu_threads) {
    cudaStream_t sg, sc, sh; CK(cudaStreamCreate(&sg)); CK(cudaStreamCreate(&sc)); CK(cudaStreamCreate(&sh));
    Transform T4(352, 4096), T32(352, 32768); T4.use_stream(sg); T32.use_stream(sg);
    CpuRoute C(T32, cpu_threads); C.own_target(sc);
    Decode D(16); D.use_stream(sg);
    Decode H(16); H.use_stream(sh);            // second GPU load for one scenario
    CpuDecode CD(D, cpu_threads);
    CD.run(); { int ok = 0; for (int a = 0; a < 16; ++a) ok += CD.word[a] == D.planted[a]; printf("verify cpu decode: planted word found in %d of 16 hypotheses\n", ok); }
    const int NR[2][2] = {{5, 3}, {1, 1}};   // routes per [executor][stage]; executor 0 GPU, 1 CPU; stage 0 transform, 1 decode
    const char* RN[2][2][5] = {{{"cuFFT@4096", "cuFFT@32768", "FP16 DFT@4096", "FP16 DFT@32768", "INT8 DFT@32768"}, {"direct", "FP16 matrix", "INT8 matrix", "", ""}}, {{"CPU FFT", "", "", "", ""}, {"CPU direct", "", "", "", ""}}};
    auto run_portion = [&](int e, int st, int r) {
        if (e == 0 && st == 0) { if (r == 0) for (int i = 0; i < 8; ++i) T4.fft(true); else if (r == 1) T32.fft(true); else if (r == 2) for (int i = 0; i < 8; ++i) T4.dft16h(true); else if (r == 3) T32.dft16h(true); else T32.dft8(true, true); CK(cudaStreamSynchronize(sg)); }
        else if (e == 0) { if (r == 0) D.direct(); else if (r == 1) D.gemm16(true); else D.gemm8(true); CK(cudaStreamSynchronize(sg)); }
        else if (st == 0) C.run();
        else CD.run();
    };
    for (int r = 0; r < 5; ++r) run_portion(0, 0, r);   // warm every route before any timing
    for (int r = 0; r < 3; ++r) run_portion(0, 1, r);
    run_portion(1, 0, 0);
    std::atomic<bool> hog(false), hstop(false);
    std::thread hogt([&] { long it = 0; while (!hstop.load()) { if (!hog.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; } H.gemm16(true); if ((++it & 1) == 0) CK(cudaStreamSynchronize(sh)); } CK(cudaStreamSynchronize(sh)); });

    struct Out { double secs; long n[2][2]; double cost[2][2][5]; long cnt[2][2][5]; bool timeout; };
    auto run_policy = [&](const Policy& P, const std::vector<Block>& wl, double throttle, double limit) {
        const int nb = (int)wl.size(), WINDOW = 2;
        std::vector<int> tri(nb, 0), trd(nb, 0), dci(nb, 0), dcd(nb, 0);
        std::mutex m; Out o{}; std::atomic<bool> abort(false);
        long taken[2][2] = {{0, 0}, {0, 0}};
        auto first_open = [&] { int b = 0; while (b < nb && trd[b] == wl[b].ntr && dcd[b] == wl[b].ndec) ++b; return b; };
        auto find = [&](int st) { int f = first_open(); for (int b = f; b < std::min(nb, f + WINDOW); ++b) { if (st == 0 && tri[b] < wl[b].ntr) return b; if (st == 1 && trd[b] == wl[b].ntr && dci[b] < wl[b].ndec) return b; } return -1; };
        auto best = [&](int e, int st) { double c = 0; for (int r = 0; r < NR[e][st]; ++r) if (o.cnt[e][st][r] && (c == 0 || o.cost[e][st][r] < c)) c = o.cost[e][st][r]; return c; };
        auto worker = [&](int e) {
            bool allow[2] = {e == 0 ? P.gpu_tr : P.cpu_tr, e == 0 ? P.gpu_dec : P.cpu_dec};
            while (!abort.load()) {
                int st = -1, blk = -1, route = 0;
                {
                    std::lock_guard<std::mutex> g(m);
                    if (first_open() == nb) break;
                    int av[2] = {allow[0] ? find(0) : -1, allow[1] ? find(1) : -1};
                    if (!P.adaptive) { st = av[0] >= 0 ? 0 : (av[1] >= 0 ? 1 : -1); if (st >= 0) route = e == 0 ? (st == 0 ? P.gtr : P.gdec) : 0; }
                    else {
                        for (int s2 = 0; s2 < 2 && st < 0; ++s2) if (av[s2] >= 0) for (int r = 0; r < NR[e][s2]; ++r) if (!o.cnt[e][s2][r]) { st = s2; route = r; break; }   // unexplored route first
                        if (st < 0) {
                            double ratio[2] = {1e300, 1e300};
                            for (int s2 = 0; s2 < 2; ++s2) if (av[s2] >= 0) { double ce = best(e, s2), co = best(1 - e, s2); ratio[s2] = co > 0 ? ce / co : 1.0; }
                            st = ratio[0] <= ratio[1] ? (av[0] >= 0 ? 0 : (av[1] >= 0 ? 1 : -1)) : (av[1] >= 0 ? 1 : (av[0] >= 0 ? 0 : -1));
                            if (st >= 0) {
                                // leave the portion to the other executor if that one would clear all queued work sooner
                                double rem[2] = {0, 0}; for (int b = 0; b < nb; ++b) { rem[0] += wl[b].ntr - tri[b]; rem[1] += wl[b].ndec - dci[b]; }
                                double co0 = best(1 - e, 0), co1 = best(1 - e, 1);
                                if (co0 > 0 && co1 > 0 && best(e, st) > rem[0] * co0 + rem[1] * co1) st = -1;
                            }
                            if (st >= 0) {
                                int rb = 0; for (int r = 1; r < NR[e][st]; ++r) if (o.cost[e][st][r] < o.cost[e][st][rb]) rb = r;
                                route = rb;
                                if (NR[e][st] > 1 && taken[e][st] % 20 == 19) route = (rb + 1 + (int)((taken[e][st] / 20) % (NR[e][st] - 1))) % NR[e][st];   // about 5% exploration
                            }
                        }
                    }
                    if (st >= 0) { blk = find(st); if (st == 0) ++tri[blk]; else ++dci[blk]; ++taken[e][st]; }
                }
                if (st < 0) { std::this_thread::sleep_for(std::chrono::microseconds(100)); continue; }
                double t0 = now(); run_portion(e, st, route); double dt = now() - t0;
                if (e == 0 && throttle > 1) { std::this_thread::sleep_for(std::chrono::duration<double>((throttle - 1) * dt)); dt *= throttle; }
                std::lock_guard<std::mutex> g(m);
                if (st == 0) ++trd[blk]; else ++dcd[blk];
                ++o.n[e][st];
                o.cost[e][st][route] = o.cnt[e][st][route] ? 0.7 * o.cost[e][st][route] + 0.3 * dt : dt; ++o.cnt[e][st][route];
            }
        };
        double t0 = now();
        std::thread tg, tc; bool ug = P.gpu_tr || P.gpu_dec, uc = P.cpu_tr || P.cpu_dec;
        if (ug) tg = std::thread(worker, 0);
        if (uc) tc = std::thread(worker, 1);
        std::thread watch([&] { while (true) { { std::lock_guard<std::mutex> g(m); if (first_open() == nb) break; } if (now() - t0 > limit) { abort = true; break; } std::this_thread::sleep_for(std::chrono::milliseconds(2)); } });
        if (ug) tg.join(); if (uc) tc.join(); watch.join();
        o.secs = now() - t0; o.timeout = abort.load();
        return o;
    };
    std::vector<Policy> pol = {
        {"stage-race outcome: GPU FP16 DFT@32768 + direct, CPU idle", false, true, true, false, false, 3, 0},
        {"GPU cheapest routes (cuFFT@4096 + direct), CPU idle", false, true, true, false, false, 0, 0},
        {"fixed: CPU FFT, GPU search", false, false, true, true, false, 0, 0},
        {"GPU cheapest + CPU pulls transforms", false, true, true, true, false, 0, 0},
        {"GPU cheapest + CPU pulls decodes", false, true, true, false, true, 0, 0},
        {"cost-based (adaptive, cold start)", true, true, true, true, true, -1, -1},
    };
    auto blocks = [](std::initializer_list<std::pair<int, Block>> parts) { std::vector<Block> v; for (auto& p : parts) for (int i = 0; i < p.first; ++i) v.push_back(p.second); return v; };
    struct Scen { const char* name; std::vector<Block> wl; double throttle; bool hog; };
    std::vector<Scen> sc2 = {
        {"decode-heavy: 32 blocks of 1 transform portion and 50 decode portions", blocks({{32, {1, 50}}}), 1, false},
        {"dwell-heavy: 4 blocks of 500 transform portions and 25 decode portions", blocks({{4, {500, 25}}}), 1, false},
        {"mixed: 16 decode-heavy blocks, then 2 dwell-heavy blocks", blocks({{16, {1, 50}}, {2, {500, 25}}}), 1, false},
        {"mixed, with a second GPU load (FP16 matrix search on another stream)", blocks({{16, {1, 50}}, {2, {500, 25}}}), 1, true},
        {"GPU slowed 50x by added waits: 6 blocks of 20 transform and 10 decode portions", blocks({{6, {20, 10}}}), 50, false},
    };
    printf("schedule: transform portion = 32768 dwells (352 bins), decode portion = 16 hypotheses; CPU executor %d threads\n", cpu_threads);
    for (auto& s2 : sc2) {
        hog = s2.hog; std::this_thread::sleep_for(std::chrono::milliseconds(300));
        printf("\nscenario: %s\n%-62s %10s %22s %22s\n", s2.name, "rule", "seconds", "GPU transf/decode", "CPU transf/decode");
        double bestfix = 1e300; Out ad{};
        for (auto& P : pol) {
            Out o = run_policy(P, s2.wl, s2.throttle, 150.0);
            char tb[32]; if (o.timeout) snprintf(tb, sizeof tb, ">150"); else snprintf(tb, sizeof tb, "%.3f", o.secs);
            printf("%-62s %10s %12ld /%8ld %12ld /%8ld\n", P.name, tb, o.n[0][0], o.n[0][1], o.n[1][0], o.n[1][1]); fflush(stdout);
            if (!P.adaptive && !o.timeout) bestfix = std::min(bestfix, o.secs);
            if (P.adaptive) ad = o;
        }
        printf("cost-based / best fixed rule: %.3f\n", ad.secs / bestfix);
        printf("cost-based learned costs, ms per portion (uses):");
        for (int e = 0; e < 2; ++e) for (int st = 0; st < 2; ++st) for (int r = 0; r < NR[e][st]; ++r) if (ad.cnt[e][st][r]) printf(" %s %.3f (%ld);", RN[e][st][r], ad.cost[e][st][r] * 1e3, ad.cnt[e][st][r]);
        printf("\n"); fflush(stdout);
    }
    hog = false; hstop = true; hogt.join();
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "transform";
    cudaDeviceProp prop; CK(cudaGetDeviceProperties(&prop, 0));
    printf("device: %s, %d SMs, %.1f GiB\n", prop.name, prop.multiProcessorCount, prop.totalGlobalMem / 1073741824.0);
    if (mode == "transform") run_transform(argc > 2 ? atol(argv[2]) : 32768, argc > 3 ? atoi(argv[3]) : 704);
    else if (mode == "decode") run_decode(argc > 2 ? atoi(argv[2]) : 16);
    else if (mode == "simulate") run_simulate(argc > 2 ? atol(argv[2]) : 32768);
    else if (mode == "chunks") run_chunks(argc > 2 ? atoi(argv[2]) : 16);
    else if (mode == "contend") run_contend(argc > 2 ? atoi(argv[2]) : 16);
    else if (mode == "schedule") run_schedule(argc > 2 ? atoi(argv[2]) : 13);
    else if (mode == "sustain") {
        std::string route = argc > 2 ? argv[2] : "dft16"; double secs = argc > 3 ? atof(argv[3]) : 12;
        if (route == "direct" || route == "gemm16" || route == "gemm8" || route == "fwht") {
            Decode D(16); double t0 = now(); long it = 0;
            std::vector<int> idx(KD, 0), val((size_t)16 * KD, 1);
            printf("sustain %s start %.3f\n", route.c_str(), t0); fflush(stdout);
            while (now() - t0 < secs) { if (route == "direct") D.direct(); else if (route == "gemm16") D.gemm16(true); else if (route == "gemm8") D.gemm8(true); else for (int a = 0; a < 16; ++a) D.fwht_one(a, idx, val); if ((++it & 3) == 0) CK(cudaDeviceSynchronize()); }
            CK(cudaDeviceSynchronize()); printf("sustain %s end %.3f calls %ld (%.1f hypotheses/s)\n", route.c_str(), now(), it, it * 16 / (now() - t0));
        } else {
            Transform T(704, 32768); double t0 = now(); long it = 0;
            printf("sustain %s start %.3f\n", route.c_str(), t0); fflush(stdout);
            while (now() - t0 < secs) { if (route == "fft") T.fft(true); else if (route == "dft16") T.dft16(true); else if (route == "dft16h") T.dft16h(true); else T.dft8(true, route == "dft8c4"); if ((++it & 3) == 0) CK(cudaDeviceSynchronize()); }
            CK(cudaDeviceSynchronize()); printf("sustain %s end %.3f calls %ld (%.0f dwells/s)\n", route.c_str(), now(), it, it * 32768 / (now() - t0));
        }
    } else { fprintf(stderr, "usage: lattice_gpu transform [B] [bins] | decode [A] | simulate [B] | chunks [cpu threads] | contend [cpu threads] | schedule [cpu threads] | sustain <route> <seconds>\n"); return 1; }
    return 0;
}
