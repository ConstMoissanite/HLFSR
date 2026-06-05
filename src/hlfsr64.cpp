// hlfsr64.cpp — HLFSR-64 MF 流密码核心
#include "hlfsr64.hpp"
#include <cstring>

const hlfsr64::u64 hlfsr64::POLY[16] = {
    0x0054010000020001ULL, 0x0080000004200083ULL, 0x0100000020040481ULL, 0x0000088040008005ULL,
    0x0804000004200101ULL, 0x0000900008400009ULL, 0x8000110004001001ULL, 0x2000000000820141ULL,
    0x2000004000430001ULL, 0x0002080800600001ULL, 0x00020A0000400009ULL, 0x000230000C000001ULL,
    0x4080000040080021ULL, 0x0080004400210001ULL, 0x0100020800008081ULL, 0x0210000500080001ULL,
};

static inline hlfsr64::u64 parity64(hlfsr64::u64 x) {
    return (hlfsr64::u64)__builtin_parityll(x) & 1ULL;
}
static inline hlfsr64::u8 ct_eq8(hlfsr64::u8 a, hlfsr64::u8 b) {
    hlfsr64::u8 d = a ^ b;
    return (hlfsr64::u8)(0 - (((d | (hlfsr64::u8)(-(std::int8_t)d)) >> 7) ^ 1));
}

void hlfsr64::init(const u8 m[32], const u8 seed[32], u8 idx_init) {
    std::memcpy(m_matrix, m, 32);
    m_idx = idx_init;
    for (int i = 0; i < 16; i++) {
        u64 val = 0; u8 base = (u8)(i * 2) & 31;
        for (int j = 0; j < 8; j++) val |= (u64)seed[(base + j) & 31] << (j * 8);
        m_lfsr[i] = val;
    }
}

hlfsr64::u64 hlfsr64::advance_lfsr(u8 s0, u8 s1, u8 s2) {
    // 预计算 3×16 掩码
    u64 sm[16], om[16], pm[16];
    for (int i = 0; i < 16; i++) {
        sm[i] = 0ULL - (ct_eq8(s0, (u8)i) & 1);
        om[i] = 0ULL - (ct_eq8(s1, (u8)i) & 1);
        pm[i] = 0ULL - (ct_eq8(s2, (u8)i) & 1);
    }
    u64 v0 = 0, v1 = 0, v2 = 0;
    for (int i = 0; i < 16; i++) {
        u64 s = m_lfsr[i];
        m_lfsr[i] = (s << 1) | parity64(s & (POLY[i] & 0x7FFFFFFFFFFFFFFFULL));
        u64 v = m_lfsr[i];
        v0 |= v & sm[i]; v1 |= v & om[i]; v2 |= v & pm[i];
    }
    return (v0 ^ v1 ^ v2) * 0x9E3779B97F4A7C15ULL;
}

// ROL16 on a 16-bit value
static inline std::uint16_t rol16(std::uint16_t x, int n) {
    n &= 15;
    return (std::uint16_t)((x << n) | (x >> (16 - n)));
}

hlfsr64::u64 hlfsr64::next() {
    // 1. 取当前行和指令字
    u8 row = m_idx >> 4;           // 0..15
    u8 byte_base = row << 1;       // row * 2
    std::uint16_t curword = (std::uint16_t)m_matrix[byte_base] | ((std::uint16_t)m_matrix[(byte_base + 1) & 31] << 8);

    u8 shift = curword >> 12;          // bits 15:12
    u8 sel0  = (curword >> 8) & 0x0F;  // bits 11:8
    u8 sel1  = (curword >> 4) & 0x0F;  // bits 7:4
    u8 sel2  = curword & 0x0F;         // bits 3:0

    // 2. 三路选通 + 乘性混合
    u64 raw = advance_lfsr(sel0, sel1, sel2);

    // 3. curbit = 当前行 bit0（取矩阵 byte_base 的 LSB）
    u8  curbit = m_matrix[byte_base] & 1;
    u64 output = raw ^ (0ULL - curbit);

    // 4. 回填：raw[15:0] XOR 进当前行
    std::uint16_t fill = (std::uint16_t)(raw & 0xFFFF);
    std::uint16_t row_val = (std::uint16_t)m_matrix[byte_base] | ((std::uint16_t)m_matrix[(byte_base + 1) & 31] << 8);
    row_val ^= fill;

    // 5. 当前行 ROL16 by shift
    row_val = rol16(row_val, shift);

    // 6. 写回矩阵
    m_matrix[byte_base]           = (u8)row_val;
    m_matrix[(byte_base + 1) & 31] = (u8)(row_val >> 8);

    // 7. idx 步进
    m_idx = (m_idx + 1) & 0xFF;

    return output;
}

void hlfsr64::keystream(void* out, std::size_t bytes) {
    u8* p = static_cast<u8*>(out);
    while (bytes >= 8) {
        u64 b = next(); p[0]=(u8)b; p[1]=(u8)(b>>8); p[2]=(u8)(b>>16); p[3]=(u8)(b>>24);
        p[4]=(u8)(b>>32); p[5]=(u8)(b>>40); p[6]=(u8)(b>>48); p[7]=(u8)(b>>56);
        p+=8; bytes-=8;
    }
    if (bytes > 0) { u64 b = next(); for (std::size_t i=0;i<bytes;i++) p[i]=(u8)(b>>(i*8)); }
}
