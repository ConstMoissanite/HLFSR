// hlfsr_bench.cpp — HLFSR-64 集成 LittleBenchmark
// 编译: g++ -std=c++20 hlfsr_bench.cpp ../hlfsr64.cpp -lssl -lcrypto -o hlfsr_bench
//       从 OpenSSL_LittleBenchmark 目录运行，或设置 -I 路径
#include "../hlfsr64.hpp"
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
// 工具：计时统计
// ============================================================
struct BenchStats {
    double min_ms, max_ms, avg_ms, stddev_ms, throughput_mbps;
    size_t data_size_bytes, rounds;
};

template <typename OpFunc>
static BenchStats measure(OpFunc &&op, size_t data_size_bytes, size_t rounds,
                          const std::string &label) {
    std::vector<double> times; times.reserve(rounds);
    op(); // warmup
    for (size_t i = 0; i < rounds; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        op();
        auto t1 = std::chrono::high_resolution_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    double sum = std::accumulate(times.begin(), times.end(), 0.0);
    double avg = sum / times.size();
    double min_t = *std::min_element(times.begin(), times.end());
    double max_t = *std::max_element(times.begin(), times.end());
    double sq = 0;
    for (double t : times) sq += (t - avg) * (t - avg);
    double sd = std::sqrt(sq / times.size());
    double mb = data_size_bytes / (1024.0 * 1024.0);
    double tp = (avg > 0) ? (mb / (avg / 1000.0)) : 0.0;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "┌─ " << label << "\n";
    std::cout << "│  Size : " << data_size_bytes << " B  Rounds: " << rounds << "\n";
    std::cout << "│  Min/Max/Avg: " << min_t << " / " << max_t << " / " << avg << " ms\n";
    std::cout << "│  Throughput: " << std::setprecision(2) << tp << " MB/s\n";
    std::cout << "└──────────────────\n\n";
    return {min_t, max_t, avg, sd, tp, data_size_bytes, rounds};
}

// ============================================================
// HLFSR-64 对称加密基准
// ============================================================
static void bench_hlfsr_symmetric(size_t data_kb = 1, size_t rounds = 500) {
    std::cout << "\n===== HLFSR-64 Symmetric Encrypt+Decrypt =====\n";
    std::cout << "Data: " << data_kb << " kB, Rounds: " << rounds << "\n\n";

    const size_t data_len = data_kb * 1024;

    // 生成随机密钥材料
    hlfsr64::u8 km[64]; hlfsr64::u16 idx;
    RAND_bytes(km, 64);
    RAND_bytes((unsigned char*)&idx, 2);
    idx &= 0x1FF;

    std::cout << "[Setup] HLFSR-64 initialized (8x8x8 matrix, 8 Galois LFSRs)\n";

    std::vector<uint8_t> plaintext(data_len), ciphertext(data_len);
    RAND_bytes(plaintext.data(), data_len);
    std::cout << "[Setup] Plaintext ready (" << data_len << " bytes)\n";

    measure([&]() {
        hlfsr64 enc;
        enc.init(km, idx);
        enc.keystream(ciphertext.data(), data_len);
        for (size_t i = 0; i < data_len; i++) ciphertext[i] ^= plaintext[i];

        std::vector<uint8_t> saved_ct(ciphertext);
	    hlfsr64 dec;
	    dec.init(km, idx);
        dec.keystream(ciphertext.data(), data_len);
        for (size_t i = 0; i < data_len; i++) ciphertext[i] ^= saved_ct[i];
    }, data_len, rounds, "HLFSR-64 Encrypt+Decrypt");
}

// ============================================================
// HLFSR-64 ECIES 基准 (ECDH + HLFSR)
// ============================================================
static void bench_hlfsr_ecies(int curve_nid, const std::string &algo_name,
                              size_t data_kb = 1, size_t rounds = 500) {
    std::cout << "\n===== HLFSR-64 ECIES: " << algo_name << " (ECDH + HLFSR) =====\n";
    std::cout << "Data: " << data_kb << " kB, Rounds: " << rounds << "\n\n";

    bool is_x = (curve_nid == EVP_PKEY_X25519);
    bool is_sm2 = (curve_nid == EVP_PKEY_SM2);

    auto gen_ec_key = [&]() -> EVP_PKEY* {
        int nid = is_x ? EVP_PKEY_X25519 : (is_sm2 ? EVP_PKEY_SM2 : EVP_PKEY_EC);
        EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(nid, nullptr);
        EVP_PKEY_keygen_init(pctx);
        if (!is_x && !is_sm2) EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, curve_nid);
        else if (is_sm2) EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_sm2);
        EVP_PKEY *k = nullptr; EVP_PKEY_keygen(pctx, &k);
        EVP_PKEY_CTX_free(pctx);
        return k;
    };

    EVP_PKEY *receiver = gen_ec_key();
    std::cout << "[Setup] " << algo_name << " receiver key generated.\n";

    const size_t data_len = data_kb * 1024;
    std::vector<uint8_t> plaintext(data_len), buf(data_len);
    RAND_bytes(plaintext.data(), data_len);
    std::cout << "[Setup] Plaintext ready.\n";

    static const uint8_t info[] = "HLFSR-ECIES-v1";
    static const size_t info_len = 15;
    // HLFSR needs: 64B key_material + 2B idx = 66 bytes
    static const int hlfsr_material = 66;

    measure([&]() {
        // ── 发送方 ──
        EVP_PKEY *eph = gen_ec_key();
        EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(eph, nullptr);
        EVP_PKEY_derive_init(dctx);
        EVP_PKEY_derive_set_peer(dctx, receiver);
        uint8_t shared[64]; size_t slen = sizeof(shared);
        EVP_PKEY_derive(dctx, shared, &slen);
        EVP_PKEY_CTX_free(dctx);

        uint8_t material[hlfsr_material];
        EVP_PKEY_CTX *kdf = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        EVP_PKEY_derive_init(kdf);
        EVP_PKEY_CTX_set_hkdf_md(kdf, EVP_sha256());
        EVP_PKEY_CTX_set1_hkdf_salt(kdf, nullptr, 0);
        EVP_PKEY_CTX_set1_hkdf_key(kdf, shared, slen);
        EVP_PKEY_CTX_add1_hkdf_info(kdf, info, info_len);
        size_t mlen = sizeof(material);
        EVP_PKEY_derive(kdf, material, &mlen);
        EVP_PKEY_CTX_free(kdf);

        // Encrypt
        hlfsr64 enc;
        enc.init(material, *(hlfsr64::u16*)(material + 64));
        enc.keystream(buf.data(), data_len);
        for (size_t i = 0; i < data_len; i++) buf[i] ^= plaintext[i];

        // ── 接收方 ──
        dctx = EVP_PKEY_CTX_new(receiver, nullptr);
        EVP_PKEY_derive_init(dctx);
        EVP_PKEY_derive_set_peer(dctx, eph);
        uint8_t shared2[64]; size_t s2 = sizeof(shared2);
        EVP_PKEY_derive(dctx, shared2, &s2);
        EVP_PKEY_CTX_free(dctx);

        uint8_t material2[hlfsr_material];
        kdf = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        EVP_PKEY_derive_init(kdf);
        EVP_PKEY_CTX_set_hkdf_md(kdf, EVP_sha256());
        EVP_PKEY_CTX_set1_hkdf_salt(kdf, nullptr, 0);
        EVP_PKEY_CTX_set1_hkdf_key(kdf, shared2, s2);
        EVP_PKEY_CTX_add1_hkdf_info(kdf, info, info_len);
        mlen = sizeof(material2);
        EVP_PKEY_derive(kdf, material2, &mlen);
        EVP_PKEY_CTX_free(kdf);

        // Decrypt
        std::vector<uint8_t> saved_ct2(buf.begin(), buf.end());
        hlfsr64 dec;
        dec.init(material2, *(hlfsr64::u16*)(material2 + 64));
        dec.keystream(buf.data(), data_len);
        for (size_t i = 0; i < data_len; i++) buf[i] ^= saved_ct2[i];

        EVP_PKEY_free(eph);
    }, data_len, rounds, "HLFSR-64 " + algo_name + " ECIES");

    EVP_PKEY_free(receiver);
}

