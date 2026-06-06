// hlfsr_stream.hpp — X25519 + HKDF + HLFSR-64 V10 + Poly1305 流协议
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace hlfsr_proto {

// 协议常量
constexpr size_t X25519_PUBKEY_LEN  = 32;   // X25519 公钥
constexpr size_t X25519_PRIVKEY_LEN = 32;   // X25519 私钥
constexpr size_t HLFSR_MATERIAL     = 98;   // 64 matrix + 32 seed + 2 idx
constexpr size_t POLY1305_KEY_LEN   = 32;   // Poly1305 认证密钥
constexpr size_t HKDF_OUTPUT_LEN    = HLFSR_MATERIAL + POLY1305_KEY_LEN; // 130
constexpr size_t POLY1305_TAG_LEN   = 16;   // 认证标签
constexpr size_t EPHEMERAL_LEN      = X25519_PUBKEY_LEN; // 临时公钥

// 消息格式:
//   ephemeral_pk (32) | ciphertext (N) | poly1305_tag (16)
// 总开销: 48 bytes/消息

struct KeyPair {
    uint8_t pub[X25519_PUBKEY_LEN];
    uint8_t priv[X25519_PRIVKEY_LEN];
};

// 生成 X25519 密钥对 (调用 OpenSSL)
KeyPair keygen();

// 加密: sender(effemeral priv, receiver pub) → ciphertext
// 返回: ephemeral_pk + ciphertext + tag (串联, 调用方释放)
std::vector<uint8_t> encrypt(const uint8_t* plaintext, size_t plain_len,
                              const uint8_t recv_pub[X25519_PUBKEY_LEN]);

// 解密: receiver(receiver priv) → plaintext
// 输入: ephemeral_pk + ciphertext + tag (串联)
// 返回空 vector 表示认证失败
std::vector<uint8_t> decrypt(const uint8_t* wire_data, size_t wire_len,
                              const uint8_t recv_priv[X25519_PRIVKEY_LEN]);

} // namespace hlfsr_proto
