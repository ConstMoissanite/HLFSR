// hlfsr64.cpp — HLFSR-64 V8 流密码核心 (8×8×8)
#include "hlfsr64.hpp"
#include <cstring>

// TODO: 用高权重 (12–15) 64 次本原多项式替换全部 9 条
// POLY[8] = 附属 LFSR, 需要 weight 15+
const hlfsr64::u64 hlfsr64::POLY[9] = {
    0x0054010000020001ULL, 0x0080000004200083ULL, 0x0100000020040481ULL, 0x0000088040008005ULL,
    0x0804000004200101ULL, 0x0000900008400009ULL, 0x8000110004001001ULL, 0x2000000000820141ULL,
    0x4080000040080021ULL,  // aux placeholder (原 POLY[12], needs weight≥15)
};

#ifdef HLFSR64_AVX2
#include <immintrin.h>
#endif

static inline hlfsr64::u8 ct_eq8(hlfsr64::u8 a, hlfsr64::u8 b) {
    hlfsr64::u8 d = a ^ b;
    return (hlfsr64::u8)(0 - (((d | (hlfsr64::u8)(-(std::int8_t)d)) >> 7) ^ 1));
}
static inline hlfsr64::u8 rol8(hlfsr64::u8 x, int n) {
    n &= 7; return (hlfsr64::u8)((x << n) | (x >> (8 - n)));
}

void hlfsr64::init(const u8 m[64], const u8 seed[32], u16 idx_init) {
    std::memcpy(m_matrix, m, 64);
    m_idx = idx_init & 0x1FF;
    for (int i = 0; i < 9; i++) {
        u64 val = 0; u8 base = (u8)(i * 3) & 31;  // stride 3, gcd(3,32)=1
        for (int j = 0; j < 8; j++) val |= (u64)seed[(base + j) & 31] << (j * 8);
        m_lfsr[i] = val;
    }
}

hlfsr64::u64 hlfsr64::advance_lfsr(u8 mask_byte) {
    u64 vx = 0;
    // 8 条主 LFSR
    for (int i = 0; i < 8; i++) {
        u64 s   = m_lfsr[i];
        u64 msb = s >> 63;
        m_lfsr[i] = (s << 1) ^ (POLY[i] & (0ULL - msb));
        vx ^= m_lfsr[i] & (0ULL - ((u64)(mask_byte >> i) & 1ULL));
    }
    // 附属高权重 LFSR (始终推进, mask=0 时选通)
    u64 sa = m_lfsr[8];
    u64 ma = sa >> 63;
    m_lfsr[8] = (sa << 1) ^ (POLY[8] & (0ULL - ma));
    u64 mz = 0ULL - (ct_eq8(mask_byte, 0) & 1);
    u64 raw = (vx & ~mz) | (m_lfsr[8] & mz);
    return raw * 0x9E3779B97F4A7C15ULL;
}

hlfsr64::u64 hlfsr64::next() {
    u8  face = (u8)(m_idx & 7);
    u8  row  = (u8)((m_idx >> 3) & 7);
    u8  col  = (u8)((m_idx >> 6) & 7);
    u8  ba   = face * 8 + row;
    u8  curbyte = m_matrix[ba];
    u8  curbit  = (curbyte >> col) & 1;

    // LFSR 掩码 = 当前行（8 位，天然匹配 8 条 LFSR）
    u8 mask = curbyte;  // mask=0 时附属 LFSR 接管
    u8 p    = (u8)((m_idx >> 6) & 7);

    u64 raw    = advance_lfsr(mask);
    u64 output = raw ^ (0ULL - curbit);

    m_matrix[ba] ^= (u8)(raw & 0xFF);
    m_matrix[ba]  = rol8(m_matrix[ba], p);

    m_idx = (m_idx + 1) & 0x1FF;
    return output;
}

void hlfsr64::keystream(void* out, std::size_t bytes) {
    u8* p = static_cast<u8*>(out);

    // 8 步批处理：利用面间隔离
    while (bytes >= 64) {
        u8 pv[8], cb[8], ba8[8], mask[8];

        for (int i = 0; i < 8; i++) {
            u16 t_idx = (m_idx + (u16)i) & 0x1FF;
            u8 f = (u8)(t_idx & 7), r = (u8)((t_idx >> 3) & 7);
            u8 addr = f * 8 + r;
            ba8[i] = addr;
            mask[i] = m_matrix[addr];  // mask=0 → 附属 LFSR
            pv[i]   = (u8)((t_idx >> 6) & 7);
            cb[i]   = (mask[i] >> pv[i]) & 1;
        }

        u64 raw[8];
        for (int i = 0; i < 8; i++) raw[i] = advance_lfsr(mask[i]);

        // 3. 输出
        for (int i = 0; i < 8; i++) {
            u64 b = raw[i] ^ (0ULL - cb[i]);
            p[0]=(u8)b; p[1]=(u8)(b>>8); p[2]=(u8)(b>>16); p[3]=(u8)(b>>24);
            p[4]=(u8)(b>>32); p[5]=(u8)(b>>40); p[6]=(u8)(b>>48); p[7]=(u8)(b>>56);
            p += 8;
        }

        // 4. 回填矩阵（8 个不同面，互不冲突）
        for (int i = 0; i < 8; i++) {
            u8 addr = ba8[i];
            m_matrix[addr] ^= (u8)(raw[i] & 0xFF);
            m_matrix[addr]  = rol8(m_matrix[addr], pv[i] & 7);
        }

        m_idx = (m_idx + 8) & 0x1FF;
        bytes -= 64;
    }

    // 尾部不足 64 字节：单步
    while (bytes >= 8) {
        u64 b = next(); p[0]=(u8)b; p[1]=(u8)(b>>8); p[2]=(u8)(b>>16); p[3]=(u8)(b>>24);
        p[4]=(u8)(b>>32); p[5]=(u8)(b>>40); p[6]=(u8)(b>>48); p[7]=(u8)(b>>56);
        p+=8; bytes-=8;
    }
    if (bytes > 0) { u64 b = next(); for (std::size_t i=0;i<bytes;i++) p[i]=(u8)(b>>(i*8)); }
}
