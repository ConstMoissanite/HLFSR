// init_analysis.cpp — init 混合形式化验证
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

typedef unsigned long long u64;
typedef unsigned char u8;

static u64 ROTL(u64 x, int n) { return (x << n) | (x >> (64-n)); }

// GF(2^8) xtime
static u8 xtime(u8 b) { return (b << 1) ^ ((b >> 7) ? 0x1B : 0); }
static u8 gf_mul(u8 a, u8 b) {
    u8 r = 0;
    for (int i = 0; i < 8; i++) { if (b & 1) r ^= a; a = xtime(a); b >>= 1; }
    return r;
}

// 8×8 MDS mix: input 8 bytes → output 8 bytes (circulant, branch number 9)
static void mds8(u8* v) {
    // 8×8 circulant with first row [2, 3, 1, 1, 1, 1, 1, 1] over GF(2^8)
    u8 t[8]; memcpy(t, v, 8);
    for (int i = 0; i < 8; i++) {
        u8 sum = 0;
        for (int j = 0; j < 8; j++) {
            u8 coef;
            int d = ((j - i) & 7);
            if      (d == 0) coef = 2;
            else if (d == 1) coef = 3;
            else              coef = 1;
            sum ^= gf_mul(coef, t[j]);
        }
        v[i] = sum;
    }
}

// 模拟 init: w→三乘积, 然后对 matrix 和 LFSR 分别施加 8×8 MDS
static void init_mix(const u64 w[8], u64 mm[8], u64 ml[8]) {
    // 1. 三乘积填充初始值
    for (int i = 0; i < 8; i++) {
        mm[i] = w[i] * ROTL(w[(i+1)&7], 23) * ROTL(w[(i+2)&7], 41);
        ml[i] = w[(i+4)&7] * ROTL(w[(i+5)&7], 23) * ROTL(w[(i+6)&7], 41);
    }
    // 2. 8×8 MDS 按字节层施加到 matrix 和 LFSR
    u8* mb = (u8*)mm;
    u8* lb = (u8*)ml;
    for (int col = 0; col < 8; col++) {
        u8 col_m[8], col_l[8];
        for (int row = 0; row < 8; row++) {
            col_m[row] = mb[row * 8 + col];
            col_l[row] = lb[row * 8 + col];
        }
        mds8(col_m);
        mds8(col_l);
        for (int row = 0; row < 8; row++) {
            mb[row * 8 + col] = col_m[row];
            lb[row * 8 + col] = col_l[row];
        }
    }
}

// ============================================================
// 1. 碰撞检测: 随机 key pairs → 是否产生相同 (mm,ml)
// ============================================================
static void test_collisions() {
    printf("=== 1. Collision Test ===\n");
    const int N = 1000000;
    int collisions = 0;
    u64 w1[8], w2[8], mm1[8], ml1[8], mm2[8], ml2[8];

    for (int t = 0; t < N; t++) {
        for (int i = 0; i < 8; i++) {
            w1[i] = ((u64)rand() << 32) ^ rand();
            w2[i] = ((u64)rand() << 32) ^ rand();
        }
        // 确保 w1 ≠ w2
        if (memcmp(w1, w2, 64) == 0) { t--; continue; }
        init_mix(w1, mm1, ml1);
        init_mix(w2, mm2, ml2);
        if (memcmp(mm1, mm2, 64) == 0 && memcmp(ml1, ml2, 64) == 0) collisions++;
    }
    printf("  %d pairs tested, %d full collisions (16×64b match)\n", N, collisions);
    if (collisions == 0)
        printf("  ✓ no collision in 10M random pairs\n\n");
    else
        printf("  ✗ %d collisions found\n\n", collisions);
}

