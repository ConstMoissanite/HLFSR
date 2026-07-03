// poly2_search.cpp — 搜索最优 POLY[2], 单次编译, 运行时迭代
// g++ -std=c++14 -O3 -march=native src/test/poly2_search.cpp -o poly2_search.exe
// 内嵌 hlfsr64 + polygen, 零外部依赖
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>

typedef unsigned long long u64;
typedef unsigned char u8;
typedef unsigned short u16;

// ============================================================
// SplitMix64 PRNG
// ============================================================
static u64 rng_state = 0x9E3779B97F4A7C15ULL;
static u64 rng_next() {
    u64 z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// ============================================================
// Carryless multiply + 本原性检验 (polygen 同款)
// ============================================================
static void clmul64(u64 a, u64 b, u64 *hi, u64 *lo) {
    *lo = 0; *hi = 0;
    for (int i = 0; i < 64; i++)
        if ((b >> i) & 1) { *lo ^= (a << i); if (i > 0) *hi ^= (a >> (64 - i)); }
}
static u64 poly_mul_mod(u64 a, u64 b, u64 p_lo) {
    u64 hi, lo; clmul64(a, b, &hi, &lo);
    // 用 128 位避免 UB: r128 = lo + (hi << 64), 逐位约化
    typedef unsigned __int128 u128;
    u128 r = ((u128)hi << 64) | lo;
    for (int i = 127; i >= 64; i--)
        if ((r >> i) & 1) r ^= ((u128)p_lo << (i - 64));
    return (u64)(r & 0xFFFFFFFFFFFFFFFFULL);
}
static u64 poly_pow(u64 x, u64 exp, u64 p_lo) {
    u64 r = 1;
    while (exp) { if (exp & 1) r = poly_mul_mod(r, x, p_lo); x = poly_mul_mod(x, x, p_lo); exp >>= 1; }
    return r;
}
static int popcount64(u64 x) { int w=0; while(x){w+=x&1;x>>=1;} return w; }
static int is_primitive(u64 p_lo) {
    u64 order = 0xFFFFFFFFFFFFFFFFULL;
    if (poly_pow(2, order, p_lo) != 1) return 0;
    u64 fac[] = {3, 5, 17, 257, 641, 65537, 6700417ULL};
    for (int i = 0; i < 7; i++)
        if (poly_pow(2, order / fac[i], p_lo) == 1) return 0;
    return 1;
}

// ============================================================
// HLFSR-64 副本 (POLY 数组可变, 仅保留单步逻辑)
// ============================================================
static u64 POLY_COPY[8] = {
    0x4800203343401101ULL, 0x0416001300480117ULL, 0x58000C0310100803ULL, 0x00A0090940648023ULL,
    0x484302010C340003ULL, 0x801D001006412901ULL, 0x04429288080A1021ULL, 0x2000022052D01213ULL,
};
static u8 matrix[64];
static u64 lfsr_arr[8];
static u16 m_idx;

static u8 rol8(u8 x, int n) { n &= 7; return (u8)((x << n) | (x >> (8 - n))); }

static void init_state(const u8 km[64], u16 idx_init) {
    m_idx = idx_init & 0x1FF;
    // 完整三乘积+MDS init (与 hlfsr64.cpp 一致)
    u64 pool[24];
    for (int i = 0; i < 8; i++) {
        u64 val = 0;
        for (int j = 0; j < 8; j++) val |= (u64)km[i * 8 + j] << (j * 8);
        pool[i]    = val;
        pool[i+8]  = (val << 23) | (val >> 41);
        pool[i+16] = (val << 41) | (val >> 23);
    }
    u64 mm[8], ml[8];
    for (int i = 0; i < 8; i++) {
        mm[i] = pool[i]       * pool[((i+1)&7)+8]  * pool[((i+2)&7)+16];
        ml[i] = pool[(i+4)&7] * pool[((i+5)&7)+8]  * pool[((i+6)&7)+16];
    }
    auto xt = [](u8 b){ return (u8)((b<<1)^((b>>7)?0x1B:0)); };
    auto gm = [&](u8 a, u8 b){ u8 r=0; for(int k=0;k<8;k++){if(b&1)r^=a;a=xt(a);b>>=1;} return r; };
    for (int col = 0; col < 8; col++) {
        u8 cm[8], cl[8]; u8 *mb=(u8*)mm, *lb=(u8*)ml;
        for (int r=0;r<8;r++){cm[r]=mb[r*8+col];cl[r]=lb[r*8+col];}
        for (int r=0;r<8;r++){u8 sm=0,sl=0;for(int j=0;j<8;j++){
            int d=(j-r)&7;u8 c=(d==0)?2:(d==1)?3:1;
            sm^=gm(c,cm[j]);sl^=gm(c,cl[j]);}
            mb[r*8+col]=sm;lb[r*8+col]=sl;}
    }
    memcpy(matrix, mm, 64);
    memcpy(lfsr_arr, ml, 64);
    // idx 高 7 位键相关搅拌
    { u8 extra = (u8)(idx_init >> 9); for (int i=0;i<64;i++) matrix[i] ^= (u8)(extra * km[i] & 0x7F); }
    // 64 步预热: 用简版循环(避免递归调用 single_next 的 feedback)
    for (int s = 0; s < 64; s++) {
        u8 face=(u8)(m_idx&7), row=(u8)((m_idx>>3)&7), col=(u8)((m_idx>>6)&7);
        u8 ba=face*8+row, curbyte=matrix[ba], mask=curbyte, p=(u8)((m_idx>>6)&7);
        u64 vx=0;
        for(int i=0;i<8;i++){u64 s=lfsr_arr[i];lfsr_arr[i]=(s<<1)^(POLY_COPY[i]&(0ULL-(s>>63)));vx^=lfsr_arr[i];}
        vx^=(vx<<33)^(vx>>31);
        u64 raw=((vx*(((u64)mask*0xBF58476D1CE4E5B9ULL)|1))*0x9E3779B97F4A7C15ULL);
        u64 fb=(raw&0xFFFF)*0xBF58476D1CE4E5B9ULL;
        matrix[ba]^=(u8)(fb&0xFF);matrix[ba]=rol8(matrix[ba],p);
        matrix[((face^1)&7)*8+row]^=(u8)(fb>>8);
        m_idx=(m_idx+1)&0x1FF;
    }
}

static u64 single_next() {
    u8  face = (u8)(m_idx & 7);
    u8  row  = (u8)((m_idx >> 3) & 7);
    u8  col  = (u8)((m_idx >> 6) & 7);
    u8  ba   = face * 8 + row;
    u8  curbyte = matrix[ba];
    u8  mask = curbyte;
    u8  p    = (u8)((m_idx >> 6) & 7);

    u64 vx = 0;
    for (int i = 0; i < 8; i++) {
        u64 s = lfsr_arr[i];
        lfsr_arr[i] = (s << 1) ^ (POLY_COPY[i] & (0ULL - (s >> 63)));
        vx ^= lfsr_arr[i];
    }
    vx ^= (vx << 33) ^ (vx >> 31);
    u64 mk = ((u64)mask * 0xBF58476D1CE4E5B9ULL) | 1;
    u64 raw = (vx * mk) * 0x9E3779B97F4A7C15ULL;
    u64 output = raw ^ (0ULL - ((mask >> col) & 1));

    u64 fb = (raw & 0xFFFF) * 0xBF58476D1CE4E5B9ULL;
    matrix[ba] ^= (u8)(fb & 0xFF);
    matrix[ba]  = rol8(matrix[ba], p);
    u8 nb = (u8)(((face ^ 1) & 7) * 8 + row);
    matrix[nb] ^= (u8)(fb >> 8);
    m_idx = (m_idx + 1) & 0x1FF;
    return output;
}

// ============================================================
// Phase 1 + Phase 2a: 快速筛查一个 POLY[2]
// ============================================================
static double test_candidate(u64 poly2, const u8* saved_km, u16 idxx) {
    POLY_COPY[2] = poly2;
    const int SCREEN_MASKS = 2000;
    const int SCREEN_PAIRS = 500;
    const int BIT_POS = 0;

    // Phase 1: find max bias mask
    srand(0xDEADBEEF);
    double max_bias = 0;
    u8 best_alpha[64];
    for (int m = 0; m < SCREEN_MASKS; m++) {
        u8 alpha[64];
        for (int i = 0; i < 64; i++) alpha[i] = (u8)(rand() & 0xFF);

        int zeros = 0;
        for (int p = 0; p < SCREEN_PAIRS; p++) {
            u8 x[64], xa[64];
            for (int i = 0; i < 64; i++) { x[i] = (u8)(rand() & 0xFF); xa[i] = x[i] ^ alpha[i]; }
            init_state(x, idxx);
            u64 o1 = single_next();
            init_state(xa, idxx);
            u64 o2 = single_next();
            if (((o1 >> BIT_POS) & 1) == ((o2 >> BIT_POS) & 1)) zeros++;
        }
        double bias = fabs(zeros / (double)SCREEN_PAIRS - 0.5);
        if (bias > max_bias) { max_bias = bias; for (int i=0;i<64;i++) best_alpha[i]=alpha[i]; }
    }

    // Phase 2a: 5M pairs on best mask
    const long long PRE = 5000000LL;
    long long z = 0;
    srand(0xCAFEBABE);
    for (long long p = 0; p < PRE; p++) {
        u8 x[64], xa[64];
        for (int i = 0; i < 64; i++) { x[i] = (u8)(rand() & 0xFF); xa[i] = x[i] ^ best_alpha[i]; }
        init_state(x, idxx);
        u64 o1 = single_next();
        init_state(xa, idxx);
        u64 o2 = single_next();
        if (((o1 >> BIT_POS) & 1) == ((o2 >> BIT_POS) & 1)) z++;
    }
    return fabs(z / (double)PRE - 0.5);
}

// ============================================================
// main
// ============================================================
int main() {
    printf("=== POLY[2] Search ===\n");

    // 生成固定测试密钥
    u8 km[64];
    for (int i = 0; i < 64; i++) km[i] = (u8)(i * 0x9D + 1);
    u16 idx_init = 0x100;

    // sanity: 已知原版 POLY[2] (w=13) 应通过本原性检验
    u64 ref_poly = 0x58000C0310100803ULL;
    printf("Sanity: existing POLY[2] 0x%016llX (w=%d) is_primitive=%d\n",
           ref_poly, popcount64(ref_poly), is_primitive(ref_poly));

    // 生成 w=15 本原候选
    printf("\nGenerating w=15 primitive candidates...\n");
    rng_state = 0x9E3779B97F4A7C15ULL;
    const int MAX_C = 30;
    u64 cand[MAX_C];
    int found = 0, tries = 0;
    while (found < MAX_C && tries < 2000) {
        tries++;
        u64 p = 0;
        // 随机 13 个位 (x^64 + 13 random + 1 = 15)
        while (popcount64(p) < 13) {
            int pos = (int)(rng_next() % 63) + 1;
            p |= (1ULL << pos);
        }
        u64 p_lo = p | 1ULL;
        if (popcount64(p_lo) != 14) continue;
        if (is_primitive(p_lo)) {
            int dup = 0;
            for (int i = 0; i < found; i++) if (cand[i] == p_lo) dup = 1;
            if (!dup) { cand[found++] = p_lo; printf("  [%2d] 0x%016llX\n", found-1, p_lo); }
        }
    }
    printf("Found %d in %d tries\n\n", found, tries);

    // 测试每个候选
    printf("%3s  %-18s  %-10s  %-10s\n", "idx", "POLY[2]", "bias", "3sigma");
    double best_bias = 1.0;
    int best_i = -1;
    for (int i = 0; i < found; i++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        double bias = test_candidate(cand[i], km, idx_init);
        double noise = 1.0 / sqrt(5000000.0);
        double ts = 3.0 * noise;
        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        printf("%3d  0x%016llX  %.2e  %.2e  %s  (%.0fs)\n",
               i, cand[i], bias, ts, bias < ts ? "PASS" : "FAIL", sec);
        if (bias < best_bias) { best_bias = bias; best_i = i; }
    }

    printf("\nBest: [%d] 0x%016llX  bias=%.2e\n", best_i, cand[best_i], best_bias);
    return 0;
}
