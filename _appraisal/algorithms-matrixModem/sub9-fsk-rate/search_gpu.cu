// Matrix Modem FSK below 9 kHz: the whole hypothesis search of one block on a GPU, with the
// frequency and drift hypotheses as a transform across a message's tone sums. Companion of
// search_bench.cpp (same block, same definitions, same planted message).
//
// Measurement aid, 28 November 2026 (Fable-5.2 pass).
//
// Per batch of messages and timing hypothesis:
//   gather     one complex 4-bit bin value per message and tone (CUDA cores)
//   gemm       all rungs at once as one matrix product on Tensor Cores:
//              C[messages x 2 rows] = U[messages x 2 N] * A[2 N x 2 rows]
//              U holds the real and imaginary tone values, A the de-chirped DFT rows of every
//              rung, window, frequency bin and drift hypothesis (zero outside the row's window).
//              FP16 operands; also tried with 8-bit integer operands.
//   fft        the same sums by cuFFT: per rung, fill (de-chirp and zero-pad), transform, energies
//   epilogue   energies, sums over a rung's windows, best bin per rung and message, best message
//   short      windows of eight tones and fewer evaluated directly on their one to three bins
//              (used by the fft route, and by the gemm route when those rungs are left out of A)
// Every route has to return the planted message, frequency bin and drift on rung 0.
//
// Build: nvcc -O3 -std=c++17 -arch=sm_120 search_gpu.cu -lcublas -lcufft -o search_gpu
// Run:   ./search_gpu [tones] [frequency bins] [blocks of 4096 messages per batch]
//        ./search_gpu sustain gemm|gemm8|fft SECONDS [tones] [bins] [blocks]     (for telemetry)
// With more frequency bins than the short rungs can take directly (more than three on windows of
// eight tones) only the product over all rungs runs; that is the scaling check.
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cublas_v2.h>
#include <cufft.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#define CK(x) do { cudaError_t e_ = (x); if (e_ != cudaSuccess) { fprintf(stderr, "CUDA error %s at %s:%d\n", cudaGetErrorString(e_), __FILE__, __LINE__); exit(2); } } while (0)
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static const int NSYM = 32, NV = 256, HB = 12, NH = 1 << HB, POS = 16, ND = 4, MAXR = 16;
static const double PI = 3.14159265358979323846;

#define REDUCE(sv, si, outv, outi) do { __syncthreads(); for (int h_ = 128; h_ > 0; h_ >>= 1) { if (tid < h_ && sv[tid + h_] > sv[tid]) { sv[tid] = sv[tid + h_]; si[tid] = si[tid + h_]; } __syncthreads(); } if (tid == 0) { outv = sv[0]; outi = si[0]; } __syncthreads(); } while (0)

struct Geo { int N, R, B; int L[MAXR], nb[MAXR], nd[MAXR], rowoff[MAXR + 1]; };

// one message per thread, 256 consecutive messages per block (all with the same c_hi)
#define MSG_ID int tid = threadIdx.x; long msg = (long)blockIdx.x * 256 + tid; int chi = h0 + (int)(msg >> HB), clo = (int)(msg & (NH - 1)); (void)chi

