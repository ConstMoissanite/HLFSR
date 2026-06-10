# HLFSR-64 独立深度审计报告

> 审计日期: 2026-06-11  
> 审计范围: `src/hlfsr64.hpp`, `src/hlfsr64.cpp`, `src/test/*.cpp`, `src/protocol/*.cpp`  
> 审计方法: 静态代码审查，独立于已有文档进行分析  
> 状态: **未修复，仅指出问题**

---

## 🔴 CRITICAL — 测试代码无法编译 (3 处)

### F1. `bench_keystream.cpp:66-73` — 未定义变量 `km`

```cpp
hlfsr64::u8 bm[64], seed[32]; hlfsr64::u16 idx;   // 声明了 bm/seed
if (RAND_bytes(km, 64) != 1 || ...)                // 使用了 km（未定义）
cipher.init(km, idx);                               // 同上
```

声明 `bm[64], seed[32]` 但从未使用；使用 `km` 但从未声明。**此文件无法编译**。

### F2. `golomb_test.cpp:218-225` — 同 F1

完全相同的 bug：声明 `bm[64], seed[32]`，却使用未定义的 `km`。**此文件无法编译**。

### F3. `nist_test.cpp:294-296` — `init()` 参数数量错误 + 未初始化数据

```cpp
hlfsr64::u8 bm[64], seed[32]; hlfsr64::u16 idx;   // 栈上未初始化
hlfsr64 c; c.init(bm, seed, idx);                   // init() 只接受 2 个参数！
```

`init()` 签名为 `(const u8[64], u16)`，不是 `(u8[64], u8[32], u16)`。无法编译。即使修复签名，`bm/seed/idx` 也是未初始化的栈垃圾。**这意味着 multi_round 模式下的 HLFSR NIST 测试结果全部无效**——它测试的是随机垃圾态，而非正确 KDF 派生的密钥。

> **根因**: 代码从旧版 `init(matrix, lfsr_seed, idx)` API 迁移到 V11 统一 API `init(key_material, idx)` 时未更新测试文件中的变量声明，导致变量名残留旧名、`init` 调用残留旧签名。

---

## 🟠 HIGH — 核心逻辑问题 (5 处)

### F4. 批处理中 mask=0 回退使用错误的 idx (`hlfsr64.cpp:38`)

```cpp
u64 raw = (vx & ~mz) | (m_lfsr[m_idx & 7] & mz);
```

`advance_lfsr()` 使用成员 `m_idx` 选通回退 LFSR。在单步模式 (`next()`) 中，`m_idx` 在 `advance_lfsr` 调用后 +1，每步自然轮转 ✓。但在批处理模式 (`keystream`) 中，8 次 `advance_lfsr(mask[i])` 调用之间 `m_idx` 不变（只在 8 次全部完成后 `m_idx += 8`）。因此**8 次批处理中如有 mask=0，全部回退到同一条 LFSR**，而非按设计文档所述"idx 低 3 位每步 +1，自然轮转 8 条"。

mask=0 概率为 1/256 ≈ 0.39%，实际影响有限，但违反了声明的面隔离/轮转保证。

### F5. 协议层 info 字符串版本不匹配 (`hlfsr_stream.cpp:69, 118`)

```cpp
const uint8_t info[] = "HLFSR64-V10-ECIES";
```

头文件声明的版本是 `V11-Uni`，核心版本宏为 `HLFSR_VERSION 11`，但 HKDF 的 domain separation info 字符串仍为 `V10-ECIES`。这破坏了 HKDF 域隔离——如果 V10 和 V11 的协议消息被混合，密钥派生将无法正确区分。

### F6. `hlfsr_bench.cpp:92,181` — 解密 benchmark 中 XOR 自身 → 全零输出

```cpp
// 对称解密 (line 92):
for (size_t i = 0; i < data_len; i++) ciphertext[i] ^= ciphertext[i];

// ECIES 解密 (line 181):
for (size_t i = 0; i < data_len; i++) buf[i] ^= buf[i];
```

将密文与自身 XOR → 结果始终为**全零**。解密基准测试的结果完全无效。应为 `ciphertext[i] ^= keystream[i]`（即用 decryptor 重新生成 keystream 后再 XOR，或直接 memcmp 验证明文恢复）。

### F7. `ct_eq8` 存在有符号整数溢出 UB (`hlfsr64.cpp:13`)

```cpp
return (hlfsr64::u8)(0 - (((d | (hlfsr64::u8)(-(std::int8_t)d)) >> 7) ^ 1));
```

