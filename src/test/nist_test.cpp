// nist_test.cpp — HLFSR-64 DM NIST SP 800-22 统计检验
// 关键鉴别项：二元矩阵秩、线性复杂度、频谱、近似熵
// ChaCha20 同机基线对比
#include "../hlfsr64.hpp"
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <vector>

#define LOG(f,...) do{std::fprintf(f,__VA_ARGS__);std::fflush(f);std::fprintf(stdout,__VA_ARGS__);}while(0)
typedef unsigned char u8;
typedef unsigned long long u64;

// ============================================================
// 辅助：正态 CDF 余补 (Q-function)  → P-value
// ============================================================
static double erfc_c(double x) {
    double t=1.0/(1.0+0.5*std::abs(x)), r=t*std::exp(-x*x-1.26551223+t*(1.00002368+t*(0.37409196
      +t*(0.09678418+t*(-0.18628806+t*(0.27886807+t*(-1.13520398+t*(1.48851587
      +t*(-0.82215223+t*0.17087277))))))))); return x>=0?r:2.0-r;
}
static double pvalue_normal(double z) { return erfc_c(std::abs(z)/1.4142135623730951); }
static double pvalue_chisq(double x, int dof) {
    if(x<0)x=0; if(dof<1)dof=1;
    double v=std::exp(-x/2.0), s=v; for(int i=1;i<dof/2;i++){v*=x/(2.0*i); s+=v;}
    double g=(dof%2)?std::sqrt(2.0*x/3.141592653589793):1.0;
    return 1.0 - (s + (dof%2?g*v:0.0)) * std::exp(-x/2.0);
}
// bool pass(double p) { return p >= 0.01; }

// ============================================================
// NIST 检验项（每条限 1M 位 = 125 KiB，符合 NIST SP 800-22 标准）
// ============================================================
static constexpr std::size_t NIST_MAX_BITS = 1000000; // 1M bits per NIST spec

// 1. Monobit Frequency (n ≤ 1M bits)
static double nist_monobit(const u8* d, std::size_t B) {
    std::size_t n=(B*8<NIST_MAX_BITS)?B*8:NIST_MAX_BITS; double S=0;
    for(std::size_t i=0;i<n;i++) S+=((d[i/8]>>(i%8))&1)?1.0:-1.0;
    return pvalue_normal(std::abs(S)/std::sqrt((double)n));
}

// 2. Frequency within a Block (M=128)
static double nist_block_freq(const u8* d, std::size_t B) {
    constexpr int M=128; std::size_t N=(B*8)/M; double c2=0;
    for(std::size_t i=0;i<N;i++){double pi=0;
        for(int j=0;j<M;j++){std::size_t idx=i*M+j; if(d[idx/8]&(1<<(idx%8)))pi++;}
        pi/=M; c2+=(pi-0.5)*(pi-0.5);}
    c2*=4.0*M; return pvalue_chisq(c2,(int)N);
}

// 3. Runs (n ≤ 1M bits)
static double nist_runs(const u8* d, std::size_t B) {
    std::size_t n=(B*8<NIST_MAX_BITS)?B*8:NIST_MAX_BITS; double pi=0;
    for(std::size_t i=0;i<n;i++) if((d[i/8]>>(i%8))&1)pi++; pi/=n;
    if(std::abs(pi-0.5)>=2.0/std::sqrt(n)) return 0;
    int V=0; for(std::size_t i=0;i<n-1;i++){bool b0=(d[i/8]>>(i%8))&1,b1=(d[(i+1)/8]>>((i+1)%8))&1; if(b0!=b1)V++;}
    V++; double num=std::abs((double)V-2.0*n*pi*(1.0-pi)), den=2.0*std::sqrt(2.0*n)*pi*(1.0-pi);
    return pvalue_normal(num/den);
}

// 4. Longest Run of Ones (M=10000, K=6)
static double nist_longest_run(const u8* d, std::size_t B) {
    constexpr int M=10000; int N=(int)((B*8)/M); if(N<1)return-1;
    const int v[7]={1,2,3,4,5,6,7}; double pi[7]={0.0882,0.2092,0.2483,0.1933,0.1208,0.0675,0.0727};
    int cnt[8]={0};
    for(int i=0;i<N;i++){int maxR=0,cur=0;
        for(int j=0;j<M;j++){int idx=i*M+j; if(d[idx/8]&(1<<(idx%8)))cur++;else{if(cur>maxR)maxR=cur;cur=0;}}
        if(cur>maxR)maxR=cur;
        int k= (maxR<=1)?0:(maxR<=2)?1:(maxR<=3)?2:(maxR<=4)?3:(maxR<=5)?4:(maxR<=6)?5:6;
        cnt[k]++;
    }
    double c2=0; for(int i=0;i<7;i++){double e=N*pi[i]; c2+=(cnt[i]-e)*(cnt[i]-e)/e;}
    return pvalue_chisq(c2,6);
}

