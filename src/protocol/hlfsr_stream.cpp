// hlfsr_stream.cpp — X25519 + HKDF + HLFSR-64 V10 + Poly1305
#include "hlfsr_stream.hpp"
#include "../hlfsr64.hpp"
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <cstring>

namespace hlfsr_proto {

static void die(const char* msg) {
    fprintf(stderr, "[hlfsr_proto] %s\n", msg);
    ERR_print_errors_fp(stderr);
    std::abort();
}

KeyPair keygen() {
    KeyPair kp{};
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0) die("X25519 keygen init");
    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) die("X25519 keygen");
    EVP_PKEY_CTX_free(ctx);
    size_t pub_len = X25519_PUBKEY_LEN, priv_len = X25519_PRIVKEY_LEN;
    EVP_PKEY_get_raw_public_key(pkey, kp.pub, &pub_len);
    EVP_PKEY_get_raw_private_key(pkey, kp.priv, &priv_len);
    EVP_PKEY_free(pkey);
    return kp;
}

static void ecdh(uint8_t out[32], const uint8_t priv[32], const uint8_t pub[32]) {
    EVP_PKEY* pk = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv, 32);
    EVP_PKEY* peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, pub, 32);
    if (!pk || !peer) die("ECDH key import");
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pk, nullptr);
    if (!ctx || EVP_PKEY_derive_init(ctx) <= 0 || EVP_PKEY_derive_set_peer(ctx, peer) <= 0)
        die("ECDH init");
    size_t len = 32;
    if (EVP_PKEY_derive(ctx, out, &len) <= 0) die("ECDH derive");
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pk);
    EVP_PKEY_free(peer);
}

static void hkdf(uint8_t out[HKDF_OUTPUT_LEN], const uint8_t* ikm, size_t ikm_len,
                  const uint8_t* info, size_t info_len) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx || EVP_PKEY_derive_init(ctx) <= 0) die("HKDF init");
    if (EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) <= 0) die("HKDF md");
    if (EVP_PKEY_CTX_set1_hkdf_salt(ctx, nullptr, 0) <= 0) die("HKDF salt");
    if (EVP_PKEY_CTX_set1_hkdf_key(ctx, ikm, ikm_len) <= 0) die("HKDF key");
    if (EVP_PKEY_CTX_add1_hkdf_info(ctx, info, info_len) <= 0) die("HKDF info");
    size_t len = HKDF_OUTPUT_LEN;
    if (EVP_PKEY_derive(ctx, out, &len) <= 0) die("HKDF derive");
    EVP_PKEY_CTX_free(ctx);
}

std::vector<uint8_t> encrypt(const uint8_t* pt, size_t pt_len,
                              const uint8_t recv_pub[32]) {
    // 1. 生成临时密钥对
    KeyPair eph = keygen();

    // 2. ECDH: 临时私钥 × 接收方公钥
    uint8_t shared[32];
    ecdh(shared, eph.priv, recv_pub);

    // 3. HKDF 派生 130 字节 (98 HLFSR + 32 Poly1305)
    const uint8_t info[] = "HLFSR64-V11-ECIES";
    uint8_t derived[HKDF_OUTPUT_LEN];
    hkdf(derived, shared, 32, info, sizeof(info) - 1);

    // 4. HLFSR 加密
    std::vector<uint8_t> ct(pt_len);
    {
        hlfsr64 h;
        h.init(derived, *(uint16_t*)(derived + 64) & 0x1FF);
        h.keystream(ct.data(), pt_len);
        for (size_t i = 0; i < pt_len; i++) ct[i] ^= pt[i];
    }

    // 5. Poly1305 MAC (认证 ciphertext)
    uint8_t tag[POLY1305_TAG_LEN];
    {
        EVP_PKEY* mac_key = EVP_PKEY_new_mac_key(EVP_PKEY_POLY1305, nullptr,
                                                   derived + HLFSR_MATERIAL, POLY1305_KEY_LEN);
        EVP_MD_CTX* mctx = EVP_MD_CTX_new();
        EVP_DigestSignInit(mctx, nullptr, nullptr, nullptr, mac_key);
        EVP_DigestSignUpdate(mctx, eph.pub, 32);
        EVP_DigestSignUpdate(mctx, ct.data(), pt_len);
        size_t tag_len = sizeof(tag);
        EVP_DigestSignFinal(mctx, tag, &tag_len);
        EVP_MD_CTX_free(mctx);
        EVP_PKEY_free(mac_key);
    }

    // 6. 组装消息: ephemeral_pk | ct | tag
    std::vector<uint8_t> out(EPHEMERAL_LEN + pt_len + POLY1305_TAG_LEN);
    memcpy(out.data(), eph.pub, EPHEMERAL_LEN);
    memcpy(out.data() + EPHEMERAL_LEN, ct.data(), pt_len);
    memcpy(out.data() + EPHEMERAL_LEN + pt_len, tag, POLY1305_TAG_LEN);
    return out;
}

std::vector<uint8_t> decrypt(const uint8_t* wire, size_t wire_len,
                              const uint8_t recv_priv[32]) {
    if (wire_len < EPHEMERAL_LEN + POLY1305_TAG_LEN) return {}; // invalid message
    size_t ct_len = wire_len - EPHEMERAL_LEN - POLY1305_TAG_LEN;
    const uint8_t* eph_pub = wire;
    const uint8_t* ct      = wire + EPHEMERAL_LEN;
    const uint8_t* tag     = wire + EPHEMERAL_LEN + ct_len;

    // 1. ECDH: 接收方私钥 × 临时公钥
    uint8_t shared[32];
    ecdh(shared, recv_priv, eph_pub);

    // 2. HKDF
    const uint8_t info[] = "HLFSR64-V11-ECIES";
    uint8_t derived[HKDF_OUTPUT_LEN];
    hkdf(derived, shared, 32, info, sizeof(info) - 1);

    // 3. Poly1305 验证
    {
        EVP_PKEY* mac_key = EVP_PKEY_new_mac_key(EVP_PKEY_POLY1305, nullptr,
                                                   derived + HLFSR_MATERIAL, POLY1305_KEY_LEN);
        EVP_MD_CTX* mctx = EVP_MD_CTX_new();
        EVP_DigestSignInit(mctx, nullptr, nullptr, nullptr, mac_key);
        EVP_DigestSignUpdate(mctx, eph_pub, 32);
        EVP_DigestSignUpdate(mctx, ct, ct_len);
        size_t tag_len = POLY1305_TAG_LEN;
        uint8_t computed[POLY1305_TAG_LEN];
        EVP_DigestSignFinal(mctx, computed, &tag_len);
        EVP_MD_CTX_free(mctx);
        EVP_PKEY_free(mac_key);
        if (CRYPTO_memcmp(computed, tag, POLY1305_TAG_LEN) != 0)
            return {}; // auth failure
    }

    // 4. HLFSR 解密
    std::vector<uint8_t> pt(ct_len);
    {
        hlfsr64 h;
        h.init(derived, *(uint16_t*)(derived + 64) & 0x1FF);
        h.keystream(pt.data(), ct_len);
        for (size_t i = 0; i < ct_len; i++) pt[i] ^= ct[i];
    }
    return pt;
}

} // namespace hlfsr_proto
