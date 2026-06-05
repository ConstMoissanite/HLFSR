// hlfsr64.hpp — HLFSR-64 V8 流密码核心 (8×8×8 矩阵)
#pragma once
#include <cstdint>
#include <cstddef>

class hlfsr64 {
public:
    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    // matrix[64] = 8 faces × 8 rows × 1 byte = 512 bits
    // lfsr_seed[32]: same as before
    // idx_init: 0–511
    void init(const u8 matrix[64], const u8 lfsr_seed[32], u16 idx_init);
    u64  next();
    void keystream(void* out, std::size_t bytes);

private:
    u64 advance_lfsr(u8 mask);

    u8  m_matrix[64];  // 8 faces × 8 rows × 8 bits
    u16 m_idx;         // 9 bits (0–511)
    u64 m_lfsr[8];     // 8 × 64-bit (匹配 8×8×8 结构)

    static const u64 POLY[8];
};
