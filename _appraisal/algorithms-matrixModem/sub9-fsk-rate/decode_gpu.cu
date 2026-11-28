// Matrix Modem FSK below 9 kHz: cost of one 24-bit block decode on a GPU, per-tone and on the
// window ladder. Companion of decode_bench.cpp (same block, same definitions).
//
// Block: 24 information bits, 32 symbols of 256 values, value_j = Ghi_j[c_hi] xor Glo_j[c_lo]
// with c = (c_hi, c_lo) of 12 bits each. One decode scores all 2^24 messages of one hypothesis.
//
//   energy    per-tone reception: Score[c] = sum_j E_j[value_j(c)]; CUDA-core kernel, tables in
//             shared memory; 16 hypotheses per call.
//   ladder    coherent attempts on every window from two tones to the whole block: one lookup of a
//             complex 4-bit bin value per message and tone, windows of 2, 4, 8, ... tones summed
//             coherently and added as energies, one maximum per rung; CUDA-core kernel.
//   table     the ladder restricted to W windows per block: complex int16 sums per (window,
//             symbol, value), 32 W lookups per message; CUDA-core kernel.
//   gemm      the same W-window ladder with the lookups as one FP16 matrix product on Tensor
//             Cores: Score[c_hi, (part, c_lo)] = P[4096 x 8192] * T[8192 x 4096*2W], P one-hot in
//             (symbol, Ghi_j[c_hi]), T holding the table entry of (symbol, v xor Glo_j[c_lo]) for
//             each window and real/imaginary part; then a kernel forms the rung energies and
//             maxima from the 2W scores of every message.
//
// Every route has to return the planted message on the whole-block rung.
//
// Build: nvcc -O3 -std=c++17 -arch=sm_120 decode_gpu.cu -lcublas -o decode_gpu
// Run:   ./decode_gpu            (all routes)
//        ./decode_gpu sustain ROUTE SECONDS   (ROUTE: energy ladder2048 table64 gemm64; for telemetry)
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cublas_v2.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#define CK(x) do { cudaError_t e_ = (x); if (e_ != cudaSuccess) { fprintf(stderr, "CUDA error %s at %s:%d\n", cudaGetErrorString(e_), __FILE__, __LINE__); exit(2); } } while (0)
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static const int NSYM = 32, NV = 256, KD = NSYM * NV, HB = 12, NH = 1 << HB, MAXR = 16;

// reduce 256 per-thread candidates to the block's best; call from every thread
#define REDUCE(sv, si, outv, outi) do { __syncthreads(); for (int h_ = 128; h_ > 0; h_ >>= 1) { if (tid < h_ && sv[tid + h_] > sv[tid]) { sv[tid] = sv[tid + h_]; si[tid] = si[tid + h_]; } __syncthreads(); } if (tid == 0) { outv = sv[0]; outi = si[0]; } __syncthreads(); } while (0)

__global__ void energy_k(const uint16_t* tq, const uint8_t* ghi, const uint8_t* glo, float* best, int* arg) {
    __shared__ uint16_t t[KD]; __shared__ uint8_t gh[NSYM]; __shared__ float sv[256]; __shared__ int si[256];
    int a = blockIdx.x / NH, chi = blockIdx.x % NH, tid = threadIdx.x;
    for (int i = tid; i < KD; i += 256) t[i] = tq[(size_t)a * KD + i];
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    int bv = -1, bi = 0;
    for (int clo = tid; clo < NH; clo += 256) { int s = 0; for (int j = 0; j < NSYM; ++j) s += t[j * NV + (gh[j] ^ glo[j * NH + clo])]; if (s > bv) { bv = s; bi = clo; } }
    sv[tid] = (float)bv; si[tid] = bi;
    REDUCE(sv, si, best[blockIdx.x], arg[blockIdx.x]);
}

