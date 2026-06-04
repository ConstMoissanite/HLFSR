// polyselect.cpp — HLFSR-64 基底 LFSR 多项式选择
//
// 方法：枚举权重 7/9/11 的候选多项式，测试本原性
//
// 本原性测试：
//   1. 不可约性：x^{2^64} ≡ x (mod p) 且 x^{2^k} ≠ x,  k∈{1,2,4,8,16,32}
//   2. 本原性：x^{(2^64-1)/q} ≠ 1 (mod p), q∈{3,5,17,257,641,65537,6700417}
//
// 2^64-1 = 3 × 5 × 17 × 257 × 641 × 65537 × 6700417 (Fermat F0..F4 及 F5 因子)
//
// 编译 (MSVC):  cl /O2 /arch:AVX2 polyselect.cpp
// 编译 (GCC):   g++ -O2 -march=native -mpclmul polyselect.cpp -o polyselect

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// PCLMULQDQ 无进位乘法
// ============================================================
#if defined(_MSC_VER)
    #include <intrin.h>
    #include <wmmintrin.h>
    #define CLMUL_AVAILABLE 1
#elif defined(__PCLMUL__)
    #include <x86intrin.h>
    #define CLMUL_AVAILABLE 1
#else
    #define CLMUL_AVAILABLE 0
#endif

static void clmul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
#if CLMUL_AVAILABLE
    __m128i _a = _mm_set_epi64x(0, (long long)a);
    __m128i _b = _mm_set_epi64x(0, (long long)b);
    __m128i _c = _mm_clmulepi64_si128(_a, _b, 0);
    // union 提取高低 64 位，避免依赖 SSE4.1
    union { __m128i v; uint64_t u[2]; } u;
    u.v = _c;
    *lo = u.u[0];
    *hi = u.u[1];
#else
    *lo = 0; *hi = 0;
    for (int i = 0; i < 64; i++) {
        if ((b >> i) & 1) {
            *lo ^= (a << i);
            if (i > 0) *hi ^= (a >> (64 - i));
        }
    }
#endif
}

// ============================================================
// GF(2) 多项式运算（模任意 64 次多项式 p(x) = x^64 + p_lo）
// ============================================================

// 无进位乘法后模约简：result = (hi*x^64 + lo) mod (x^64 + p_lo)
// x^64 ≡ p_lo，hi*x^64 ≡ hi * p_lo（无进位乘），可能产生高次项，迭代消除
static uint64_t poly_reduce(uint64_t hi, uint64_t lo, uint64_t p_lo) {
    uint64_t r = lo;
    while (hi) {
        uint64_t hi2, lo2;
        clmul64(hi, p_lo, &hi2, &lo2);
        r ^= lo2;
        hi = hi2;
    }
    return r;
}

// 多项式乘法模 p: (a * b) mod (x^64 + p_lo)
static uint64_t poly_mul_mod(uint64_t a, uint64_t b, uint64_t p_lo) {
    uint64_t hi, lo;
    clmul64(a, b, &hi, &lo);
    return poly_reduce(hi, lo, p_lo);
}

// 多项式平方模 p
static uint64_t poly_sqr_mod(uint64_t a, uint64_t p_lo) {
    return poly_mul_mod(a, a, p_lo);
}

// 多项式幂模 p（二进制指数）
static uint64_t poly_pow_mod(uint64_t base, uint64_t exp, uint64_t p_lo) {
    uint64_t r = 1;
    uint64_t b = base;
    while (exp) {
        if (exp & 1) r = poly_mul_mod(r, b, p_lo);
        b = poly_sqr_mod(b, p_lo);
        exp >>= 1;
    }
    return r;
}

// ============================================================
// 不可约性测试
// p_lo: x^64 + p_lo 候选多项式低 63 位
// 返回 1 = 不可约, 0 = 可约
// ============================================================
static int test_irreducible(uint64_t p_lo) {
    uint64_t v = 2;  // 多项式 x (0x02)

    for (int k = 1; k <= 64; k++) {
        v = poly_sqr_mod(v, p_lo);  // v = x^{2^k} mod p

        // 64 的所有真因子: 1, 2, 4, 8, 16, 32
        switch (k) {
        case 1: case 2: case 4: case 8: case 16: case 32:
            if (v == 2) return 0;  // x^{2^k} = x → p 有因子度数 | k → 可约
        }
    }

    // x^{2^64} 必须等于 x
    return (v == 2) ? 1 : 0;
}

