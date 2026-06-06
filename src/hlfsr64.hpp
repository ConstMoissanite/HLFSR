// hlfsr64.hpp — HLFSR-64 流密码核心
#pragma once
#include <cstdint>
#include <cstddef>

// 算法版本标识
#define HLFSR_VERSION     11
#define HLFSR_VARIANT     "V11-Uni"
#define HLFSR_MATRIX      8     // 8×8×8
#define HLFSR_LFSR_COUNT  8     // 8 条 Galois LFSR
#define HLFSR_MASK_BITS   8     // 8 位掩码选通
#define HLFSR_MATRIX_BYTES 64  // 512 bits
#define HLFSR_SEED_BYTES   64  // lfsr_seed (8×8, 无重叠)
#define HLFSR_IDX_BITS     9   // idx 范围 0-511
#define HLFSR_OUTPUT_BITS  64  // 输出宽度
// 先前版本: V1=ISA, V2=MF, V3=MV, V4=V8-Fib, V5=Galois, V6=Mask16, V7=Mask8, V8=V8-Mask

class hlfsr64 {
public:
    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    // matrix[64] = 8 faces × 8 rows × 1 byte = 512 bits
    // lfsr_seed[32]: same as before
    // idx_init: 0–511
    void init(const u8 key_material[64], u16 idx_init);
    u64  next();
    void keystream(void* out, std::size_t bytes);

private:
    u64 advance_lfsr(u8 mask);

    u8  m_matrix[64];
    u16 m_idx;
    u64 m_lfsr[8];

    static const u64 POLY[8];
};
