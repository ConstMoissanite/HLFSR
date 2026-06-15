// hlfsr64.cpp — HLFSR-64 流密码核心 (标量 / AVX2 / AVX-512)
#include "hlfsr64.hpp"
#include <cstring>

#if defined(__AVX512F__) && defined(__AVX512DQ__)
  #define HLFSR_SIMD 512
  #include <immintrin.h>
#elif defined(__AVX2__)
  #define HLFSR_SIMD 256
  #include <immintrin.h>
#endif

// 8 个 64 次本原多项式，权重 13--15
const hlfsr64::u64 hlfsr64::POLY[8] = {
    0x4800203343401101ULL, 0x0416001300480117ULL, 0x58000C0310100803ULL, 0x00A0090940648023ULL,
    0x484302010C340003ULL, 0x801D001006412901ULL, 0x04429288080A1021ULL, 0x2000022052D01213ULL,
};

static inline hlfsr64::u8 ct_eq8(hlfsr64::u8 a, hlfsr64::u8 b) {
    hlfsr64::u8 d = a ^ b;
    hlfsr64::u8 is_nz = (d | (hlfsr64::u8)((~d + 1))) >> 7;
    return (hlfsr64::u8)(0 - (is_nz ^ 1));
}
static inline hlfsr64::u8 rol8(hlfsr64::u8 x, int n) {
    n &= 7; return (hlfsr64::u8)((x << n) | (x >> (8 - n)));
}

void hlfsr64::init(const u8 km[64], u16 idx_init) {
    const u64* p = (const u64*)km;
    if ((p[0]|p[1]|p[2]|p[3]|p[4]|p[5]|p[6]|p[7]) == 0) return;
    m_idx = idx_init & 0x1FF;

    u64 pool[24];
    for (int i = 0; i < 8; i++) {
        u64 val = 0;
        for (int j = 0; j < 8; j++) val |= (u64)km[i * 8 + j] << (j * 8);
        pool[i]      = val;
        pool[i+8]    = (val << 23) | (val >> 41);
        pool[i+16]   = (val << 41) | (val >> 23);
    }
    u64 mm[8], ml[8];
    for (int i = 0; i < 8; i++) {
        mm[i] = pool[i]                * pool[((i+1)&7)+8]  * pool[((i+2)&7)+16];
        ml[i] = pool[(i+4)&7]          * pool[((i+5)&7)+8]  * pool[((i+6)&7)+16];
    }
    for (int col = 0; col < 8; col++) {
        u8 cm[8], cl[8];
        u8* mb = (u8*)mm, *lb = (u8*)ml;
        for (int r = 0; r < 8; r++) { cm[r] = mb[r*8+col]; cl[r] = lb[r*8+col]; }
        auto xt = [](u8 b){ return (u8)((b<<1)^((b>>7)?0x1B:0)); };
        auto gm = [&](u8 a, u8 b){ u8 r=0; for(int k=0;k<8;k++){if(b&1)r^=a;a=xt(a);b>>=1;} return r; };
        for (int r = 0; r < 8; r++) {
            u8 sm=0, sl=0;
            for (int j = 0; j < 8; j++) {
                int d = (j-r)&7;
                u8 c = (d==0)?2 : (d==1)?3 : 1;
                sm ^= gm(c, cm[j]); sl ^= gm(c, cl[j]);
            }
            mb[r*8+col]=sm; lb[r*8+col]=sl;
        }
    }
    std::memcpy(m_matrix, mm, 64);
    std::memcpy(m_lfsr,   ml, 64);
    for (int i = 0; i < 64; i++) next();
}

// ============================================================
// advance_lfsr
// ============================================================
#if HLFSR_SIMD == 256

