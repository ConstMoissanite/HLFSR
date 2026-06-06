# HLFSR-64 V11-Uni API

## 头文件

```cpp
#include "src/hlfsr64.hpp"
```

## 版本常量

```cpp
#define HLFSR_VERSION     11
#define HLFSR_VARIANT     "V11-Uni"
#define HLFSR_MATRIX      8        // 8×8×8
#define HLFSR_LFSR_COUNT  8        // 8 条 Galois LFSR
#define HLFSR_MASK_BITS   8        // 8 位掩码选通
#define HLFSR_MATRIX_BYTES 64     // 512 bits
#define HLFSR_IDX_BITS     9      // idx 0-511
```

## 类型

```cpp
class hlfsr64 {
public:
    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u64 = std::uint64_t;

    void init(const u8 key_material[64], u16 idx_init);
    u64  next();
    void keystream(void* out, std::size_t bytes);
};
```

## init

```cpp
void init(const u8 key_material[64], u16 idx_init);
```

64 字节密钥材料统一初始化矩阵和 8 条 LFSR。

| 参数 | 大小 | 说明 |
|------|------|------|
| `key_material` | 64 字节 | 前 64B→matrix, LFSR[i]=km[i*8..i*8+7] (小端) |
| `idx_init` | 2 字节 (u16) | 起始游标 (0–511)，低 9 位有效 |

调用方职责：使用外部 KDF 派生 64+2=66 字节材料。

## next

```cpp
u64 next();
```

单步推进。每步：读矩阵一行 → 8 位掩码选通 LFSR → Galois 推进 → 乘性混合 → 输出 + 回填 + ROL8。mask=0 时回退 LFSR[idx&7]。常数时间。

## keystream

```cpp
void keystream(void* out, std::size_t bytes);
```

批量密钥流。内部 8 步批处理。输出 64 位小端写入。

## 使用示例

```cpp
#include "src/hlfsr64.hpp"

uint8_t km[64]; uint16_t idx;
hkdf_sha256(key, nonce, km, (uint8_t*)&idx);

hlfsr64 ctx;
ctx.init(km, idx & 0x1FF);
uint8_t buf[1024];
ctx.keystream(buf, sizeof(buf));
for (auto& b : buf) b ^= plaintext[i];
```
