// hlfsr64.hpp — HLFSR-64 MF (Matrix-Feedback) 流密码核心
#pragma once
#include <cstdint>
#include <cstddef>

class hlfsr64 {
public:
    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u64 = std::uint64_t;

    void init(const u8 matrix[32], const u8 lfsr_seed[32], u8 idx_init);
    u64  next();
    void keystream(void* out, std::size_t bytes);

private:
    u64 advance_lfsr(u8 s0, u8 s1, u8 s2);

    u8  m_matrix[32];  // 16×16 bits, byte-addressed
    u8  m_idx;
    u64 m_lfsr[16];

    static const u64 POLY[16];
};
