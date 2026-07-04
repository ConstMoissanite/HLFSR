#include <openssl/evp.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    const size_t MiB = 1024ULL * 1024;
    int sizes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};
    int n_sizes = 8;

    if (argc > 1) {
        sizes[0] = atoi(argv[1]);
        n_sizes  = 1;
    }

    printf("ChaCha20 AVX2 raw encryption throughput (OpenSSL EVP)\n");
    printf("  Block   GB/s\n");

    for (int si = 0; si < n_sizes; si++) {
        size_t kb    = sizes[si];
        size_t chunk = (size_t)kb * 1024;
        size_t total_bytes = 256ULL * MiB;
        if (total_bytes < chunk * 2) total_bytes = chunk * 2;
        unsigned char* buf = new unsigned char[total_bytes];
        unsigned char key[32], iv[12];
        memset(key, 0x55, 32);
        memset(iv, 0, 12);

        {
            EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
            EVP_EncryptInit_ex(ctx, EVP_chacha20(), nullptr, key, iv);
            int outl = 0;
            EVP_EncryptUpdate(ctx, buf, &outl, buf, (int)chunk);
            EVP_CIPHER_CTX_free(ctx);
        }

        auto t0 = std::chrono::high_resolution_clock::now();

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_chacha20(), nullptr, key, iv);
        int outl = 0;
        EVP_EncryptUpdate(ctx, buf, &outl, buf, (int)total_bytes);
        EVP_CIPHER_CTX_free(ctx);

        auto t1 = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double GB  = (double)total_bytes / (1024.0 * 1024.0 * 1024.0);
        printf(" %5d KB  %6.2f\n", (int)kb, GB / sec);

        delete[] buf;
    }
}
