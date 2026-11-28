// Matrix Modem FSK below 9 kHz on a GPU: (1) a cell-mode Monte Carlo of the keyed 10-bit block with
// the frequency and drift search as one matrix product, to measure simulation speed; (2) one pass
// over the messages with vector table entries against a pass per hypothesis.
//
// Measurement aid, 28 November 2026 (Fable-5.2 pass). Same block definitions as search_gpu.cu,
// with 10 information bits: 512 tones on 16 positions, 32 symbols of 256 values.
//
// (1) A trial is one block with its own noise, planted message, frequency bin and drift. Trials are
//     batched as the receiver batches messages: the rows of the matrix operand are (trial, message).
//       gen       bin values of every tone and position for every trial: complex Gaussian noise of
//                 2.2 units per component from a counter-based generator, the planted tone added on
//                 its position, quantised to the 4-bit code (step 0.45 sigma, clip at 7)
//       gather    the tone values of every (trial, message) row as FP16 planes
//       product   C[rows x 2 hypotheses] = U[rows x 2 N] * A[2 N x 2 hypotheses] on Tensor Cores
//       epilogue  per row: the best energy over all whole-block hypotheses, and the energy on the
//                 trial's true hypothesis; per trial the best message of each
//     A trial is wrong when the best message is not the planted one. "Known" uses the true
//     frequency bin and drift only; "searched" takes the best over every bin and drift.
//     Block energy: E/N0 = N a^2 / (2 sigma^2) for tone amplitude a.
//     Everything that does not depend on the noise (code values, keyed map, interleaver, the
//     matrix A) is built once and shared by all trials.
// (2) vec: all 2^24 messages, H real 16-bit tables; one kernel pass with the entries of all
//     hypotheses for a symbol value stored together, against one pass per hypothesis with that
//     hypothesis's table in shared memory (the per-tone kernel of decode_gpu.cu).
//
// Build: nvcc -O3 -std=c++17 -arch=sm_120 sim_gpu.cu -lcublas -o sim_gpu
// Run:   ./sim_gpu [trials per level, default 40960]
//        ./sim_gpu sustain SECONDS          (the simulation loop, for telemetry)
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cublas_v2.h>

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

static const int NSYM = 32, NV = 256, POS = 16, ND = 4, MAXR = 16, NM = 1024, N = 512, NB0 = 174;
static const int HB = 12, NH = 1 << HB;
static const double PI = 3.14159265358979323846;

#define REDUCE(sv, si, outv, outi) do { __syncthreads(); for (int h_ = 128; h_ > 0; h_ >>= 1) { if (tid < h_ && sv[tid + h_] > sv[tid]) { sv[tid] = sv[tid + h_]; si[tid] = si[tid + h_]; } __syncthreads(); } if (tid == 0) { outv = sv[0]; outi = si[0]; } __syncthreads(); } while (0)

// ---------------------------------------------------------------- (1) simulation
static __device__ __forceinline__ uint64_t mix(uint64_t x) { x += 0x9E3779B97F4A7C15ull; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull; x = (x ^ (x >> 27)) * 0x94D049BB133111EBull; return x ^ (x >> 31); }