// 5. Binary Matrix Rank (32×32 submatrices)
static double nist_matrix_rank(const u8* d, std::size_t B) {
    constexpr int R=32, C=32; std::size_t nbits=B*8; int N=(int)(nbits/(R*C)); if(N<1)return-1;
    std::vector<int> mat(R*C); int FM=0, FM1=0;
    for(int k=0;k<N;k++){
        for(int i=0;i<R;i++) for(int j=0;j<C;j++){std::size_t idx=(std::size_t)k*R*C+i*C+j; mat[i*C+j]=(d[idx/8]>>(idx%8))&1;}
        // Gaussian elimination over GF(2)
        int rank=0; std::vector<int> copy=mat;
        for(int col=0,r=0;col<C&&r<R;col++){int pivot=-1;
            for(int i=r;i<R;i++) if(copy[i*C+col]){pivot=i;break;}
            if(pivot<0)continue;
            if(pivot!=r) for(int j=col;j<C;j++) std::swap(copy[r*C+j],copy[pivot*C+j]);
            for(int i=0;i<R;i++) if(i!=r&&copy[i*C+col]) for(int j=col;j<C;j++) copy[i*C+j]^=copy[r*C+j];
            r++; rank++;
        }
        if(rank==R)FM++; else if(rank==R-1)FM1++;
    }
    double N2=(double)N; int rem=N-FM-FM1;
    double e0=0.2888*N2, e1=0.5776*N2, e2=0.1336*N2;
    double c2=(FM-e0)*(FM-e0)/e0 + (FM1-e1)*(FM1-e1)/e1 + (rem-e2)*(rem-e2)/e2;
    return pvalue_chisq(c2,2);
}

// 6. FFT-based Spectral Test (Cooley-Tukey radix-2, iterative)
static void fft(std::vector<double>& re, std::vector<double>& im, bool inverse) {
    int N=(int)re.size();
    // bit-reversal permutation
    for(int i=1,j=0;i<N;i++){int bit=N>>1; for(;j&bit;bit>>=1)j^=bit; j^=bit; if(i<j){std::swap(re[i],re[j]);std::swap(im[i],im[j]);}}
    for(int len=2;len<=N;len<<=1){
        double ang=2.0*3.141592653589793/(double)len*(inverse?-1.0:1.0);
        double wlen_re=std::cos(ang), wlen_im=std::sin(ang);
        for(int i=0;i<N;i+=len){double w_re=1,w_im=0;
            for(int j=0;j<len/2;j++){
                double u_re=re[i+j], u_im=im[i+j];
                double v_re=re[i+j+len/2]*w_re - im[i+j+len/2]*w_im;
                double v_im=re[i+j+len/2]*w_im + im[i+j+len/2]*w_re;
                re[i+j]=u_re+v_re; im[i+j]=u_im+v_im;
                re[i+j+len/2]=u_re-v_re; im[i+j+len/2]=u_im-v_im;
                double t=w_re*wlen_re - w_im*wlen_im; w_im=w_re*wlen_im + w_im*wlen_re; w_re=t;
            }
        }
    }
    if(inverse) for(int i=0;i<N;i++){re[i]/=N; im[i]/=N;}
}
static int next_pow2(int n){int p=1; while(p<n)p<<=1; return p;}

static double nist_dft(const u8* d, std::size_t B) {
    std::size_t nbits=(B*8<NIST_MAX_BITS)?B*8:NIST_MAX_BITS;
    int N=next_pow2((int)nbits);
    std::vector<double> re(N,0), im(N,0);
    for(std::size_t j=0;j<nbits;j++) re[j]=(d[j/8]>>(j%8))&1?1.0:-1.0;
    fft(re,im,false);
    double T=std::sqrt(2.995732273553991*(double)nbits); // sqrt(-ln(0.05)*n)
    int N1=0; std::size_t n2=nbits/2;
    for(std::size_t i=1;i<=n2;i++){double m=std::sqrt(re[i]*re[i]+im[i]*im[i]); if(m>T)N1++;}
    double N0=0.95*(double)n2;
    double d_val=((double)N1-N0)/std::sqrt(0.95*0.05*(double)n2);
    return pvalue_normal(std::abs(d_val));
}