// ============================================================
// 本原性测试
// p_lo: 不可约多项式 x^64 + p_lo
// 返回 1 = 本原, 0 = 非本原
// ============================================================
static const uint64_t PRIME_FACTORS[] = {3, 5, 17, 257, 641, 65537, 6700417};
#define NFACTORS 7

static int test_primitive(uint64_t p_lo) {
    // x 是在 GF(2^64) ≅ GF(2)[x]/(p) 中的本原候选
    // 对于不可约 p，x^{2^64-1} ≡ 1 自动成立
    // 只需验证对所有 q | 2^64-1 有 x^{(2^64-1)/q} ≠ 1
    uint64_t x = 2;
    for (int i = 0; i < NFACTORS; i++) {
        uint64_t q = PRIME_FACTORS[i];
        uint64_t exp = UINT64_MAX / q;  // (2^64-1) / q
        if (poly_pow_mod(x, exp, p_lo) == 1)
            return 0;
    }
    return 1;
}

// ============================================================
// 工具函数
// ============================================================
#ifdef _MSC_VER
static int popcount64(uint64_t x) {
    return (int)__popcnt64(x);
}
#else
static int popcount64(uint64_t x) {
    return __builtin_popcountll(x);
}
#endif

// 打印多项式
static void print_polynomial(uint64_t p) {
    printf("x^64");
    for (int i = 63; i >= 0; i--) {
        if (p & (1ULL << i)) {
            if      (i == 0) printf(" + 1");
            else if (i == 1) printf(" + x");
            else             printf(" + x^%d", i);
        }
    }
}

