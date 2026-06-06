# HLFSR-64 V8-Mask 指令格式

## 概述

无传统 ISA。每步从矩阵当前面取一行 (8 位) = LFSR 选通掩码。每 bit 直接选中一条 LFSR。移位量 p 由列位置决定。

## 编码

```
mask  = matrix[face*8 + row] | 1   // 8 位, bit 0 保底置 1
p     = col                          // 3 位 (0–7)
curbit = (matrix[addr] >> col) & 1  // 1 位
```

## 每步

```
坐标解码  face=idx&7, row=(idx>>3)&7, col=(idx>>6)&7
掩码读取  mask = matrix[face*8+row] | 1
LFSR  X   raw = (⊕ mask[i]·LFSR[i]) × K
curbit     output = raw ^ {64{curbit}}
回填       matrix[addr] ^= raw[7:0]
行移位     matrix[addr] = ROL8(matrix[addr], col)
推进       idx = (idx + 1) & 0x1FF
```

## 与前期版本对比

| | ISA (V1) | MF (V2) | V8-Mask |
|---|---------|-----|---------|
| 选通 | 3×4b ct_eq8×48 | 3×4b ct_eq8×48 | 1×8b mask |
| 指令位 | 8b opcode+param | 16b p+sel | 8b mask |
| 移位 | 无 | ROL16(p) | ROL8(col) |
| 吞吐 | 115 | 238 | 1200 |