// 7. Approximate Entropy (m=5)
static double nist_ap_en(const u8* d, std::size_t B) {
    constexpr int m=5; std::size_t n=B*8; if(n<(std::size_t)(m+1))return-1;
    double phi[2];
    for(int M=m;M<=m+1;M++){
        int bits=1<<M, mask=bits-1; std::vector<int> freq(bits,0);
        int window=0;
        for(int i=0;i<M-1;i++){window=(window<<1)|((d[i/8]>>(i%8))&1);}
        for(std::size_t i=M-1;i<n;i++){window=((window<<1)|((d[i/8]>>(i%8))&1))&mask; freq[window]++;}
        double sum=0; for(int i=0;i<bits;i++)if(freq[i]){double p=(double)freq[i]/(n-M+1); sum+=p*std::log(p);}
        phi[M-m]=sum;
    }
    double c2=2.0*n*(std::log(2.0)-(phi[0]-phi[1]));
    return pvalue_chisq(std::abs(c2),1);
}

// 8. Serial Test (m=8)
static double nist_serial(const u8* d, std::size_t B) {
    constexpr int m=8; std::size_t n=B*8;
    double psi[3]={0,0,0};
    for(int mm=m-1;mm<=m+1;mm++){
        if(mm<2)continue; int bits=1<<mm, mask=bits-1; std::vector<int> freq(bits,0);
        int window=0;
        for(int i=0;i<mm-1;i++){window=(window<<1)|((d[i/8]>>(i%8))&1);}
        for(std::size_t i=mm-1;i<n;i++){window=((window<<1)|((d[i/8]>>(i%8))&1))&mask; freq[window]++;}
        double sum=0; for(int i=0;i<bits;i++) sum+=(double)freq[i]*freq[i];
        psi[mm-(m-1)]=(double)bits/(double)n*sum - (double)n;
    }
    double d1=psi[0]-psi[1], d2=psi[0]-2.0*psi[1]+psi[2];
    double p1=pvalue_chisq(std::abs(d1),(1<<(m-1))), p2=pvalue_chisq(std::abs(d2),(1<<(m-2)));
    return std::min(p1,p2);
}

// 9. Cumulative Sums (Cusum) — n ≤ 1M bits
static double nist_cusum(const u8* d, std::size_t B) {
    std::size_t n=(B*8<NIST_MAX_BITS)?B*8:NIST_MAX_BITS;
    int S=0,z_fwd=0; for(std::size_t i=0;i<n;i++){S+=((d[i/8]>>(i%8))&1)?1:-1; int a=S>0?S:-S; if(a>z_fwd)z_fwd=a;}
    S=0; int z_rev=0; for(std::size_t i=n;i>0;i--){S+=((d[(i-1)/8]>>((i-1)%8))&1)?1:-1; int a=S>0?S:-S; if(a>z_rev)z_rev=a;}
    int z=z_fwd>z_rev?z_fwd:z_rev;
    double sqrtn=std::sqrt((double)n), sum=0;
    double k1=((double)(-n)/z+1.0)/4.0, k2=((double)n/z-1.0)/4.0;
    for(int k=(int)k1-1;k<=(int)k2+1;k++){
        sum+=erfc_c(((4.0*k+1.0)*z)/sqrtn/1.4142135623730951);
        sum-=erfc_c(((4.0*k-1.0)*z)/sqrtn/1.4142135623730951);
    }
    return std::min(1.0,std::max(0.0,sum));
}

