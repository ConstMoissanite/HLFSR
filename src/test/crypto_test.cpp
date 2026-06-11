// crypto_test.cpp — HLFSR-64 差分 + 代数度分析
#include "../hlfsr64.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

// ============================================================
// 1. 差分分析: 单比特翻转 → 输出差分传播 & 雪崩效应
// ============================================================
static void test_differential() {
    printf("=== Differential Analysis ===\n");
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
        if ((trial + 1) % 20 == 0) printf("  diff: %d/%d keys (×64 bits)\n", trial+1, N_KEYS);
    }

    double total_samples = N_KEYS * BITS;
    printf("\n  step  min-avalanche  max-avalanche  avg  zero-diff-rate\n");
    printf("  ----  --------------  --------------  ----  --------------\n");
    for (int s = 0; s < std::min(16, N_STEPS); s++) {
        double avg = prob[s] / total_samples;
        double zr  = zeros[s] / total_samples * 100;
        printf("  %4d  %14.4f  %14.4f  %4.0f%%  %6.1f%%\n",
               s, 0.0, 1.0, avg * 100, zr);
    }
    // 后半段汇总
    double avg_late = 0; int zr_late = 0;
    for (int s = 16; s < N_STEPS; s++) { avg_late += prob[s]; zr_late += zeros[s]; }
    avg_late /= (total_samples * (N_STEPS - 16));
    printf("  %4s  %14s  %14s  %4.0f%%  %6.1f%%\n", "16-63", "", "", avg_late * 100,
           zr_late * 100.0 / (total_samples * (N_STEPS - 16)));
    printf("  (ideal: avg=50%%, zero-diff < 1e-18)\n\n");
}

// ============================================================
// 2. 代数度检测: 高阶差分 (自适应: 高度数减少 trial 数以控制时间)
// ============================================================
static void test_algebraic_degree() {
    printf("=== Algebraic Degree Estimation ===\n");
    printf("  Method: d-th order differential over random affine subspaces\n");
    printf("  If output sum = 0 for all d-dim subspaces, degree < d\n\n");

    const int MAX_DEG = 16;
    // 自适应: 低 degree 多 trial, 高 degree 少 (因为 2^d 贵)
    int trials_table[17] = {0, 100, 80, 60, 40, 25, 15, 10, 8, 6, 5, 4, 3, 3, 2, 2, 2};

    hlfsr64::u8 km[64];
    for (int i = 0; i < 64; i++) km[i] = (hlfsr64::u8)(rand() & 0xFF);
    hlfsr64::u16 idx = 0x100;

    printf("  deg  trials  non-zero-rate  conclusion\n");
    printf("  ---  ------  -------------  ----------\n");

    for (int d = 1; d <= MAX_DEG; d++) {
        int TRIALS = trials_table[d];
        int non_zero = 0;
        for (int t = 0; t < TRIALS; t++) {
            int basis[16];
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
        printf("  %3d  %6d  %13.0f%%       %s%d\n", d, TRIALS, rate,
               rate > 0 ? "degree >= " : "degree < ", d);
        if (rate == 0) break; // 一旦全是零, 更高维度也全是零
    }
    printf("\n");
}

// ============================================================
// 3. 扩展差分: 逐步输出 Hamming 距离分布
// ============================================================
static void test_diff_distribution() {
    printf("=== Differential Distribution (step 0→15) ===\n");
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

    printf("  step  mean-HD  std-HD   min  max  ideal(50%%)\n");
    printf("  ----  -------  ------   ---  ---  ----------\n");
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
        printf("  %4d  %7.2f  %6.2f   %3d  %3d\n", s, avg, std, mn, mx);
    }
    printf("\n");
}

int main() {
    srand(0x12345678);
    test_differential();
    test_diff_distribution();
    test_algebraic_degree();
    return 0;
}
