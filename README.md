# HLFSR-64

掩码乘法驱动的流密码核心。1040 位内部状态：8×8×8 矩阵 (512b) + 8 条 Galois LFSR (512b) + idx (16b，低 9 位计数 + 高 7 位反馈调节)。高位反馈 + 动态 mask 混合 + 面交叉注入 + TV 256-bit 输出。

| 指标 | 值 | 对标 |
|------|-----|------|
| 纯软件吞吐 | **5.84 GB/s** (-O3, 无 SIMD) | ChaCha20 AVX2: 1.60 GB/s |
| 代数度 | **≥26** (d=21 94%, d=22-25 100%, d=26 94%) | 乘法理论峰值 ≈32 |
| 线性偏差 | **PASS 3.45e-5** (2^30 对) | 3σ 阈值 9.16e-5 |
| 差分 | 50% avalanche, 0.0% zero-diff | 理想 50% |
| NIST SP 800-22 | 200 流全部 9 项有效通过 | 官方 STS 2.1.2 |
| Golomb G3 | **0/32 PASS** | = ChaCha20 |
| ASIC (7nm) | 48K 门, 0.83 Gbps/Kgate | ChaCha20: 18K/1.3 |
| 代码量 | ~150 行 (核心) | ChaCha20: ~200 行 |
| 平台依赖 | **零** (C++14) | — |

## 快速开始

```cpp
#include "src/hlfsr64.hpp"

uint8_t km[64]; uint16_t idx;
your_kdf(key, nonce, km, (uint8_t*)&idx);

hlfsr64 cipher;
cipher.init(km, idx & 0x1FF);
cipher.keystream256(buf, len);  // TV 256-bit 输出模式
```

编译: `g++ -std=c++14 -O3 -march=native src/hlfsr64.cpp your_app.cpp`

## 核心算法

```
每步 256-bit 输出 (TV 模式):
  face=idx&7, row=(idx>>3)&7, col=(idx>>6)&7
  mask = matrix[face*8+row]
  curbit = (mask >> col) & 1
  8 条 Galois LFSR 各推 1 步, 全 XOR 得 vx, vx ^= ROTL33(vx)
  mk = (mask × K2) | 1
  raw = vx × mk × K1
  season = (mask ^ (idx>>9)) | 1
  tv = lfsr[face] × matrix[addr] × season × K3
  out[i] = raw × ROTL(33+17i)(tv) ⊕ lfsr[(face+2i)%8] ⊕ curbit^64   (i=0..3)
  fb = (raw[63:48] × K2)
  matrix[addr] ^= fb_lo, ROL8 by col
  matrix[(face⊕1)*8+row] ^= fb_hi
  idx = (idx+1)&0x1FF | ((fb_lo&0x7F)<<9)
```

## 协议封装

```cpp
#include "src/protocol/hlfsr_stream.hpp"

auto kp = hlfsr_proto::keygen();
auto wire = hlfsr_proto::encrypt(msg, len, recv_pub);
auto pt   = hlfsr_proto::decrypt(wire.data(), wire.size(), recv_priv);
```

## 文档

| 文档 | 内容 |
|------|------|
| [specs/paper/paper_zh.tex](specs/paper/paper_zh.tex) | 学术论文（中文） |
| [hdl/hlfsr64.v](hdl/hlfsr64.v) | Verilog 参考实现 |

## 许可

MIT