// tone values as FP16 planes U[B x 2N], column-major: (msg, t) real, (msg, N + t) imaginary
__global__ void gather_h(const int8_t* zmap, const uint8_t* sym, const uint8_t* ghi, const uint8_t* glo, int nt, int h0, long B, __half* ut) {
    __shared__ uint8_t gh[NSYM];
    MSG_ID;
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    uint8_t val[NSYM]; for (int j = 0; j < NSYM; ++j) val[j] = gh[j] ^ glo[j * NH + clo];
    for (int t = 0; t < nt; ++t) { const int8_t* z = zmap + ((size_t)t * NV + val[sym[t]]) * 2; ut[(size_t)t * B + msg] = __float2half((float)z[0]); ut[(size_t)(nt + t) * B + msg] = __float2half((float)z[1]); }
}
// the same as 8-bit integer planes
__global__ void gather_b(const int8_t* zmap, const uint8_t* sym, const uint8_t* ghi, const uint8_t* glo, int nt, int h0, long B, int8_t* ut) {
    __shared__ uint8_t gh[NSYM];
    MSG_ID;
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    uint8_t val[NSYM]; for (int j = 0; j < NSYM; ++j) val[j] = gh[j] ^ glo[j * NH + clo];
    for (int t = 0; t < nt; ++t) { const int8_t* z = zmap + ((size_t)t * NV + val[sym[t]]) * 2; ut[(size_t)t * B + msg] = z[0]; ut[(size_t)(nt + t) * B + msg] = z[1]; }
}
// tone values as complex floats, message-major: ub[msg * N + t]
__global__ void gather_c(const int8_t* zmap, const uint8_t* sym, const uint8_t* ghi, const uint8_t* glo, int nt, int h0, float2* ub) {
    __shared__ uint8_t gh[NSYM];
    MSG_ID;
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    uint8_t val[NSYM]; for (int j = 0; j < NSYM; ++j) val[j] = gh[j] ^ glo[j * NH + clo];
    float2* u = ub + (size_t)msg * nt;
    for (int t = 0; t < nt; ++t) { const int8_t* z = zmap + ((size_t)t * NV + val[sym[t]]) * 2; u[t] = make_float2((float)z[0], (float)z[1]); }
}

static __device__ __forceinline__ float tof(__half v) { return __half2float(v); }
static __device__ __forceinline__ float tof(float v) { return v; }
static __device__ __forceinline__ float tof(int v) { return (float)v; }

// epilogue of the matrix product: C[B x 2 rows], column-major. best/arg are [B / 256][MAXR];
// arg packs c_lo (12 bits) and the hypothesis index within the rung above them.
template <class T>
__global__ void epi_k(const T* c, long B, int R0, int R1, Geo g, float* best, int* arg) {
    __shared__ float sv[256]; __shared__ int si[256];
    const int h0 = 0; MSG_ID;
    for (int r = R0; r < R1; ++r) {
        int W = 1 << r, nb = g.nb[r], nd = g.nd[r]; float bv = -1.f; int bi_ = 0;
        for (int d = 0; d < nd; ++d) for (int b = 0; b < nb; ++b) {
            float e = 0.f;
            for (int w = 0; w < W; ++w) { size_t row = (size_t)g.rowoff[r] + ((size_t)d * W + w) * nb + b; float re = tof(c[(2 * row) * B + msg]), im = tof(c[(2 * row + 1) * B + msg]); e += re * re + im * im; }
            if (e > bv) { bv = e; bi_ = clo | ((d * nb + b) << HB); }
        }
        sv[tid] = bv; si[tid] = bi_; REDUCE(sv, si, best[(size_t)blockIdx.x * MAXR + r], arg[(size_t)blockIdx.x * MAXR + r]);
    }
}

// short rungs directly from the bin values: streaming sums for the rung's bins
__global__ void short_k(const int8_t* zmap, const uint8_t* sym, const uint8_t* ghi, const uint8_t* glo, const float2* rot, int R0, Geo g, int h0, float* best, int* arg) {
    __shared__ uint8_t gh[NSYM]; __shared__ float sv[256]; __shared__ int si[256];
    MSG_ID;
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    float bv[4]; int bi_[4]; for (int l = 0; l < 4; ++l) { bv[l] = -1.f; bi_[l] = 0; }
    int nr = g.R - R0;
    uint8_t val[NSYM]; for (int j = 0; j < NSYM; ++j) val[j] = gh[j] ^ glo[j * NH + clo];
    float ar[4][3], ai[4][3], en[4][3];
    for (int l = 0; l < 4; ++l) for (int b = 0; b < 3; ++b) { ar[l][b] = 0.f; ai[l][b] = 0.f; en[l][b] = 0.f; }
    for (int t = 0; t < g.N; ++t) {
        const int8_t* z = zmap + ((size_t)t * NV + val[sym[t]]) * 2; float zr = (float)z[0], zi = (float)z[1];
        for (int l = 0; l < nr; ++l) {
            int r = R0 + l, L = g.L[r], i = t & (L - 1), nb = g.nb[r];
            for (int b = 0; b < nb; ++b) { float2 q = rot[(l * 3 + b) * 8 + i]; ar[l][b] += zr * q.x - zi * q.y; ai[l][b] += zr * q.y + zi * q.x; }
            if (i == L - 1) for (int b = 0; b < nb; ++b) { en[l][b] += ar[l][b] * ar[l][b] + ai[l][b] * ai[l][b]; ar[l][b] = 0.f; ai[l][b] = 0.f; }
        }
    }
    for (int l = 0; l < nr; ++l) for (int b = 0; b < g.nb[R0 + l]; ++b) if (en[l][b] > bv[l]) { bv[l] = en[l][b]; bi_[l] = clo | (b << HB); }
    for (int l = 0; l < nr; ++l) { sv[tid] = bv[l]; si[tid] = bi_[l]; REDUCE(sv, si, best[(size_t)blockIdx.x * MAXR + R0 + l], arg[(size_t)blockIdx.x * MAXR + R0 + l]); }
}

