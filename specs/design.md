# HLFSR-64 技术规格书

> V8-Mask · 8×8×8 矩阵 (512b) · 8×64b Galois LFSR · 8b 掩码选通 · 1.2 GB/s

## 1. 设计概述

HLFSR-64 是一个密钥驱动的流密码核心。512 位内部状态组织为 8×8×8 三维矩阵（8 面 × 8 行 × 8 列），每步读取当前面的一行（8 位）作为 8 条 Galois 64 位 LFSR 的选通掩码。掩码位直接 XOR 选中的 LFSR 输出，经 64×64 乘性混合（黄金比常数），取低 8 位 XOR 回填当前行，行内循环移位 col 位。64 位输出经 curbit 翻转后产生密钥流。

核心创新：
- **面隔离**：`face = idx & 7` 使得连续 8 步访问 8 个不同面，互不冲突，天然支持 8 步批处理
- **掩码直接选通**：一行字节即 8 条 LFSR 的选通信号，无比较、无查表、无 ct_eq8
- **Galois LFSR**：左移 MSB 反馈，消除 Fibonacci 的 parity 折叠开销
- **乘性混合**：64×64 奇数模乘 (双射) 打破 LFSR 线性结构

## 2. 顶层参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 矩阵 | 8×8×8 = 512 bits (64 bytes) | 三维状态存储器 |
| LFSR | 8 × 64-bit Galois | MSB 反馈，左移 |
| 输出宽度 | 64 bits/step | 小端写入 |
| 选通 | 8-bit mask = 矩阵一行 | bit 0 保底置 1 |
| 移位量 p | col (3 bits, 0–7) | 行内 ROL8 量 |
| idx | 9 bits (0–511) | 每步 +1，模 512 |
| 乘性常数 | K = 0x9E3779B97F4A7C15 | 64-bit 黄金比 |
| 吞吐 | 1.2 GB/s | 纯 C++14，无 SIMD |
| 多项式 | 8 × 64 次本原（权重 7→待升 12-15） | Galois 配置 |

## 3. 坐标编码

```
face = idx & 7          // 3 bits, 0–7: 哪一面
row  = (idx >> 3) & 7   // 3 bits, 0–7: 面内哪一行
col  = (idx >> 6) & 7   // 3 bits, 0–7: 行内哪一列

addr = face * 8 + row   // 字节地址 (0–63)
```

面隔离特性：idx 低 3 位为 face，每步 +1 遍历 8 个面。连续 8 步 `face` 各不相同，步骤 i 的写回不影响步骤 i+1..i+7 的读取。这是 8 步批处理的数学保证。

## 4. 每步操作

```
1. 解码坐标
   face = idx & 7
   row  = (idx >> 3) & 7
   col  = (idx >> 6) & 7
   addr = face * 8 + row

2. 读取掩码
   mask = matrix[addr] | 1     // bit 0 强制置 1，防止全零选通

3. 取移位量和 curbit
   p      = col
   curbit = (matrix[addr] >> col) & 1

4. Galois LFSR 推进（8 条全体）
   for i in 0..7:
       msb = LFSR[i] >> 63
       LFSR[i] = (LFSR[i] << 1) ^ (POLY[i] & -(msb))

5. 掩码 XOR + 乘性混合
   vx = 0
   for i in 0..7:
       if mask bit i:
           vx ^= LFSR[i]
   raw = vx × 0x9E3779B97F4A7C15ULL

6. 输出
   output = raw ^ {64{curbit}}

7. 回填
   matrix[addr] ^= (u8)(raw & 0xFF)

8. 行内循环移位
   matrix[addr] = ROL8(matrix[addr], p)

9. 推进 idx
   idx = (idx + 1) & 0x1FF
```

## 5. LFSR 推进细节

### 5.1 Galois 配置

Fibonacci 需要计算 parity(tap_bits) 作为反馈位（popcnt 指令）。Galois 使用 MSB 反馈：

```
if MSB == 1:  state' = (state << 1) XOR polynomial
if MSB == 0:  state' = (state << 1)
```

常数时间实现：
```
msb  = state >> 63
mask = 0 - msb               // all-ones or all-zeros
state = (state << 1) ^ (polynomial & mask)
```

