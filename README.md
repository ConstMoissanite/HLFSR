# HLFSR-64

掩码乘法驱动的流密码核心。512 位 8×8×8 矩阵 + 8 条 Galois LFSR (权重 13–15) + mask-multiply 混合 + 面交叉反馈 + 面隔离批处理。

| 指标 | 值 | 对标 |
|------|-----|------|
| 纯软件吞吐 | **2.03 GB/s** (-O3) | ChaCha20 纯 C: 550 MB/s |
| 代数度 | **≥24** (d=21-24 100%) | 乘法理论峰值 ≈32 |
| 线性偏差 | **PASS 3.45e-5** (2³⁰ 对) | 3σ 阈值 9.16e-5 |
| 差分 | 50% avalanche, 0.0% zero-diff | 理想 50% |
| NIST SP 800-22 | 200 流全部 9 项有效通过 | 官方 STS 2.1.2 |
| Golomb G3 | **0/32 PASS** | = ChaCha20 |
| ASIC (7nm) | 23K 门, 0.28 Gbps/Kgate | ChaCha20: 20K/0.125 |
| 代码量 | ~130 行 (核心) | ChaCha20: ~200 行 |
| 平台依赖 | **零** (C++14) | — |

## 快速开始

```cpp
#include "src/hlfsr64.hpp"

uint8_t km[64]; uint16_t idx;
your_kdf(key, nonce, km, (uint8_t*)&idx);   // 66 bytes total

hlfsr64 cipher;
cipher.init(km, idx & 0x1FF);
cipher.keystream(buf, len);  // XOR w/ plaintext
```

编译: `g++ -std=c++14 -O2 src/hlfsr64.cpp your_app.cpp`

## 协议封装

```cpp
#include "src/protocol/hlfsr_stream.hpp"

auto kp = hlfsr_proto::keygen();
auto wire = hlfsr_proto::encrypt(msg, len, recv_pub);
auto pt   = hlfsr_proto::decrypt(wire.data(), wire.size(), recv_priv);
```

## 核心算法

```
每步:
  face=idx&7, row=(idx>>3)&7, col=(idx>>6)&7
  mask = matrix[face*8+row]           // 自修改选通
  curbit = (mask >> col) & 1
  ∀i: LFSR[i] Galois 移位 1 步
  vx = ⊕ mask bit i · LFSR[i]
  raw = vx × 0x9E3779B97F4A7C15       // 黄金比乘性混合
  output = raw ^ {64{curbit}}
  matrix[face*8+row] ^= raw[7:0]      // 闭环回填
  matrix[face*8+row] ROL8 by col      // 行内扩散
  idx = (idx+1) & 0x1FF
```

## 文档

| 文档 | 内容 |
|------|------|
| [specs/design.md](specs/design.md) | 完整技术规格书 (9 节, 含版本演进) |
| [specs/api.md](specs/api.md) | API 接口 |
| [specs/isa.md](specs/isa.md) | mask 选通格式 |
| [specs/polynomials.md](specs/polynomials.md) | LFSR 多项式 |
| [specs/safety/analysis.md](specs/safety/analysis.md) | 安全性分析 (10 节, 定量攻击估算) |
| [specs/benchmarks.md](specs/benchmarks.md) | 基准测试 (symmetric + ECIES + ASIC) |
| [specs/protocol.md](specs/protocol.md) | 流协议规范 |

## 许可

MIT