// full ladder over tones; one block per c_hi (blockIdx.x + h0); best/arg are [blocks][MAXR]
__global__ void ladder_k(const int8_t* zmap, const uint8_t* sym, const uint8_t* ghi, const uint8_t* glo, int nt, int R, int h0, float* best, int* arg) {
    __shared__ uint8_t gh[NSYM]; __shared__ float sv[256]; __shared__ int si[256];
    int chi = blockIdx.x + h0, tid = threadIdx.x;
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    float bv[MAXR]; int bi[MAXR]; for (int l = 0; l < MAXR; ++l) { bv[l] = -1.f; bi[l] = 0; }
    for (int clo = tid; clo < NH; clo += 256) {
        uint8_t val[NSYM]; for (int j = 0; j < NSYM; ++j) val[j] = gh[j] ^ glo[j * NH + clo];
        int ar[MAXR + 1], ai[MAXR + 1]; float en[MAXR]; for (int l = 0; l <= MAXR; ++l) { ar[l] = 0; ai[l] = 0; } for (int l = 0; l < MAXR; ++l) en[l] = 0.f;
        for (int t = 0; t < nt; ++t) {
            const int8_t* z = zmap + ((size_t)t * NV + val[sym[t]]) * 2;
            ar[0] += z[0]; ai[0] += z[1];
            int lvl = 0;
            for (int tt = t + 1; (tt & 1) == 0; tt >>= 1, ++lvl) { float x = (float)ar[lvl], y = (float)ai[lvl]; en[lvl] += x * x + y * y; ar[lvl + 1] += ar[lvl]; ai[lvl + 1] += ai[lvl]; ar[lvl] = 0; ai[lvl] = 0; }
        }
        for (int l = 0; l < R; ++l) if (en[l] > bv[l]) { bv[l] = en[l]; bi[l] = clo; }
    }
    for (int l = 0; l < R; ++l) { sv[tid] = bv[l]; si[tid] = bi[l]; REDUCE(sv, si, best[(size_t)blockIdx.x * MAXR + l], arg[(size_t)blockIdx.x * MAXR + l]); }
}

// close the windows that end with window index w (0-based) in a ladder of R rungs over W windows
#define LADDER_STEP(w) do { int lvl_ = 0; for (int ww_ = (w) + 1;; ww_ >>= 1, ++lvl_) { en[lvl_] += ar[lvl_] * ar[lvl_] + ai[lvl_] * ai[lvl_]; ar[lvl_ + 1] += ar[lvl_]; ai[lvl_ + 1] += ai[lvl_]; ar[lvl_] = 0.f; ai[lvl_] = 0.f; if ((ww_ & 1) != 0 || lvl_ + 1 >= R) break; } } while (0)

// table ladder on CUDA cores: tab[W][NSYM][NV][2] int16
__global__ void table_k(const int16_t* tab, const uint8_t* ghi, const uint8_t* glo, int W, int R, int h0, float* best, int* arg) {
    __shared__ uint8_t gh[NSYM]; __shared__ float sv[256]; __shared__ int si[256];
    int chi = blockIdx.x + h0, tid = threadIdx.x;
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    float bv[MAXR]; int bi[MAXR]; for (int l = 0; l < MAXR; ++l) { bv[l] = -1.f; bi[l] = 0; }
    for (int clo = tid; clo < NH; clo += 256) {
        uint8_t val[NSYM]; for (int j = 0; j < NSYM; ++j) val[j] = gh[j] ^ glo[j * NH + clo];
        float ar[MAXR + 1], ai[MAXR + 1], en[MAXR]; for (int l = 0; l <= MAXR; ++l) { ar[l] = 0.f; ai[l] = 0.f; } for (int l = 0; l < MAXR; ++l) en[l] = 0.f;
        for (int w = 0; w < W; ++w) {
            int re = 0, im = 0; const int16_t* tw = tab + (size_t)w * NSYM * NV * 2;
            for (int j = 0; j < NSYM; ++j) { const int16_t* z = tw + ((size_t)j * NV + val[j]) * 2; re += z[0]; im += z[1]; }
            ar[0] += (float)re; ai[0] += (float)im;
            LADDER_STEP(w);
        }
        for (int l = 0; l < R; ++l) if (en[l] > bv[l]) { bv[l] = en[l]; bi[l] = clo; }
    }
    for (int l = 0; l < R; ++l) { sv[tid] = bv[l]; si[tid] = bi[l]; REDUCE(sv, si, best[(size_t)blockIdx.x * MAXR + l], arg[(size_t)blockIdx.x * MAXR + l]); }
}