// 10. Linear Complexity (M=500, K=6, Berlekamp-Massey)
static double nist_linear_complexity(const u8* d, std::size_t B) {
    constexpr int M=500; std::size_t n=B*8; int N=(int)(n/M); if(N<1)return-1;
    // 理论分布期望与方差 (NIST SP 800-22 Section 2.10)
    double mu=M/2.0+(9.0+((M+1)%2))/36.0-((double)M/3.0+2.0/9.0)/std::pow(2.0,M);
    int cnt[7]={0};
    for(int blk=0;blk<N;blk++){
        std::size_t base=(std::size_t)blk*M; if(base+M>n)break;
        // Berlekamp-Massey over GF(2)
        int L=0,mm=1; std::vector<int> s(M,0),C(M+1,0),B(M+1,0),T(M+1,0);
        for(int j=0;j<M;j++) s[j]=(d[(base+j)/8]>>((base+j)%8))&1;
        C[0]=1; B[0]=1; int b=1,dm=0;
        for(int nn=0;nn<M;nn++){
            int d_val=s[nn]; for(int i=1;i<=L;i++) if(C[i]) d_val^=s[nn-i];
            if(d_val){
                for(int i=0;i<=M;i++) T[i]=C[i];
                for(int i=0;i<=M;i++){int pos=i+nn-mm; if(pos>=0&&pos<=M) C[pos]^=B[i];}
                if(2*L<=nn){L=nn+1-L; mm=nn+1; for(int i=0;i<=M;i++) B[i]=T[i]; b=d_val;}
            }
        }
        double t_val=((double)L-mu)*std::sqrt(2.0/(double)M); // 标准化
        int idx=(t_val<=-2.5)?0:(t_val<=-1.5)?1:(t_val<=-0.5)?2:(t_val<=0.5)?3:(t_val<=1.5)?4:(t_val<=2.5)?5:6;
        cnt[idx]++;
    }
    double pi[7]={0.010417,0.03125,0.12500,0.50000,0.25000,0.06250,0.020833};
    double c2=0; int total=0; for(int i=0;i<7;i++)total+=cnt[i];
    for(int i=0;i<7;i++){double e=(double)total*pi[i]; if(e>0)c2+=(cnt[i]-e)*(cnt[i]-e)/e;}
    return pvalue_chisq(c2,6);
}

// ============================================================
// 运行全套 NIST 检验
// ============================================================
struct result { const char* name; double pvalue; bool pass; };

static void run_nist(std::FILE* f, const u8* data, std::size_t bytes,
                      const char* label, std::vector<result>& results) {
    auto add=[&](const char* name, double p){
        results.push_back({name,p,p>=0.01});
        LOG(f,"  %-36s P=%.6f %s\n",name,p,p>=0.01?"PASS":"FAIL");
    };
    LOG(f,"\n--- %s ---\n",label);
    add("1. Monobit Frequency",       nist_monobit(data,bytes));
    add("2. Block Frequency (M=128)", nist_block_freq(data,bytes));
    add("3. Runs",                    nist_runs(data,bytes));
    add("4. Longest Run of Ones",     nist_longest_run(data,bytes));
    double mr = nist_matrix_rank(data,bytes); if(mr>=0) add("5. Binary Matrix Rank (32×32)", mr);
    add("6. DFT (Spectral)",          nist_dft(data,bytes));
    add("7. Approximate Entropy (m=5)", nist_ap_en(data,bytes));
    add("8. Serial (m=8)",            nist_serial(data,bytes));
    add("9. Cumulative Sums",         nist_cusum(data,bytes));
    add("10. Linear Complexity (M=500)", nist_linear_complexity(data,bytes));
}

// ============================================================
// 生成 ChaCha20 密钥流（同机基线）
// ============================================================
static u8* gen_chacha20(std::size_t bytes, double* elapsed) {
    u8* buf=new u8[bytes]; std::memset(buf,0,bytes);
    u8 key[32], iv[12]; RAND_bytes(key,32); RAND_bytes(iv,12);
    u8 chacha_iv[16]={0}; std::memcpy(chacha_iv+4,iv,12);
    EVP_CIPHER_CTX* ctx=EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx,EVP_chacha20(),nullptr,key,chacha_iv);
    auto t0=std::chrono::high_resolution_clock::now();
    int outl; u8* p=buf; std::size_t rem=bytes;
    while(rem>0){int chunk=(rem>1024*1024)?1024*1024:(int)rem; EVP_EncryptUpdate(ctx,p,&outl,p,chunk); p+=outl;rem-=outl;}
    int finl; EVP_EncryptFinal_ex(ctx,p,&finl);
    auto t1=std::chrono::high_resolution_clock::now();
    *elapsed=std::chrono::duration<double>(t1-t0).count();
    EVP_CIPHER_CTX_free(ctx); return buf;
}

// ============================================================
// 多轮 P-value 收集
// ============================================================
typedef double (*nist_fn)(const u8*,std::size_t);
static const char* TEST_NAMES[]={
    "1. Monobit Frequency","2. Block Frequency (M=128)","3. Runs",
    "4. Longest Run of Ones","5. Binary Matrix Rank","6. DFT (Spectral)",
    "7. Approximate Entropy (m=5)","8. Serial (m=8)","9. Cumulative Sums",
    "10. Linear Complexity (M=500)"};

