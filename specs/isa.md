# HLFSR-64 指令集 (ISA)

每步执行一条指令。指令来自 curbyte = bitmap[idx/8]，高 4 位为操作码，低 4 位为参数。除 CurB 操作自身字节外，其余指令操作对象为下一个字节：`target = (idx/8 + 1) mod 32`。指令未改变 idx 时，默认 `idx = (idx + 1) mod 256`。

## 总表

```
操作码  助记符   类别     语义
──────  ──────  ──────   ──────────────────────────────────────────
0000    IncB    算术     bitmap[target] += param          (mod 256)
0001    Copb    位操作   bitmap[target] ^= (1 << param)
0010    DecB    算术     bitmap[target] -= param          (mod 256)
0011    CopO    位操作   bitmap[target] ^= (1 << (7-param))
0100    StpB    步进     idx += param × 8
0101    Stpb    步进     idx += param
0110    RStpB   步进     idx -= param × 8
0111    RStpb   步进     idx -= param
1000    JmpBL   跳转     idx = curbyte[7] × 128 + param × 8
1001    JmpBR   跳转     idx = curbyte[0] × 128 + param × 8
1010    XorB    逻辑     bitmap[target] ^= param
1011    AndB    逻辑     bitmap[target] &= param
1100    OrB     逻辑     bitmap[target] |= param
1101    SwapB   数据     bitmap[target].nibble_swap; idx += 8
1110    CurB    自修改   byte = bitmap[idx/8]; byte ^= bitmap[(idx/8 + sext(param)) mod 32]; bitmap[idx/8] = byte
1111    NotB    自修改   t = (idx/8 + sext(param)) mod 32; bitmap[t] = ~bitmap[t]; idx += 8
```

## 逐条说明

### IncB (0000)
`bitmap[target] = (bitmap[target] + param) & 0xFF`

### Copb (0001)
`bitmap[target] ^= (1u << param)`
param=0 翻转 bit0。

### DecB (0010)
`bitmap[target] = (bitmap[target] - param) & 0xFF`

### CopO (0011)
`bitmap[target] ^= (1u << (7 - param))`
param=0 翻转 bit7。与 Copb 索引反向。

### StpB (0100) / RStpB (0110)
以字节为单位跳步进/退，`idx = (idx ± param × 8) & 0xFF`。步进/跳转指令额外将本步输出低 8 位异或进目标字节：`bitmap[target] ^= output & 0xFF`。

### Stpb (0101) / RStpb (0111)
以比特为单位，`idx = (idx ± param) & 0xFF`。同样额外执行 `bitmap[target] ^= output & 0xFF`。

### JmpBL (1000) / JmpBR (1001)
无条件跳转。curbyte[7] 或 curbyte[0] 选择半区（0=0~127, 1=128~255），param 为半区内字节偏移 (0~15)，乘 8 得比特地址。跳转指令额外将本步输出低 8 位异或进目标字节：`bitmap[target] ^= output & 0xFF`。
```
half = opcode[0] ? curbyte[7] : curbyte[0]    // JmpBL 取 bit7, JmpBR 取 bit0
idx  = half × 128 + param × 8
```

### XorB (1010) / AndB (1011) / OrB (1100)
param 零扩展为 8 位，对目标字节按位操作。
```
bitmap[target] ^= param    // XorB
bitmap[target] &= param    // AndB
bitmap[target] |= param    // OrB
```

### SwapB (1101)
交换目标字节高低 4 位，强制 `idx += 8`。
```
byte_t b = bitmap[target];
bitmap[target] = (b << 4) | (b >> 4);
idx = (idx + 8) & 0xFF;
```

### CurB (1110)
将 param 视为 4 位有符号数：0~7 为正，8~15 为负 (−8 ~ −1)。符号扩展到 8 位得 offset。读取 `bitmap[(idx/8 + offset) mod 32]` 异或到 curbyte，写回 `bitmap[idx/8]`。
```
offset = (param & 8) ? (param | 0xF0) : param  // 符号扩展
byte_t remote = bitmap[(idx/8 + offset) & 31];
byte_t local  = bitmap[idx/8];
bitmap[idx/8] = local ^ remote;
```
**不改变 idx**（若无其他改变，走默认 +1）。

### NotB (1111)
与 CurB 相同的 offset 计算。读取 `bitmap[(idx/8+offset) mod 32]` 按位取反，写回 **target**。强制 `idx += 8`。
```
offset = (param & 8) ? (param | 0xF0) : param
src = (idx/8 + offset) & 31
bitmap[target] = ~bitmap[src]
idx = (idx + 8) & 0xFF
```

## 默认行为

指令执行后，若未显式修改 idx（即非 JmpBL/JmpBR/StpB/Stpb/RStpB/RStpb/SwapB/NotB），执行 `idx = (idx + 1) & 0xFF`。

## 常数时间实现要点

- 16 条指令的计算结果**全部并行计算**，用操作码解码掩码选择最终生效的那一个结果
- 跳转目标、是否写回 bitmap、是否修改 idx 均用掩码控制
- 无 `if` / `switch` / 分支预测
