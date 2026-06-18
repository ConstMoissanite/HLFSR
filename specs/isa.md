# HLFSR-64 操作编码

## 概述

每步从矩阵读一字节（掩码），全部 8 条 LFSR 无条件异或。掩码经乘法映射为 64 位奇数乘子（$\mathsf{mk} = \mathsf{mask} \times K_2 \mid 1$），与 $\mathsf{vx}$ 和 $K_1$ 级联相乘。$\mathsf{vx}$ 进乘法前经 $\mathrm{ROTL}_{33}$ 自旋转打破 LSb 线性依赖。反馈取乘法结果低 16 位乘 $K_2$，低 8 位回填当前面、高 8 位交叉注入相邻面。

## 每步数据流

```
坐标解码  face=idx&7, row=(idx>>3)&7, col=(idx>>6)&7, addr=face*8+row
掩码读取  mask = matrix[addr]
提取翻转  curbit = (mask >> col) & 1
LFSR 推进 8 条 Galois, 同时累加: vx = ⊕ LFSR[i]
自旋转    vx ^= ROTL33(vx)
掩码乘子  mk = (mask × K₂) | 1     (mask=0 → mk=1, 自然恒等)
级联乘法  raw = vx × mk × K₁
输出翻转  output = raw ^ curbit^64
16b 反馈  fb = (raw & 0xFFFF) × K₂
低 8 回填 matrix[addr] ^= fb[0:7]
行内扩散 matrix[addr] = ROTL8(matrix[addr], col)
高 8 交叉 matrix[(face⊕1)*8+row] ^= fb[8:15]
游标推进 idx = (idx + 1) & 0x1FF
```

## 关键常数

| 常数 | 值 | 用途 |
|------|-----|------|
| $K_1$ | `0x9E3779B97F4A7C15` | 黄金比, 级联乘法的输出端 |
| $K_2$ | `0xBF58476D1CE4E5B9` | SplitMix64, 掩码乘子 + 反馈 |
| $\mathrm{ROTL}_{33}$ | — | vx 自旋转, 33 与 64 互素 |

## 与前期版本对比

| 版本 | 选通机制 | mask=0 处理 | 步间 LFSR 依赖 |
|------|---------|------------|---------------|
| V11-Uni | 逐位选通 (mask bit → XOR gating) | ct_eq8 + LFSR[idx&7] 回退 | 有 (选通依赖) |
| **V12-MM** | **掩码乘法 (mask → odd multiplier)** | **mk=1 恒等** | **仅 Galois 递推** |

V12 砍掉了 ct_eq8 和 mask=0 回退分支，消除 per-bit 条件操作。编译器 -O3 自动向量化全异或路径，无需手写 SIMD。
