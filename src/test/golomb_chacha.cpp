// golomb_chacha.cpp — ChaCha20 基线对比（同机同测验框架）
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <ctime>

using clock_ns = std::chrono::high_resolution_clock;

#if defined(__GNUC__)
    #define CMP_NAME "GCC"
    #define CMP_VER  __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__
#elif defined(__clang__)
    #define CMP_NAME "Clang"
    #define CMP_VER  __clang_major__, __clang_minor__, __clang_patchlevel__
#elif defined(_MSC_VER)
    #define CMP_NAME "MSVC"
    #define CMP_VER  _MSC_VER / 100, (_MSC_VER / 10) % 10, _MSC_VER % 10
#else
    #define CMP_NAME "Unknown"
    #define CMP_VER  0, 0, 0
#endif

#define LOG(f, ...) do { \
    std::fprintf(f, __VA_ARGS__); std::fflush(f); \
    std::fprintf(stdout, __VA_ARGS__); \
} while(0)

typedef unsigned char u8;
typedef unsigned long long u64;

// ============================================================
// G1-G3 测验（与 golomb_test.cpp 一致的实现）
// ============================================================
static void test_balance(std::FILE* f, const u8* data, std::size_t bytes) {
    std::size_t ones = 0, total = bytes * 8;
    for (std::size_t i = 0; i < bytes; i++)
        for (int j = 0; j < 8; j++)
            if (data[i] & (1 << j)) ones++;
    double ratio = (double)ones / (double)total;
    LOG(f, "G1 Monobit: 1=%llu (%.4f%%) 偏差=%.4f%%  %s\n\n",
        (unsigned long long)ones, 100.0*ratio, 100.0*(ratio-0.5),
        std::abs(ratio-0.5)<0.005 ? "PASS":"FAIL");
}

static void test_runs(std::FILE* f, const u8* data, std::size_t bytes) {
    constexpr int MR = 16;
    std::size_t r0[MR+1]={0}, r1[MR+1]={0};
    int prev=-1, len=0;
    std::size_t tr0=0, tr1=0;
    for (std::size_t i=0; i<bytes; i++)
        for (int j=0; j<8; j++) {
            int b = (data[i]>>j)&1;
            if (b==prev) len++;
            else {
                if (prev>=0) {
                    int idx = (len<MR)?(len-1):MR;
                    if(prev==0){r0[idx]++;tr0++;}else{r1[idx]++;tr1++;}
                }
                prev=b; len=1;
            }
        }
    if (prev>=0) {
        int idx = (len<MR)?(len-1):MR;
        if(prev==0){r0[idx]++;tr0++;}else{r1[idx]++;tr1++;}
    }
    std::size_t tr = tr0+tr1;
    LOG(f, "G2 Runs: total=%llu (0:%llu 1:%llu)\n", (unsigned long long)tr, (unsigned long long)tr0, (unsigned long long)tr1);
    double c2=0; int cn=0;
    for (int k=1; k<=MR; k++) {
        int idx=k-1;
        double e0=(double)tr0/(1ULL<<k), e1=(double)tr1/(1ULL<<k);
        if (k==MR) { e0=(double)tr0/(1ULL<<(MR-1)); e1=(double)tr1/(1ULL<<(MR-1)); }
        if (e0>0) { double d=(double)r0[idx]-e0; c2+=d*d/e0; cn++; }
        if (e1>0) { double d=(double)r1[idx]-e1; c2+=d*d/e1; cn++; }
    }
    int dof=cn-1;
    double crit=(double)dof+3.0*std::sqrt(2.0*(double)dof);
    LOG(f, "  chi2=%.1f dof~%d crit=%.1f %s\n\n", c2, dof, crit, c2<crit?"PASS":"FAIL");
}