// bin values zpos[trial][tone][position][2], one thread per (trial, tone)
__global__ void gen_k(const uint8_t* sym, const uint8_t* pmap, const uint8_t* val10, const int* pm, const int* pf, const int* pd, long total, uint64_t seed, float amp, int8_t* zpos) {
    long idx = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (idx >= total) return;
    int trial = (int)(idx / N), t = (int)(idx % N);
    const float gdk[ND] = {-5.82f, -1.94f, 1.94f, 5.82f};
    int pp = pmap[t * NV + val10[sym[t] * NM + pm[trial]]];
    float x = (float)t / N, ph = (float)PI * (pf[trial] - NB0 / 2) * x + (float)PI * gdk[pd[trial]] * x * x * 0.5f;
    for (int p = 0; p < POS; ++p) {
        uint64_t r = mix(mix(seed) + (uint64_t)(idx * POS + p));
        float u1 = ((float)(uint32_t)(r >> 32) + 0.5f) * 2.3283064e-10f, u2 = ((float)(uint32_t)r + 0.5f) * 2.3283064e-10f;
        float rad = 2.2f * sqrtf(-2.f * logf(u1)), zr = rad * cosf(6.2831853f * u2), zi = rad * sinf(6.2831853f * u2);
        if (p == pp) { zr += amp * cosf(ph); zi += amp * sinf(ph); }
        zpos[(idx * POS + p) * 2] = (int8_t)fminf(7.f, fmaxf(-7.f, rintf(zr)));
        zpos[(idx * POS + p) * 2 + 1] = (int8_t)fminf(7.f, fmaxf(-7.f, rintf(zi)));
    }
}
// tone values of every (trial, message) row as FP16 planes U[B x 2N]
__global__ void sgather_k(const uint8_t* sym, const uint8_t* pmap, const uint8_t* val10, const int8_t* zpos, long B, __half* ut) {
    long row = (long)blockIdx.x * 256 + threadIdx.x; int trial = (int)(row >> 10), m = (int)(row & (NM - 1));
    for (int t = 0; t < N; ++t) {
        int p = pmap[t * NV + val10[sym[t] * NM + m]];
        const int8_t* z = zpos + (((size_t)trial * N + t) * POS + p) * 2;
        ut[(size_t)t * B + row] = __float2half((float)z[0]); ut[(size_t)(N + t) * B + row] = __float2half((float)z[1]);
    }
}
// per row: best whole-block energy over all hypotheses, and the energy on the trial's own
// hypothesis; out[block][0..1], arg[block][0..1] hold the best row of each kind in the block
__global__ void sepi_k(const __half* c, long B, int nb0, const int* pf, const int* pd, float* out, int* arg) {
    __shared__ float sv[256]; __shared__ int si[256];
    int tid = threadIdx.x; long row = (long)blockIdx.x * 256 + tid; int trial = (int)(row >> 10), m = (int)(row & (NM - 1));
    float best = -1.f;
    for (int h = 0; h < ND * nb0; ++h) { float re = __half2float(c[(size_t)(2 * h) * B + row]), im = __half2float(c[(size_t)(2 * h + 1) * B + row]); float e = re * re + im * im; if (e > best) best = e; }
    int hk = pd[trial] * nb0 + pf[trial];
    float rk = __half2float(c[(size_t)(2 * hk) * B + row]), ik = __half2float(c[(size_t)(2 * hk + 1) * B + row]);
    sv[tid] = best; si[tid] = m; REDUCE(sv, si, out[blockIdx.x * 2], arg[blockIdx.x * 2]);
    sv[tid] = rk * rk + ik * ik; si[tid] = m; REDUCE(sv, si, out[blockIdx.x * 2 + 1], arg[blockIdx.x * 2 + 1]);
}
__global__ void f2h(const float* f, __half* h, long n) { long i = blockIdx.x * (long)blockDim.x + threadIdx.x; if (i < n) h[i] = __float2half(f[i]); }