hlfsr64::u64 hlfsr64::advance_lfsr(u8 mask_byte, u16 step_idx) {
    __m256i lfsr01 = _mm256_loadu_si256((const __m256i*)&m_lfsr[0]);
    __m256i lfsr23 = _mm256_loadu_si256((const __m256i*)&m_lfsr[4]);
    __m256i poly01 = _mm256_loadu_si256((const __m256i*)&POLY[0]);
    __m256i poly23 = _mm256_loadu_si256((const __m256i*)&POLY[4]);

    __m256i one  = _mm256_set1_epi64x(1);
    __m256i msb01 = _mm256_srli_epi64(lfsr01, 63);
    __m256i msb23 = _mm256_srli_epi64(lfsr23, 63);
    __m256i sh01  = _mm256_sllv_epi64(lfsr01, one);
    __m256i sh23  = _mm256_sllv_epi64(lfsr23, one);
    lfsr01 = _mm256_xor_si256(sh01, _mm256_and_si256(poly01,
             _mm256_sub_epi64(_mm256_setzero_si256(), msb01)));
    lfsr23 = _mm256_xor_si256(sh23, _mm256_and_si256(poly23,
             _mm256_sub_epi64(_mm256_setzero_si256(), msb23)));

    _mm256_storeu_si256((__m256i*)&m_lfsr[0], lfsr01);
    _mm256_storeu_si256((__m256i*)&m_lfsr[4], lfsr23);

    __m256i mbits01 = _mm256_set_epi64x(
        0ULL - ((mask_byte >> 3) & 1), 0ULL - ((mask_byte >> 2) & 1),
        0ULL - ((mask_byte >> 1) & 1), 0ULL - ((mask_byte >> 0) & 1));
    __m256i mbits23 = _mm256_set_epi64x(
        0ULL - ((mask_byte >> 7) & 1), 0ULL - ((mask_byte >> 6) & 1),
        0ULL - ((mask_byte >> 5) & 1), 0ULL - ((mask_byte >> 4) & 1));

    __m256i vx = _mm256_xor_si256(
        _mm256_and_si256(lfsr01, mbits01),
        _mm256_and_si256(lfsr23, mbits23));

    // 纯寄存器水平 XOR 归约，避免 store-forwarding 失速
    __m128i vx_lo  = _mm256_castsi256_si128(vx);
    __m128i vx_hi  = _mm256_extracti128_si256(vx, 1);
    __m128i vx_2   = _mm_xor_si128(vx_lo, vx_hi);
    u64 vx_scalar = _mm_extract_epi64(vx_2, 0) ^ _mm_extract_epi64(vx_2, 1);

    u64 mz = 0ULL - (ct_eq8(mask_byte, 0) & 1);
    u64 raw = (vx_scalar & ~mz) | (m_lfsr[step_idx & 7] & mz);
    return raw * 0x9E3779B97F4A7C15ULL;
}

#elif HLFSR_SIMD == 512

hlfsr64::u64 hlfsr64::advance_lfsr(u8 mask_byte, u16 step_idx) {
    __m512i lfsr = _mm512_loadu_si512((const __m512i*)m_lfsr);
    __m512i poly = _mm512_loadu_si512((const __m512i*)POLY);

    __m512i one = _mm512_set1_epi64(1);
    __m512i msb = _mm512_srli_epi64(lfsr, 63);
    __m512i sh  = _mm512_sllv_epi64(lfsr, one);
    lfsr = _mm512_xor_si512(sh, _mm512_and_si512(poly,
           _mm512_sub_epi64(_mm512_setzero_si512(), msb)));

    _mm512_storeu_si512((__m512i*)m_lfsr, lfsr);

    u64 mbits[8];
    for (int i = 0; i < 8; i++)
        mbits[i] = 0ULL - ((mask_byte >> i) & 1);
    __m512i mb = _mm512_set_epi64(mbits[7], mbits[6], mbits[5], mbits[4],
                                   mbits[3], mbits[2], mbits[1], mbits[0]);

    __m512i vx = _mm512_and_si512(lfsr, mb);

    // 水平 XOR 归约: 8 lanes → 1
    __m256i vx_lo = _mm512_castsi512_si256(vx);
    __m256i vx_hi = _mm512_extracti64x4_epi64(vx, 1);
    __m256i vx_4  = _mm256_xor_si256(vx_lo, vx_hi);
    __m128i vx_2  = _mm_xor_si128(_mm256_castsi256_si128(vx_4),
                                   _mm256_extracti128_si256(vx_4, 1));
    u64 vx_scalar = _mm_extract_epi64(vx_2, 0) ^ _mm_extract_epi64(vx_2, 1);

    u64 mz = 0ULL - (ct_eq8(mask_byte, 0) & 1);
    u64 raw = (vx_scalar & ~mz) | (m_lfsr[step_idx & 7] & mz);
    return raw * 0x9E3779B97F4A7C15ULL;
}

