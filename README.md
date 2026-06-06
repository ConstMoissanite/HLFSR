# HLFSR-64 V11-Uni

密钥驱动的流密码核心。512 位 8×8×8 矩阵 + 8 条 Galois LFSR (权重 13–15) + 8 位掩码直接选通。64 位输出/步，乘性混合 (黄金比)，面隔离批处理。NIST 100 轮 Monobit 98/100、其余 7 项全部满分，Golomb G3 0/32。

| 指标 | 值 |
|------|-----|
| 纯软件吞吐 | **1.36 GB/s** (纯 C++14) |
| 对称加密 (8 MB) | 1.05 GB/s |
| ECIES 复合 (X25519+HLFSR+Poly1305) | 693 MB/s (enc+dec) |
| 统计质量 | NIST 7/8 项 100%, Golomb G3 0/32 = ChaCha20 |
| 代码量 | ~250 行 (核心) |

## 快速开始

```cpp
#include "src/hlfsr64.hpp"

// KDF 派生 64+2=66 字节材料
uint8_t km[64]; uint16_t idx;
your_kdf(key, nonce, km, (uint8_t*)&idx);

hlfsr64 cipher;
cipher.init(km, idx & 0x1FF);
cipher.keystream(buf, len); // XOR with plaintext
```

编译: `g++ -std=c++14 -O2 src/hlfsr64.cpp your_app.cpp`

## 协议封装

```
src/protocol/  — X25519 + HKDF-SHA256 + HLFSR-64 V10 + Poly1305
  48 字节开销, 前向安全, 认证加密
```

## 文档

| 文档 | 内容 |
|------|------|
| [specs/design.md](specs/design.md) | 完整技术规格书 |
| [specs/api.md](specs/api.md) | API 接口 |
| [specs/isa.md](specs/isa.md) | 指令格式 |
| [specs/polynomials.md](specs/polynomials.md) | LFSR 多项式 |
| [specs/safety/analysis.md](specs/safety/analysis.md) | 安全性分析 |
| [specs/benchmarks.md](specs/benchmarks.md) | 基准测试 |
| [specs/protocol.md](specs/protocol.md) | 流协议规范 |

## 许可

MIT