当 `d = 0x80` (即 `a ^ b = 0x80`) 时，`-(std::int8_t)0x80` 在 int8_t 范围 [-128, 127] 内溢出 → **C++ 标准的未定义行为**。在补码平台上恰好工作（int8_t 0x80 的负值实现定义为 0x80），但严格按标准不合法。修复方式：将 `-(std::int8_t)d` 改为 `-(std::uint8_t)d` 或直接用位运算 `(~d + 1)`。

### F8. 全零种子无防护 (`hlfsr64.cpp:19-27`)

`safety/analysis.md §7.1` 明确指出全零 `key_material` 导致永久零输出，并推荐在 `init()` 中添加检查。但代码中**未实现任何校验**。`init()` 静默接受全零输入，输出将始终为零，且调用方无法检测此退化态。

`safety/analysis.md §7.4` 推荐的修复：
```cpp
uint64_t *p = (uint64_t*)key_material;
if ((p[0]|p[1]|p[2]|p[3]|p[4]|p[5]|p[6]|p[7]) == 0) return ERROR;
```

---

## 🟡 MEDIUM — 测试基础设施问题 (3 处)

### F9. DFT/Cusum 测试同时失败 HLFSR 和 ChaCha20 (`nist_test.cpp`)

设计文档标注为"测试框架 bug"（`(impl)` 标记），但具体 bug 未被定位或修复。两个算法同时 0/100 说明自写的 DFT/Cusum 实现与 NIST STS 参考工具包之间存在实质性差异。需要与 NIST SP 800-22 官方 STS 工具包交叉验证。

### F10. Golomb G2 游程检验：自由度假算 (`golomb_test.cpp:138`)

```cpp
int dof = chi2_n - 1;
```

`chi2_n` 随 `expected > 0` 动态增长，但 χ² 检验的自由度应事先固定为分箱数 − 1。此处自由度是运行时决定的，且使用 3σ 经验界 `(dof + 3 * sqrt(2*dof))` 而非标准 P-value 判定——这使得 PASS/FAIL 判定缺乏严格的统计意义。

### F11. `nist_test.cpp` Serial 检验：`mm<2` 守卫对 m<3 有隐忧

```cpp
for(int mm=m-1;mm<=m+1;mm++){
    if(mm<2)continue;  // 当 m<3 时，psi[0] 可能不被初始化
```

当前 m=8 不受影响（mm 遍历 7,8,9），但如果 m 降低到 2 或 1，psi[0] 将不被初始化，导致后续计算使用未初始化的值。

---

## 🔵 LOW — 文档/注释不一致 (2 处)

### F12. `hlfsr64.cpp:1` 注释版本号过期

```cpp
// hlfsr64.cpp — HLFSR-64 V8 流密码核心 (8×8×8)
```

实际版本为 V11-Uni (`HLFSR_VERSION 11`)，注释自 V8 起未更新。

### F13. `bench_keystream.cpp` 和 `golomb_test.cpp` 残留未使用的 `seed[32]` 变量

旧版 `init(matrix, seed, idx)` API 的残留，`seed` 声明后从未被使用（因为 `init` 调用已改用 `km`——但 `km` 本身也未定义，见 F1/F2）。

---

## 📊 严重性汇总

| 等级 | 数量 | 影响 |
|------|------|------|
| 🔴 CRITICAL | 3 | 3 个测试文件无法编译 |
| 🟠 HIGH | 5 | 批处理 behavior 偏差、协议版本错误、benchmark 数据无效、UB、退化态无防护 |
| 🟡 MEDIUM | 3 | 测试框架统计正确性存疑 |
| 🔵 LOW | 2 | 注释/残留变量不一致 |
| **总计** | **13** | |

---

## 💡 核心代码质量评价

剥离测试代码后，`hlfsr64.hpp` 和 `hlfsr64.cpp` 的核心实现质量较高：

- Galois LFSR 的实现简洁正确，8 条多项式均经过本原性验证
- 常数时间 discipline 基本到位（无条件分支、固定内存访问模式）
- 乘性常数的选择合理（64-bit golden ratio）
- 面隔离原理的数学保证正确
- `init()` 和 `next()` 的单步逻辑自洽

**最值得关注的逻辑问题**是：
1. **F4** — 批处理 mask=0 回退 LFSR 选择偏差（行为与设计文档不符）
2. **F7** — ct_eq8 的有符号整数溢出 UB
3. **F8** — 全零种子退化态无防护（设计文档已识别但未在代码中修复）

测试基础设施的 3 个编译错误 (F1-F3) 使得所有现有基准/NIST/Golomb 测试结果的可复现性存疑。修复这些编译问题后应重新运行全部测试以获得可信的性能和统计结果。
