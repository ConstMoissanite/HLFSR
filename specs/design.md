# HLFSR-64 技术规格书

> V10-Idx · 8×8×8 (512b) · 8×64b Galois · 8b mask · mask=0→LFSR[idx&7] · 1.24 GB/s

## 1. 设计概述

HLFSR-64 是密钥驱动的流密码核心。512 位 8×8×8 矩阵 + 8 条 64 位 Galois LFSR。每步取当前面一行 (8 位) 作为 LFSR 选通掩码。若 mask≠0，XOR 选中 LFSR 输出；若 mask=0，回退为 LFSR[idx&7] (idx 低 3 位自然轮转)。结果经 64×64 乘性混合，低 8 位 XOR 回填当前行，行内循环移位。

## 2. 顶层参数

| 参数 | 值 |
|------|-----|
| 矩阵 | 8×8×8 = 512 bits (64 bytes) |
| LFSR | 8 × 64-bit Galois, 权重 13–15 |
| 输出 | 64 bits/step |
| 选通 | 8-bit mask = 矩阵一行 |
| mask=0 | 回退 LFSR[idx & 7] (idx 低 3 位轮转) |
| 移位 p | col (3 bits) |
| idx | 9 bits (0–511), 每步 +1 |
| 乘性常数 | K = 0x9E3779B97F4A7C15 |
| 吞吐 | 1.24 GB/s (NIST 50/50, G3 0/32) |

## 3. 坐标编码

```
face = idx & 7           // 面隔离, 连续 8 步不同面
row  = (idx >> 3) & 7
col  = (idx >> 6) & 7
addr = face * 8 + row    // 0–63
```

## 4. 每步操作

```
1. face=idx&7, row=(idx>>3)&7, col=(idx>>6)&7
2. mask = matrix[face*8+row]
3. p = col, curbit = (matrix[addr] >> col) & 1
4. ∀i: LFSR[i] = (LFSR[i]<<1) ^ (POLY[i] & -(LFSR[i]>>63))
5. vx = ⊕(mask[i]·LFSR[i])
6. raw = mask ? (vx × K) : (LFSR[idx&7] × K)  // mask=0→idx fallback
7. output = raw ^ {64{curbit}}
8. matrix[addr] ^= raw[7:0]
9. matrix[addr] = ROL8(matrix[addr], p)
10. idx = (idx + 1) & 0x1FF
```

## 5. 初始化

```
输入: matrix[64], lfsr_seed[32], idx_init(u16)
1. memcpy(m_matrix, matrix, 64)
2. m_idx = idx_init & 0x1FF
3. for i in 0..7:
       base = (i * 4) & 31   // stride 4
       LFSR[i] = seed[base..base+7] (mod 32, 小端填充)
```

## 6. 批处理

面隔离保证连续 8 步访问不同面，keystream 以 8 步批量处理。

## 7. 版本历史

| Ver | 代号 | LFSR | mask=0 | MB/s | NIST |
|-----|------|------|--------|------|------|
| V1 | ISA | 16 Fib | — | 115 | — |
| V2 | MF | 16 Fib | — | 238 | — |
| V3 | MV | 16 Fib | — | 266 | — |
| V4 | V8-Fib | 16 Fib | — | 271 | — |
| V5 | Galois | 16 Gal | — | 358 | — |
| V6 | Mask16 | 16 Gal | — | 790 | — |
| V7 | Mask8 | 16 Gal | — | 790 | — |
| V8 | V8-Mask | 8 Gal | bit0 强制 | 1200 | 49/50 |
| V9 | V9-Aux | 8+1 Gal | aux LFSR | 1220 | — |
| **V10** | **V10-Idx** | **8 Gal** | **idx&7** | **1240** | **50/50** |