#else
// 标量 fallback

hlfsr64::u64 hlfsr64::advance_lfsr(u8 mask_byte, u16 step_idx) {
    u64 vx = 0;
    for (int i = 0; i < 8; i++) {
        u64 s   = m_lfsr[i];
        u64 msb = s >> 63;
        m_lfsr[i] = (s << 1) ^ (POLY[i] & (0ULL - msb));
        vx ^= m_lfsr[i] & (0ULL - ((u64)(mask_byte >> i) & 1ULL));
    }
    u64 mz = 0ULL - (ct_eq8(mask_byte, 0) & 1);
    u64 raw = (vx & ~mz) | (m_lfsr[step_idx & 7] & mz);
    return raw * 0x9E3779B97F4A7C15ULL;
}

#endif // HLFSR_SIMD

// ============================================================
// next + keystream (与 SIMD 无关，复用 advance_lfsr)
// ============================================================
hlfsr64::u64 hlfsr64::next() {
    u8  face = (u8)(m_idx & 7);
    u8  row  = (u8)((m_idx >> 3) & 7);
    u8  col  = (u8)((m_idx >> 6) & 7);
    u8  ba   = face * 8 + row;
    u8  curbyte = m_matrix[ba];
    u8  curbit  = (curbyte >> col) & 1;

    u8 mask = curbyte;
    u8 p    = (u8)((m_idx >> 6) & 7);

    u64 raw    = advance_lfsr(mask, m_idx);
    u64 output = raw ^ (0ULL - curbit);

    m_matrix[ba] ^= (u8)(((raw & 0xFF) * 0xBF58476D1CE4E5B9ULL) & 0xFF);
    m_matrix[ba]  = rol8(m_matrix[ba], p);

    m_idx = (m_idx + 1) & 0x1FF;
    return output;
}

void hlfsr64::keystream(void* out, std::size_t bytes) {
    u8* p = static_cast<u8*>(out);

    while (bytes >= 64) {
        u8 pv[8], cb[8], ba8[8], mask[8];

        for (int i = 0; i < 8; i++) {
            u16 t_idx = (m_idx + (u16)i) & 0x1FF;
            u8 f = (u8)(t_idx & 7), r = (u8)((t_idx >> 3) & 7);
            u8 addr = f * 8 + r;
            ba8[i] = addr;
            mask[i] = m_matrix[addr];
            pv[i]   = (u8)((t_idx >> 6) & 7);
            cb[i]   = (mask[i] >> pv[i]) & 1;
        }

        u64 raw[8];
        for (int i = 0; i < 8; i++) raw[i] = advance_lfsr(mask[i], m_idx + i);

        for (int i = 0; i < 8; i++) {
            u64 b = raw[i] ^ (0ULL - cb[i]);
            p[0]=(u8)b; p[1]=(u8)(b>>8); p[2]=(u8)(b>>16); p[3]=(u8)(b>>24);
            p[4]=(u8)(b>>32); p[5]=(u8)(b>>40); p[6]=(u8)(b>>48); p[7]=(u8)(b>>56);
            p += 8;
        }

        for (int i = 0; i < 8; i++) {
            u8 addr = ba8[i];
            m_matrix[addr] ^= (u8)(((raw[i] & 0xFF) * 0xBF58476D1CE4E5B9ULL) & 0xFF);
            m_matrix[addr]  = rol8(m_matrix[addr], pv[i] & 7);
        }

        m_idx = (m_idx + 8) & 0x1FF;
        bytes -= 64;
    }

    while (bytes >= 8) {
        u64 b = next(); p[0]=(u8)b; p[1]=(u8)(b>>8); p[2]=(u8)(b>>16); p[3]=(u8)(b>>24);
        p[4]=(u8)(b>>32); p[5]=(u8)(b>>40); p[6]=(u8)(b>>48); p[7]=(u8)(b>>56);
        p+=8; bytes-=8;
    }
    if (bytes > 0) { u64 b = next(); for (std::size_t i=0;i<bytes;i++) p[i]=(u8)(b>>(i*8)); }
}