// cuFFT route: fill the transform input of rung r, drift d: x[(msg * W + w) * 2L + i]
__global__ void fill_k(const float2* ub, const float2* chirp, int N, int L, int r, int d, long total, float2* x) {
    long e = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (e >= total) return;
    long msg = e / (2 * N); int pos = (int)(e % (2 * N)), w = pos / (2 * L), i = pos % (2 * L);
    float2 v = make_float2(0.f, 0.f);
    if (i < L) { int t = w * L + i; v = ub[msg * N + t]; if (r == 0) { float2 c = chirp[d * N + t]; v = make_float2(v.x * c.x - v.y * c.y, v.x * c.y + v.y * c.x); } }
    x[e] = v;
}
// energies of rung r from the transforms; keeps the better of the stored and the new result
__global__ void epi_c(const float2* x, int N, int L, int r, int nb, int d, float* best, int* arg) {
    __shared__ float sv[256]; __shared__ int si[256];
    const int h0 = 0; MSG_ID; int W = 1 << r, n2 = 2 * L;
    float bv = -1.f; int bi_ = 0;
    const float2* xm = x + (size_t)msg * (2 * N);
    for (int b = 0; b < nb; ++b) {
        int k = (b - nb / 2 + n2) % n2; float e = 0.f;
        for (int w = 0; w < W; ++w) { float2 v = xm[(size_t)w * n2 + k]; e += v.x * v.x + v.y * v.y; }
        if (e > bv) { bv = e; bi_ = clo | ((d * nb + b) << HB); }
    }
    sv[tid] = bv; si[tid] = bi_;
    float ov; int oi; REDUCE(sv, si, ov, oi);
    if (tid == 0) { size_t s = (size_t)blockIdx.x * MAXR + r; if (d == 0 || ov > best[s]) { best[s] = ov; arg[s] = oi; } }
}
__global__ void f2h(const float* f, __half* h, long n) { long i = blockIdx.x * (long)blockDim.x + threadIdx.x; if (i < n) h[i] = __float2half(f[i]); }

