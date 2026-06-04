# HLFSR-64 API

## 头文件

```cpp
#include "src/hlfsr64.hpp"
```

## 类型

```cpp
class hlfsr64 {
public:
    using u8  = std::uint8_t;
    using u64 = std::uint64_t;

    void init(const u8 bitmap[32], const u8 lfsr_seed[32], u8 idx_init);
    u64  next();
    void keystream(void* out, std::size_t bytes);
};
```

## init

```cpp
void init(const u8 bitmap[32], const u8 lfsr_seed[32], u8 idx_init);
```

初始化内部状态。三个参数均由外部 KDF 提供，库不做密钥派生。

| 参数 | 大小 | 说明 |
|------|------|------|
| `bitmap` | 32 字节 | 256 位内部状态，同时作为数据存储器和指令存储器 |
| `lfsr_seed` | 32 字节 | 16 条基底 LFSR 初态种子，库内部以步长 2 滑动窗口填入各 LFSR |
| `idx_init` | 1 字节 | 起始比特游标（0~255），首次取 curbit / sel 的位置 |

调用方职责：
- 使用选定的 KDF（SHA-256 / BLAKE3 / HKDF 等）从主密钥派生上述材料
- 同一密钥下确保 Nonce 不重复
- 建议检查并拒绝退化种子（bitmap 全 0 或全 1）

## next

```cpp
u64 next();
```

单步推进：推进全部 16 条 LFSR，产出 64 位密钥流，执行一步指令，更新 idx。

- **返回值**：64 位密钥流（小端字节序）
- **常数时间**：无秘密依赖分支
- **可重复调用**：状态在每次调用间持续演化

## keystream

```cpp
void keystream(void* out, std::size_t bytes);
```

批量产生密钥流。

- `out`：输出缓冲区，调用方分配
- `bytes`：所需字节数（公开值，尾部不足 8 字节时截断最后一个块）

等价于循环调用 `next()` 并将结果按小端写入 `out`。

## 使用示例

```cpp
#include "src/hlfsr64.hpp"

// 外部 KDF
uint8_t master_key[32], nonce[12];
uint8_t bitmap[32], lfsr_seed[32], idx_init;
hkdf_sha256(master_key, nonce, bitmap, lfsr_seed, &idx_init);

// 加密
hlfsr64 ctx;
ctx.init(bitmap, lfsr_seed, idx_init);

uint8_t plaintext[]  = "Hello, World!";
uint8_t ciphertext[sizeof(plaintext)];

ctx.keystream(ciphertext, sizeof(plaintext));
for (size_t i = 0; i < sizeof(plaintext); i++)
    ciphertext[i] ^= plaintext[i];

// 解密：重置后相同密钥流
ctx.init(bitmap, lfsr_seed, idx_init);
ctx.keystream(ciphertext, sizeof(ciphertext));
for (size_t i = 0; i < sizeof(ciphertext); i++)
    ciphertext[i] ^= ciphertext[i];  // 还原为明文
```