// ============================================================
// OpenSSL 对称加密基准（对比用）
// ============================================================
static void bench_ossl_symmetric(const EVP_CIPHER *cipher, const std::string &name,
                                  int key_len, int iv_len, size_t data_kb, size_t rounds) {
    std::vector<uint8_t> key(key_len), iv(iv_len);
    RAND_bytes(key.data(), key_len); RAND_bytes(iv.data(), iv_len);
    const size_t data_len = data_kb * 1024;
    std::vector<uint8_t> pt(data_len), ct(data_len + 16);
    RAND_bytes(pt.data(), data_len);

    measure([&]() {
        EVP_CIPHER_CTX *enc = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(enc, cipher, nullptr, key.data(), iv.data());
        int ol = 0, tl = 0;
        EVP_EncryptUpdate(enc, ct.data(), &ol, pt.data(), (int)data_len);
        EVP_EncryptFinal_ex(enc, ct.data() + ol, &tl);
        int total = ol + tl;
        EVP_CIPHER_CTX_free(enc);

        EVP_CIPHER_CTX *dec = EVP_CIPHER_CTX_new();
        EVP_DecryptInit_ex(dec, cipher, nullptr, key.data(), iv.data());
        std::vector<uint8_t> db(total + 16);
        EVP_DecryptUpdate(dec, db.data(), &ol, ct.data(), total);
        EVP_DecryptFinal_ex(dec, db.data() + ol, &tl);
        EVP_CIPHER_CTX_free(dec);
    }, data_len, rounds, name + " Encrypt+Decrypt");
}

