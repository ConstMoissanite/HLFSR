# HLFSR-64

密钥驱动的自修改流密码核心。256 位内部状态（bitmap）同时作为数据存储器和指令存储器。8 位比特游标（idx）在 bitmap 上滑动；当前位控制输出翻转，当前字节中 4 位作为基底选择信号，驱动 16 条基底 LFSR 之一产生 64 位输出。bitmap 中嵌入的指令每步执行，动态改写 bitmap 自身和控制流。

## 设计要点

| 参数 | 值 |
|------|-----|
| 内部状态 (bitmap) | 256 位 (32 字节) |
| 基底 LFSR | 16 条 × 64 位 |
| 输出宽度 | 64 位/步 |
| 指令集 | 16 条，4-bit 操作码 + 4-bit 参数 |
| 指令执行 | 每步执行 |
| 等效密钥安全 | 256 位 |
| 多项式 | 16 个 64 次本原多项式（权重 7） |

- **自修改状态机**：bitmap 既是数据也是指令存储器，每步 curbyte 的高 4 位为操作码、低 4 位为参数
- **非规则 LFSR 交织**：sel 从 bitmap 动态取值，16 条 LFSR 每步全体推进，sel 选择输出源
- **curbit 翻转**：输出 = raw XOR {64{curbit}}，打破 LFSR 输出的直接可观测性
- **常数时间**：16 条指令全算 + 掩码选择，无秘密依赖分支
- **零平台依赖**：C++11 标准库即可，KDF 由调用方选择

## 快速开始

```cpp
#include "src/hlfsr64.hpp"

// 1. 外部 KDF 派生密钥材料
uint8_t bitmap[32], lfsr_seed[32], idx_init;
your_kdf(master_key, nonce, bitmap, lfsr_seed, &idx_init);

// 2. 初始化
hlfsr64 cipher;
cipher.init(bitmap, lfsr_seed, idx_init);

// 3. 加密/解密
cipher.keystream(ciphertext, plaintext_len);
// 或单步
uint64_t block = cipher.next();
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
