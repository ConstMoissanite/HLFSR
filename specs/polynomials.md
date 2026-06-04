# HLFSR-64 基底 LFSR 多项式

## 概述

为 HLFSR-64 选定 16 条基底 LFSR 的本原多项式，每条 LFSR 使用一个 64 次本原多项式作为反馈抽头。全部多项式均通过不可约性和本原性验证。

## 选择方法

### 候选生成

随机采样权重 7 的 64 次多项式：

```
p(x) = x^64 + x^a + x^b + x^c + x^d + x^e + 1
0 < a < b < c < d < e < 64, 均匀随机
```

权重取奇数（7/9/11），偶数权重多项式在 GF(2) 上必有因式 x+1（p(1)=0），自动可约。使用 SplitMix64 PRNG 进行均匀随机采样，确保多项式项分布无结构性偏聚。

### 不可约性测试

对候选 p(x) = x^64 + p_lo（p_lo 为低 63 位），依次计算 v_k = x^{2^k} mod p：

1. 对于 64 的所有真因子 k ∈ {1, 2, 4, 8, 16, 32}，验证 v_k ≠ x
2. 验证 v_64 = x

两步均通过则 p(x) 不可约。

### 本原性测试

2^64 − 1 = 3 × 5 × 17 × 257 × 641 × 65537 × 6700417

对于不可约的 p(x)，对所有素因子 q | 2^64−1：

```
x^{(2^64−1)/q} ≠ 1 (mod p)
```

全部 7 个检验通过则 p(x) 为本原多项式。

### 多项式运算

使用 x86 PCLMULQDQ 指令进行 GF(2) 上的无进位乘法，模约简通过迭代代换 x^64 ≡ p_lo 实现。

## 结果

全部 16 个多项式均为权重 7 的本原多项式（heptanomials），随机测试 684 个候选内找齐。

### 十六进制表示

```
static const uint64_t HLFSR64_LFSR_POLY[16] = {
    0x0054010000020001ULL,  // [ 0] weight=7
    0x0080000004200083ULL,  // [ 1] weight=7
    0x0100000020040481ULL,  // [ 2] weight=7
    0x0000088040008005ULL,  // [ 3] weight=7
    0x0804000004200101ULL,  // [ 4] weight=7
    0x0000900008400009ULL,  // [ 5] weight=7
    0x8000110004001001ULL,  // [ 6] weight=7
    0x2000000000820141ULL,  // [ 7] weight=7
    0x2000004000430001ULL,  // [ 8] weight=7
    0x0002080800600001ULL,  // [ 9] weight=7
    0x00020A0000400009ULL,  // [10] weight=7
    0x000230000C000001ULL,  // [11] weight=7
    0x4080000040080021ULL,  // [12] weight=7
    0x0080004400210001ULL,  // [13] weight=7
    0x0100020800008081ULL,  // [14] weight=7
    0x0210000500080001ULL,  // [15] weight=7
};
```

uint64_t 的 bit i 对应 x^i 项。x^64 项始终隐式存在。

### 展开形式

| # | 多项式 | 权重 |
|---|--------|------|
| 0 | x^64 + x^54 + x^52 + x^50 + x^40 + x^17 + 1 | 7 |
| 1 | x^64 + x^55 + x^26 + x^21 + x^7 + x + 1 | 7 |
| 2 | x^64 + x^56 + x^29 + x^18 + x^10 + x^7 + 1 | 7 |
| 3 | x^64 + x^43 + x^39 + x^30 + x^15 + x^2 + 1 | 7 |
| 4 | x^64 + x^59 + x^50 + x^26 + x^21 + x^8 + 1 | 7 |
| 5 | x^64 + x^47 + x^44 + x^27 + x^22 + x^3 + 1 | 7 |
| 6 | x^64 + x^63 + x^44 + x^40 + x^26 + x^12 + 1 | 7 |
| 7 | x^64 + x^61 + x^23 + x^17 + x^8 + x^6 + 1 | 7 |
| 8 | x^64 + x^61 + x^38 + x^22 + x^17 + x^16 + 1 | 7 |
| 9 | x^64 + x^49 + x^43 + x^35 + x^22 + x^21 + 1 | 7 |
| 10 | x^64 + x^49 + x^43 + x^41 + x^22 + x^3 + 1 | 7 |
| 11 | x^64 + x^49 + x^45 + x^44 + x^27 + x^26 + 1 | 7 |
| 12 | x^64 + x^62 + x^55 + x^30 + x^19 + x^5 + 1 | 7 |
| 13 | x^64 + x^55 + x^38 + x^34 + x^21 + x^16 + 1 | 7 |
| 14 | x^64 + x^56 + x^41 + x^35 + x^15 + x^7 + 1 | 7 |
| 15 | x^64 + x^57 + x^52 + x^34 + x^32 + x^19 + 1 | 7 |

### 结构特征

16 个多项式通过随机采样生成，低次项和高次项分布均匀，无共享模式。各多项式项分布在 [1, 63] 全范围内，平均项间距约 21 位。随机权重 7 的本原多项式约占全部权重 7 候选多项式的 1/64（约 110,000 个），所选 16 个在此集合内均匀散布。

## 参考文献

1. **Golomb, S.W.** — *Shift Register Sequences* (Aegean Park Press, 1967; 3rd revised ed. 2017). 关于线性反馈移位寄存器序列的基础著作。

2. **Lidl, R. and Niederreiter, H.** — *Finite Fields* (Cambridge University Press, 2nd ed., 1997). 有限域理论的权威参考，第 3 章关于本原多项式。

3. **Seroussi, G.** — *Table of Low-Weight Binary Irreducible Polynomials*, HP Labs Technical Report HPL-98-135 (1998). 低权重不可约多项式的系统列表。

4. **NIST SP 800-38D** — *Recommendation for Block Cipher Modes of Operation: Galois/Counter Mode (GCM)*. GCM 模式使用多项式 x^64 + x^4 + x^3 + x + 1 定义 GF(2^64) 域。

5. **Zierler, N.** — *Primitive Trinomials Whose Degree is a Mersenne Exponent*, Information and Control, 15(1):67–69, 1969. 本原三项式的经典研究。

6. **Zsigmondy, K.** — *Zur Theorie der Potenzreste*, Monatshefte für Mathematik, 3:265–284, 1892. Zsigmondy 定理保证了 2^n−1 对 n>2 存在本原因子（对 n=64, 2^64−1 的素因子均为 Fermat 素数 F0..F4 及 F5 的因子 641, 6700417）。

## 生成程序

`polyselect.cpp` — 使用 PCLMULQDQ 指令加速 GF(2) 域运算，枚举/随机采样候选多项式，验证不可约性和本原性。

编译：
```
g++ -O2 -march=native -mpclmul -std=c++14 polyselect.cpp -o polyselect
```

运行：
```
./polyselect [count]     # 默认16个
```
