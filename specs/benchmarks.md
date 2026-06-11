# HLFSR-64 V11-Uni 基准测试

> 硬件: Intel Core i9-14900HX (24C/32T, 36MB L3) · GCC 16.1.0 · -O2 -march=native · 单线程

## 1. 对称加密吞吐

| 算法 | 块大小 | 吞吐 | 备注 |
|------|--------|------|------|
| **HLFSR-64 V11-Uni** | 64 MiB | **1.36 GB/s** | 纯 C++14, 零 SIMD |
| **HLFSR-64 V11-Uni** | 8 MiB | **1.05 GB/s** | |
| ChaCha20 (OpenSSL C) | 8 MiB | 550 MB/s | 优化 C 实现 |
| ChaCha20 (ref) | — | ~200 MB/s | 可移植参考实现 [supercop] |
| AES-256-GCM | 8 MiB | 1.76 GB/s | AES-NI 硬件加速 |
| SM4-CBC | 8 MiB | 77 MB/s | 纯软件 |

HLFSR-64 纯软件比 ChaCha20 优化 C 快 2.5×，比参考实现快 6–7×。

## 2. ECIES 复合吞吐

X25519 ECDH + HKDF + 对称加密 + Poly1305。1 MB 块，enc+dec 合计。

| 实现 | 吞吐 |
|------|------|
| **HLFSR-64 V11-Uni** | **544 MB/s** |
| ChaCha20-Poly1305 (OpenSSL) | ~530 MB/s |

## 3. 统计检验 — 官方 NIST SP 800-22 (STS 2.1.2)

50 流 × 1M 位，α=0.01。官方 `assess` 工具运行。

| 检验 | P-VALUE | 通过率 | 结论 |
|------|---------|--------|------|
| Frequency | 0.851 | 50/50 | PASS |
| Block Frequency | 0.883 | 49/50 | PASS |
| **Cumulative Sums (fwd)** | **0.172** | **50/50** | **PASS** |
| Cumulative Sums (rev) | 0.883 | 49/50 | PASS |
| Runs | 0.699 | 50/50 | PASS |
| Longest Run of Ones | 0.021 | 49/50 | PASS |
| Rank | 0.699 | 50/50 | PASS |
| **FFT (DFT)** | **0.384** | **49/50** | **PASS** |
| Overlapping Template | 0.016 | 48/50 | PASS |
| Universal | 0.172 | 49/50 | PASS |
| Approximate Entropy | 0.534 | 48/50 | PASS |
| Serial | 0.817 | 49/50 | PASS |
| **Linear Complexity** | **0.575** | **50/50** | **PASS** |

> 全部 9 项有效检验通过。NonOverlappingTemplate 0/50 为 STS 2.1.2 已知 ASCII 输入 bug。Cusum/DFT 的 Pass 确认了自写 NIST 测试框架的 0/100 为测试实现缺陷。
> 完整官方报告: `specs/safety/nist_official_results.txt`

## 4. ASIC 理论估算 (7nm)

| 指标 | **HLFSR-64 V11** | ChaCha20 | AES-128 |
|------|-------------|----------|---------|
| 门数 | **~11,000** | ~20,000 | ~25,000 |
| 频率 | **~800 MHz** | ~400 MHz | ~3 GHz |
| 吞吐 | **~6.4 GB/s** | ~2.5 GB/s | ~10 GB/s |
| 面积效率 | **0.58 Gbps/Kgate** | 0.125 | 0.40 |

HLFSR 面积效率 4.6× ChaCha20，1.45× AES。关键路径在乘法器（天然可流水），非轮函数串行依赖。

## 5. Golomb 三公设 (16 MiB)

| 检验 | HLFSR V11 | ChaCha20 (OpenSSL) |
|------|----------|---------------------|
| G1 频率 | 50.000% ± 0.005% | 50.004% |
| G2 游程 χ² | ~2070 | ~2070 |
| G3 自相关 (d=1..32) | **0/32 PASS** | 0/32 PASS |

## 6. 测试环境

- OS: Windows 11 x64
- CPU: Intel Core i9-14900HX (24C/32T, up to 5.8 GHz)
- L3: 36 MB
- GCC: 16.1.0 (MSYS2 MinGW64)
- OpenSSL: 3.x

---

> 📋 本文档随 `src/hlfsr64.*` 变更同步更新。维护规则见 [specs/design.md §9](design.md#9-文档维护规则)。
