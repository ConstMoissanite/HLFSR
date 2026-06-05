// bench_keystream.cpp — HLFSR-64 密钥流加密带宽测试
// 使用 OpenSSL RAND_bytes 生成随机密钥材料
// 结果写入 benchmarks/YYYY_MM_DD_HH_MM_SS.txt
#include "../hlfsr64.hpp"
#include <openssl/rand.h>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <ctime>

using clock_ns = std::chrono::high_resolution_clock;

// 编译器检测
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

// 优化级别检测
#if defined(__OPTIMIZE__)
    #if defined(__clang__)
        #define OPT_STR "-O2"
    #elif defined(_MSC_VER)
        #define OPT_STR "/O2"
    #else
        #define OPT_STR "-O2"
    #endif
#else
    #define OPT_STR "-O0"
#endif

// 双写：同时输出到文件和控制台
#define LOG(f, ...) do { \
    std::fprintf(f, __VA_ARGS__); std::fflush(f); \
    std::fprintf(stdout, __VA_ARGS__); \
} while(0)

int main() {
    constexpr std::size_t MiB   = 1024ULL * 1024;
    constexpr std::size_t chunk = 64 * MiB;
    constexpr int          warm = 2;
    constexpr int          runs = 8;

    // 时间戳文件名
    auto now = std::time(nullptr);
    auto tm  = *std::localtime(&now);
    char path[256];
    std::snprintf(path, sizeof(path),
                  "benchmarks/%04d_%02d_%02d_%02d_%02d_%02d.txt",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);

    std::FILE* f = std::fopen(path, "w");
    if (!f) { std::fprintf(stderr, "无法创建 %s\n", path); return 1; }

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

    hlfsr64::u8* buf = new hlfsr64::u8[chunk];
    if (!buf) { LOG(f, "内存分配失败\n"); std::fclose(f); return 1; }

    LOG(f, "HLFSR-64 密钥流带宽测试\n");
    LOG(f, "========================\n");
    LOG(f, "时间: %s", std::ctime(&now));
    LOG(f, "编译器: " CMP_NAME " %d.%d.%d  " OPT_STR "\n", CMP_VER);
    LOG(f, "数据量: %llu MiB × %d 轮\n\n",
        (unsigned long long)(chunk / MiB), runs);

    // 预热
    LOG(f, "预热 %d 轮...\n", warm);
    for (int i = 0; i < warm; i++) cipher.keystream(buf, chunk);

    // 正式测试
    LOG(f, "测试 %d 轮:\n", runs);
    LOG(f, "  %-4s  %10s  %12s\n", "轮", "耗时(s)", "MiB/s");

    double total_s = 0, min_s = 1e99, max_s = 0;
    for (int i = 0; i < runs; i++) {
        auto t0 = clock_ns::now();
        cipher.keystream(buf, chunk);
        auto t1 = clock_ns::now();

        double dt = std::chrono::duration<double>(t1 - t0).count();
        double bw = (double)chunk / dt;
        total_s += dt;
        if (dt < min_s) min_s = dt;
        if (dt > max_s) max_s = dt;

        LOG(f, "  %-4d  %10.3f  %12.1f\n", i + 1, dt, bw / (double)MiB);
    }

    double avg_bw = (double)chunk / (total_s / runs);
    double min_bw = (double)chunk / max_s;
    double max_bw = (double)chunk / min_s;

    LOG(f, "\n");
    LOG(f, "  平均带宽: %8.1f MiB/s  (%8.1f MB/s)\n",
        avg_bw / (double)MiB, avg_bw / 1e6);
    LOG(f, "  峰值带宽: %8.1f MiB/s\n", max_bw / (double)MiB);
    LOG(f, "  谷值带宽: %8.1f MiB/s\n", min_bw / (double)MiB);
    LOG(f, "  周期/字节: %5.1f  (假设 4 GHz)\n",
        total_s * 4e9 / double(runs) / double(chunk));

    delete[] buf;
    std::fclose(f);
    return 0;
}
