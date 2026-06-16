// bench_symmetric.cpp — HLFSR-64 + Poly1305 对称加密带宽
#include "../hlfsr64.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    const size_t MiB  = 1024ULL * 1024;
    size_t data_mb    = (argc > 1) ? (size_t)atoll(argv[1]) : 8;
    size_t data_bytes = data_mb * MiB;
    int    rounds     = (argc > 2) ? atoi(argv[2]) : 50;

    printf("HLFSR-64 + Poly1305 Benchmark\n");
    printf("  Data: %zu MiB, Rounds: %d\n\n", data_mb, rounds);

    // 固定密钥材料
    uint8_t km[64], poly_key[32];
    RAND_bytes(km, 64);
    RAND_bytes(poly_key, 32);
    uint16_t idx_init = 0x100;

    uint8_t* pt = new uint8_t[data_bytes];
    uint8_t* ct = new uint8_t[data_bytes];
    uint8_t* tag = new uint8_t[16];
    memset(pt, 0xAA, data_bytes);

    // 预热 + 正确性
    {
        hlfsr64 h;
        h.init(km, idx_init);
        h.keystream(ct, data_bytes);
        for (size_t i = 0; i < data_bytes; i++) ct[i] ^= pt[i];

        EVP_MAC* mac = EVP_MAC_fetch(nullptr, "POLY1305", nullptr);
        EVP_MAC_CTX* mctx = EVP_MAC_CTX_new(mac);
        EVP_MAC_init(mctx, poly_key, 32, nullptr);
        EVP_MAC_update(mctx, ct, data_bytes);
        size_t tlen = 16;
        uint8_t tag2[16];
        EVP_MAC_final(mctx, tag2, &tlen, 16);

        // decrypt: ct ^ keystream → pt
        uint8_t* saved_ct = new uint8_t[data_bytes];
        memcpy(saved_ct, ct, data_bytes);
        hlfsr64 h2;
        h2.init(km, idx_init);
        h2.keystream(ct, data_bytes);
        for (size_t i = 0; i < data_bytes; i++) ct[i] ^= saved_ct[i];
        if (memcmp(ct, pt, data_bytes) != 0) { printf("ERROR: decrypt mismatch\n"); return 1; }
        delete[] saved_ct;

        EVP_MAC_CTX_free(mctx);
        EVP_MAC_free(mac);
    }
    printf("  Correctness: PASS\n\n");

    // 加密 + 认证
    auto t0 = std::chrono::high_resolution_clock::now();
    double total_gb = 0;
    for (int r = 0; r < rounds; r++) {
        hlfsr64 h;
        h.init(km, (uint16_t)(idx_init + r));
        h.keystream(ct, data_bytes);
        for (size_t i = 0; i < data_bytes; i++) ct[i] ^= pt[i];

        EVP_MAC* mac = EVP_MAC_fetch(nullptr, "POLY1305", nullptr);
        EVP_MAC_CTX* mctx = EVP_MAC_CTX_new(mac);
        EVP_MAC_init(mctx, poly_key, 32, nullptr);
        EVP_MAC_update(mctx, ct, data_bytes);
        size_t tlen = 16;
        EVP_MAC_final(mctx, tag, &tlen, 16);
        EVP_MAC_CTX_free(mctx);
        EVP_MAC_free(mac);

        total_gb += (double)data_bytes / (1024.0 * 1024.0 * 1024.0);
        if ((r + 1) % 10 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - t0).count();
            printf("  Enc+MAC %2d/%d: %.1f GB/s\n", r+1, rounds, total_gb / elapsed);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    printf("  Enc+MAC avg: %.2f GB/s\n\n", total_gb / sec);

    delete[] pt; delete[] ct; delete[] tag;
    return 0;
}
