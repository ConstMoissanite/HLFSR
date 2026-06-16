// linear_deep.cpp — 线性偏差深测：先筛选最高偏差掩码，再 2^30 对验证
#include "../hlfsr64.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

static FILE* g_log = nullptr;
#define LOG(fmt, ...) do { printf(fmt, ##__VA_ARGS__); if (g_log) fprintf(g_log, fmt, ##__VA_ARGS__); } while(0)

using hlfsr64_u64 = hlfsr64::u64;
using hlfsr64_u8  = hlfsr64::u8;
using hlfsr64_u16 = hlfsr64::u16;

int main() {
    g_log = fopen("linear_deep_result.txt", "w");

    const int SCREEN_MASKS = 5000;
    const int SCREEN_PAIRS = 500;
    const hlfsr64_u16 IDX = 0x100;
    const int BIT_POS = 0;

    LOG("=== Linear Bias Deep Test ===\n");
    LOG("Phase 1: screening %d masks x %d pairs\n", SCREEN_MASKS, SCREEN_PAIRS);

    // 阶段 1：筛选最高偏差掩码
    srand(0xDEADBEEF);
    double max_bias = 0;
    hlfsr64_u8 best_alpha[64];
    int best_idx = -1;

    for (int m = 0; m < SCREEN_MASKS; m++) {
        hlfsr64_u8 alpha[64];
        for (int i = 0; i < 64; i++) alpha[i] = (hlfsr64_u8)(rand() & 0xFF);

        int zeros = 0;
        for (int p = 0; p < SCREEN_PAIRS; p++) {
            hlfsr64_u8 x[64];
            for (int i = 0; i < 64; i++) x[i] = (hlfsr64_u8)(rand() & 0xFF);
            hlfsr64_u8 xa[64];
            for (int i = 0; i < 64; i++) xa[i] = x[i] ^ alpha[i];

            hlfsr64 c1, c2;
            c1.init(x, IDX);
            c2.init(xa, IDX);
            if (((c1.next() >> BIT_POS) & 1) == ((c2.next() >> BIT_POS) & 1)) zeros++;
        }
        double bias = fabs(zeros / (double)SCREEN_PAIRS - 0.5);
        if (bias > max_bias) {
            max_bias = bias;
            for (int i = 0; i < 64; i++) best_alpha[i] = alpha[i];
            best_idx = m;
        }
        if ((m + 1) % 1000 == 0)
            LOG("  screened %d/%d, max_bias=%.6f (mask #%d)\n", m+1, SCREEN_MASKS, max_bias, best_idx);
    }
    LOG("Phase 1 done: max_bias=%.6f at mask #%d\n", max_bias, best_idx);

    // 最高偏差掩码的 Hamming 权重
    int hw = 0;
    for (int i = 0; i < 64; i++) hw += __builtin_popcount(best_alpha[i]);
    LOG("  mask #%d Hamming weight: %d / 512 (%.1f%%)\n", best_idx, hw, hw * 100.0 / 512);

    // 阶段 2a：上界筛查 (10^7 对, bias > 10^-4 → 直接 FAIL)
    const long long PRE_PAIRS = 10000000LL;
    LOG("\nPhase 2a: upper-bound check with 10^7 pairs on mask #%d\n", best_idx);
    LOG("  threshold: bias > 1e-4 → FAIL\n");
    {
        long long z = 0;
        srand(0xCAFEBABE);
        for (long long p = 0; p < PRE_PAIRS; p++) {
            hlfsr64_u8 x[64], xa[64];
            for (int i = 0; i < 64; i++) { x[i] = (hlfsr64_u8)(rand() & 0xFF); xa[i] = x[i] ^ best_alpha[i]; }
            hlfsr64 c1, c2;
            c1.init(x, IDX); c2.init(xa, IDX);
            if (((c1.next() >> BIT_POS) & 1) == ((c2.next() >> BIT_POS) & 1)) z++;
        }
        double pre_bias = fabs(z / (double)PRE_PAIRS - 0.5);
        double pre_noise = 1.0 / sqrt((double)PRE_PAIRS);
        LOG("  pre-bias: %.8f  noise: %.8f  3σ: %.8f\n", pre_bias, pre_noise, 3.0*pre_noise);
        if (pre_bias > 1e-4) {
            LOG("  *** FAIL: bias=%.8f (%.1f× threshold, 3σ=%.8f) ***\n",
                pre_bias, pre_bias / 1e-4, 3.0 * pre_noise);
            if (g_log) fclose(g_log);
            return 1;
        }
        LOG("  PASS — bias below 1e-4, proceeding to full 2^30\n");
    }

    // 阶段 2b：2^30 对深测
    const long long DEEP_PAIRS = 1LL << 30;
    LOG("\nPhase 2b: deep test with 2^30 pairs on mask #%d\n", best_idx);

    auto t0 = std::chrono::high_resolution_clock::now();
    long long zeros = 0;
    const long long BLOCK = 1LL << 24; // 每 16M 输出一次进度

    // 重新播种以确保可复现
    srand(0xCAFEBABE);

    for (long long p = 0; p < DEEP_PAIRS; p++) {
        hlfsr64_u8 x[64];
        for (int i = 0; i < 64; i++) x[i] = (hlfsr64_u8)(rand() & 0xFF);
        hlfsr64_u8 xa[64];
        for (int i = 0; i < 64; i++) xa[i] = x[i] ^ best_alpha[i];

        hlfsr64 c1, c2;
        c1.init(x, IDX);
        c2.init(xa, IDX);
        if (((c1.next() >> BIT_POS) & 1) == ((c2.next() >> BIT_POS) & 1)) zeros++;

        if ((p + 1) % BLOCK == 0) {
            double cur_bias = fabs(zeros / (double)(p + 1) - 0.5);
            double noise   = 1.0 / sqrt((double)(p + 1));
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - t0).count();
            LOG("  %lld / %lld  bias=%.8f  noise=%.8f  3σ=%.8f  (%.0fs)\n",
                   p + 1, DEEP_PAIRS, cur_bias, noise, 3.0*noise, elapsed);
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    double final_bias = fabs(zeros / (double)DEEP_PAIRS - 0.5);
    double noise      = 1.0 / sqrt((double)DEEP_PAIRS);
    double three_sigma = 3.0 * noise;

    LOG("\n=== Final Result ===\n");
    LOG("  Mask #%d, 2^30 pairs\n", best_idx);
    LOG("  Final bias:     %.10f\n", final_bias);
    LOG("  Noise floor:    %.10f\n", noise);
    LOG("  3σ threshold:   %.10f\n", three_sigma);
    LOG("  Verdict:        %s\n", final_bias < three_sigma ? "PASS — no detectable bias" : "elevated");
    LOG("  Elapsed:        %.0f s\n", elapsed);
    if (g_log) { fclose(g_log); printf("\nResults saved to linear_deep_result.txt\n"); }
    return 0;
}