template <class F> static double timeit(F f, double min_s = 2.0) {
    f(); CK(cudaDeviceSynchronize());
    double t0 = now(); int n = 0; do { f(); ++n; CK(cudaDeviceSynchronize()); } while (now() - t0 < min_s);
    return (now() - t0) / n;
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "all"; int a0 = 1; std::string sroute = "gemm"; double ssecs = 10;
    if (mode == "sustain") { sroute = argc > 2 ? argv[2] : "gemm"; ssecs = argc > 3 ? atof(argv[3]) : 10; a0 = 4; }
    const int N = argc > a0 ? atoi(argv[a0]) : 512, NB0 = argc > a0 + 1 ? atoi(argv[a0 + 1]) : 174, CB = argc > a0 + 2 ? atoi(argv[a0 + 2]) : 32;
    cudaDeviceProp pr; CK(cudaGetDeviceProperties(&pr, 0));
    Geo g{}; g.N = N; g.R = 0; while ((2 << g.R) <= N) ++g.R; g.B = CB * NH;
    if ((1 << g.R) != N || g.R > MAXR || NB0 > 2 * N || N < 64) { fprintf(stderr, "tones must be a power of two from 64, bins at most 2N\n"); return 2; }
    const int R = g.R; const long B = g.B;
    for (int r = 0; r < R; ++r) { g.L[r] = N >> r; g.nb[r] = std::max(1, (NB0 + (1 << r) - 1) >> r); g.nd[r] = r == 0 ? ND : 1; }
    const int RS = R - 3;                                  // rungs RS..R-1 have windows of 8, 4, 2 tones

    // ---- the block: identical to search_bench.cpp
    std::mt19937_64 rng(2026); std::normal_distribution<double> gauss(0, 1);
    std::vector<uint8_t> ghi((size_t)NSYM * NH), glo((size_t)NSYM * NH);
    for (int j = 0; j < NSYM; ++j) {
        uint32_t gg[8]; for (auto& x : gg) x = (uint32_t)(rng() & 0xFFFFFF); const uint8_t m = (uint8_t)rng();
        auto val = [&](uint32_t c) { uint8_t v = 0; for (int b = 0; b < 8; ++b) v |= (uint8_t)(__builtin_parity(gg[b] & c) << b); return v; };
        for (int h = 0; h < NH; ++h) { ghi[(size_t)j * NH + h] = val((uint32_t)h << HB) ^ m; glo[(size_t)j * NH + h] = val((uint32_t)h); }
    }
    auto value = [&](int j, uint32_t c) { return (uint8_t)(ghi[(size_t)j * NH + (c >> HB)] ^ glo[(size_t)j * NH + (c & (NH - 1))]); };
    std::vector<uint8_t> sym(N); for (int t = 0; t < N; ++t) sym[t] = (uint8_t)(t % NSYM);
    for (int t = N - 1; t > 0; --t) std::swap(sym[t], sym[rng() % (uint64_t)(t + 1)]);
    const uint32_t planted = (uint32_t)(rng() & 0xFFFFFF);
    const int f0 = -NB0 / 2 + (int)(rng() % (uint64_t)NB0), d0 = 2;
    const double gd[ND] = {-5.82, -1.94, 1.94, 5.82};
    std::vector<int8_t> zmap((size_t)N * NV * 2);
    for (int t = 0; t < N; ++t) {
        uint8_t map[NV]; for (int v = 0; v < NV; ++v) map[v] = (uint8_t)(v % POS);
        for (int v = NV - 1; v > 0; --v) std::swap(map[v], map[rng() % (uint64_t)(v + 1)]);
        double zr[POS], zi[POS];
        for (int p = 0; p < POS; ++p) { zr[p] = 2.2 * gauss(rng); zi[p] = 2.2 * gauss(rng); }
        const double x = (double)t / N, ph = PI * f0 * x + PI * gd[d0] * x * x / 2;
        const int pp = map[value(sym[t], planted)];
        zr[pp] += 2.5 * std::cos(ph); zi[pp] += 2.5 * std::sin(ph);
        for (int v = 0; v < NV; ++v) {
            zmap[((size_t)t * NV + v) * 2] = (int8_t)std::max(-7, std::min(7, (int)std::lrint(zr[map[v]])));
            zmap[((size_t)t * NV + v) * 2 + 1] = (int8_t)std::max(-7, std::min(7, (int)std::lrint(zi[map[v]])));
        }
    }
    const int h0 = (int)(planted >> HB) / CB * CB;         // batch that holds the planted message
    const int want0 = (d0 * g.nb[0]) + (f0 + g.nb[0] / 2);

    uint8_t *dghi, *dglo, *dsym; int8_t* dz; float* dbest; int* darg;
    CK(cudaMalloc(&dghi, ghi.size())); CK(cudaMemcpy(dghi, ghi.data(), ghi.size(), cudaMemcpyHostToDevice));
    CK(cudaMalloc(&dglo, glo.size())); CK(cudaMemcpy(dglo, glo.data(), glo.size(), cudaMemcpyHostToDevice));
    CK(cudaMalloc(&dsym, sym.size())); CK(cudaMemcpy(dsym, sym.data(), sym.size(), cudaMemcpyHostToDevice));
    CK(cudaMalloc(&dz, zmap.size())); CK(cudaMemcpy(dz, zmap.data(), zmap.size(), cudaMemcpyHostToDevice));
    const int NBLK = (int)(B / 256);
    CK(cudaMalloc(&dbest, sizeof(float) * (size_t)NBLK * MAXR)); CK(cudaMalloc(&darg, sizeof(int) * (size_t)NBLK * MAXR));
    auto found = [&](int r, uint32_t& msg, int& hyp) {
        std::vector<float> b((size_t)NBLK * MAXR); std::vector<int> a((size_t)NBLK * MAXR);
        CK(cudaMemcpy(b.data(), dbest, sizeof(float) * b.size(), cudaMemcpyDeviceToHost)); CK(cudaMemcpy(a.data(), darg, sizeof(int) * a.size(), cudaMemcpyDeviceToHost));
        float bv = -2; for (int i = 0; i < NBLK; ++i) if (b[(size_t)i * MAXR + r] > bv) { bv = b[(size_t)i * MAXR + r]; msg = ((uint32_t)(h0 + ((i * 256) >> HB)) << HB) | (uint32_t)(a[(size_t)i * MAXR + r] & (NH - 1)); hyp = a[(size_t)i * MAXR + r] >> HB; }
        return bv;
    };
    auto verdict = [&]() { uint32_t m = 0; int h = 0; found(0, m, h); return (m == planted && h == want0) ? "correct" : "WRONG"; };

    // ---- matrix operand A for rungs [0, RG): rows (rung, drift, window, bin)
    auto build_rows = [&](int RG, std::vector<float>& af, int& rows) {
        g.rowoff[0] = 0; for (int r = 0; r < R; ++r) g.rowoff[r + 1] = g.rowoff[r] + g.nd[r] * (1 << r) * g.nb[r];
        rows = g.rowoff[RG]; af.assign((size_t)2 * N * 2 * rows, 0.f);
        for (int r = 0; r < RG; ++r) { const int W = 1 << r, L = g.L[r], nb = g.nb[r];
            for (int d = 0; d < g.nd[r]; ++d) for (int w = 0; w < W; ++w) for (int b = 0; b < nb; ++b) {
                const size_t row = (size_t)g.rowoff[r] + ((size_t)d * W + w) * nb + b;
                for (int i = 0; i < L; ++i) { const int n = w * L + i; const double x = (double)n / N;
                    const double a = -PI * (b - nb / 2) * i / L - (r == 0 ? PI * gd[d] * x * x / 2 : 0.0), ar = std::cos(a), ai = std::sin(a);
                    af[(2 * row) * (size_t)(2 * N) + n] = (float)ar; af[(2 * row) * (size_t)(2 * N) + N + n] = (float)-ai;
                    af[(2 * row + 1) * (size_t)(2 * N) + n] = (float)ai; af[(2 * row + 1) * (size_t)(2 * N) + N + n] = (float)ar; } } }
    };
    cublasHandle_t blas; if (cublasCreate(&blas) != CUBLAS_STATUS_SUCCESS) { fprintf(stderr, "cublas create failed\n"); return 2; }
    std::vector<float2> rot((size_t)3 * 3 * 8, make_float2(0, 0));
    for (int l = 0; l < 3; ++l) { const int r = RS + l, L = g.L[r], nb = g.nb[r]; for (int b = 0; b < nb && b < 3; ++b) for (int i = 0; i < L; ++i) { const double a = -PI * (b - nb / 2) * i / L; rot[(l * 3 + b) * 8 + i] = make_float2((float)std::cos(a), (float)std::sin(a)); } }
    const bool noshort = g.nb[RS] > 3;                     // then only the all-rungs product runs
    float2* drot; CK(cudaMalloc(&drot, sizeof(float2) * rot.size())); CK(cudaMemcpy(drot, rot.data(), sizeof(float2) * rot.size(), cudaMemcpyHostToDevice));
    auto run_short = [&]() { short_k<<<NBLK, 256>>>(dz, dsym, dghi, dglo, drot, RS, g, h0, dbest, darg); };

    const double M24 = 16777216.0, Dblock = 5242.88;
    auto report = [&](const char* name, double per_batch) {
        const double one = per_batch / B * M24; printf("  %-34s %8.3f ns per message and timing; one timing hypothesis over 2^24 messages %.3f s; 20 timing offsets %.2f s; block/search %.0f\n", name, per_batch / B * 1e9, one, 20 * one, Dblock / (20 * one));
    };

    if (mode != "sustain") {
        printf("device: %s, %d SMs, %.1f GiB\n", pr.name, pr.multiProcessorCount, pr.totalGlobalMem / 1073741824.0);
        printf("tones %d, frequency bins on the whole block %d, drift hypotheses %d, rungs %d, batch %ld messages\n", N, NB0, ND, R, B);
        printf("planted message %06x, frequency bin %d, drift hypothesis %d\n", planted, f0, d0);
    }

    // ---- matrix-product route
    for (int variant = 0; variant < 2; ++variant) {
        const int RG = variant == 0 ? R : RS; if ((mode == "sustain" && (variant == 1 || sroute == "fft")) || (noshort && variant == 1)) continue;
        std::vector<float> af; int rows; build_rows(RG, af, rows);
        const size_t na = af.size(); float* daf; __half* dah; int8_t* da8;
        CK(cudaMalloc(&daf, sizeof(float) * na)); CK(cudaMemcpy(daf, af.data(), sizeof(float) * na, cudaMemcpyHostToDevice));
        CK(cudaMalloc(&dah, sizeof(__half) * na)); f2h<<<(unsigned)((na + 255) / 256), 256>>>(daf, dah, (long)na); CK(cudaDeviceSynchronize()); CK(cudaFree(daf));
        std::vector<int8_t> a8(na); for (size_t i = 0; i < na; ++i) a8[i] = (int8_t)std::lrint(127 * af[i]);
        CK(cudaMalloc(&da8, na)); CK(cudaMemcpy(da8, a8.data(), na, cudaMemcpyHostToDevice));
        __half *duh, *dch; float* dcf; int8_t* du8; int* dci;
        CK(cudaMalloc(&duh, sizeof(__half) * (size_t)B * 2 * N)); CK(cudaMalloc(&du8, (size_t)B * 2 * N));
        CK(cudaMalloc(&dch, sizeof(__half) * (size_t)B * 2 * rows)); CK(cudaMalloc(&dcf, sizeof(float) * (size_t)B * 2 * rows)); dci = (int*)dcf;
        const float one = 1, zero = 0; const int ione = 1, izero = 0;
        auto gather = [&]() { gather_h<<<NBLK, 256>>>(dz, dsym, dghi, dglo, N, h0, B, duh); };
        auto gather8 = [&]() { gather_b<<<NBLK, 256>>>(dz, dsym, dghi, dglo, N, h0, B, du8); };
        auto prod_hh = [&]() { return cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, (int)B, 2 * rows, 2 * N, &one, duh, CUDA_R_16F, (int)B, dah, CUDA_R_16F, 2 * N, &zero, dch, CUDA_R_16F, (int)B, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT); };
        auto prod_hf = [&]() { return cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, (int)B, 2 * rows, 2 * N, &one, duh, CUDA_R_16F, (int)B, dah, CUDA_R_16F, 2 * N, &zero, dcf, CUDA_R_32F, (int)B, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT); };
        auto prod_8i = [&]() { return cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, (int)B, 2 * rows, 2 * N, &ione, du8, CUDA_R_8I, (int)B, da8, CUDA_R_8I, 2 * N, &izero, dci, CUDA_R_32I, (int)B, CUBLAS_COMPUTE_32I, CUBLAS_GEMM_DEFAULT); };
        auto epi_h = [&]() { epi_k<__half><<<NBLK, 256>>>(dch, B, 0, RG, g, dbest, darg); };
        auto epi_f = [&]() { epi_k<float><<<NBLK, 256>>>(dcf, B, 0, RG, g, dbest, darg); };
        auto epi_i = [&]() { epi_k<int><<<NBLK, 256>>>(dci, B, 0, RG, g, dbest, darg); };
        const double flop = 2.0 * B * (2.0 * rows) * (2.0 * N);
        if (mode == "sustain") {
            double t0 = now(); long it = 0;
            while (now() - t0 < ssecs) { if (sroute == "gemm8") { gather8(); prod_8i(); epi_i(); } else { gather(); prod_hh(); epi_h(); } CK(cudaDeviceSynchronize()); ++it; }
            printf("sustain %s: %ld batches of %ld messages in %.1f s\n", sroute.c_str(), it, B, now() - t0); return 0;
        }
        printf("\nmatrix product on Tensor Cores, rungs 0 to %d in the product (%d complex rows, %.3g flop per batch)%s\n", RG - 1, rows, flop, RG < R ? ", short rungs by the direct kernel" : "");
        const double tg = timeit(gather), ts = RG < R ? timeit(run_short) : 0.0;
        printf("  gather to FP16 planes: %.3f ms per batch (%.3g tone lookups/s)%s\n", tg * 1e3, (double)B * N / tg, RG < R ? "" : "");
        if (RG < R) printf("  short rungs direct: %.3f ms per batch\n", ts * 1e3);
        gather(); cublasStatus_t st = prod_hh(); CK(cudaDeviceSynchronize());
        if (st == CUBLAS_STATUS_SUCCESS) { epi_h(); if (RG < R) run_short(); CK(cudaDeviceSynchronize()); const char* v = verdict();
            const double tp = timeit([&] { prod_hh(); }, 3.0), te = timeit(epi_h);
            printf("  FP16 x FP16 -> FP16: product %.3f ms (%.1f nominal TFLOP/s), epilogue %.3f ms (%.0f GB/s read); planted: %s\n", tp * 1e3, flop / tp / 1e12, te * 1e3, (double)B * 2 * rows * 2 / te / 1e9, v);
            report("gather + product + epilogue", tg + tp + te + ts);
        } else printf("  FP16 x FP16 -> FP16: cublasGemmEx status %d\n", (int)st);
        gather(); st = prod_hf(); CK(cudaDeviceSynchronize());
        if (st == CUBLAS_STATUS_SUCCESS) { epi_f(); if (RG < R) run_short(); CK(cudaDeviceSynchronize()); const char* v = verdict();
            const double tp = timeit([&] { prod_hf(); }, 3.0), te = timeit(epi_f);
            printf("  FP16 x FP16 -> FP32: product %.3f ms (%.1f nominal TFLOP/s), epilogue %.3f ms (%.0f GB/s read); planted: %s\n", tp * 1e3, flop / tp / 1e12, te * 1e3, (double)B * 2 * rows * 4 / te / 1e9, v);
            report("gather + product + epilogue", tg + tp + te + ts);
        } else printf("  FP16 x FP16 -> FP32: cublasGemmEx status %d\n", (int)st);
        gather8(); st = prod_8i(); CK(cudaDeviceSynchronize());
        if (st == CUBLAS_STATUS_SUCCESS) { epi_i(); if (RG < R) run_short(); CK(cudaDeviceSynchronize()); const char* v = verdict();
            const double tg8 = timeit(gather8), tp = timeit([&] { prod_8i(); }, 3.0), te = timeit(epi_i);
            printf("  INT8 x INT8 -> INT32: gather %.3f ms, product %.3f ms (%.1f nominal Top/s), epilogue %.3f ms; planted: %s\n", tg8 * 1e3, tp * 1e3, flop / tp / 1e12, te * 1e3, v);
            report("gather + product + epilogue", tg8 + tp + te + ts);
        } else printf("  INT8 x INT8 -> INT32: cublasGemmEx status %d (not available)\n", (int)st);
        cudaFree(dah); cudaFree(da8); cudaFree(duh); cudaFree(du8); cudaFree(dch); cudaFree(dcf);
    }

    // ---- cuFFT route
    if (!noshort) {
        std::vector<float2> chirp((size_t)ND * N);
        for (int d = 0; d < ND; ++d) for (int t = 0; t < N; ++t) { const double x = (double)t / N, a = -PI * gd[d] * x * x / 2; chirp[(size_t)d * N + t] = make_float2((float)std::cos(a), (float)std::sin(a)); }
        float2 *dchirp, *dub, *dx;
        CK(cudaMalloc(&dchirp, sizeof(float2) * chirp.size())); CK(cudaMemcpy(dchirp, chirp.data(), sizeof(float2) * chirp.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&dub, sizeof(float2) * (size_t)B * N)); CK(cudaMalloc(&dx, sizeof(float2) * (size_t)B * 2 * N));
        std::vector<cufftHandle> plan(RS);
        for (int r = 0; r < RS; ++r) { int n = 2 * g.L[r]; if (cufftPlanMany(&plan[r], 1, &n, nullptr, 1, n, nullptr, 1, n, CUFFT_C2C, (int)(B << r)) != CUFFT_SUCCESS) { fprintf(stderr, "cufft plan failed for size %d\n", n); return 2; } }
        const long total = B * 2L * N; const unsigned fb = (unsigned)((total + 255) / 256);
        auto gatherc = [&]() { gather_c<<<NBLK, 256>>>(dz, dsym, dghi, dglo, N, h0, dub); };
        auto fill = [&](int r, int d) { fill_k<<<fb, 256>>>(dub, dchirp, N, g.L[r], r, d, total, dx); };
        auto fft = [&](int r) { if (cufftExecC2C(plan[r], (cufftComplex*)dx, (cufftComplex*)dx, CUFFT_FORWARD) != CUFFT_SUCCESS) { fprintf(stderr, "cufft exec failed\n"); exit(2); } };
        auto epi = [&](int r, int d) { epi_c<<<NBLK, 256>>>(dx, N, g.L[r], r, g.nb[r], d, dbest, darg); };
        auto whole = [&]() { gatherc(); for (int r = 0; r < RS; ++r) for (int d = 0; d < g.nd[r]; ++d) { fill(r, d); fft(r); epi(r, d); } run_short(); };
        if (mode == "sustain") {
            double t0 = now(); long it = 0; while (now() - t0 < ssecs) { whole(); CK(cudaDeviceSynchronize()); ++it; }
            printf("sustain fft: %ld batches of %ld messages in %.1f s\n", it, B, now() - t0); return 0;
        }
        whole(); CK(cudaDeviceSynchronize()); const char* v = verdict();
        printf("\ncuFFT, rungs 0 to %d by transforms of %d to %d points, short rungs by the direct kernel; planted: %s\n", RS - 1, 2 * N, 2 * g.L[RS - 1], v);
        const double tg = timeit(gatherc), ts = timeit(run_short);
        gatherc(); CK(cudaDeviceSynchronize());
        const double tf0 = timeit([&] { fill(0, 0); }), tx0 = timeit([&] { fft(0); }), te0 = timeit([&] { epi(0, 1); });
        double tfr = 0, txr = 0, ter = 0;
        for (int r = 1; r < RS; ++r) { tfr += timeit([&] { fill(r, 0); }, 1.0); txr += timeit([&] { fft(r); }, 1.0); ter += timeit([&] { epi(r, 0); }, 1.0); }
        const double tw = timeit(whole, 3.0);
        printf("  gather to complex floats: %.3f ms per batch; short rungs direct: %.3f ms\n", tg * 1e3, ts * 1e3);
        printf("  rung 0, per drift hypothesis: fill %.3f ms, transform %.3f ms (%.3g transforms of %d points per second), energies %.3f ms\n", tf0 * 1e3, tx0 * 1e3, (double)B / tx0, 2 * N, te0 * 1e3);
        printf("  rungs 1 to %d together: fill %.3f ms, transforms %.3f ms, energies %.3f ms\n", RS - 1, tfr * 1e3, txr * 1e3, ter * 1e3);
        report("whole route, timed as one", tw);
    }
    return 0;
}
