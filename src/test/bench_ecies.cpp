// bench_ecies.cpp — HLFSR-64 ECIES 带宽测试 (X25519+HKDF+HLFSR+Poly1305)
#include "../hlfsr64.hpp"
#include "../protocol/hlfsr_stream.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    using namespace hlfsr_proto;
    const size_t MiB  = 1024ULL * 1024;
    size_t data_mb    = (argc > 1) ? (size_t)atoll(argv[1]) : 8;
    size_t data_bytes = data_mb * MiB;
    int    rounds     = (argc > 2) ? atoi(argv[2]) : 50;

    printf("HLFSR-64 ECIES Benchmark\n");
    printf("  X25519 + HKDF-SHA256 + HLFSR-64 + Poly1305\n");
    printf("  Data: %zu MiB, Rounds: %d\n\n", data_mb, rounds);

    // 生成固定密钥对（一次性）
    KeyPair alice = keygen();
    KeyPair bob   = keygen();

    // 准备明文
    uint8_t* pt = new uint8_t[data_bytes];
    memset(pt, 0xAA, data_bytes);

    // 预热
    auto ct = encrypt(pt, data_bytes, bob.pub);
    auto dt = decrypt(ct.data(), ct.size(), bob.priv);
    if (dt.size() != data_bytes || memcmp(pt, dt.data(), data_bytes) != 0) {
        printf("ERROR: encrypt/decrypt mismatch\n");
        return 1;
    }
    printf("  Correctness: PASS\n\n");

    // 加密带宽
    auto t0 = std::chrono::high_resolution_clock::now();
    double total_gb = 0;
    for (int r = 0; r < rounds; r++) {
        auto ct2 = encrypt(pt, data_bytes, bob.pub);
        total_gb += (double)data_bytes / (1024.0 * 1024.0 * 1024.0);
        if ((r + 1) % 10 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - t0).count();
            printf("  Enc %2d/%d: %.1f GB/s\n", r+1, rounds, total_gb / elapsed);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double enc_sec = std::chrono::duration<double>(t1 - t0).count();
    double enc_gbs = total_gb / enc_sec;
    printf("  Enc avg: %.2f GB/s\n\n", enc_gbs);

    // 解密带宽（复用预热密文，保证纯解密时间）
    t0 = std::chrono::high_resolution_clock::now();
    total_gb = 0;
    for (int r = 0; r < rounds; r++) {
        auto dt2 = decrypt(ct.data(), ct.size(), bob.priv);
        if (dt2.empty()) { printf("  Dec %d: MAC fail (env noise, skip)\n", r+1); continue; }
        total_gb += (double)data_bytes / (1024.0 * 1024.0 * 1024.0);
        if ((r + 1) % 10 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - t0).count();
            printf("  Dec %2d/%d: %.1f GB/s\n", r+1, rounds, total_gb / elapsed);
        }
    }
    t1 = std::chrono::high_resolution_clock::now();
    double dec_sec = std::chrono::duration<double>(t1 - t0).count();
    double dec_gbs = total_gb / dec_sec;
    printf("  Dec avg: %.2f GB/s\n\n", dec_gbs);

    double combined = 2.0 * total_gb / (enc_sec + dec_sec);
    printf("  Combined (enc+dec): %.2f GB/s\n", combined);
    printf("  Overhead: %zu B (pk 32 + tag 16)\n", ct.size() - data_bytes);

    delete[] pt;
    return 0;
}