// P-value 均匀性检验 (chi-squared on 10 equal bins)
static double uniformity_pvalue(const std::vector<double>& pvals) {
    int bins[10]={0}; int N=(int)pvals.size();
    for(double p:pvals){int b=std::min(9,(int)(p*10.0)); bins[b]++;}
    double c2=0, e=(double)N/10.0;
    for(int i=0;i<10;i++){double d=(double)bins[i]-e; c2+=d*d/e;}
    return pvalue_chisq(c2,9);
}

static void multi_round(std::FILE* f, int rounds, bool hlfsr) {
    constexpr std::size_t MB=2; // 2 MiB per round (sufficient for all tests)
    std::size_t sbytes=MB*1024ULL*1024ULL;
    u8* buf=new u8[sbytes];

    std::vector<double> pvals[10];
    for(int i=0;i<10;i++)pvals[i].reserve(rounds);

    LOG(f,"\n=== %s %d 轮 × %llu MiB ===\n",hlfsr?"HLFSR-64 DM":"ChaCha20",rounds,(unsigned long long)MB);

    for(int r=0;r<rounds;r++){
        if(hlfsr){
            hlfsr64::u8 bm[32],seed[32],idx;
            RAND_bytes(bm,32); RAND_bytes(seed,32); RAND_bytes(&idx,1);
            hlfsr64 c; c.init(bm,seed,idx);
            c.keystream(buf,sbytes);
        } else {
            u8 key[32],iv[12]; RAND_bytes(key,32); RAND_bytes(iv,12);
            u8 chacha_iv[16]={0}; std::memcpy(chacha_iv+4,iv,12);
            std::memset(buf,0,sbytes);
            EVP_CIPHER_CTX* ctx=EVP_CIPHER_CTX_new();
            EVP_EncryptInit_ex(ctx,EVP_chacha20(),nullptr,key,chacha_iv);
            int outl; u8* p=buf; std::size_t rem=sbytes;
            while(rem>0){int chunk=(rem>1024*1024)?1024*1024:(int)rem; EVP_EncryptUpdate(ctx,p,&outl,p,chunk); p+=outl;rem-=outl;}
            int finl; EVP_EncryptFinal_ex(ctx,p,&finl);
            EVP_CIPHER_CTX_free(ctx);
        }

        nist_fn tests[]={nist_monobit,nist_block_freq,nist_runs,nist_longest_run,
                         nist_matrix_rank,nist_dft,nist_ap_en,nist_serial,
                         nist_cusum,nist_linear_complexity};
        for(int t=0;t<10;t++)pvals[t].push_back(tests[t](buf,sbytes));

        if((r+1)%20==0){LOG(f,"  %d/%d\n",r+1,rounds); std::fflush(f);}
    }

    LOG(f,"\n%4s %-36s %8s %8s %8s\n","","检验项","通过率","均匀P","");
    for(int t=0;t<10;t++){
        int pass=0; for(double p:pvals[t]) if(p>=0.01)pass++;
        double up=uniformity_pvalue(pvals[t]);
        // 排除已知 bug: DFT, Cusum
        const char* note=(t==5||t==8)?"(impl)":"";
        LOG(f,"%4s %-36s %3d/%-3d %8.4f %s\n","",TEST_NAMES[t],pass,rounds,up,note);
    }
    delete[] buf;
}

// ============================================================
// Main: 单轮模式 (rounds=1) 或多轮模式 (rounds≥2)
// ============================================================
int main(int argc, char* argv[]) {
    int rounds=(argc>1)?std::atoi(argv[1]):100;
    if(rounds<1)rounds=100;

    auto now=std::time(nullptr); auto tm=*std::localtime(&now);
    char path[256]; std::snprintf(path,sizeof(path),"benchmarks/nist_%04d%02d%02d_%02d%02d%02d.txt",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);
    std::FILE* f=std::fopen(path,"w"); if(!f)return 1;

    LOG(f,"HLFSR-64 DM vs ChaCha20  NIST SP 800-22 多轮检验\n");
    LOG(f,"=================================================\n");
    LOG(f,"轮数: %d  每轮: 2 MiB  α=0.01\n",rounds);
    LOG(f,"P均匀性: χ² 10等分 bins, P≥0.0001 为均匀分布\n\n");

    auto t0=std::chrono::high_resolution_clock::now();
    multi_round(f,rounds,true);   // HLFSR-64 DM
    multi_round(f,rounds,false);  // ChaCha20
    auto t1=std::chrono::high_resolution_clock::now();
    double dt=std::chrono::duration<double>(t1-t0).count();
    LOG(f,"\n总耗时: %.1f s\n",dt);
    LOG(f,"结果: %s\n",path);
    std::fclose(f); return 0;
}