// matrix operand T[KD x NH*2W] (column-major): row (j, v), column (part = 2 w + re/im, c_lo)
__global__ void build_t(const int16_t* tab, const uint8_t* glo, __half* w, long total) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < total) {
        int row = (int)(i % KD); long col = i / KD; int clo = (int)(col % NH), part = (int)(col / NH), j = row / NV, v = row % NV;
        w[i] = __float2half((float)tab[(((size_t)(part >> 1) * NSYM + j) * NV + (v ^ glo[j * NH + clo])) * 2 + (part & 1)]);
    }
}
// rung energies and maxima from the product S[NH x NH*2W] (column-major, row = c_hi)
__global__ void combine_k(const float* s, int W, int R, float* best, int* arg) {
    __shared__ float sv[256]; __shared__ int si[256];
    int chi = blockIdx.x, tid = threadIdx.x;
    float bv[MAXR]; int bi[MAXR]; for (int l = 0; l < MAXR; ++l) { bv[l] = -1.f; bi[l] = 0; }
    for (int clo = tid; clo < NH; clo += 256) {
        float ar[MAXR + 1], ai[MAXR + 1], en[MAXR]; for (int l = 0; l <= MAXR; ++l) { ar[l] = 0.f; ai[l] = 0.f; } for (int l = 0; l < MAXR; ++l) en[l] = 0.f;
        for (int w = 0; w < W; ++w) {
            ar[0] += s[((size_t)(2 * w) * NH + clo) * NH + chi]; ai[0] += s[((size_t)(2 * w + 1) * NH + clo) * NH + chi];
            LADDER_STEP(w);
        }
        for (int l = 0; l < R; ++l) if (en[l] > bv[l]) { bv[l] = en[l]; bi[l] = clo; }
    }
    for (int l = 0; l < R; ++l) { sv[tid] = bv[l]; si[tid] = bi[l]; REDUCE(sv, si, best[(size_t)blockIdx.x * MAXR + l], arg[(size_t)blockIdx.x * MAXR + l]); }
}
__global__ void f2h(const float* f, __half* h, long n) { long i = blockIdx.x * (long)blockDim.x + threadIdx.x; if (i < n) h[i] = __float2half(f[i]); }

struct Code {
    std::vector<uint8_t> ghi, glo; uint8_t *dghi = nullptr, *dglo = nullptr;
    explicit Code(std::mt19937_64& r) : ghi((size_t)NSYM * NH), glo((size_t)NSYM * NH) {
        for (int j = 0; j < NSYM; ++j) {
            uint32_t g[8]; for (auto& x : g) x = (uint32_t)(r() & 0xFFFFFF); uint8_t m = (uint8_t)r();
            auto val = [&](uint32_t c) { uint8_t v = 0; for (int b = 0; b < 8; ++b) v |= (uint8_t)(__builtin_parity(g[b] & c) << b); return v; };
            for (int h = 0; h < NH; ++h) { ghi[(size_t)j * NH + h] = val((uint32_t)h << HB) ^ m; glo[(size_t)j * NH + h] = val((uint32_t)h); }
        }
        CK(cudaMalloc(&dghi, ghi.size())); CK(cudaMemcpy(dghi, ghi.data(), ghi.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&dglo, glo.size())); CK(cudaMemcpy(dglo, glo.data(), glo.size(), cudaMemcpyHostToDevice));
    }
    uint8_t value(int j, uint32_t c) const { return ghi[(size_t)j * NH + (c >> HB)] ^ glo[(size_t)j * NH + (c & (NH - 1))]; }
};

// host: best message of rung l from per-block results
static uint32_t pick(const std::vector<float>& b, const std::vector<int>& g, int blocks, int h0, int l) {
    float bv = -2; uint32_t w = 0; for (int i = 0; i < blocks; ++i) if (b[(size_t)i * MAXR + l] > bv) { bv = b[(size_t)i * MAXR + l]; w = ((uint32_t)(i + h0) << HB) | (uint32_t)g[(size_t)i * MAXR + l]; } return w;
}

