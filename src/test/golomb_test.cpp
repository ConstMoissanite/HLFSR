// golomb_test.cpp — HLFSR-64 Golomb 随机性测验
// 基于 Golomb 三公设检验密钥流输出的统计随机性
// 使用 OpenSSL RAND_bytes 生成随机密钥材料
#include "../hlfsr64.hpp"
#include <openssl/rand.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <ctime>

using clock_ns = std::chrono::high_resolution_clock;

#define LOG(f, ...) do { \
    std::fprintf(f, __VA_ARGS__); std::fflush(f); \
    std::fprintf(stdout, __VA_ARGS__); \
} while(0)

// ============================================================
// 编译器检测
// ============================================================
#if defined(__clang__)
    #define CMP_NAME "Clang"
    #define CMP_VER  __clang_major__, __clang_minor__, __clang_patchlevel__
#elif defined(_MSC_VER)
    #define CMP_NAME "MSVC"
    #define CMP_VER  _MSC_VER / 100, (_MSC_VER / 10) % 10, _MSC_VER % 10
#elif defined(__GNUC__)
    #define CMP_NAME "GCC"
    #define CMP_VER  __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__
#else
    #define CMP_NAME "Unknown"
    #define CMP_VER  0, 0, 0
#endif

// ============================================================
// Golomb 三公设检验
// ============================================================

// G1: 频率检验 — 0/1 计数是否均衡
static void test_balance(std::FILE* f, const hlfsr64::u8* data, std::size_t bytes) {
    std::size_t ones = 0, total = bytes * 8;
    for (std::size_t i = 0; i < bytes; i++) {
        hlfsr64::u8 b = data[i];
        for (int j = 0; j < 8; j++)
            if (b & (1 << j)) ones++;
    }
    std::size_t zeros = total - ones;
    double ratio = (double)ones / (double)total;

    LOG(f, "G1 频率检验 (Monobit)\n");
    LOG(f, "  样本: %llu 位\n", (unsigned long long)total);
    LOG(f, "  1: %llu  (%.4f%%)\n", (unsigned long long)ones,   100.0 * ratio);
    LOG(f, "  0: %llu  (%.4f%%)\n", (unsigned long long)zeros,  100.0 * (1.0 - ratio));
    LOG(f, "  偏差: %.4f%%  %s\n", 100.0 * (ratio - 0.5),
        std::abs(ratio - 0.5) < 0.005 ? "PASS" : "FAIL");
    LOG(f, "\n");
}

// G2: 游程检验 — 游程长度分布是否符合几何分布 P(L=k)=2^{-k}
static void test_runs(std::FILE* f, const hlfsr64::u8* data, std::size_t bytes) {
    constexpr int MAX_RUN = 16;  // 统计 1..MAX_RUN 长度，更长游程合并
    std::size_t runs0[MAX_RUN + 1] = {0};  // runs0[k] = 0-游程长度 k 的计数, runs0[MAX_RUN] = 长度 ≥ MAX_RUN
    std::size_t runs1[MAX_RUN + 1] = {0};

    int prev_bit = -1;
    int run_len  = 0;
    std::size_t total_runs0 = 0, total_runs1 = 0;

    for (std::size_t i = 0; i < bytes; i++) {
        hlfsr64::u8 b = data[i];
        for (int j = 0; j < 8; j++) {
            int bit = (b >> j) & 1;
            if (bit == prev_bit) {
                run_len++;
            } else {
                if (prev_bit >= 0) {
                    int idx = (run_len < MAX_RUN) ? (run_len - 1) : (MAX_RUN);
                    if (prev_bit == 0) { runs0[idx]++; total_runs0++; }
                    else               { runs1[idx]++; total_runs1++; }
                }
                prev_bit = bit;
                run_len  = 1;
            }
        }
    }
    // 最后一个游程
    if (prev_bit >= 0) {
        int idx = (run_len < MAX_RUN) ? (run_len - 1) : (MAX_RUN);
        if (prev_bit == 0) { runs0[idx]++; total_runs0++; }
        else               { runs1[idx]++; total_runs1++; }
    }

    std::size_t total_runs = total_runs0 + total_runs1;

    LOG(f, "G2 游程检验 (Runs)\n");
    LOG(f, "  总游程数: %llu  (0游程: %llu, 1游程: %llu)\n",
        (unsigned long long)total_runs,
        (unsigned long long)total_runs0,
        (unsigned long long)total_runs1);

    LOG(f, "  %-8s  %8s  %8s  %8s  %8s\n",
        "长度", "0-计数", "0-期望", "1-计数", "1-期望");

    double chi2_0 = 0, chi2_1 = 0;
    int    chi2_n  = 0;

    for (int k = 1; k <= MAX_RUN; k++) {
        int idx = k - 1;
        double expected0 = (double)total_runs0 / (1ULL << k);
        double expected1 = (double)total_runs1 / (1ULL << k);

        if (k == MAX_RUN) {
            expected0 = (double)total_runs0 / (1ULL << (MAX_RUN - 1));
            expected1 = (double)total_runs1 / (1ULL << (MAX_RUN - 1));
        }

        double d0 = (double)runs0[idx] - expected0;
        double d1 = (double)runs1[idx] - expected1;
        if (expected0 > 0) { chi2_0 += d0 * d0 / expected0; chi2_n++; }
        if (expected1 > 0) { chi2_1 += d1 * d1 / expected1; chi2_n++; }

        if (k < MAX_RUN || runs0[idx] > 0 || runs1[idx] > 0) {
            LOG(f, "  %-8d  %8llu  %8.1f  %8llu  %8.1f\n",
                k,
                (unsigned long long)runs0[idx], expected0,
                (unsigned long long)runs1[idx], expected1);
        }
    }

    double run_ratio = (double)total_runs0 / (double)std::max(total_runs1, (std::size_t)1);
    LOG(f, "  0/1游程比: %.4f  %s\n", run_ratio,
        std::abs(run_ratio - 1.0) < 0.02 ? "PASS" : "WARN");

    // χ² 检验（自由度 ≈ 2 × MAX_RUN）
    int    dof = chi2_n - 1;
    double chi2_crit = (double)dof + 3.0 * std::sqrt(2.0 * (double)dof);  // 3σ 上界
    LOG(f, "  χ² = %.1f  (自由度≈%d, 临界≈%.1f)  %s\n\n",
        chi2_0 + chi2_1, dof, chi2_crit,
        (chi2_0 + chi2_1) < chi2_crit ? "PASS" : "FAIL");
}