// ---------------------------------------------------------------- (2) vector entries
// one pass: tv[(j * NV + v) * H + h]; one block per c_hi, 16 messages per thread
__global__ void vec_k(const int16_t* tv, const uint8_t* ghi, const uint8_t* glo, int H, float* best, int* arg) {
    __shared__ uint8_t gh[NSYM]; __shared__ float sv[256]; __shared__ int si[256];
    int chi = blockIdx.x, tid = threadIdx.x;
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    int bv = -1, bi = 0;
    for (int clo = tid; clo < NH; clo += 256) {
        uint8_t val[NSYM]; for (int j = 0; j < NSYM; ++j) val[j] = gh[j] ^ glo[j * NH + clo];
        for (int h0 = 0; h0 < H; h0 += 8) {
            int a[8] = {0, 0, 0, 0, 0, 0, 0, 0}; int n = H - h0 < 8 ? H - h0 : 8;
            for (int j = 0; j < NSYM; ++j) { const int16_t* x = tv + ((size_t)j * NV + val[j]) * H + h0; for (int u = 0; u < n; ++u) a[u] += x[u]; }
            for (int u = 0; u < n; ++u) if (a[u] > bv) { bv = a[u]; bi = clo; }
        }
    }
    sv[tid] = (float)bv; si[tid] = bi;
    REDUCE(sv, si, best[blockIdx.x], arg[blockIdx.x]);
}
// one hypothesis: its table tb[j * NV + v] in shared memory, as the per-tone kernel
__global__ void one_k(const int16_t* tb, const uint8_t* ghi, const uint8_t* glo, float* best, int* arg) {
    __shared__ int16_t t[NSYM * NV]; __shared__ uint8_t gh[NSYM]; __shared__ float sv[256]; __shared__ int si[256];
    int chi = blockIdx.x, tid = threadIdx.x;
    for (int i = tid; i < NSYM * NV; i += 256) t[i] = tb[i];
    if (tid < NSYM) gh[tid] = ghi[tid * NH + chi];
    __syncthreads();
    int bv = -1, bi = 0;
    for (int clo = tid; clo < NH; clo += 256) { int s = 0; for (int j = 0; j < NSYM; ++j) s += t[j * NV + (gh[j] ^ glo[j * NH + clo])]; if (s > bv) { bv = s; bi = clo; } }
    sv[tid] = (float)bv; si[tid] = bi;
    REDUCE(sv, si, best[blockIdx.x], arg[blockIdx.x]);
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "all";
    const double ssecs = mode == "sustain" && argc > 2 ? atof(argv[2]) : 0;
    const long want = (mode != "sustain" && argc > 1 && atol(argv[1]) > 0) ? atol(argv[1]) : 40960;
    cudaDeviceProp pr; CK(cudaGetDeviceProperties(&pr, 0));
    std::mt19937_64 rng(2026);

    // ---- shared by all trials: code values, interleaver, keyed map, hypothesis matrix
    std::vector<uint8_t> val10((size_t)NSYM * NM);
    for (int j = 0; j < NSYM; ++j) { uint32_t g[8]; for (auto& x : g) x = (uint32_t)(rng() & (NM - 1)); const uint8_t msk = (uint8_t)rng();
        for (int m = 0; m < NM; ++m) { uint8_t v = 0; for (int b = 0; b < 8; ++b) v |= (uint8_t)(__builtin_parity(g[b] & (uint32_t)m) << b); val10[(size_t)j * NM + m] = v ^ msk; } }
    std::vector<uint8_t> sym(N); for (int t = 0; t < N; ++t) sym[t] = (uint8_t)(t % NSYM);
    for (int t = N - 1; t > 0; --t) std::swap(sym[t], sym[rng() % (uint64_t)(t + 1)]);
    std::vector<uint8_t> pmap((size_t)N * NV);
    for (int t = 0; t < N; ++t) { uint8_t map[NV]; for (int v = 0; v < NV; ++v) map[v] = (uint8_t)(v % POS); for (int v = NV - 1; v > 0; --v) std::swap(map[v], map[rng() % (uint64_t)(v + 1)]); for (int v = 0; v < NV; ++v) pmap[(size_t)t * NV + v] = map[v]; }
    const double gd[ND] = {-5.82, -1.94, 1.94, 5.82};
    int R = 0; while ((2 << R) <= N) ++R;
    std::vector<int> nb(R), rowoff(R + 1, 0);
    for (int r = 0; r < R; ++r) { nb[r] = std::max(1, (NB0 + (1 << r) - 1) >> r); rowoff[r + 1] = rowoff[r] + (r == 0 ? ND : 1) * (1 << r) * nb[r]; }
    auto build = [&](int RG, int& rows) {                 // A[2N x 2 rows] as FP16 on the device
        rows = rowoff[RG]; std::vector<float> af((size_t)2 * N * 2 * rows, 0.f);
        for (int r = 0; r < RG; ++r) { const int W = 1 << r, L = N >> r, nbr = nb[r];
            for (int d = 0; d < (r == 0 ? ND : 1); ++d) for (int w = 0; w < W; ++w) for (int b = 0; b < nbr; ++b) { const size_t row = (size_t)rowoff[r] + ((size_t)d * W + w) * nbr + b;
                for (int i = 0; i < L; ++i) { const int n = w * L + i; const double x = (double)n / N, a = -PI * (b - nbr / 2) * i / L - (r == 0 ? PI * gd[d] * x * x / 2 : 0.0), ar = std::cos(a), ai = std::sin(a);
                    af[(2 * row) * (size_t)(2 * N) + n] = (float)ar; af[(2 * row) * (size_t)(2 * N) + N + n] = (float)-ai; af[(2 * row + 1) * (size_t)(2 * N) + n] = (float)ai; af[(2 * row + 1) * (size_t)(2 * N) + N + n] = (float)ar; } } }
        float* daf; __half* dah; CK(cudaMalloc(&daf, sizeof(float) * af.size())); CK(cudaMemcpy(daf, af.data(), sizeof(float) * af.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&dah, sizeof(__half) * af.size())); f2h<<<(unsigned)((af.size() + 255) / 256), 256>>>(daf, dah, (long)af.size()); CK(cudaDeviceSynchronize()); CK(cudaFree(daf)); return dah;
    };
    uint8_t *dsym, *dpmap, *dval;
    CK(cudaMalloc(&dsym, sym.size())); CK(cudaMemcpy(dsym, sym.data(), sym.size(), cudaMemcpyHostToDevice));
    CK(cudaMalloc(&dpmap, pmap.size())); CK(cudaMemcpy(dpmap, pmap.data(), pmap.size(), cudaMemcpyHostToDevice));
    CK(cudaMalloc(&dval, val10.size())); CK(cudaMemcpy(dval, val10.data(), val10.size(), cudaMemcpyHostToDevice));
    cublasHandle_t blas; if (cublasCreate(&blas) != CUBLAS_STATUS_SUCCESS) { fprintf(stderr, "cublas create failed\n"); return 2; }

    const int TB = 128; const long B = (long)TB * NM; const int NBLK = (int)(B / 256);   // 128 trials per batch
    int8_t* dz; __half *dut, *dc; int *dpm, *dpf, *dpd, *darg; float* dout;
    CK(cudaMalloc(&dz, (size_t)TB * N * POS * 2)); CK(cudaMalloc(&dut, sizeof(__half) * (size_t)B * 2 * N));
    CK(cudaMalloc(&dpm, sizeof(int) * TB)); CK(cudaMalloc(&dpf, sizeof(int) * TB)); CK(cudaMalloc(&dpd, sizeof(int) * TB));
    CK(cudaMalloc(&dout, sizeof(float) * NBLK * 2)); CK(cudaMalloc(&darg, sizeof(int) * NBLK * 2));
    std::vector<int> pm(TB), pf(TB), pd(TB), harg((size_t)NBLK * 2); std::vector<float> hout((size_t)NBLK * 2);
    const float one = 1, zero = 0;

    if (mode != "sustain") {
        printf("device: %s, %d SMs, %.1f GiB\n", pr.name, pr.multiProcessorCount, pr.totalGlobalMem / 1073741824.0);
        printf("\n# Monte Carlo of the 10-bit block, cell mode, %d tones on %d positions, %d trials per batch\n", N, POS, TB);
    }
    for (int variant = 0; variant < 2; ++variant) {
        const int RG = variant == 0 ? 1 : R; int rows; __half* dah = build(RG, rows);
        if (mode == "sustain" && variant == 0) { cudaFree(dah); continue; }
        CK(cudaMalloc(&dc, sizeof(__half) * (size_t)B * 2 * rows));
        uint64_t batchno = 0; double tgen = 0, tgat = 0, tpro = 0, tepi = 0;
        auto batch = [&](float amp, long& errs, long& errk, bool timed) {
            for (int t = 0; t < TB; ++t) { pm[t] = (int)(rng() & (NM - 1)); pf[t] = (int)(rng() % (uint64_t)NB0); pd[t] = (int)(rng() % ND); }
            CK(cudaMemcpy(dpm, pm.data(), sizeof(int) * TB, cudaMemcpyHostToDevice)); CK(cudaMemcpy(dpf, pf.data(), sizeof(int) * TB, cudaMemcpyHostToDevice)); CK(cudaMemcpy(dpd, pd.data(), sizeof(int) * TB, cudaMemcpyHostToDevice));
            double t0 = now();
            gen_k<<<(unsigned)(((long)TB * N + 255) / 256), 256>>>(dsym, dpmap, dval, dpm, dpf, dpd, (long)TB * N, 0x5EEDull * 1000003ull + (++batchno), amp, dz);
            if (timed) { CK(cudaDeviceSynchronize()); tgen += now() - t0; t0 = now(); }
            sgather_k<<<NBLK, 256>>>(dsym, dpmap, dval, dz, B, dut);
            if (timed) { CK(cudaDeviceSynchronize()); tgat += now() - t0; t0 = now(); }
            if (cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, (int)B, 2 * rows, 2 * N, &one, dut, CUDA_R_16F, (int)B, dah, CUDA_R_16F, 2 * N, &zero, dc, CUDA_R_16F, (int)B, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT) != CUBLAS_STATUS_SUCCESS) { fprintf(stderr, "gemm failed\n"); exit(2); }
            if (timed) { CK(cudaDeviceSynchronize()); tpro += now() - t0; t0 = now(); }
            sepi_k<<<NBLK, 256>>>(dc, B, nb[0], dpf, dpd, dout, darg);
            CK(cudaMemcpy(hout.data(), dout, sizeof(float) * hout.size(), cudaMemcpyDeviceToHost)); CK(cudaMemcpy(harg.data(), darg, sizeof(int) * harg.size(), cudaMemcpyDeviceToHost));
            if (timed) tepi += now() - t0;
            for (int t = 0; t < TB; ++t) for (int kind = 0; kind < 2; ++kind) { float bv = -2; int bm = -1; for (int q = 0; q < 4; ++q) { const int blk = t * 4 + q; if (hout[(size_t)blk * 2 + kind] > bv) { bv = hout[(size_t)blk * 2 + kind]; bm = harg[(size_t)blk * 2 + kind]; } } if (bm != pm[t]) (kind == 0 ? errs : errk)++; }
        };
        if (mode == "sustain") { long e1 = 0, e2 = 0, n = 0; const double t0 = now(); while (now() - t0 < ssecs) { batch(0.6f, e1, e2, false); n += TB; } printf("sustain sim: %ld trials in %.1f s\n", n, now() - t0); return 0; }
        printf("\n## %s: %d complex rows in the matrix operand\n\n", variant == 0 ? "whole-block window only (4 drift hypotheses x 174 bins)" : "every rung of the ladder in the product, decision on the whole block", rows);
        { long e1 = 0, e2 = 0; batch(0.6f, e1, e2, false); }                 // warm up
        printf("| Block E/N0 | Trials | Wrong, frequency and drift known | Wrong, searched over %d hypotheses | Wall time | Trials per second |\n| ---: | ---: | ---: | ---: | ---: | ---: |\n", ND * nb[0]);
        const long nbatch = (want + TB - 1) / TB; double rate = 0;
        for (double db : {11.0, 12.0, 13.0, 14.0, 15.0, 16.0}) {
            const float amp = 2.2f * (float)std::sqrt(2.0 * std::pow(10.0, db / 10) / N); long es = 0, ek = 0; const double t0 = now();
            for (long i = 0; i < nbatch; ++i) batch(amp, es, ek, false);
            const double dt = now() - t0, trials = (double)nbatch * TB; rate = trials / dt;
            printf("| %.0f dB | %.0f | %.4f | %.4f | %.2f s | %.0f |\n", db, trials, ek / trials, es / trials, dt, rate);
        }
        { long e1 = 0, e2 = 0; for (int i = 0; i < 50; ++i) batch(0.6f, e1, e2, true); const double tot = tgen + tgat + tpro + tepi;
          printf("\nshare of a batch: generation %.0f%%, gather %.0f%%, product %.0f%% (%.0f nominal TFLOP/s), epilogue and read-back %.0f%%; %.2f ms per batch of %d trials when each stage is waited for\n", 100 * tgen / tot, 100 * tgat / tot, 100 * tpro / tot, 2.0 * B * (2.0 * rows) * (2.0 * N) / (tpro / 50) / 1e12, 100 * tepi / tot, tot / 50 * 1e3, TB);
          printf("one trial stands for a block of 5,242.88 s: %.3g times real time\n", rate * 5242.88); }
        cudaFree(dc); cudaFree(dah);
    }

    // ---- (2) vector entries
    {
        printf("\n# One pass over 2^24 messages with vector entries against a pass per hypothesis, real 16-bit entries\n\n");
        std::vector<uint8_t> ghi((size_t)NSYM * NH), glo((size_t)NSYM * NH);
        for (int j = 0; j < NSYM; ++j) { uint32_t g[8]; for (auto& x : g) x = (uint32_t)(rng() & 0xFFFFFF); const uint8_t msk = (uint8_t)rng();
            auto val = [&](uint32_t c) { uint8_t v = 0; for (int b = 0; b < 8; ++b) v |= (uint8_t)(__builtin_parity(g[b] & c) << b); return v; };
            for (int h = 0; h < NH; ++h) { ghi[(size_t)j * NH + h] = val((uint32_t)h << HB) ^ msk; glo[(size_t)j * NH + h] = val((uint32_t)h); } }
        auto value = [&](int j, uint32_t c) { return (uint8_t)(ghi[(size_t)j * NH + (c >> HB)] ^ glo[(size_t)j * NH + (c & (NH - 1))]); };
        const uint32_t planted = (uint32_t)(rng() & 0xFFFFFF);
        uint8_t *dghi, *dglo; float* db; int* da;
        CK(cudaMalloc(&dghi, ghi.size())); CK(cudaMemcpy(dghi, ghi.data(), ghi.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&dglo, glo.size())); CK(cudaMemcpy(dglo, glo.data(), glo.size(), cudaMemcpyHostToDevice));
        CK(cudaMalloc(&db, sizeof(float) * NH)); CK(cudaMalloc(&da, sizeof(int) * NH));
        auto found = [&]() { std::vector<float> b(NH); std::vector<int> a(NH); CK(cudaMemcpy(b.data(), db, sizeof(float) * NH, cudaMemcpyDeviceToHost)); CK(cudaMemcpy(a.data(), da, sizeof(int) * NH, cudaMemcpyDeviceToHost));
            float bv = -2; uint32_t w = 0; for (int i = 0; i < NH; ++i) if (b[i] > bv) { bv = b[i]; w = ((uint32_t)i << HB) | (uint32_t)a[i]; } return w == planted; };
        auto timeit = [&](auto f) { f(); CK(cudaDeviceSynchronize()); const double t0 = now(); int n = 0; do { f(); ++n; CK(cudaDeviceSynchronize()); } while (now() - t0 < 2.0); return (now() - t0) / n; };
        std::vector<int16_t> tb((size_t)NSYM * NV); for (auto& x : tb) x = (int16_t)(rng() % 400);
        for (int j = 0; j < NSYM; ++j) tb[(size_t)j * NV + value(j, planted)] += 300;
        int16_t* dtb; CK(cudaMalloc(&dtb, sizeof(int16_t) * tb.size())); CK(cudaMemcpy(dtb, tb.data(), sizeof(int16_t) * tb.size(), cudaMemcpyHostToDevice));
        one_k<<<NH, 256>>>(dtb, dghi, dglo, db, da); CK(cudaDeviceSynchronize()); const bool ok1 = found();
        const double t1 = timeit([&] { one_k<<<NH, 256>>>(dtb, dghi, dglo, db, da); });
        printf("per hypothesis: %.4f ms for all messages (%.0f passes per second); planted %s\n\n", t1 * 1e3, 1 / t1, ok1 ? "found" : "NOT found");
        printf("| Hypotheses | One pass | Per hypothesis would take | One pass is faster by | Planted |\n| ---: | ---: | ---: | ---: | --- |\n");
        for (int H : {7, 56, 350}) {
            std::vector<int16_t> tv((size_t)NSYM * NV * H); for (auto& x : tv) x = (int16_t)(rng() % 400);
            for (int j = 0; j < NSYM; ++j) for (int h = 0; h < H; ++h) tv[((size_t)j * NV + value(j, planted)) * H + h] += 300;
            int16_t* dtv; CK(cudaMalloc(&dtv, sizeof(int16_t) * tv.size())); CK(cudaMemcpy(dtv, tv.data(), sizeof(int16_t) * tv.size(), cudaMemcpyHostToDevice));
            vec_k<<<NH, 256>>>(dtv, dghi, dglo, H, db, da); CK(cudaDeviceSynchronize()); const bool ok = found();
            const double tvv = timeit([&] { vec_k<<<NH, 256>>>(dtv, dghi, dglo, H, db, da); });
            printf("| %d | %.3f ms | %.3f ms | %.2f | %s |\n", H, tvv * 1e3, t1 * H * 1e3, t1 * H / tvv, ok ? "found" : "NOT found");
            cudaFree(dtv);
        }
    }
    return 0;
}
