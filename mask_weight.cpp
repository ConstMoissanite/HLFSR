#include <cstdio>
#include <cstdlib>
#include <cstdint>

int main() {
    srand(0xDEADBEEF);
    // 跳过前 4112 个掩码 (每个 64 bytes = 64 次 rand() & 0xFF)
    for (int m = 0; m < 4112; m++)
        for (int i = 0; i < 64; i++) (void)(rand() & 0xFF);
    // 第 4113 个 (index 4112)
    uint8_t mask[64];
    int weight = 0;
    printf("mask #4112:\n  ");
    for (int i = 0; i < 64; i++) {
        mask[i] = (uint8_t)(rand() & 0xFF);
        weight += __builtin_popcount(mask[i]);
        printf("%02x ", mask[i]);
        if ((i + 1) % 16 == 0) printf("\n  ");
    }
    printf("\n  weight = %d / 512 (%.1f%%)\n", weight, weight * 100.0 / 512);
    return 0;
}
