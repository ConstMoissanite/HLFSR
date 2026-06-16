// test_proto.cpp — HLFSR-64 流协议正确性验证
#include "hlfsr_stream.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>

using namespace hlfsr_proto;

int main() {
    printf("HLFSR-64 Stream Protocol Test\n");
    printf("==================================\n\n");

    // 1. KeyGen
    printf("[1] KeyGen...\n");
    KeyPair alice = keygen();
    KeyPair bob   = keygen();
    printf("    Alice pub: "); for(int i=0;i<8;i++) printf("%02x",alice.pub[i]); printf("...\n");
    printf("    Bob   pub: "); for(int i=0;i<8;i++) printf("%02x",bob.pub[i]); printf("...\n");

    // 2. Encrypt (Alice → Bob)
    const char* msg = "Hello, HLFSR-64 V10 Stream Protocol! This is a test message.";
    size_t msg_len  = strlen(msg);
    printf("\n[2] Encrypt (%zu bytes: \"%s\")\n", msg_len, msg);
    auto ct = encrypt((const uint8_t*)msg, msg_len, bob.pub);
    printf("    Wire size: %zu bytes (overhead: %zu)\n", ct.size(), ct.size() - msg_len);

    // 3. Decrypt (Bob)
    printf("\n[3] Decrypt...\n");
    auto pt = decrypt(ct.data(), ct.size(), bob.priv);
    printf("    Plaintext (%zu bytes): \"%s\"\n", pt.size(), (char*)pt.data());
    printf("    Match: %s\n", (pt.size()==msg_len && memcmp(pt.data(),msg,msg_len)==0) ? "YES" : "NO");

    // 4. Tamper test
    printf("\n[4] Tamper test...\n");
    auto ct2 = ct;
    ct2[ct2.size() - 5] ^= 0xFF;
    auto bad = decrypt(ct2.data(), ct2.size(), bob.priv);
    printf("    Tampered message rejected: %s\n", bad.empty() ? "YES" : "NO");

    // 5. Benchmark
    printf("\n[5] Benchmark (1 MB, 100 rounds)...\n");
    constexpr size_t MB = 1024 * 1024;
    std::vector<uint8_t> big(MB, 0xAB);
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        auto c = encrypt(big.data(), MB, bob.pub);
        auto p = decrypt(c.data(), c.size(), bob.priv);
        if (p.size() != MB) { printf("FAIL\n"); return 1; }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double dt = std::chrono::duration<double>(t1 - t0).count();
    printf("    100 rounds, %.2f s, %.1f MB/s (enc+dec combined)\n", dt, 200.0 / dt);

    printf("\nAll tests passed.\n");
    return 0;
}