// ============================================================
// 主流程
// ============================================================
int main(int argc, char *argv[]) {
    int target_count = (argc > 1) ? atoi(argv[1]) : 16;
    if (target_count < 1) target_count = 16;

    printf("// ============================================================\n");
    printf("// HLFSR-64 基底 LFSR 多项式筛选\n");
    printf("// ============================================================\n\n");

    // 验证基础多项式 x^64 + x^4 + x^3 + x + 1
    printf("// 验证基础多项式...\n");
    uint64_t base = 0x1BULL;  // x^4 + x^3 + x + 1
    if (!test_irreducible(base)) {
        printf("// 错误: x^64+x^4+x^3+x+1 不可约测试失败!\n");
        return 1;
    }
    if (!test_primitive(base)) {
        printf("// 错误: x^64+x^4+x^3+x+1 本原测试失败!\n");
        return 1;
    }
    printf("// x^64 + x^4 + x^3 + x + 1: 本原 ✓\n\n");

    printf("// 2^64-1 素因子分解: 3 × 5 × 17 × 257 × 641 × 65537 × 6700417\n");
    printf("// 目标: %d 个本原多项式, 权重 7/9/11\n\n", target_count);

    // ----------------------------------------------------------
    // 权重 7 随机采样: 选 5 个中间项（位置 1..63）, x^64 和 x^0 固定
    // 随机策略确保多项式项分布均匀，避免字典序枚举的结构聚集
    // ----------------------------------------------------------
    uint64_t found[64];
    int      found_w[64];
    int count = 0;

    printf("// 权重 7 随机搜索中...\n");

    // SplitMix64 PRNG (种子固定以复现结果)
    uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
    auto rng_next = [&]() -> uint64_t {
        uint64_t z = (rng_state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };

    uint64_t tested7 = 0;
    while (count < target_count && tested7 < 50000000ULL) {
        // Fisher-Yates 随机选 5 个不同中间项 (1..63)
        uint8_t bits[64];
        memset(bits, 0, sizeof(bits));
        bits[0] = 1;  // x^0 固定
        int picked = 1;
        while (picked < 6) {  // 5 个中间项 + x^0 = 6 bits
            int pos = 1 + (int)(rng_next() % 63);  // 1..63
            if (!bits[pos]) {
                bits[pos] = 1;
                picked++;
            }
        }

        uint64_t p_lo = 0;
        for (int i = 0; i < 64; i++)
            if (bits[i]) p_lo |= (1ULL << i);

        tested7++;
        if (popcount64(p_lo) != 6) continue;

        if (!test_irreducible(p_lo)) continue;
        if (!test_primitive(p_lo)) continue;

        found[count]   = p_lo;
        found_w[count] = 7;
        count++;

        printf("  [%2d] weight=7  trials=%llu  ", count, (unsigned long long)tested7);
        print_polynomial(p_lo);
        printf("\n");
    }

    printf("// 权重 7 随机: 测试 %llu 个, 找到 %d 个\n\n",
           (unsigned long long)tested7, count);

    // ----------------------------------------------------------
    // 权重 9 枚举 (如需要)
    // C(63,7) = 553M 太多，用随机采样
    // ----------------------------------------------------------
    if (count < target_count) {
        printf("// 权重 9 随机搜索中...\n");
        // 简单 LCG 随机数
        uint64_t seed = 0x123456789ABCDEF0ULL;
        auto rand64 = [&]() -> uint64_t {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            return seed;
        };

        uint64_t trials9 = 0;
        int need = target_count - count;
        while (count < target_count && trials9 < 50000000ULL) {
            // 随机选 7 个中间项
            uint8_t bits[64];
            memset(bits, 0, sizeof(bits));
            bits[0] = 1;  // x^0
            int picked = 1;  // 已选 x^0
            while (picked < 8) {  // 需要 7 个中间项 + x^0 = 8 个, 但 weight 9 = x^64 + 7midd + x^0
                int pos = 1 + (int)(rand64() % 63);  // 1..63
                if (!bits[pos]) {
                    bits[pos] = 1;
                    picked++;
                }
            }
            // 构造 p_lo
            uint64_t p_lo = 0;
            for (int i = 0; i < 64; i++) {
                if (bits[i]) p_lo |= (1ULL << i);
            }

            trials9++;
            if (popcount64(p_lo) != 8) continue;  // 7 中间项 + x^0 = 8 bits
            // weight = 1(x^64) + popcount(p_lo) = 1 + 8 = 9
            if (!test_irreducible(p_lo)) continue;
            if (!test_primitive(p_lo)) continue;

            found[count]   = p_lo;
            found_w[count] = 9;
            count++;

            printf("  [%2d] weight=9  trials=%llu  ", count, (unsigned long long)trials9);
            print_polynomial(p_lo);
            printf("\n");
        }
        printf("// 权重 9: 测试 %llu 个, 找到 %d 个\n\n",
               (unsigned long long)trials9, count - (target_count - need));
    }

    // ----------------------------------------------------------
    // 权重 11 随机采样 (如需要)
    // ----------------------------------------------------------
    if (count < target_count) {
        printf("// 权重 11 随机搜索中...\n");
        uint64_t seed = 0xABCDEF0123456789ULL;
        auto rand64_11 = [&]() -> uint64_t {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            return seed;
        };

        uint64_t trials11 = 0;
        while (count < target_count && trials11 < 100000000ULL) {
            uint8_t bits[64];
            memset(bits, 0, sizeof(bits));
            bits[0] = 1;
            int picked = 1;
            while (picked < 10) {  // 9 中间项 + x^0
                int pos = 1 + (int)(rand64_11() % 63);
                if (!bits[pos]) { bits[pos] = 1; picked++; }
            }
            uint64_t p_lo = 0;
            for (int i = 0; i < 64; i++) {
                if (bits[i]) p_lo |= (1ULL << i);
            }

            trials11++;
            if (popcount64(p_lo) != 10) continue;
            if (!test_irreducible(p_lo)) continue;
            if (!test_primitive(p_lo)) continue;

            found[count]   = p_lo;
            found_w[count] = 11;
            count++;

            printf("  [%2d] weight=11 trials=%llu  ", count, (unsigned long long)trials11);
            print_polynomial(p_lo);
            printf("\n");
        }
        printf("// 权重 11: 测试 %llu 个, 找到 %d 个\n\n",
               (unsigned long long)trials11, count);
    }

    // ----------------------------------------------------------
    // 输出结果
    // ----------------------------------------------------------
    printf("// ============================================================\n");
    printf("// 结果: %d 个本原多项式 (%d 位)\n", count, (int)(sizeof(found)/sizeof(found[0])));
    printf("// ============================================================\n\n");

    printf("static const uint64_t HLFSR64_LFSR_POLY[%d] = {\n", count);
    for (int i = 0; i < count; i++) {
        printf("    0x%016llXULL,  // [%2d] weight=%d  ",
               (unsigned long long)found[i], i, found_w[i]);
        print_polynomial(found[i]);
        printf("\n");
    }
    printf("};\n\n");

    printf("// 多项式表示: uint64_t 的 bit i 对应 x^i 项存在\n");
    printf("// x^64 项始终存在 (隐式)\n");
    printf("// 每条 LFSR 使用对应的多项式作为反馈抽头\n");

    return 0;
}
