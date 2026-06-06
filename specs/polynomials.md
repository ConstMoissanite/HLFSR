# HLFSR-64 V10-Idx 基底 LFSR 多项式

8 条 64 位 Galois LFSR，权重 13–15。全部通过不可约+本原验证。

```
static const u64 POLY[8] = {
    0x4800203343401101ULL,  // w=15
    0x0416001300480117ULL,  // w=15
    0x58000C0310100803ULL,  // w=13
    0x00A0090940648023ULL,  // w=15
    0x484302010C340003ULL,  // w=15
    0x801D001006412901ULL,  // w=15
    0x04429288080A1021ULL,  // w=15
    0x2000022052D01213ULL,  // w=15
};
```

生成: `tools/polygen_high.cpp` (随机采样 + 软件 carryless multiply)
