# HLFSR-64 API

## 头文件

```cpp
#include "src/hlfsr64.hpp"
```

## 版本常量

```cpp
#define HLFSR_VERSION     12
#define HLFSR_VARIANT     "V12-MM"
#define HLFSR_MATRIX      8        // 8×8×8
#define HLFSR_LFSR_COUNT  8        // 8 条 Galois LFSR
#define HLFSR_MASK_BITS   8        // 8 位掩码 → 64b 乘子
#define HLFSR_MATRIX_BYTES 64     // 512 bits
#define HLFSR_KEY_BYTES   64      // key_material
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

三阶段初始化：

1. **三乘积自混合** — 64 字节密钥材料展开为 24 词池（原始 + ROTL23 + ROTL41），三角乘积生成 matrix 和 LFSR 初始状态
2. **8×8 MDS over GF(2^8)** — circulant [2,3,1,1,1,1,1,1] 逐列混合，将局部依赖（3/8）扩展为全依赖（8/8）
3. **64 步预热** — 运行 64 步 next() 丢弃输出，完整初始化扩散

| 参数 | 说明 |
|------|------|
| `key_material` | 64 字节, 拒绝全零 |
| `idx_init` | 16-bit, 低 9 位起始游标 (0–511), 高 7 位矩阵搅拌 (128 种变体) |

## next

```cpp
u64 next();
```

单步推进。全 8 条 LFSR 无条件异或，vx 经 ROTL33 自旋转后与 mask 派生的奇数乘子 mk 和 K₁ 级联相乘。16-bit 反馈取乘法结果低 16 位乘 K₂，低 8 位回填当前矩阵地址并 ROTL8 行内扩散，高 8 位交叉注入相邻面。mask=0 时 mk=1 自然恒等，无分支。

常数时间。

## keystream

```cpp
void keystream(void* out, std::size_t bytes);
```

批量密钥流。内部 8 步批处理利用面隔离无 RAW 冲突。输出 64 位小端写入。

## 使用示例

```cpp
#include "src/hlfsr64.hpp"

uint8_t km[64]; uint16_t idx;
// 调用方用 HKDF 等派生 km + idx
hkdf_sha256(key, nonce, km, sizeof(km), (uint8_t*)&idx, sizeof(idx));

hlfsr64 ctx;
ctx.init(km, idx & 0x1FF);
uint8_t buf[1024];
ctx.keystream(buf, sizeof(buf));
for (size_t i = 0; i < sizeof(buf); i++) buf[i] ^= plaintext[i];
```

## 编译

```
g++ -std=c++14 -O3 -march=native src/hlfsr64.cpp your_app.cpp
```

推荐 `-O3`，编译器自动向量化全异或路径。无需 `-mavx2`，标量路径已最优。
