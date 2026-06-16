// nist_sts_gen.cpp — 生成 NIST STS 2.1.2 格式 ASCII 比特流文件
// 输出目录: nist_streams/  每文件 1 Mbit, 200 条独立流
#include "../hlfsr64.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

int main() {
    const int STREAMS  = 200;
    const int MBIT     = 1000000;
    const int BYTES    = MBIT / 8;

    // 随机种子, 确保可复现
    srand(0x4E495354);  // "NIST"

    printf("Generating %d streams x %d bits for NIST STS 2.1.2...\n", STREAMS, MBIT);

    // 创建输出目录
    system("mkdir nist_streams 2>nul");

    auto t0 = std::chrono::high_resolution_clock::now();

    for (int s = 0; s < STREAMS; s++) {
        // 随机 key_material + idx
        hlfsr64::u8 km[64];
        for (int i = 0; i < 64; i++) km[i] = (hlfsr64::u8)(rand() & 0xFF);
        hlfsr64::u16 idx = (hlfsr64::u16)(rand() & 0x1FF);

        hlfsr64 h;
        h.init(km, idx);

        // 输出缓冲区: 每字节 = 8 个 ASCII '0'/'1'
        char* buf = new char[MBIT + 1];
        char* p   = buf;
        hlfsr64::u8 out8[8];  // 一次 8 字节 = 64 bit keystream
        int remaining = BYTES;
        while (remaining >= 8) {
            h.keystream(out8, 8);
            for (int b = 0; b < 8; b++) {
                hlfsr64::u8 v = out8[b];
                *p++ = '0' + (v & 1); v >>= 1;
                *p++ = '0' + (v & 1); v >>= 1;
                *p++ = '0' + (v & 1); v >>= 1;
                *p++ = '0' + (v & 1); v >>= 1;
                *p++ = '0' + (v & 1); v >>= 1;
                *p++ = '0' + (v & 1); v >>= 1;
                *p++ = '0' + (v & 1); v >>= 1;
                *p++ = '0' + (v & 1);
            }
            remaining -= 8;
        }
        if (remaining > 0) {
            hlfsr64::u8 tail[8];
            h.keystream(tail, (size_t)remaining);
            for (int i = 0; i < remaining; i++) {
                hlfsr64::u8 v = tail[i];
                for (int b = 0; b < 8; b++) { *p++ = '0' + (v & 1); v >>= 1; }
            }
        }
        *p = '\0';

        // 写入文件
        char fname[64];
        sprintf(fname, "nist_streams/stream_%03d.txt", s + 1);
        FILE* f = fopen(fname, "w");
        if (!f) { printf("ERROR: cannot create %s\n", fname); return 1; }
        fputs(buf, f);
        fclose(f);
        delete[] buf;

        if ((s + 1) % 50 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - t0).count();
            printf("  %d/%d streams (%.0fs)\n", s + 1, STREAMS, elapsed);
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    printf("Done: %d streams in %.0f s\n", STREAMS,
           std::chrono::duration<double>(t1 - t0).count());
    printf("Output: nist_streams/stream_001.txt .. stream_%03d.txt\n", STREAMS);
    printf("\nRun official NIST STS:\n");
    printf("  assess 1000000 > nist_result.txt\n");
    printf("Then analyze:\n");
    printf("  g++ tools/analyze_nist.cpp -o analyze.exe && analyze.exe nist_result.txt\n");
    return 0;
}
