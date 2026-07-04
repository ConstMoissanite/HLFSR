#pragma once
#include <cstddef>
#include <cstdint>

#define HLFSR_VERSION 12
#define HLFSR_VARIANT "V12-MM"
#define HLFSR_MATRIX 8
#define HLFSR_LFSR_COUNT 8
#define HLFSR_MASK_BITS 8
#define HLFSR_MATRIX_BYTES 64
#define HLFSR_KEY_BYTES 64
#define HLFSR_IDX_BITS 9
#define HLFSR_OUTPUT_BITS 64

class hlfsr64
{
  public:
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    void init(const u8 key_material[64], u16 idx_init);
    u64 next();
    void next256(u64 out[4]);
    void keystream(void *out, std::size_t bytes);
    void keystream256(void *out, std::size_t bytes);

  private:
    u64 advance_lfsr(u8 mask, u16 step_idx);

    u8 m_matrix[64];
    u16 m_idx;
    u64 m_lfsr[8];

    static const u64 POLY[8];
};
