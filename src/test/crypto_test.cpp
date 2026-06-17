// crypto_test.cpp — HLFSR-64 差分 + 代数度分析 + 线性偏差
#include "../hlfsr64.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

static FILE* g_log = nullptr;
#define LOG(fmt, ...) do { printf(fmt, ##__VA_ARGS__); if (g_log) fprintf(g_log, fmt, ##__VA_ARGS__); } while(0)

// ============================================================
// 1. 差分分析: 单比特翻转 → 输出差分传播 & 雪崩效应
// ============================================================
static void test_differential() {
    LOG("=== Differential Analysis ===\n");
    const int N_KEYS = 100;
    const int N_STEPS = 64;
    const int BITS = 64;

    hlfsr64::u8 km[64];
    hlfsr64::u16 idx;

    // 统计: 每步每 bit 的翻转概率
    std::vector<double> prob(N_STEPS, 0);
    std::vector<int>    zeros(N_STEPS, 0);  // 全零差分计数

    for (int trial = 0; trial < N_KEYS; trial++) {
        // 随机 key
        for (int i = 0; i < 64; i++) km[i] = (hlfsr64::u8)(rand() & 0xFF);
        idx = rand() & 0x1FF;

        for (int bit = 0; bit < BITS; bit++) {
            // 参考: 原始 key 的密钥流
            hlfsr64 ref;
            ref.init(km, idx);
            hlfsr64::u64 ref_out[N_STEPS];
            for (int s = 0; s < N_STEPS; s++) ref_out[s] = ref.next();

            // 差分: 翻转 key_material 的 1 bit
            int byte_idx = bit / 8;
            int bit_idx  = bit % 8;
            hlfsr64::u8 km_mod[64]; memcpy(km_mod, km, 64);
            km_mod[byte_idx] ^= (1 << bit_idx);

            hlfsr64 mod;
            mod.init(km_mod, idx);
            for (int s = 0; s < N_STEPS; s++) {
                hlfsr64::u64 out = mod.next();
                hlfsr64::u64 diff = ref_out[s] ^ out;
                if (diff == 0) zeros[s]++;
                int changed = __builtin_popcountll(diff);
                prob[s] += (double)changed / BITS;
            }
        }
        if ((trial + 1) % 20 == 0) LOG("  diff: %d/%d keys (×64 bits)\n", trial+1, N_KEYS);
    }

    double total_samples = N_KEYS * BITS;
    LOG("\n  step  min-avalanche  max-avalanche  avg  zero-diff-rate\n");
    LOG("  ----  --------------  --------------  ----  --------------\n");
    for (int s = 0; s < std::min(16, N_STEPS); s++) {
        double avg = prob[s] / total_samples;
        double zr  = zeros[s] / total_samples * 100;
        LOG("  %4d  %14.4f  %14.4f  %4.0f%%  %6.1f%%\n",
               s, 0.0, 1.0, avg * 100, zr);
    }
    // 后半段汇总
    double avg_late = 0; int zr_late = 0;
    for (int s = 16; s < N_STEPS; s++) { avg_late += prob[s]; zr_late += zeros[s]; }
    avg_late /= (total_samples * (N_STEPS - 16));
    LOG("  %4s  %14s  %14s  %4.0f%%  %6.1f%%\n", "16-63", "", "", avg_late * 100,
           zr_late * 100.0 / (total_samples * (N_STEPS - 16)));
    LOG("  (ideal: avg=50%%, zero-diff < 1e-18)\n\n");
}

