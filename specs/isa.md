# HLFSR-64 V10-Idx 指令格式

## 概述

无传统 ISA。每步取矩阵当前面一行 (8 位) = LFSR 选通掩码。mask=0 时回退为 LFSR[idx&7]。

## 编码

```
mask   = matrix[face*8 + row]    // 8 位掩码
p      = col                       // 3 位移位量
curbit = (matrix[addr] >> col) & 1
```

## 每步

```
坐标解码  face=idx&7, row=(idx>>3)&7, col=(idx>>6)&7
掩码读取  mask = matrix[addr]
LFSR  X   raw = mask ? (⊕ mask[i]·LFSR[i])×K : LFSR[idx&7]×K
输出      output = raw ^ {64{curbit}}
回填      matrix[addr] ^= raw[7:0]
移位      matrix[addr] = ROL8(matrix[addr], p)
推进      idx = (idx + 1) & 0x1FF
```

## 与前期对比

| | V8-Mask | V9-Aux | **V10-Idx** |
|---|---------|--------|-------------|
| LFSR | 8 | 9 (8+aux) | **8** |
| mask=0 | bit0 强制 1 | aux LFSR | **LFSR[idx&7]** |
| 附加上下文 | — | aux LFSR 状态 | **idx (已存在)** |
