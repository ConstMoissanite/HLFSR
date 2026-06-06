# HLFSR-64 V11-Uni 基准测试

> 硬件: Intel Core i9-14900HX (24C/32T, 36MB L3) · GCC 16.1.0 · -O2 -march=native · 单线程

## 1. 对称加密吞吐

| 算法 | 块大小 | 吞吐 | 备注 |
|------|--------|------|------|
| HLFSR-64 V10 | 64 MiB | **1.36 GB/s** | 纯 C++14, 无 SIMD |
| HLFSR-64 V10 | 8 MiB | 1.05 GB/s | |
| ChaCha20-Poly1305 | 8 MiB | 550 MB/s | OpenSSL C 实现 |
| AES-256-GCM | 8 MiB | 1.76 GB/s | AES-NI 硬件加速 |
| SM4-CBC | 8 MiB | 77 MB/s | 软件 SM4 |

HLFSR-64 V10 纯软件比 ChaCha20 快 2.5×，比 SM4 快 17×。AES 领先来自硬件加速（AES-NI），非软件算法优势。

## 2. ECIES 复合吞吐

X25519 ECDH + HKDF + 对称加密 + Poly1305。1 MB 块，enc+dec 合计。

| 实现 | 吞吐 |
|------|------|
| HLFSR-64 V10 | **693 MB/s** |
| ChaCha20-Poly1305 | ~530 MB/s |
| SM2 + SM4 | 暂未测试 |

## 3. 统计检验

| 检验 | HLFSR-64 V10 | ChaCha20 |
|------|-------------|----------|
| Golomb G1 (频率) | 50.000% ± 0.005% PASS | 50.004% PASS |
| Golomb G2 (游程 χ²) | ~2070 | ~2070 |
| Golomb G3 (自相关) | 0/32 PASS | 0/32 PASS |
| NIST Monobit (100r) | **98/100** | 99/100 |
| NIST Runs (100r) | **100/100** | 100/100 |
| NIST 矩阵秩 (100r) | **100/100** | 100/100 |
| NIST 线性复杂度 (100r) | **100/100** | 100/100 |

注: DFT 和 Cusum 两方同时 0/100 — 测试框架实现问题，非算法缺陷。

## 4. ASIC 理论估算

7nm 工艺:

| 指标 | HLFSR-64 V10 | AES-128 | ChaCha20 |
|------|-------------|---------|----------|
| 门数 | ~11,000 | ~25,000 | ~20,000 |
| 频率 | ~800 MHz | ~3 GHz | ~1 GHz |
| 吞吐 | ~6.4 GB/s | ~10 GB/s | ~2 GB/s |

HLFSR 面积效率高于 ChaCha20 和 AES, 关键路径在 64×64 乘法器。

## 5. 测试环境

- OS: Windows 11 x64
- CPU: Intel Core i9-14900HX (24C/32T, up to 5.8 GHz)
- L3: 36 MB
- GCC: 16.1.0 (MSYS2 MinGW64)
- OpenSSL: 3.x
