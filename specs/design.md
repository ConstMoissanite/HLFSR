# HLFSR-64 MF 技术规格书

## 1. 设计概述

HLFSR-64 MF（Matrix-Feedback）是一个密钥驱动的流密码核心。256 位内部矩阵（16×16 位）作为状态存储器，每步取当前行 16 位指令字控制三路 LFSR 选通和行内循环移位。16 条 64 位基底 LFSR 每步全体推进，三路选通异或后经 64×64 乘性混合，取低 16 位回填当前行，行内循环移位。64 位输出经 curbit 翻转后产生密钥流。

## 2. 顶层参数

| 参数 | 值 |
|------|-----|
| 矩阵 (matrix) | 16×16 位 (32 字节) |
| 基底 LFSR | 16 条 × 64 位 |
| 输出宽度 | 64 位/步 |
| 指令字 | 16 位：移位量 p (4) + 三路选通 sel0,sel1,sel2 (12) |
| idx | 8 位 (0~255)，每步自动 +1 |
| 密钥输入 | 外部 KDF 派生 |
| 多项式 | 16 个 64 次本原多项式，见 specs/polynomials.md |

## 3. 组件定义

**matrix**：16×16 位矩阵，按字节寻址 (0~31)。row = idx >> 4，col = idx & 15。加密过程中被回填和行移位动态修改。

**idx**：8 位指针 (0~255)，每步自动 `idx = (idx + 1) & 0xFF`。高 4 位为行号，低 4 位为列号。

**curbit**：`matrix[row_byte] & 1`，当前行的 bit 0。用于控制输出翻转。

**指令字**：16 位 `curword = matrix[row*2] | (matrix[row*2+1] << 8)`。
- bits [15:12]：行内循环移位数 p (0~15)
- bits [11:8]：sel0 — 第一 LFSR 选通
- bits [7:4]：sel1 — 第二 LFSR 选通
- bits [3:0]：sel2 — 第三 LFSR 选通

**基底 LFSR**：16 条 64 位 Fibonacci LFSR，16 个本原多项式预设。每步全体推进一次。

## 4. 每步操作

```
1. row = idx >> 4
2. curword = matrix[row*2] | (matrix[row*2+1] << 8)
3. p = curword >> 12,  sel0 = (curword >> 8) & 0xF,  sel1 = (curword >> 4) & 0xF,  sel2 = curword & 0xF
4. 全部 16 条 LFSR 各移位一次
5. raw = (L_sel0 ^ L_sel1 ^ L_sel2) × 0x9E3779B97F4A7C15ULL
6. curbit = matrix[row*2] & 1
7. output = raw XOR {64{curbit}}
8. matrix[row] ^= raw & 0xFFFF                          // 低 16 位回填当前行
9. matrix[row] = ROL16(matrix[row], p)                   // 当前行循环移位 p 位
10. idx = (idx + 1) & 0xFF
```

## 5. 初始化

纯对称场景：bitmap/matrix 和 lfsr_seed 由外部 KDF（如 SHA-256）派生传入。16 条基底 LFSR 初态由 32 字节 lfsr_seed 经步长 2 滑动窗口填入。

## 6. 常数时间实现要求

- 三路 LFSR 选通：掩码查表，循环外预计算
- 矩阵读写：固定地址偏移，无数据依赖分支
- 64×64 乘法：单条 imul，常数时间
- ROL16：单条移位+OR，常数时间

## 7. 实现约束

- 零平台依赖：C++11 标准库
- 库形态：头文件 + 实现文件
- 对外接口：init / next / keystream

## 8. 使用约束

同一密钥下 Nonce 不可重复使用。