struct Bench {
    std::mt19937_64 rng{2026}; Code code{rng}; cublasHandle_t blas = nullptr;
    float* dbest = nullptr; int* darg = nullptr;
    Bench() { CK(cudaMalloc(&dbest, sizeof(float) * (size_t)NH * MAXR * 16)); CK(cudaMalloc(&darg, sizeof(int) * (size_t)NH * MAXR * 16)); if (cublasCreate(&blas) != CUBLAS_STATUS_SUCCESS) { fprintf(stderr, "cublas create failed\n"); exit(2); } }
    double gauss() { std::normal_distribution<double> d(0, 1); return d(rng); }

    // ---- energy
    uint16_t* dtq = nullptr; std::vector<uint32_t> eplant; int A = 16;
    void energy_setup() {
        std::vector<uint16_t> tq((size_t)A * KD); eplant.resize(A);
        for (auto& e : tq) e = (uint16_t)(rng() % 4000);
        for (int a = 0; a < A; ++a) { eplant[a] = (uint32_t)(rng() & 0xFFFFFF); for (int j = 0; j < NSYM; ++j) tq[(size_t)a * KD + j * NV + code.value(j, eplant[a])] += 3000; }
        CK(cudaMalloc(&dtq, sizeof(uint16_t) * tq.size())); CK(cudaMemcpy(dtq, tq.data(), sizeof(uint16_t) * tq.size(), cudaMemcpyHostToDevice));
    }
    void energy_run() { energy_k<<<NH * A, 256>>>(dtq, code.dghi, code.dglo, dbest, darg); }
    int energy_check() {
        std::vector<float> b((size_t)NH * A); std::vector<int> g((size_t)NH * A); CK(cudaMemcpy(b.data(), dbest, sizeof(float) * b.size(), cudaMemcpyDeviceToHost)); CK(cudaMemcpy(g.data(), darg, sizeof(int) * g.size(), cudaMemcpyDeviceToHost));
        int ok = 0; for (int a = 0; a < A; ++a) { float bv = -2; uint32_t w = 0; for (int c = 0; c < NH; ++c) if (b[(size_t)a * NH + c] > bv) { bv = b[(size_t)a * NH + c]; w = ((uint32_t)c << HB) | (uint32_t)g[(size_t)a * NH + c]; } ok += w == eplant[a]; } return ok;
    }

    // ---- ladder over tones
    int8_t* dz = nullptr; uint8_t* dsym = nullptr; int nt = 0, RL = 0; uint32_t lplant = 0;
    void ladder_setup(int nt_) {
        if (dz) { cudaFree(dz); cudaFree(dsym); } nt = nt_; RL = 0; for (int w = 2; w <= nt; w *= 2) ++RL;
        std::vector<uint8_t> sym(nt); for (int t = 0; t < nt; ++t) sym[t] = (uint8_t)(t % NSYM);
        for (int t = nt - 1; t > 0; --t) std::swap(sym[t], sym[rng() % (uint64_t)(t + 1)]);
        std::vector<int8_t> z((size_t)nt * NV * 2); for (auto& x : z) x = (int8_t)std::max(-7, std::min(7, (int)std::lrint(2.2 * gauss())));
        lplant = (uint32_t)(rng() & 0xFFFFFF);
        for (int t = 0; t < nt; ++t) { int8_t& x = z[((size_t)t * NV + code.value(sym[t], lplant)) * 2]; x = (int8_t)std::min(7, x + 2); }
        CK(cudaMalloc(&dz, z.size())); CK(cudaMemcpy(dz, z.data(), z.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&dsym, sym.size())); CK(cudaMemcpy(dsym, sym.data(), sym.size(), cudaMemcpyHostToDevice));
    }
    void ladder_run(int blocks = NH, int h0 = 0) { ladder_k<<<blocks, 256>>>(dz, dsym, code.dghi, code.dglo, nt, RL, h0, dbest, darg); }
    bool top_ok(int blocks, int h0, int R, uint32_t planted) {
        std::vector<float> b((size_t)blocks * MAXR); std::vector<int> g((size_t)blocks * MAXR); CK(cudaMemcpy(b.data(), dbest, sizeof(float) * b.size(), cudaMemcpyDeviceToHost)); CK(cudaMemcpy(g.data(), darg, sizeof(int) * g.size(), cudaMemcpyDeviceToHost));
        return pick(b, g, blocks, h0, R - 1) == planted;
    }

