// quicktest.cpp — HLFSR-64 快速正确性验证
#include "../hlfsr64.hpp"
#include <cstdio>

int main() {
    // 测试数据（任意值，仅验证状态机运行正确）
    hlfsr64::u8 bm[32] = {
        0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0,
        0x0F,0xED,0xCB,0xA9,0x87,0x65,0x43,0x21,
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,
    };
    hlfsr64::u8 seed[32] = {
        0xA0,0xB1,0xC2,0xD3,0xE4,0xF5,0x06,0x17,
        0x28,0x39,0x4A,0x5B,0x6C,0x7D,0x8E,0x9F,
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10,
    };
    hlfsr64::u8 idx_init = 0x42;

    hlfsr64 cipher;
    cipher.init(bm, seed, idx_init);

    // 验证 next() 不崩溃，产出非零输出
    printf("Testing next() for 1000 steps...\n");
    int zero_count = 0;
    hlfsr64::u64 prev = 0;
    int same_count = 0;

    for (int i = 0; i < 1000; i++) {
        hlfsr64::u64 out = cipher.next();
        if (out == 0) zero_count++;
        if (out == prev) same_count++;
        prev = out;
    }

    printf("  zero outputs: %d / 1000\n", zero_count);
    printf("  consecutive identical: %d / 999\n", same_count);

    // 验证批量 keystream
    printf("Testing keystream 1024 bytes...\n");
    hlfsr64::u8 buf[1024];
    cipher.keystream(buf, sizeof(buf));

    // 简单统计：0 和 1 的计数
    int ones = 0;
    for (int i = 0; i < 1024; i++) {
        for (int b = 0; b < 8; b++) {
            if (buf[i] & (1 << b)) ones++;
        }
    }
    printf("  bit ones: %d / 8192 (%.1f%%)\n", ones, 100.0 * ones / 8192.0);

    // re-init 验证确定性
    printf("Testing determinism...\n");
    hlfsr64 c2;
    c2.init(bm, seed, idx_init);
    for (int i = 0; i < 100; i++) c2.next();
    // 重置再跑
    c2.init(bm, seed, idx_init);
    hlfsr64::u64 first = c2.next();
    cipher.init(bm, seed, idx_init);
    hlfsr64::u64 first2 = cipher.next();
    printf("  re-init yields same first output: %s\n",
           first == first2 ? "YES" : "NO");

    printf("\nDone.\n");
    return 0;
}
