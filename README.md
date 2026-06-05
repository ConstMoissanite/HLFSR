# HLFSR-64

密钥驱动的流密码核心。512 位 8×8×8 矩阵 + 8 条 64 位 Galois LFSR + 1 字节掩码直接选通。64 位输出/步，乘性混合，面隔离批处理，1.2 GB/s。

## 设计要点

| 参数 | 值 |
|------|-----|
| 矩阵 | 8×8×8 (512 bits = 64 bytes) |
| LFSR | 8 × 64-bit Galois |
| 输出宽度 | 64 位/步 |
| 选通 | 8 位掩码 = 矩阵一行 |
| 混合 | 三路 XOR + 64×64 乘 (0x9E3779B97F4A7C15) |
| 吞吐 | 1.2 GB/s (纯 C++) |
| 等效密钥安全 | 256 位 |

- **最小化设计**：无指令 ISA，无 ct_eq8 比较，一行字节 = 完整选通逻辑
- **面隔离**：8 面独立，8 步批处理无 RAW 冲突
- **Galois LFSR**：移位 + 条件 XOR，比 Fibonacci 少 60% 运算
- **常数时间**：无秘密依赖分支
- **零平台依赖**：C++14 标准库即可

## 快速开始

```cpp
#include "src/hlfsr64.hpp"

// 1. 外部 KDF 派生 (matrix: 64B, lfsr_seed: 32B, idx: u16)
uint8_t matrix[64], lfsr_seed[32]; uint16_t idx;
your_kdf(key, nonce, matrix, lfsr_seed, &idx);

// 2. 初始化
hlfsr64 cipher;
cipher.init(matrix, lfsr_seed, idx);

// 3. 加密/解密
cipher.keystream(buf, len);
```

编译：`g++ -std=c++14 -O2 src/hlfsr64.cpp your_app.cpp`

## 文档

| 文档 | 内容 |
|------|------|
| [specs/design.md](specs/design.md) | 完整技术规格书 |
| [specs/api.md](specs/api.md) | API 接口文档 |
| [specs/isa.md](specs/isa.md) | 指令集参考 |
| [specs/polynomials.md](specs/polynomials.md) | 基底 LFSR 多项式 |
| [specs/safety/analysis.md](specs/safety/analysis.md) | 安全性分析 |

## 许可

MIT
