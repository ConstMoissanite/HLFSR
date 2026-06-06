# HLFSR-64 V8-Mask API

## 头文件

```cpp
#include "src/hlfsr64.hpp"
```

## 版本常量

```cpp
#define HLFSR_VERSION     8
#define HLFSR_VARIANT     "V8-Mask"
#define HLFSR_MATRIX      8        // 8×8×8
#define HLFSR_LFSR_COUNT  8        // 8 条 Galois LFSR
#define HLFSR_MASK_BITS   8        // 8 位掩码选通
#define HLFSR_MATRIX_BYTES 64     // 512 bits
#define HLFSR_SEED_BYTES   32     // lfsr_seed
#define HLFSR_IDX_BITS     9      // idx 0-511
```

## 类型

```cpp
class hlfsr64 {
public:
    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u64 = std::uint64_t;

    void init(const u8 matrix[64], const u8 lfsr_seed[32], u16 idx_init);
    u64  next();
    void keystream(void* out, std::size_t bytes);
};
```

## init

```cpp
void init(const u8 matrix[64], const u8 lfsr_seed[32], u16 idx_init);
```

初始化 8×8×8 矩阵和 8 条 Galois LFSR。

| 参数 | 大小 | 说明 |
|------|------|------|
| `matrix` | 64 字节 | 8 面×8 行×8 位，512 bits 初始状态 |
| `lfsr_seed` | 32 字节 | LFSR 初态种子，步长 4 滑动窗口填入 8 条 LFSR |
| `idx_init` | 2 字节 (u16) | 起始游标 (0–511)，低 9 位有效 |

调用方职责：使用外部 KDF 派生上述 98 字节密钥材料。

## next

```cpp
u64 next();
```

单步推进。每步：读矩阵一行 → 8 位掩码选通 LFSR → Galois 推进 → 乘性混合 → 输出 + 回填 + ROL8。常数时间。

## keystream

```cpp
void keystream(void* out, std::size_t bytes);
```

批量密钥流。内部 8 步批处理 (面隔离)，`bytes` 不足 64 时回退单步。

输出格式：64 位块小端写入。

## 使用示例

```cpp
#include "src/hlfsr64.hpp"

// 外部 KDF 派生 98 字节材料
uint8_t matrix[64], seed[32]; uint16_t idx;
hkdf_sha256(key, nonce, matrix, seed, (uint8_t*)&idx);

// 加密
hlfsr64 ctx;
ctx.init(matrix, seed, idx & 0x1FF);
uint8_t buf[1024];
ctx.keystream(buf, sizeof(buf));
for (auto& b : buf) b ^= plaintext[i];  // XOR encrypt
```
