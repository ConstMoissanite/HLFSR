// quicktest.cpp — HLFSR-64 V8 快速验证
#include "../hlfsr64.hpp"
#include <cstdio>
#include <cstring>

int main() {
    hlfsr64::u8  m[64];  std::memset(m, 0xA5, 64);
    hlfsr64::u8  seed[32]; std::memset(seed, 0x3C, 32);
    hlfsr64::u16 idx = 0x142;

    hlfsr64 cipher;
    cipher.init(m, seed, idx);

    printf("Testing next() for 1000 steps...\n");
    int zeros = 0, same = 0;
    hlfsr64::u64 prev = 0;
    for (int i = 0; i < 1000; i++) {
        hlfsr64::u64 out = cipher.next();
        if (out == 0) zeros++;
        if (out == prev) same++;
        prev = out;
    }
    printf("  zero outputs: %d/1000\n  consecutive same: %d/999\n", zeros, same);

    hlfsr64::u8 buf[1024];
    cipher.keystream(buf, sizeof(buf));
    int ones = 0;
    for (int i = 0; i < 1024; i++)
        for (int b = 0; b < 8; b++) if (buf[i] & (1 << b)) ones++;
    printf("  bit ones: %d/8192 (%.1f%%)\n", ones, 100.0 * ones / 8192.0);

    hlfsr64 c2; c2.init(m, seed, idx);
    hlfsr64::u64 a = c2.next();
    cipher.init(m, seed, idx);
    printf("  determinism: %s\n", a == cipher.next() ? "YES" : "NO");
    return 0;
}
