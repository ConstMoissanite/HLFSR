// hlfsr64.cpp — HLFSR-64 流密码核心实现
// 常数时间（无秘密依赖分支），零平台依赖（仅 C++11 标准库）
#include "hlfsr64.hpp"
#include <cstring>

// ============================================================
// 16 个 64 次本原多项式（权重 7），参见 specs/polynomials.md
// bit i = 1 表示 x^i 项存在，x^64 项隐式
// ============================================================
const hlfsr64::u64 hlfsr64::POLY[16] = {
    0x0054010000020001ULL,  // [ 0] x^64 + x^54 + x^52 + x^50 + x^40 + x^17 + 1
    0x0080000004200083ULL,  // [ 1] x^64 + x^55 + x^26 + x^21 + x^7  + x    + 1
    0x0100000020040481ULL,  // [ 2] x^64 + x^56 + x^29 + x^18 + x^10 + x^7  + 1
    0x0000088040008005ULL,  // [ 3] x^64 + x^43 + x^39 + x^30 + x^15 + x^2  + 1
    0x0804000004200101ULL,  // [ 4] x^64 + x^59 + x^50 + x^26 + x^21 + x^8  + 1
    0x0000900008400009ULL,  // [ 5] x^64 + x^47 + x^44 + x^27 + x^22 + x^3  + 1
    0x8000110004001001ULL,  // [ 6] x^64 + x^63 + x^44 + x^40 + x^26 + x^12 + 1
    0x2000000000820141ULL,  // [ 7] x^64 + x^61 + x^23 + x^17 + x^8  + x^6  + 1
    0x2000004000430001ULL,  // [ 8] x^64 + x^61 + x^38 + x^22 + x^17 + x^16 + 1
    0x0002080800600001ULL,  // [ 9] x^64 + x^49 + x^43 + x^35 + x^22 + x^21 + 1
    0x00020A0000400009ULL,  // [10] x^64 + x^49 + x^43 + x^41 + x^22 + x^3  + 1
    0x000230000C000001ULL,  // [11] x^64 + x^49 + x^45 + x^44 + x^27 + x^26 + 1
    0x4080000040080021ULL,  // [12] x^64 + x^62 + x^55 + x^30 + x^19 + x^5  + 1
    0x0080004400210001ULL,  // [13] x^64 + x^55 + x^38 + x^34 + x^21 + x^16 + 1
    0x0100020800008081ULL,  // [14] x^64 + x^56 + x^41 + x^35 + x^15 + x^7  + 1
    0x0210000500080001ULL,  // [15] x^64 + x^57 + x^52 + x^34 + x^32 + x^19 + 1
};

// ============================================================
// 常数时间工具
// ============================================================

// 64 位奇偶校验（__builtin_parityl → 单条 popcnt）
static inline hlfsr64::u64 parity64(hlfsr64::u64 x) {
    return (hlfsr64::u64)__builtin_parityll(x) & 1ULL;
}

// 常数时间字节相等：返回 0xFF 若 a==b，否则 0x00
static inline hlfsr64::u8 ct_eq8(hlfsr64::u8 a, hlfsr64::u8 b) {
    hlfsr64::u8 diff = a ^ b;
    hlfsr64::u8 is_nonzero = (diff | (hlfsr64::u8)(-(std::int8_t)diff)) >> 7;
    return (hlfsr64::u8)(0 - (is_nonzero ^ 1));
}

// ============================================================
// 初始化
// ============================================================
void hlfsr64::init(const u8 bm[32], const u8 seed[32], u8 idx_init) {
    std::memcpy(m_bitmap, bm, 32);
    m_idx = idx_init;

    // 滑动窗口：LFSR[i] 取 seed[(i*2) mod 32 .. (i*2+7) mod 32]
    for (int i = 0; i < 16; i++) {
        u64 val = 0;
        u8  base = (u8)(i * 2) & 31;
        for (int j = 0; j < 8; j++) {
            u8 b = seed[(base + j) & 31];
            val |= (u64)b << (j * 8);
        }
        m_lfsr[i] = val;
    }
}

