# HLFSR-64 MF 指令字格式

## 概述

MF 设计没有传统 ISA。每步从矩阵当前行读取 16 位指令字（`curword`），分为 1 个移位量 + 3 个 LFSR 选通信号。没有操作码，没有参数值计算——LFSR 选通 + 行移位 + 回填构成状态演化。

## 指令字编码

```
curword = matrix[row*2] | (matrix[row*2+1] << 8)

bits [15:12]  p     行内循环移位数 (0~15)
bits [11:8]   sel0  第一 LFSR 选通
bits [7:4]    sel1  第二 LFSR 选通
bits [3:0]    sel2  第三 LFSR 选通
```

## 每步执行

```
行列解码    row = idx >> 4
指令字读取  curword = matrix[row*2] | (matrix[row*2+1] << 8)
LFSR 选通   raw = (L_sel0 ^ L_sel1 ^ L_sel2) × K
curbit 翻转 output = raw XOR {64{curbit}}, curbit = matrix[row*2] & 1
回填       matrix[row] ^= (raw & 0xFFFF)
行移位     matrix[row] = ROL16(matrix[row], p)
idx 推进   idx = (idx + 1) & 0xFF
```

## 与传统 ISA 对比

| | ISA (IM) | MF |
|---|---------|-----|
| 指令位宽 | 8 位 (4 opcode + 4 param) | 16 位 (4 shift + 12 select) |
| 指令条数 | 16 | 无（连续编码） |
| bitmap 修改 | 16 条指令 3 类目标 | 1 行 XOR + ROL |
| 吞吐 | 115 MiB/s | 238 MiB/s |
| 自修改 | 指令改变 bitmap 字节 | 回填改变矩阵行 |
