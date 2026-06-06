# HLFSR-64 技术规格书

> V8-Mask · 8×8×8 (512b) · 8×64b Galois · 8b mask · 1.2 GB/s

## 1. 设计概述

HLFSR-64 是密钥驱动的流密码核心。512 位 8×8×8 矩阵 + 8 条 64 位 Galois LFSR。每步取当前面一行 (8 位) 作为 LFSR 选通掩码，8 条 LFSR 全体推进，掩码 XOR 后经 64×64 乘性混合，低 8 位 XOR 回填当前行，行内循环移位。面隔离实现 8 步批处理。

## 2. 顶层参数

| 参数 | 值 |
|------|-----|
| 矩阵 | 8×8×8 = 512 bits (64 bytes) |
| LFSR | 8 × 64-bit Galois |
| 输出 | 64 bits/step |
| 选通 | 8-bit mask = 矩阵一行 (bit 0 保底置 1) |
| 移位 p | col 位置 (3 bits) |
| idx | 9 bits (0–511), 每步 +1 |
| 乘性常数 | K = 0x9E3779B97F4A7C15 |
| 吞吐 | 1.2 GB/s (纯 C++) |
| 多项式 | 8 个 64 次本原多项式 |

## 3. 坐标编码

```
face = idx & 7          // 0–7, 面间隔离
row  = (idx >> 3) & 7   // 0–7
col  = (idx >> 6) & 7   // 0–7
addr = face * 8 + row
```

## 4. 每步操作

```
1. face=idx&7, row=(idx>>3)&7, col=(idx>>6)&7
2. mask = matrix[face*8+row] | 1
3. p = col
4. curbit = (matrix[addr] >> col) & 1
5. ∀i: LFSR[i] = (LFSR[i]<<1) ^ (POLY[i] & -(LFSR[i]>>63))
6. raw = (⊕ mask_bit_i · LFSR[i]) × K
7. output = raw ^ {64{curbit}}
8. matrix[addr] ^= (u8)(raw & 0xFF)
9. matrix[addr] = ROL8(matrix[addr], p)
10. idx = (idx + 1) & 0x1FF
```

## 5. 初始化

- `matrix[64]` 和 `lfsr_seed[32]` 由外部 KDF 派生
- LFSR[i] = lfsr_seed[(i×4) mod 32 .. (i×4+7) mod 32], i=0..7
- `idx_init` 外部传入, & 0x1FF

## 6. 版本历史

| Ver | 代号 | 结构 | LFSR | 选通 | MB/s |
|-----|------|------|------|------|------|
| V1 | ISA | 256b bitmap | 16 Fib | 3-sel ct_eq8 | 115 |
| V2 | MF | 16×16 | 16 Fib | 3-sel ct_eq8 | 238 |
| V3 | MV | 16×16 batch | 16 Fib | 3-sel ct_eq8 | 266 |
| V4 | V8-Fib | 8×8×8 | 16 Fib | 3-sel ct_eq8 | 271 |
| V5 | Galois | 8×8×8 | 16 Galois | 3-sel ct_eq8 | 358 |
| V6 | Mask16 | 8×8×8 | 16 Galois | 16b mask | 790 |
| V7 | Mask8 | 8×8×8 | 16 Galois | 16b mask (!) | 790 |
| **V8** | **V8-Mask** | **8×8×8** | **8 Galois** | **8b mask** | **1200** |