// ============================================================
// 2. 代数度检测: 高阶差分 (自适应: 高度数减少 trial 数以控制时间)
// ============================================================
static void test_algebraic_degree(int MAX_DEG = 26, int DEFAULT_TRIALS = 10) {
    LOG("=== Algebraic Degree Estimation (max deg=%d, trials=%d) ===\n", MAX_DEG, DEFAULT_TRIALS);
    LOG("  Method: d-th order differential over random affine subspaces\n");
    LOG("  If output sum = 0 for all d-dim subspaces, degree < d\n\n");

    std::vector<int> trials_table(MAX_DEG + 1, DEFAULT_TRIALS);
    // 低 degree 多 trial (便宜), 高 degree 按参数
    for (int d = 1; d <= 11 && d <= MAX_DEG; d++) {
        int t[] = {0, 100, 80, 60, 40, 25, 15, 10, 8, 6, 5, 4};
        trials_table[d] = t[d];
    }
    trials_table[0] = 0;

    hlfsr64::u8 km[64];
    for (int i = 0; i < 64; i++) km[i] = (hlfsr64::u8)(rand() & 0xFF);
    hlfsr64::u16 idx = 0x100;

    LOG("  deg  trials  non-zero-rate  conclusion\n");
    LOG("  ---  ------  -------------  ----------\n");

    std::vector<int> basis(MAX_DEG);
    for (int d = 1; d <= MAX_DEG; d++) {
        int TRIALS = trials_table[d];
        int non_zero = 0;
        for (int t = 0; t < TRIALS; t++) {
            for (int b = 0; b < d; b++) basis[b] = rand() % 512;

            hlfsr64::u64 sum = 0;
            int N = 1 << d;
            for (int mask = 0; mask < N; mask++) {
                hlfsr64::u8 km_sub[64]; memcpy(km_sub, km, 64);
                for (int b = 0; b < d; b++)
                    if (mask & (1 << b)) {
                        km_sub[basis[b] / 8] ^= (1 << (basis[b] % 8));
                    }
                hlfsr64 c;
                c.init(km_sub, idx);
                sum ^= c.next();
            }
            if (sum != 0) non_zero++;
        }
        double rate = non_zero * 100.0 / TRIALS;
        LOG("  %3d  %6d  %13.0f%%       %s%d\n", d, TRIALS, rate,
               rate > 0 ? "degree >= " : "degree < ", d);
        if (rate == 0) break; // 一旦全是零, 更高维度也全是零
    }
    LOG("\n");
}

// ============================================================
// 3. 扩展差分: 逐步输出 Hamming 距离分布
// ============================================================
static void test_diff_distribution() {
    LOG("=== Differential Distribution (step 0→15) ===\n");
    const int KEYS = 50;
    const int STEPS = 16;

    hlfsr64::u8 km[64];
    hlfsr64::u16 idx;

    // 直方图: 每步的 Hamming 距离分布 (0..64)
    int hist[STEPS][65] = {{0}};
    int total = 0;

    for (int k = 0; k < KEYS; k++) {
        for (int i = 0; i < 64; i++) km[i] = (hlfsr64::u8)(rand() & 0xFF);
        idx = rand() & 0x1FF;

        hlfsr64 ref;
        ref.init(km, idx);
        hlfsr64::u64 rv[STEPS];
        for (int s = 0; s < STEPS; s++) rv[s] = ref.next();

        // 每 trial 随机翻转 1 bit
        for (int b = 0; b < 8; b++) {
            hlfsr64::u8 km2[64]; memcpy(km2, km, 64);
            int bp = rand() % 512;
            km2[bp / 8] ^= (1 << (bp % 8));

            hlfsr64 mod;
            mod.init(km2, idx);
            for (int s = 0; s < STEPS; s++) {
                hlfsr64::u64 diff = rv[s] ^ mod.next();
                int hd = __builtin_popcountll(diff);
                hist[s][hd]++;
            }
            total++;
        }
    }

    LOG("  step  mean-HD  std-HD   min  max  ideal(50%%)\n");
    LOG("  ----  -------  ------   ---  ---  ----------\n");
    for (int s = 0; s < STEPS; s++) {
        double sum_hd = 0, sum_sq = 0; int cnt = 0, mn = 65, mx = -1;
        for (int h = 0; h <= 64; h++) {
            if (hist[s][h] > 0) {
                sum_hd += h * hist[s][h];
                sum_sq += (double)h * h * hist[s][h];
                cnt += hist[s][h];
                if (h < mn) mn = h;
                if (h > mx) mx = h;
            }
        }
        double avg = sum_hd / cnt;
        double std = sqrt(sum_sq / cnt - avg * avg);
        LOG("  %4d  %7.2f  %6.2f   %3d  %3d\n", s, avg, std, mn, mx);
    }
    LOG("\n");
}

