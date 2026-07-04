// hlfsr64.cpp — HLFSR-64 流密码核心
#include "hlfsr64.hpp"
#include <cstring>

// 8 个 64 次本原多项式，权重 13--15
const hlfsr64::u64 hlfsr64::POLY[8] = {
    0x4800203343401101ULL, 0x0416001300480117ULL,
    0x58000C0310100803ULL,
    0x00A0090940648023ULL,
    0x484302010C340003ULL, 0x801D001006412901ULL, 0x04429288080A1021ULL, 0x2000022052D01213ULL,
};

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
    // idx 高 7 位搅拌: km 键相关, 消除全局仿射偏移
    {
        u8 extra = (u8)(idx_init >> 9);
        for (int i = 0; i < 64; i++)
            m_matrix[i] ^= (u8)(extra * km[i] & 0x7F);
    }
    for (int i = 0; i < 64; i++) next();
}

// ============================================================
// advance_lfsr: 全 LFSR XOR + vx 自旋转 + mask 乘法调味
// ============================================================
hlfsr64::u64 hlfsr64::advance_lfsr(u8 mask_byte, u16 step_idx) {
    u64 vx = 0;
    for (int i = 0; i < 8; i++) {
        u64 s   = m_lfsr[i];
        u64 msb = s >> 63;
        m_lfsr[i] = (s << 1) ^ (POLY[i] & (0ULL - msb));
        vx ^= m_lfsr[i];
    }
    vx ^= (vx << 33) | (vx >> 31);
    u64 mk = ((u64)mask_byte * 0xBF58476D1CE4E5B9ULL) | 1;
    return (vx * mk) * 0x9E3779B97F4A7C15ULL;
}

// ============================================================
// next + keystream
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

    // 16b 反馈: 低 8 位回填当前地址，高 8 位打到相邻面同行
    u64 fb = (raw & 0xFFFF) * 0xBF58476D1CE4E5B9ULL;
    m_matrix[ba] ^= (u8)(fb & 0xFF);
    m_matrix[ba]  = rol8(m_matrix[ba], p);
    u8 nb = (u8)(((face ^ 1) & 7) * 8 + row);
    m_matrix[nb] ^= (u8)(fb >> 8);

    m_idx = (m_idx + 1) & 0x1FF;
    return output;
}

// 256-bit 输出: raw × ROTL33(K₃ × TV[i]), TV[i] = LFSR × matrix 交叉 + 7-bit 调味
void hlfsr64::next256(u64 out[4]) {
    u8  face = (u8)(m_idx & 7);
    u8  row  = (u8)((m_idx >> 3) & 7);
    u8  col  = (u8)((m_idx >> 6) & 7);
    u8  ba   = face * 8 + row;
    u8  curbyte = m_matrix[ba];
    u8  curbit  = (curbyte >> col) & 1;

    u8 mask = curbyte;
    u8 p    = (u8)((m_idx >> 6) & 7);

    u64 raw = advance_lfsr(mask, m_idx);

    // TV: 1 extra multiplier, 4 lanes — ROTL derive + LFSR XOR break
    u64 tv = m_lfsr[face] * (u64)m_matrix[ba] * ((mask & 0x7F) | 1);
    u64 t  = tv * 0x94D049BB133111EBULL;           // K₃
    t = (t << 33) | (t >> 31);                     // ROTL33
    u64 c  = 0ULL - curbit;
    out[0] = (raw * t) ^ c ^ m_lfsr[face];
    out[1] = (raw * ((t << 17) | (t >> 47))) ^ c ^ m_lfsr[(face+2)&7];
    out[2] = (raw * ((t << 34) | (t >> 30))) ^ c ^ m_lfsr[(face+4)&7];
    out[3] = (raw * ((t << 51) | (t >> 13))) ^ c ^ m_lfsr[(face+6)&7];

    // 反馈: 16b 回填 (同 next)
    u64 fb = (raw & 0xFFFF) * 0xBF58476D1CE4E5B9ULL;
    m_matrix[ba] ^= (u8)(fb & 0xFF);
    m_matrix[ba]  = rol8(m_matrix[ba], p);
    u8 nb = (u8)(((face ^ 1) & 7) * 8 + row);
    m_matrix[nb] ^= (u8)(fb >> 8);

    m_idx = (m_idx + 1) & 0x1FF;
}

void hlfsr64::keystream(void* out, std::size_t bytes) {
    u8* p = static_cast<u8*>(out);

    while (bytes >= 64) {
        u8 pv[8], cb[8], ba8[8], mask[8], nb8[8];

        for (int i = 0; i < 8; i++) {
            u16 t_idx = (m_idx + (u16)i) & 0x1FF;
            u8 f = (u8)(t_idx & 7), r = (u8)((t_idx >> 3) & 7);
            u8 addr = f * 8 + r;
            ba8[i] = addr;
            mask[i] = m_matrix[addr];
            nb8[i]  = (u8)(((f ^ 1) & 7) * 8 + r);
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
            u64 fb = (raw[i] & 0xFFFF) * 0xBF58476D1CE4E5B9ULL;
            m_matrix[addr] ^= (u8)(fb & 0xFF);
            m_matrix[addr]  = rol8(m_matrix[addr], pv[i] & 7);
            m_matrix[nb8[i]] ^= (u8)(fb >> 8);
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

// 256-bit 批量密钥流 (next256 循环)
void hlfsr64::keystream256(void* out, std::size_t bytes) {
    u8* p = static_cast<u8*>(out);
    while (bytes >= 32) {
        u64 o[4];
        next256(o);
        for (int i = 0; i < 4; i++) {
            u64 w = o[i];
            p[0]=(u8)w; p[1]=(u8)(w>>8); p[2]=(u8)(w>>16); p[3]=(u8)(w>>24);
            p[4]=(u8)(w>>32); p[5]=(u8)(w>>40); p[6]=(u8)(w>>48); p[7]=(u8)(w>>56);
            p += 8;
        }
        bytes -= 32;
    }
    if (bytes > 0) {
        u64 o[4]; next256(o);
        u8* tail = (u8*)o;
        for (std::size_t i = 0; i < bytes; i++) p[i] = tail[i];
    }
}