// ============================================================
// OpenSSL ECIES 基准（对比用）
// ============================================================
static void bench_ossl_ecies(int curve_nid, const std::string &name,
                              size_t data_kb, size_t rounds) {
    bool is_x = (curve_nid == EVP_PKEY_X25519);
    bool is_sm2 = (curve_nid == EVP_PKEY_SM2);
    auto gen_key = [&]() -> EVP_PKEY* {
        int nid = is_x ? EVP_PKEY_X25519 : (is_sm2 ? EVP_PKEY_SM2 : EVP_PKEY_EC);
        EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(nid, nullptr);
        EVP_PKEY_keygen_init(pctx);
        if (!is_x && !is_sm2) EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, curve_nid);
        else if (is_sm2) EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_sm2);
        EVP_PKEY *k = nullptr; EVP_PKEY_keygen(pctx, &k);
        EVP_PKEY_CTX_free(pctx); return k;
    };
    EVP_PKEY *recv = gen_key();
    const size_t data_len = data_kb * 1024;
    std::vector<uint8_t> pt(data_len), buf(data_len + 16);
    RAND_bytes(pt.data(), data_len);
    static const uint8_t info[] = "ECIES-v1";

    measure([&]() {
        EVP_PKEY *eph = gen_key();
        EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(eph, nullptr);
        EVP_PKEY_derive_init(dctx); EVP_PKEY_derive_set_peer(dctx, recv);
        uint8_t sh[64]; size_t sl = sizeof(sh);
        EVP_PKEY_derive(dctx, sh, &sl); EVP_PKEY_CTX_free(dctx);

        uint8_t ck[32];
        EVP_PKEY_CTX *kdf = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        EVP_PKEY_derive_init(kdf);
        EVP_PKEY_CTX_set_hkdf_md(kdf, EVP_sha256());
        EVP_PKEY_CTX_set1_hkdf_salt(kdf, nullptr, 0);
        EVP_PKEY_CTX_set1_hkdf_key(kdf, sh, sl);
        EVP_PKEY_CTX_add1_hkdf_info(kdf, info, 8);
        size_t kl = sizeof(ck); EVP_PKEY_derive(kdf, ck, &kl);
        EVP_PKEY_CTX_free(kdf);

        uint8_t iv[12]; RAND_bytes(iv, 12);
        EVP_CIPHER_CTX *enc = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(enc, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr);
        EVP_CIPHER_CTX_ctrl(enc, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr);
        EVP_EncryptInit_ex(enc, nullptr, nullptr, ck, iv);
        int ol = 0, tl = 0;
        EVP_EncryptUpdate(enc, buf.data(), &ol, pt.data(), (int)data_len);
        EVP_EncryptFinal_ex(enc, buf.data() + ol, &tl);
        int total = ol + tl;
        uint8_t tag[16];
        EVP_CIPHER_CTX_ctrl(enc, EVP_CTRL_AEAD_GET_TAG, 16, tag);
        EVP_CIPHER_CTX_free(enc);

        dctx = EVP_PKEY_CTX_new(recv, nullptr);
        EVP_PKEY_derive_init(dctx); EVP_PKEY_derive_set_peer(dctx, eph);
        uint8_t sh2[64]; size_t s2 = sizeof(sh2);
        EVP_PKEY_derive(dctx, sh2, &s2); EVP_PKEY_CTX_free(dctx);

        uint8_t ck2[32];
        kdf = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        EVP_PKEY_derive_init(kdf);
        EVP_PKEY_CTX_set_hkdf_md(kdf, EVP_sha256());
        EVP_PKEY_CTX_set1_hkdf_salt(kdf, nullptr, 0);
        EVP_PKEY_CTX_set1_hkdf_key(kdf, sh2, s2);
        EVP_PKEY_CTX_add1_hkdf_info(kdf, info, 8);
        kl = sizeof(ck2); EVP_PKEY_derive(kdf, ck2, &kl);
        EVP_PKEY_CTX_free(kdf);

        EVP_CIPHER_CTX *dec = EVP_CIPHER_CTX_new();
        EVP_DecryptInit_ex(dec, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr);
        EVP_CIPHER_CTX_ctrl(dec, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr);
        EVP_DecryptInit_ex(dec, nullptr, nullptr, ck2, iv);
        std::vector<uint8_t> db(total + 16);
        EVP_DecryptUpdate(dec, db.data(), &ol, buf.data(), total);
        EVP_CIPHER_CTX_ctrl(dec, EVP_CTRL_AEAD_SET_TAG, 16, (void*)tag);
        EVP_DecryptFinal_ex(dec, db.data() + ol, &tl);
        EVP_CIPHER_CTX_free(dec);
        EVP_PKEY_free(eph);
    }, data_len, rounds, name + " ECIES (ChaCha20-Poly1305)");
}

