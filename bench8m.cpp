// bench8m.cpp — HLFSR-64 多块大小吞吐 (默认遍历 1--64 MiB)
#include "src/hlfsr64.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
int main(int argc, char* argv[]) {
    using u8  = hlfsr64::u8;
    using u64 = hlfsr64::u64;
    const size_t MiB = 1024ULL * 1024;
    int sizes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};
    int n_sizes = 8;
    u8 km[64] = {1};

    if (argc > 1) {
        sizes[0] = atoi(argv[1]);
        n_sizes  = 1;
    }
    int runs = (argc > 2) ? atoi(argv[2]) : 100;

    printf("HLFSR-64 TV throughput (GCC -O3)\n");
    if (argc > 1) printf("  %d KB: ", sizes[0]);
    printf("  Block   GB/s\n");
    for (int si = 0; si < n_sizes; si++) {
        size_t kb    = sizes[si];
        size_t chunk = kb * 1024;
        u8* buf = new u8[chunk + 32];
        hlfsr64 h; h.init(km, 0);
        { u64 tmp[4]; h.next256(tmp); }

        auto t0 = std::chrono::high_resolution_clock::now();
        size_t steps = chunk / 32;
        // ~256 MB total data
        size_t total = (size_t)(256ULL * 1024 / kb);
        if (total < 2) total = 2;
        for (size_t ri = 0; ri < total; ri++) {
            u64* p = (u64*)buf;
            for (size_t s = 0; s < steps; s++, p += 4)
                h.next256(p);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double GB  = (double)(chunk * total) / (1024.0*1024.0*1024.0);
        printf(" %5d KB  %6.2f\n", (int)kb, GB/sec);
        delete[] buf;
    }
}
