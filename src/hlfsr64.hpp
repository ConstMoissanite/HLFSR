// hlfsr64.hpp — HLFSR-64 流密码核心
// 零平台依赖，仅需 C++11 以上标准库
#pragma once
#include <cstdint>
#include <cstddef>

class hlfsr64 {
public:
    using u8  = std::uint8_t;
    using u64 = std::uint64_t;

    // 初始化：三个参数均由外部 KDF 提供
    //   bitmap[32]   — 256 位内部状态（数据+指令存储器）
    //   lfsr_seed[32] — LFSR 初态种子（步长2滑动窗口填入16条LFSR）
    //   idx_init      — 起始比特游标（0~255）
    void init(const u8 bitmap[32], const u8 lfsr_seed[32], u8 idx_init);

    // 单步：产出 64 位密钥流，执行一步指令，推进状态
    u64 next();

    // 批量密钥流
    void keystream(void* out, std::size_t bytes);

private:
    // 推进全部 16 条 LFSR，双路选通 + 乘性混合
    u64 advance_lfsr(u8 s0, u8 s1);

    u8  m_bitmap[32];   // 256 位内部状态
    u8  m_idx;          // 8 位比特游标（0~255）
    u64 m_lfsr[16];     // 16 条 64 位 LFSR

    // 16 个 64 次本原多项式（权重 7），bit i = x^i，x^64 隐式
    static const u64 POLY[16];
};