// ============================================================
// Main
// ============================================================
int main(int argc, char *argv[]) {
    size_t data_kb = (argc > 1) ? std::stoull(argv[1]) : 8192; // 默认 8 MB
    size_t rounds  = (argc > 2) ? std::stoull(argv[2]) : 50;

    std::cout << "HLFSR-64 " HLFSR_VARIANT "  vs OpenSSL\n";
    std::cout << "Matrix: " << HLFSR_MATRIX << "^3 (" << HLFSR_MATRIX_BYTES << "B)  "
              << "LFSR: " << HLFSR_LFSR_COUNT << "x64b Galois  "
              << "Mask: " << HLFSR_MASK_BITS << "b  "
              << "Idx: " << HLFSR_IDX_BITS << "b  Output: " << HLFSR_OUTPUT_BITS << "b\n";
    std::cout << "Data: " << data_kb << " kB, Rounds: " << rounds << "\n\n";

    // ═══════════ Symmetric ═══════════
    std::cout << "═════ Symmetric Encrypt+Decrypt ═════\n";
    bench_hlfsr_symmetric(data_kb, rounds);
    bench_ossl_symmetric(EVP_chacha20_poly1305(), "ChaCha20-Poly1305", 32, 12, data_kb, rounds);
    bench_ossl_symmetric(EVP_aes_256_gcm(), "AES-256-GCM", 32, 12, data_kb, rounds);
    bench_ossl_symmetric(EVP_sm4_cbc(), "SM4-CBC", 16, 16, data_kb, rounds);

    // ═══════════ ECIES ═══════════
    std::cout << "═════ ECIES (ECDH + Symmetric) ═════\n";
    bench_hlfsr_ecies(EVP_PKEY_X25519, "X25519", data_kb, rounds);
    bench_ossl_ecies(EVP_PKEY_X25519, "X25519", data_kb, rounds);
    bench_hlfsr_ecies(EVP_PKEY_SM2, "SM2", data_kb, rounds);
    bench_ossl_ecies(EVP_PKEY_SM2, "SM2", data_kb, rounds);
    bench_hlfsr_ecies(NID_secp256k1, "secp256k1", data_kb, rounds);
    bench_ossl_ecies(NID_secp256k1, "secp256k1", data_kb, rounds);

    std::cout << "Done.\n";
    return 0;
}
