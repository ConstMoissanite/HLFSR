// bench_speed.cpp — HLFSR-64 多块大小吞吐 (对标 openssl speed)
#include "src/hlfsr64.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>

int main() {
    using u8  = hlfsr64::u8;
    using u16 = hlfsr64::u16;
    using u64 = hlfsr64::u64;
    int sizes[] = {16, 64, 256, 1024, 8192, 16384};
    const int ROUNDS = 10000;

    printf("HLFSR-64 throughput (GCC -O3)\n");
    printf("type             16 bytes     64 bytes    256 bytes   1024 bytes   8192 bytes  16384 bytes\n");
    printf("HLFSR-64        ");

    u8 km[64];
    for (int i = 0; i < 64; i++) km[i] = (u8)(i * 0x9D + 1);
    u16 idx_init = 0x100;

    for (int si = 0; si < 6; si++) {
        int sz = sizes[si];
        int rounds = ROUNDS;
        if (sz < 256) rounds *= 5;
        if (sz < 64) rounds *= 5;

        u8* buf = new u8[sz + 32];  // 安全余量, 避免最后一块溢出
        hlfsr64 h;
        h.init(km, idx_init);
        { u64 tmp[4]; h.next256(tmp); }

        auto t0 = std::chrono::high_resolution_clock::now();
        size_t blocks = (sz + 31) / 32;
        for (int r = 0; r < rounds; r++) {
            u64* p = (u64*)buf;
            for (size_t b = 0; b < blocks; b++, p += 4)
                h.next256(p);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double kbps = (sz / 1000.0) * rounds / sec;
        printf("%10.2fk  ", kbps);
        delete[] buf;
    }
    printf("\n");
    return 0;
}
