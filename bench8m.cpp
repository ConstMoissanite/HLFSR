#include "src/hlfsr64.hpp"
#include <chrono>
#include <cstdio>
int main() {
    using u64 = hlfsr64::u64;
    constexpr size_t MiB = 1024ULL * 1024;
    constexpr size_t chunk = 8 * MiB;
    uint8_t km[64] = {1};
    uint8_t* buf = new uint8_t[chunk];
    hlfsr64 h; h.init(km, 0);
    // warmup
    { u64 out[4]; h.next256(out); }
    auto t0 = std::chrono::high_resolution_clock::now();
    const size_t blocks = chunk / 32;  // 32 bytes = 256 bits per call
    const int runs = 100;
    for (int r = 0; r < runs; r++) {
        h.init(km, (uint16_t)r);
        for (size_t b = 0; b < blocks; b++) {
            u64 out[4];
            h.next256(out);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    double GB = (double)(chunk * runs) / (1024.0*1024.0*1024.0);
    printf("8 MiB x %d: %.2f GB/s\n", runs, GB/sec);
    delete[] buf;
    return 0;
}