    // ---- table ladder and matrix product
    int16_t* dtab = nullptr; __half *dp = nullptr, *dw = nullptr; float* ds = nullptr; int W = 0, RT = 0; uint32_t tplant = 0;
    void table_setup(int W_, bool with_gemm) {
        if (dtab) { cudaFree(dtab); dtab = nullptr; } if (dw) { cudaFree(dw); dw = nullptr; } if (ds) { cudaFree(ds); ds = nullptr; }
        W = W_; RT = 0; for (int w = 1; w <= W; w *= 2) ++RT;
        std::vector<int16_t> tab((size_t)W * KD * 2); for (auto& x : tab) x = (int16_t)std::lrint(20 * gauss());
        tplant = (uint32_t)(rng() & 0xFFFFFF);
        for (int w = 0; w < W; ++w) for (int j = 0; j < NSYM; ++j) tab[(((size_t)w * NSYM + j) * NV + code.value(j, tplant)) * 2] += 12;
        CK(cudaMalloc(&dtab, sizeof(int16_t) * tab.size())); CK(cudaMemcpy(dtab, tab.data(), sizeof(int16_t) * tab.size(), cudaMemcpyHostToDevice));
        if (!with_gemm) return;
        if (!dp) {
            std::vector<float> pf((size_t)NH * KD, 0.f); for (int chi = 0; chi < NH; ++chi) for (int j = 0; j < NSYM; ++j) pf[(size_t)(j * NV + code.ghi[(size_t)j * NH + chi]) * NH + chi] = 1.f;
            float* pfd; CK(cudaMalloc(&pfd, sizeof(float) * pf.size())); CK(cudaMemcpy(pfd, pf.data(), sizeof(float) * pf.size(), cudaMemcpyHostToDevice));
            CK(cudaMalloc(&dp, sizeof(__half) * pf.size())); f2h<<<(unsigned)((pf.size() + 255) / 256), 256>>>(pfd, dp, (long)pf.size()); CK(cudaDeviceSynchronize()); CK(cudaFree(pfd));
        }
        size_t ncol = (size_t)NH * 2 * W;
        CK(cudaMalloc(&dw, sizeof(__half) * KD * ncol)); CK(cudaMalloc(&ds, sizeof(float) * NH * ncol));
    }
    void table_run(int blocks = NH, int h0 = 0) { table_k<<<blocks, 256>>>(dtab, code.dghi, code.dglo, W, RT, h0, dbest, darg); }
    void gemm_build() { long total = (long)KD * NH * 2 * W; build_t<<<(unsigned)((total + 255) / 256), 256>>>(dtab, code.dglo, dw, total); }
    void gemm_product() {
        float a = 1, b = 0; int ncol = NH * 2 * W;
        cublasStatus_t st = cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, NH, ncol, KD, &a, dp, CUDA_R_16F, NH, dw, CUDA_R_16F, KD, &b, ds, CUDA_R_32F, NH, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT);
        if (st != CUBLAS_STATUS_SUCCESS) { if (!gemm_failed) printf("gemm W = %d: cublasGemmEx status %d\n", W, (int)st); gemm_failed = true; }
    }
    bool gemm_failed = false;
    void gemm_combine() { combine_k<<<NH, 256>>>(ds, W, RT, dbest, darg); }
    void gemm_run() { gemm_build(); gemm_product(); gemm_combine(); }
};

