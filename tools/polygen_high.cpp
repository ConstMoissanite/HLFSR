// polygen_high.cpp — 快速生成权重 12-15 的 64 次本原多项式
// 编译: g++ -std=c++14 -O2 polygen_high.cpp -o polygen_high
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>

typedef unsigned long long u64;

// SplitMix64 PRNG
static u64 rng_state = 0x9E3779B97F4A7C15ULL;
static u64 rng_next() {
    u64 z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Carryless multiply: hi:x^64 + lo
static void clmul64(u64 a, u64 b, u64 *hi, u64 *lo) {
    *lo = 0; *hi = 0;
    for (int i = 0; i < 64; i++)
        if ((b >> i) & 1) { *lo ^= (a << i); if (i > 0) *hi ^= (a >> (64 - i)); }
}

// Polynomial multiply mod p(x) = x^64 + p_lo
static u64 poly_mul_mod(u64 a, u64 b, u64 p_lo) {
    u64 hi, lo; clmul64(a, b, &hi, &lo);
    u64 r = lo;
    while (hi) { u64 h2, l2; clmul64(hi, p_lo, &h2, &l2); r ^= l2; hi = h2; }
    return r;
}
static u64 poly_sqr_mod(u64 a, u64 p_lo) { return poly_mul_mod(a, a, p_lo); }
static u64 poly_pow_mod(u64 base, u64 exp, u64 p_lo) {
    u64 r = 1, b = base;
    while (exp) { if (exp & 1) r = poly_mul_mod(r, b, p_lo); b = poly_sqr_mod(b, p_lo); exp >>= 1; }
    return r;
}

// Primitivity test for p(x) = x^64 + p_lo
// 2^64-1 = 3×5×17×257×641×65537×6700417
static const u64 factors[] = {3, 5, 17, 257, 641, 65537, 6700417};

static int test_primitive(u64 p_lo) {
    u64 x = 2; // polynomial x
    // Irreducibility: x^{2^64} == x, and x^{2^k} != x for k in {1,2,4,8,16,32}
    u64 v = x;
    for (int k = 1; k <= 64; k++) {
        v = poly_sqr_mod(v, p_lo);
        if (k==1||k==2||k==4||k==8||k==16||k==32) { if (v == x) return 0; }
    }
    if (v != x) return 0;
    // Primitivity: x^{(2^64-1)/q} != 1 for all q
    for (int i = 0; i < 7; i++) {
        u64 q = factors[i], e = 0xFFFFFFFFFFFFFFFFULL / q;
        if (poly_pow_mod(x, e, p_lo) == 1) return 0;
    }
    return 1;
}

static int popcnt(u64 x) { int c=0; while(x){c++; x&=x-1;} return c; }

int main() {
    rng_state = (u64)std::time(nullptr);
    printf("// 权重 12-15 的 64 次本原多项式 (V9-Aux)\n");
    printf("// 随机采样 + 不可约+本原验证\n\n");

    int found = 0;
    u64 results[9];
    int  weights[9];
    int  tries = 0;

    while (found < 9 && tries < 500000) {
        // 随机选 10-13 个中间项 (weight 12-15 total with x^64 and x^0)
        int target_w = 12 + (int)(rng_next() % 4); // 12-15
        int need = target_w - 2; // minus x^64 and x^0

        unsigned char bits[64]; memset(bits, 0, 64); bits[0] = 1;
        int picked = 1;
        while (picked < need + 1) {
            int pos = 1 + (int)(rng_next() % 63);
            if (!bits[pos]) { bits[pos] = 1; picked++; }
        }
        u64 p_lo = 0;
        for (int i = 0; i < 64; i++) if (bits[i]) p_lo |= (1ULL << i);
        tries++;
        if (popcnt(p_lo) + 1 != target_w) continue; // +1 for x^64

        if (!test_primitive(p_lo)) continue;

        results[found] = p_lo;
        weights[found] = target_w;
        printf("[%d] weight=%d  0x%016llXULL  // ", found, target_w, p_lo);
        printf("x^64");
        for (int i = 63; i >= 0; i--) if (p_lo & (1ULL << i)) {
            if (i==0) printf(" + 1"); else if (i==1) printf(" + x"); else printf(" + x^%d", i);
        }
        printf("\n");
        found++;
    }
    printf("\n// Found %d in %d tries\n", found, tries);
    printf("static const u64 POLY[9] = {\n");
    for (int i = 0; i < found; i++)
        printf("    0x%016llXULL,  // [%d] weight=%d\n", results[i], i, weights[i]);
    printf("};\n");
    return 0;
}