无 popcnt，无水平折叠。移位 + sbb + xor = 3 条指令/LFSR。

### 5.2 多项式互反

Fibonacci 多项式 P(x) 与 Galois 多项式 Q(x) = x^64·P(1/x) 产生相同周期序列。当前 8 个多项式可直接用于 Galois 配置，周期均为 2^64−1。

## 6. 掩码选通

### 6.1 直接位选通

```
vx = XOR over { LFSR[i] : mask bit i = 1 }
```

8 位随机掩码期望 Hamming weight = 4，平均 4 条主 LFSR 参与 XOR。

### 6.2 附属 LFSR (mask=0 保底)

```
if mask == 0:  raw = LFSR[8] × K   // 附属高权重 LFSR 接管
if mask != 0:  raw = vx     × K
```

常数时间：`raw = ((vx & ~mz) | (LFSR[8] & mz)) × K`，其中 mz = -(mask==0)。

mask=0 概率 = 1/256 ≈ 0.4%。附属 LFSR 采用权重 15+ 多项式 (待筛选)，始终推进。消除 `mask|=1` 的 forced-bit 偏倚。

### 6.3 与旧版对比

| | ISA (V1) | V8-Mask | **V9-Aux** |
|---|---------|---------|------------|
| 选通方式 | ct_eq8 × 48 | 1 字节位掩码 | 同 V8 |
| 选通数 | 固定 3 | 1–8 (bit0 强制) | 0–8 (附属接管 0) |
| 零块处理 | sel0==sel1 | mask|=1 | 附属 LFSR |
| 指令开销 | 16 指令 + 48 比较 | 0 | 0 |

## 7. 初始化

```
输入: matrix[64], lfsr_seed[32], idx_init (u16)

1. memcpy(m_matrix, matrix, 64)
2. m_idx = idx_init & 0x1FF
3. for i in 0..8:                    // 9 条 LFSR (8 主 + 1 附属)
       base = (i * 3) & 31           // stride=3, gcd(3,32)=1
       for j in 0..7:
           LFSR[i] |= seed[(base + j) & 31] << (j * 8)
```

**滑动窗口推导**：9 条 × 8 字节 = 72 字节需求，32 字节种子。stride=3 (gcd(3,32)=1) 保证所有 32 字节被均匀覆盖，每字节被约 2.25 条 LFSR 共享。附属 LFSR (i=8) 取 seed[(24+j) mod 32] = seed[24..31]。

**调用方职责**：使用选定的 KDF 从主密钥派生 64+32+2=98 字节材料。同一密钥下 Nonce 不可重复。

## 8. 批处理

面隔离保证连续 8 步的 `(face, row)` 互不冲突。keystream 以 8 步为批量：

1. 预解码全部 8 个掩码和 curbit
2. 8 步 LFSR 推进 (串行)
3. 8 步输出
4. 8 步回填 (全部不同地址)

尾部不足 64 字节回退单步。

## 9. 版本历史

| Ver | 代号 | 结构 | LFSR | 选通 | MB/s | δ |
|-----|------|------|------|------|------|---|
| V1 | ISA | 256b bitmap | 16 Fib | 3-sel ct_eq8 | 115 | — |
| V2 | MF | 16×16 | 16 Fib | 3-sel ct_eq8 | 238 | +107% |
| V3 | MV | 16×16 batch | 16 Fib | 3-sel ct_eq8 | 266 | +12% |
| V4 | V8-Fib | 8×8×8 | 16 Fib | 3-sel ct_eq8 | 271 | +2% |
| V5 | Galois | 8×8×8 | 16 Gal | 3-sel ct_eq8 | 358 | +32% |
| V6 | Mask16 | 8×8×8 | 16 Gal | 16b mask | 790 | +121% |
| V7 | Mask8 | 8×8×8 | 16 Gal | 16b mask | 790 | +0% |
| **V8** | **V8-Mask** | **8×8×8** | **8 Gal** | **8b mask** | **1200** | **+52%** |

V1→V8 总提升: **10.4×** (115 → 1200 MB/s)