// ============================================================
// LFSR 推进 (Fibonacci 结构, 三路选通 + 乘性混合)
// ============================================================
hlfsr64::u64 hlfsr64::advance_lfsr(u8 s0, u8 s1, u8 s2) {
    // 预计算掩码表，循环内只查表不复算 ct_eq8
    u64 sm[16], om[16], pm[16];
    for (int i = 0; i < 16; i++) {
        sm[i] = 0ULL - (ct_eq8(s0, (u8)i) & 1);
        om[i] = 0ULL - (ct_eq8(s1, (u8)i) & 1);
        pm[i] = 0ULL - (ct_eq8(s2, (u8)i) & 1);
    }
    u64 v0 = 0, v1 = 0, v2 = 0;
    for (int i = 0; i < 16; i++) {
        u64 s   = m_lfsr[i];
        u64 tap = s & (POLY[i] & 0x7FFFFFFFFFFFFFFFULL);
        m_lfsr[i] = (s << 1) | parity64(tap);
        u64 v = m_lfsr[i];
        v0 |= v & sm[i];
        v1 |= v & om[i];
        v2 |= v & pm[i];
    }
    return (v0 ^ v1 ^ v2) * 0x9E3779B97F4A7C15ULL;
}

// ============================================================
// 单步：产出 64 位密钥流 + 执行指令 + 推进状态
// ============================================================
hlfsr64::u64 hlfsr64::next() {
    // ---- 1. 取 curbit, sel, curbyte ----
    u8 byte_idx = m_idx >> 3;
    u8 bit_pos  = m_idx & 7;
    u8 curbyte  = m_bitmap[byte_idx];
    u8 curbit   = (curbyte >> bit_pos) & 1;
    u8 opcode   = curbyte >> 4;
    u8 param    = curbyte & 0x0F;
    u8 sel      = (curbyte >> bit_pos) & 0x0F;

    // ---- 2. 三路选通 (sel + opcode + param) + 乘性混合 ----
    u64 raw = advance_lfsr(sel, opcode, param);

    // ---- 3. 输出 = raw XOR {64{curbit}} ----
    u64 curbit_mask = 0ULL - curbit;
    u64 output = raw ^ curbit_mask;

    // ---- 4. 指令执行 ----

    // 操作目标
    u8 target_idx = (byte_idx + 1) & 31;
    u8 self_idx   = byte_idx;

    // 符号扩展（CurB / NotB 共用）
    // param: 0~7→正, 8~15→负(-8~-1)
    std::int8_t offset = (param & 8) ? (std::int8_t)(param | 0xF0)
                                     : (std::int8_t)param;
    u8 remote_idx = (byte_idx + offset) & 31;

    // JmpBL / JmpBR 的半区选择
    u8 halfL = curbyte >> 7;       // bit7
    u8 halfR = curbyte & 1;        // bit0

    // 旧值备份
    u8 old_target = m_bitmap[target_idx];
    u8 old_self   = m_bitmap[self_idx];
    u8 old_remote = m_bitmap[remote_idx];

    // ---- 每条指令的 bitmap 写入值（16×3 目标位） ----
    // vt[i] = 指令 i 对 target_idx 的写入值（不写入=旧值）
    // vs[i] = 指令 i 对 self_idx   的写入值
    // vr[i] = 指令 i 对 remote_idx 的写入值

    u8 vt[16], vs[16], vr[16];

    // IncB (0): target += param
    vt[0] = old_target + param;
    vs[0] = old_self;
    vr[0] = old_remote;

    // Copb (1): target ^= (1 << param)
    vt[1] = old_target ^ (u8)(1u << param);
    vs[1] = old_self;
    vr[1] = old_remote;

    // DecB (2): target -= param
    vt[2] = old_target - param;
    vs[2] = old_self;
    vr[2] = old_remote;

    // CopO (3): target ^= (1 << (7-param))
    vt[3] = old_target ^ (u8)(1u << (7 - param));
    vs[3] = old_self;
    vr[3] = old_remote;

    // StpB (4): idx步进, target ^= output[7:0]
    vt[4] = old_target ^ (u8)output; vs[4]=old_self; vr[4]=old_remote;

    // Stpb (5): idx步进, target ^= output[7:0]
    vt[5] = old_target ^ (u8)output; vs[5]=old_self; vr[5]=old_remote;

    // RStpB (6): idx步退, target ^= output[7:0]
    vt[6] = old_target ^ (u8)output; vs[6]=old_self; vr[6]=old_remote;

    // RStpb (7): idx步退, target ^= output[7:0]
    vt[7] = old_target ^ (u8)output; vs[7]=old_self; vr[7]=old_remote;

    // JmpBL (8): 跳转, target ^= output[7:0]
    vt[8] = old_target ^ (u8)output; vs[8]=old_self; vr[8]=old_remote;

    // JmpBR (9): 跳转, target ^= output[7:0]
    vt[9] = old_target ^ (u8)output; vs[9]=old_self; vr[9]=old_remote;

    // XorB (10): target ^= param
    vt[10] = old_target ^ param;
    vs[10] = old_self;
    vr[10] = old_remote;

    // AndB (11): target &= param
    vt[11] = old_target & param;
    vs[11] = old_self;
    vr[11] = old_remote;

    // OrB (12): target |= param
    vt[12] = old_target | param;
    vs[12] = old_self;
    vr[12] = old_remote;

    // SwapB (13): target nibble swap
    vt[13] = (old_target << 4) | (old_target >> 4);
    vs[13] = old_self;
    vr[13] = old_remote;

    // CurB (14): self ^= bitmap[remote_idx]
    vs[14] = old_self ^ old_remote;
    vt[14] = old_target;
    vr[14] = old_remote;

    // NotB (15): remote = ~old_remote
    vr[15] = ~old_remote;
    vt[15] = old_target;
    vs[15] = old_self;

    // ---- 各指令的 idx 输出值 ----
    u8 ni[16];
    ni[0]  = (m_idx + 1) & 0xFF;                         // IncB:  默认 +1
    ni[1]  = (m_idx + 1) & 0xFF;                         // Copb:  默认 +1
    ni[2]  = (m_idx + 1) & 0xFF;                         // DecB:  默认 +1
    ni[3]  = (m_idx + 1) & 0xFF;                         // CopO:  默认 +1
    ni[4]  = (m_idx + param * 8) & 0xFF;                 // StpB
    ni[5]  = (m_idx + param) & 0xFF;                     // Stpb
    ni[6]  = (m_idx - param * 8) & 0xFF;                 // RStpB
    ni[7]  = (m_idx - param) & 0xFF;                     // RStpb
    ni[8]  = (halfL * 128u + param * 8u) & 0xFF;         // JmpBL
    ni[9]  = (halfR * 128u + param * 8u) & 0xFF;         // JmpBR
    ni[10] = (m_idx + 1) & 0xFF;                         // XorB:  默认 +1
    ni[11] = (m_idx + 1) & 0xFF;                         // AndB:  默认 +1
    ni[12] = (m_idx + 1) & 0xFF;                         // OrB:   默认 +1
    ni[13] = (m_idx + 8) & 0xFF;                         // SwapB: 强制 +8
    ni[14] = (m_idx + 1) & 0xFF;                         // CurB:  默认 +1
    ni[15] = (m_idx + 8) & 0xFF;                         // NotB:  强制 +8

    // ---- 掩码选择：预计算 opcode 掩码表，循环内只查表 ----
    u64 omask[16];
    for (int i = 0; i < 16; i++)
        omask[i] = 0ULL - (ct_eq8(opcode, (u8)i) & 1);

    u64 merged_target = 0, merged_self = 0, merged_remote = 0, merged_idx = 0;
    for (int i = 0; i < 16; i++) {
        u64 m = omask[i];
        merged_target |= ((u64)vt[i] & m);
        merged_self   |= ((u64)vs[i] & m);
        merged_remote |= ((u64)vr[i] & m);
        merged_idx    |= ((u64)ni[i] & m);
    }

    // 写回 bitmap（3 个可能目标）
    m_bitmap[target_idx] = (u8)merged_target;
    m_bitmap[self_idx]   = (u8)merged_self;
    m_bitmap[remote_idx] = (u8)merged_remote;

    // 更新 idx
    m_idx = (u8)merged_idx;

    return output;
}

// ============================================================
// 批量密钥流
// ============================================================
void hlfsr64::keystream(void* out, std::size_t bytes) {
    u8* p = static_cast<u8*>(out);

    // 整块 8 字节
    while (bytes >= 8) {
        u64 block = next();
        p[0] = (u8)(block);
        p[1] = (u8)(block >> 8);
        p[2] = (u8)(block >> 16);
        p[3] = (u8)(block >> 24);
        p[4] = (u8)(block >> 32);
        p[5] = (u8)(block >> 40);
        p[6] = (u8)(block >> 48);
        p[7] = (u8)(block >> 56);
        p += 8;
        bytes -= 8;
    }

    // 尾部不足 8 字节（分支仅依赖公开的输出长度）
    if (bytes > 0) {
        u64 block = next();
        for (std::size_t i = 0; i < bytes; i++) {
            p[i] = (u8)(block >> (i * 8));
        }
    }
}