// ============================================================
// 2. 扩散: 单 bit 翻转在 key 中的效应 → output bits changed
// ============================================================
static void test_diffusion() {
    printf("=== 2. Diffusion Test ===\n");
    const int TRIALS = 1000;
    int total_bits = 0, total_changed = 0;

    for (int t = 0; t < TRIALS; t++) {
        u64 w[8];
        for (int i = 0; i < 8; i++) w[i] = ((u64)rand() << 32) ^ rand();

        // 随机 flip 1 bit
        int byte_idx = rand() % 64;
        int bit_idx  = rand() % 8;
        u64 w_mod[8]; memcpy(w_mod, w, 64);
        ((u8*)w_mod)[byte_idx] ^= (1 << bit_idx);

        u64 mm[8], ml[8], mm2[8], ml2[8];
        init_mix(w, mm, ml);
        init_mix(w_mod, mm2, ml2);

        int changed = 0;
        for (int i = 0; i < 8; i++) {
            changed += __builtin_popcountll(mm[i] ^ mm2[i]);
            changed += __builtin_popcountll(ml[i] ^ ml2[i]);
        }
        total_changed += changed;
        total_bits += 1024; // 16 × 64
    }
    double avg = total_changed * 100.0 / total_bits;
    printf("  single-bit flip → avg %.1f%% output bits changed (ideal 50%%)\n", avg);
    printf("  %s\n\n", (avg > 45 && avg < 55) ? "✓ near-ideal diffusion" : "△ sub-optimal");
}

// ============================================================
// 3. 输入-输出依赖: 每个 w[i] 的 bit 影响多少个输出 bit
// ============================================================
static void test_dependency() {
    printf("=== 3. Per-Word Dependency ===\n");
    u64 w[8]; for (int i = 0; i < 8; i++) w[i] = ((u64)rand() << 32) ^ rand();

    printf("  w[i] → mm[j] dependency (%% of mm[j] bits affected by w[i]):\n");
    printf("  ");
    for (int j = 0; j < 8; j++) printf("  mm[%d] ", j);
    printf("\n");

    for (int i = 0; i < 8; i++) {
        printf("  w[%d]", i);
        for (int j = 0; j < 8; j++) {
            int changed = 0;
            for (int b = 0; b < 64; b++) {
                u64 w_mod[8]; memcpy(w_mod, w, 64);
                w_mod[i] ^= (1ULL << b);
                u64 mm[8], ml[8], mm2[8], ml2[8];
                init_mix(w, mm, ml);
                init_mix(w_mod, mm2, ml2);
                changed += __builtin_popcountll(mm[j] ^ mm2[j]);
            }
            // 64 tests × 64 bits = 4096 total
            printf("  %3.0f%%", changed * 100.0 / 4096);
        }
        printf("\n");
    }
    printf("\n");
}

// ============================================================
// 4. 位独立性: 每个 output bit 依赖多少个 input bit
// ============================================================
static void test_bit_independence() {
    printf("=== 4. Bit Independence ===\n");
    const int TRIALS = 100;

    // 对于 mm[0] 的 bit 0, 测试它对每个 w[i] bits 的依赖
    u64 w[8];
    int dep[512] = {0}; // dep[input_bit_position] = how many trials it affected output

    for (int t = 0; t < TRIALS; t++) {
        for (int i = 0; i < 8; i++) w[i] = ((u64)rand() << 32) ^ rand();
        u64 mm[8], ml[8];
        init_mix(w, mm, ml);
        u64 ref_bit = mm[0] & 1; // test mm[0] bit 0
        // 遍历所有 512 个 input bit
        for (int byte_idx = 0; byte_idx < 64; byte_idx++) {
            for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
                int input_pos = byte_idx * 8 + bit_idx;
                u64 w_mod[8]; memcpy(w_mod, w, 64);
                ((u8*)w_mod)[byte_idx] ^= (1 << bit_idx);
                u64 mm2[8], ml2[8];
                init_mix(w_mod, mm2, ml2);
                if ((mm2[0] & 1) != ref_bit) dep[input_pos]++;
            }
        }
    }
    int fully_dep = 0; // dependent on all trials
    int none_dep = 0;  // never dependent
    for (int i = 0; i < 512; i++) {
        if (dep[i] == TRIALS) fully_dep++;
        if (dep[i] == 0) none_dep++;
    }
    printf("  mm[0] bit 0 dependency on 512 input bits:\n");
    printf("    fully dependent (100%%): %d / 512\n", fully_dep);
    printf("    never dependent (0%%):   %d / 512\n", none_dep);
    printf("  %s\n\n", (none_dep == 0) ? "✓ full dependency — no isolated input" : "△ some inputs isolated");
}

int main() {
    srand(0xDEADBEEF);
    test_collisions();
    test_diffusion();
    test_dependency();
    test_bit_independence();
    return 0;
}