template <class F> static double timeit(F f, double min_s = 2.0) {
    f(); CK(cudaDeviceSynchronize());
    double t0 = now(); int n = 0; do { f(); ++n; CK(cudaDeviceSynchronize()); } while (now() - t0 < min_s);
    return (now() - t0) / n;
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "all";
    cudaDeviceProp pr; CK(cudaGetDeviceProperties(&pr, 0));
    printf("device: %s, %d SMs, %.1f GiB\n", pr.name, pr.multiProcessorCount, pr.totalGlobalMem / 1073741824.0);
    Bench B;
    if (mode == "sustain") {
        std::string route = argc > 2 ? argv[2] : "gemm64"; double secs = argc > 3 ? atof(argv[3]) : 10;
        if (route == "energy") B.energy_setup(); else if (route.rfind("ladder", 0) == 0) B.ladder_setup(atoi(route.c_str() + 6)); else B.table_setup(atoi(route.c_str() + (route[0] == 'g' ? 4 : 5)), route[0] == 'g');
        double t0 = now(); long it = 0;
        while (now() - t0 < secs) { if (route == "energy") B.energy_run(); else if (route.rfind("ladder", 0) == 0) B.ladder_run(); else if (route[0] == 'g') B.gemm_run(); else B.table_run(); CK(cudaDeviceSynchronize()); ++it; }
        printf("sustain %s: %ld calls in %.1f s\n", route.c_str(), it, now() - t0); return 0;
    }
    printf("\nenergy (per-tone) decode, 32 symbols of 256 values, 16 hypotheses per call\n");
    B.energy_setup(); B.energy_run(); CK(cudaDeviceSynchronize());
    printf("verify energy: planted message found in %d of 16 hypotheses\n", B.energy_check());
    { double t = timeit([&] { B.energy_run(); }); printf("energy                 %10.4f ms/call  %12.1f decodes/s  %.3g lookups/s\n", t * 1e3, 16 / t, 16.0 * 32 * 16777216.0 / t); }

    printf("\nfull ladder over tones (one lookup per message and tone)\n");
    for (int nt : {128, 512, 2048, 8192, 32768}) {
        B.ladder_setup(nt); B.ladder_run(); CK(cudaDeviceSynchronize()); bool ok = B.top_ok(NH, 0, B.RL, B.lplant);
        double t = timeit([&] { B.ladder_run(); }, nt >= 8192 ? 4.0 : 2.0);
        printf("ladder %6d tones    %10.2f ms/decode %12.3f decodes/s  %.3g message-tones/s  rungs %d  planted on the whole-block rung: %s\n", nt, t * 1e3, 1 / t, (double)nt * 16777216.0 / t, B.RL, ok ? "yes" : "NO");
    }
    printf("\ntable ladder with W windows (32 W lookups per message), CUDA cores against one FP16 matrix product\n");
    for (int W : {16, 64, 128}) {
        size_t need = (size_t)KD * NH * 2 * W * 2 + (size_t)NH * NH * 2 * W * 4;
        bool with_gemm = need + ((size_t)6 << 30) < pr.totalGlobalMem;
        B.table_setup(W, with_gemm);
        B.table_run(); CK(cudaDeviceSynchronize()); bool ok = B.top_ok(NH, 0, B.RT, B.tplant);
        double t = timeit([&] { B.table_run(); }, 3.0);
        printf("table  W = %3d (direct) %10.2f ms/decode %12.3f decodes/s  %.3g lookups/s  rungs %d  planted: %s\n", W, t * 1e3, 1 / t, 32.0 * W * 16777216.0 / t, B.RT, ok ? "yes" : "NO");
        if (!with_gemm) { printf("gemm   W = %3d: skipped, needs %.1f GiB\n", W, need / 1073741824.0); continue; }
        B.gemm_failed = false; B.gemm_run(); CK(cudaDeviceSynchronize()); if (B.gemm_failed) { printf("gemm   W = %3d: product not available on this device or library\n", W); continue; }
        bool okg = B.top_ok(NH, 0, B.RT, B.tplant);
        double tb = timeit([&] { B.gemm_build(); }, 2.0), tp = timeit([&] { B.gemm_product(); }, 3.0), tc = timeit([&] { B.gemm_combine(); }, 2.0), ta = timeit([&] { B.gemm_run(); }, 3.0);
        double flop = 2.0 * NH * (double)KD * NH * 2 * W;
        printf("gemm   W = %3d (FP16)   %10.2f ms/decode %12.3f decodes/s  build %.2f ms, product %.2f ms (%.1f nominal TFLOP/s), rungs and maxima %.2f ms  planted: %s\n", W, ta * 1e3, 1 / ta, tb * 1e3, tp * 1e3, flop / tp / 1e12, tc * 1e3, okg ? "yes" : "NO");
    }
    return 0;
}
