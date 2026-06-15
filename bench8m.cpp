#include "src/hlfsr64.hpp"
#include <chrono>
#include <cstdio>
int main() {
    constexpr size_t MiB = 1024ULL * 1024;
    constexpr size_t chunk = 8 * MiB;
    uint8_t km[64] = {1};
    uint8_t* buf = new uint8_t[chunk];
    hlfsr64 h; h.init(km, 0);
    h.keystream(buf, chunk);
    auto t0 = std::chrono::high_resolution_clock::now();
    const int runs = 100;
    for (int i = 0; i < runs; i++) {
        h.init(km, (uint16_t)i);
        h.keystream(buf, chunk);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    double GB = (double)(chunk * runs) / (1024.0*1024.0*1024.0);
    printf("8 MiB x %d: %.2f GB/s\n", runs, GB/sec);
    delete[] buf;
    return 0;
}
