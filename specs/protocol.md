# HLFSR-64 流加密协议

## 概述

基于 X25519 ECDH + HKDF-SHA256 + HLFSR-64 + Poly1305 的认证加密协议。ECIES 体系，48 字节消息开销。

## 协议流程

```
发送方 (Alice)                         接收方 (Bob)
───────────                            ──────────
1. 生成临时 X25519 密钥对
2. ECDH(eph_priv, bob_pub) → shared
3. HKDF(shared, info) → 98 bytes
   ├─ [0..63]   key_material (64B) → init(matrix, LFSR) 经三乘积+MDS+64步预热
   ├─ [64..65]  idx_init (2B: 低9位起始步 + 高7位矩阵搅拌)
   └─ [66..97]  poly1305_key (32B)
4. HLFSR 加密 → ciphertext
5. Poly1305(eph_pub || ct) → tag
6. 发送: eph_pub | ct | tag
                                      7. ECDH(bob_priv, eph_pub) → shared
                                      8. HKDF(shared, info) → 130 bytes
                                      9. Poly1305 验证 tag
                                      10. HLFSR 解密 → plaintext
```

## 消息格式

```
┌──────────────┬──────────────┬──────────┐
│ eph_pub (32) │ ct (N bytes) │ tag (16) │
└──────────────┴──────────────┴──────────┘
```

总大小 = N + 48 bytes。

## 参数

| 参数 | 值 |
|------|-----|
| 密钥交换 | X25519 (RFC 7748) |
| 密钥派生 | HKDF-SHA256 (RFC 5869) |
| 流加密 | HLFSR-64 V11-Uni |
| 认证 | Poly1305 (RFC 8439) |
| HKDF info | "HLFSR64-V11-Uni-ECIES" |
| HKDF salt | 空 |
| 总开销 | 48 bytes/消息 |

## 安全性

- **前向安全性**: 每次加密使用新临时密钥对，临时私钥用完即弃
- **认证**: Poly1305 防止篡改和选择密文攻击
- **Nonce 重用**: 不需要 Nonce——每消息唯一临时密钥保证 HKDF 输入唯一
- **密钥管理**: 接收方仅需长期 X25519 密钥对，无需管理 Nonce 状态

## API

```cpp
#include "src/protocol/hlfsr_stream.hpp"

// 密钥生成
hlfsr_proto::KeyPair kp = hlfsr_proto::keygen();

// 加密
auto wire = hlfsr_proto::encrypt(plaintext, len, recv_pub);

// 解密 (失败返回空)
auto pt = hlfsr_proto::decrypt(wire.data(), wire.size(), recv_priv);
if (pt.empty()) { /* auth failure */ }
```

---

> 📋 本文档随  变更同步更新。维护规则见 [specs/design.md §9](design.md#9-文档维护规则)。