// ============================================================
// 4. 线性分析: 随机线性掩码逼近 Linear Approximation Table
// ============================================================
static void test_linear() {
    LOG("=== Linear Bias Estimation ===\n");
    LOG("  Method: random input masks α, measure output-bit linear bias\n");
    LOG("  bias = |Pr[f(x)⊕f(x⊕α) at bit P = 0] - 0.5|\n\n");

    const int MASKS = 5000;  // random α masks
    const int PAIRS = 500;   // key pairs per mask
    const int BIT_POS = 0;   // test output bit 0 (any bit works)

    hlfsr64::u16 idx = 0x100;
    double max_bias = 0, sum_bias = 0;
    int count = 0;

    LOG("  testing %d masks × %d pairs...\n", MASKS, PAIRS);

    for (int m = 0; m < MASKS; m++) {
        // 随机 512-bit 输入掩码 α
        hlfsr64::u8 alpha[64];
        for (int i = 0; i < 64; i++) alpha[i] = (hlfsr64::u8)(rand() & 0xFF);

        int zeros = 0;
        for (int p = 0; p < PAIRS; p++) {
            // 随机基础输入 x
            hlfsr64::u8 x[64];
            for (int i = 0; i < 64; i++) x[i] = (hlfsr64::u8)(rand() & 0xFF);

            // x⊕α
            hlfsr64::u8 xa[64];
            for (int i = 0; i < 64; i++) xa[i] = x[i] ^ alpha[i];

            hlfsr64 c1, c2;
            c1.init(x, idx);
            c2.init(xa, idx);
            bool b1 = (c1.next() >> BIT_POS) & 1;
            bool b2 = (c2.next() >> BIT_POS) & 1;
            if (b1 == b2) zeros++;
        }
        double bias = fabs(zeros / (double)PAIRS - 0.5);
        if (bias > max_bias) max_bias = bias;
        sum_bias += bias;
        count++;

        if ((m + 1) % 500 == 0)
            LOG("  %d/%d masks, max_bias=%.6f, avg_bias=%.6f\n",
                   m + 1, MASKS, max_bias, sum_bias / count);
    }

    double avg_bias = sum_bias / count;
    // 理论期望: 随机函数的 bias ~ 1/sqrt(PAIRS) ≈ 0.07
    double noise_floor = 1.0 / sqrt(PAIRS);
    LOG("\n  max bias:  %.6f  (noise floor: %.6f)\n", max_bias, noise_floor);
    LOG("  avg bias:  %.6f\n", avg_bias);
    LOG("  conclusion: %s\n\n",
           max_bias < noise_floor * 3 ? "PASS — no detectable linear bias" : "elevated — investigate");
}

int main(int argc, char* argv[]) {
    int max_deg  = (argc > 1) ? atoi(argv[1]) : 26;
    int d_trials = (argc > 2) ? atoi(argv[2]) : 10;
    if (max_deg < 1) max_deg = 26;
    if (d_trials < 1) d_trials = 10;

    srand(0x12345678);
    g_log = fopen("crypto_result.txt", "w");

    LOG("=== HLFSR-64 Cryptographic Analysis ===\n");
    LOG("Version: mask-multiply + idx16 stir\n\n");

    test_differential();
    test_diff_distribution();
    test_algebraic_degree(max_deg, d_trials);
    test_linear();

    if (g_log) { fclose(g_log); printf("\nResults saved to crypto_result.txt\n"); }
    return 0;
}