// G3: 自相关检验 — 移位自相关应接近 0（两值自相关）
static void test_autocorr(std::FILE* f, const hlfsr64::u8* data, std::size_t bytes) {
    constexpr int MAX_SHIFT = 32;
    constexpr std::size_t TEST_BITS = 1024ULL * 1024; // 1 MiB bits

    std::size_t total = (bytes * 8 < TEST_BITS) ? bytes * 8 : TEST_BITS;
    if (total < MAX_SHIFT * 64) total = MAX_SHIFT * 64;

    LOG(f, "G3 自相关检验 (Autocorrelation)\n");
    LOG(f, "  测试位: %llu\n", (unsigned long long)total);
    LOG(f, "  %-6s  %12s  %10s\n", "移位", "A(d)", "判定");

    // 转为位数组（仅取前 TEST_BITS 位）
    std::size_t n_words = (total + 63) / 64;
    hlfsr64::u64* bits = new hlfsr64::u64[n_words];
    std::memcpy(bits, data, (n_words * 8 < bytes) ? n_words * 8 : bytes);

    int fail_count = 0;
    for (int d = 1; d <= MAX_SHIFT; d++) {
        std::size_t agree = 0;
        std::size_t n = total - d;

        for (std::size_t i = 0; i < n; i++) {
            std::size_t i0 = i, i1 = i + d;
            bool b0 = (bits[i0 / 64] >> (i0 % 64)) & 1;
            bool b1 = (bits[i1 / 64] >> (i1 % 64)) & 1;
            if (b0 == b1) agree++;
        }

        double a_d = 2.0 * (double)agree / (double)n - 1.0;
        double sigma = 1.0 / std::sqrt((double)n);
        double z = std::abs(a_d) / sigma;

        bool ok = z < 3.3;  // 99.9% 置信 (3.3σ)
        if (!ok) fail_count++;

        LOG(f, "  %-6d  %+12.6f  (%.1fσ)  %s\n", d, a_d, z,
            ok ? "PASS" : "WARN");
    }

    LOG(f, "  警告: %d / %d  (期望 ≤1)\n", fail_count, MAX_SHIFT);
    LOG(f, "  %s\n\n", (fail_count <= 2) ? "PASS" : "FAIL");
    delete[] bits;
}

// ============================================================
// 主流程
// ============================================================
int main(int argc, char* argv[]) {
    // 样本量：默认 16 MiB，可通过命令行参数调整
    std::size_t sample_mib = (argc > 1) ? (std::size_t)std::atol(argv[1]) : 16;
    if (sample_mib < 1) sample_mib = 16;
    std::size_t sample_bytes = sample_mib * 1024ULL * 1024ULL;

    // 时间戳输出文件
    auto now = std::time(nullptr);
    auto tm  = *std::localtime(&now);
    char path[256];
    std::snprintf(path, sizeof(path),
                  "benchmarks/golomb_%04d_%02d_%02d_%02d_%02d_%02d.txt",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);

    std::FILE* f = std::fopen(path, "w");
    if (!f) { std::fprintf(stderr, "无法创建 %s\n", path); return 1; }

    LOG(f, "HLFSR-64 Golomb 随机性测验\n");
    LOG(f, "==========================\n");
    LOG(f, "时间: %s", std::ctime(&now));
    LOG(f, "编译器: " CMP_NAME " %d.%d.%d\n", CMP_VER);
    LOG(f, "样本量: %llu MiB\n\n", (unsigned long long)sample_mib);

    // 随机密钥材料
    hlfsr64::u8 bm[64], seed[32]; hlfsr64::u16 idx;
    if (RAND_bytes(bm, 64) != 1 ||
        RAND_bytes(seed, 32) != 1 ||
        RAND_bytes((unsigned char*)&idx, 2) != 1) {
        LOG(f, "RAND_bytes 失败\n");
        std::fclose(f); return 1;
    }

    hlfsr64 cipher;
    cipher.init(bm, seed, idx);

    // 分配缓冲并生成密钥流
    hlfsr64::u8* buf = new hlfsr64::u8[sample_bytes];
    if (!buf) { LOG(f, "内存分配失败\n"); std::fclose(f); return 1; }

    LOG(f, "生成 %llu MiB 密钥流...\n", (unsigned long long)sample_mib);
    auto t0 = clock_ns::now();
    cipher.keystream(buf, sample_bytes);
    auto t1 = clock_ns::now();
    double dt = std::chrono::duration<double>(t1 - t0).count();
    LOG(f, "生成耗时: %.3f s (%.1f MiB/s)\n\n", dt,
        (double)sample_bytes / dt / (1024.0 * 1024.0));

    // 执行三项测验
    test_balance(f, buf, sample_bytes);
    test_runs(f, buf, sample_bytes);
    test_autocorr(f, buf, sample_bytes);

    LOG(f, "结果已写入: %s\n", path);
    delete[] buf;
    std::fclose(f);
    return 0;
}