static void test_autocorr(std::FILE* f, const u8* data, std::size_t bytes) {
    constexpr int MS=32;
    constexpr std::size_t TB=1048576;
    std::size_t total = (bytes*8<TB)?bytes*8:TB;
    if (total<MS*64) total=MS*64;
    std::size_t nw=(total+63)/64;
    u64* bits=new u64[nw];
    std::memcpy(bits,data,(nw*8<bytes)?nw*8:bytes);
    int fc=0;
    for (int d=1; d<=MS; d++) {
        std::size_t agree=0, n=total-d;
        for(std::size_t i=0;i<n;i++){
            bool b0=(bits[i/64]>>(i%64))&1, b1=(bits[(i+d)/64]>>((i+d)%64))&1;
            if(b0==b1)agree++;
        }
        double ad=2.0*(double)agree/(double)n-1.0;
        double sigma=1.0/std::sqrt((double)n);
        double z=std::abs(ad)/sigma;
        if(z>=3.3)fc++;
        LOG(f,"  d=%2d A=%+12.6f (%.1fσ) %s\n",d,ad,z,z<3.3?"PASS":"WARN");
    }
    LOG(f,"  warns=%d/32 (expect<=1) %s\n\n",fc,fc<=2?"PASS":"FAIL");
    delete[] bits;
}

// ============================================================
int main(int argc, char* argv[]) {
    std::size_t smib=(argc>1)?(std::size_t)std::atol(argv[1]):16;
    std::size_t sbytes=smib*1024ULL*1024ULL;

    auto now=std::time(nullptr);
    auto tm=*std::localtime(&now);
    char path[256];
    std::snprintf(path,sizeof(path),"benchmarks/chacha_%04d_%02d_%02d_%02d_%02d_%02d.txt",
        tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);
    std::FILE* f=std::fopen(path,"w");
    if(!f){std::fprintf(stderr,"can't create %s\n",path);return 1;}

    LOG(f,"ChaCha20 基线 Golomb 测验 (OpenSSL)\n");
    LOG(f,"======================================\n");
    LOG(f,"时间: %s",std::ctime(&now));
    LOG(f,"编译器: " CMP_NAME " %d.%d.%d\n",CMP_VER);
    LOG(f,"样本: %llu MiB\n\n",(unsigned long long)smib);

    u8 key[32], iv[12];
    if(RAND_bytes(key,32)!=1||RAND_bytes(iv,12)!=1){
        LOG(f,"RAND_bytes fail\n");std::fclose(f);return 1;}

    // OpenSSL ChaCha20: 生成连续的零计数器的密钥流
    u8* buf = new u8[sbytes];
    if (!buf) { LOG(f, "alloc fail\n"); std::fclose(f); return 1; }
    // 填充零作为明文（加密零 = 密钥流）
    std::memset(buf, 0, sbytes);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outl;

    LOG(f, "生成 %llu MiB ChaCha20 密钥流...\n", (unsigned long long)smib);
    auto t0 = clock_ns::now();

    // ChaCha20 IV: 16 字节 = counter(4 LE) || nonce(12)
    u8 chacha_iv[16] = {0};
    std::memcpy(chacha_iv + 4, iv, 12);
    EVP_EncryptInit_ex(ctx, EVP_chacha20(), nullptr, key, chacha_iv);

    u8* p = buf;
    std::size_t remain = sbytes;
    while (remain > 0) {
        int chunk = (remain > 1024 * 1024) ? 1024 * 1024 : (int)remain;
        EVP_EncryptUpdate(ctx, p, &outl, p, chunk);
        p += outl; remain -= outl;
    }
    int finl;
    EVP_EncryptFinal_ex(ctx, p, &finl);
    EVP_CIPHER_CTX_free(ctx);

    auto t1 = clock_ns::now();
    double dt = std::chrono::duration<double>(t1 - t0).count();
    LOG(f, "耗时: %.3f s (%.1f MiB/s)\n\n", dt,
        (double)sbytes / dt / (1024.0 * 1024.0));

    test_balance(f, buf, sbytes);
    test_runs(f, buf, sbytes);
    test_autocorr(f, buf, sbytes);

    LOG(f,"结果: %s\n",path);
    delete[] buf;
    std::fclose(f);
    return 0;
}
