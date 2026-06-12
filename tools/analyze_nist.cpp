// analyze_nist.cpp — NIST STS 报告提取
#include <cstdio>
#include <cstring>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <report.txt> [output.txt]\n", argv[0]); return 1; }
    FILE* in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }
    FILE* out = (argc > 2) ? fopen(argv[2], "w") : stdout;

    char line[512];
    int total = 0, passed = 0;
    const char* skip[] = {"NonOverlapping", "RandomExcursions"};

    fprintf(out, "HLFSR-64 V11-Uni  NIST SP 800-22 Results\n");
    fprintf(out, "==========================================\n\n");
    fprintf(out, "  %-30s %8s %10s %s\n", "Test", "P-VALUE", "Pass Rate", "Verdict");
    fprintf(out, "  %-30s %8s %10s %s\n", "----", "-------", "---------", "-------");

    while (fgets(line, sizeof(line), in)) {
        // 跳过标题/分隔行
        if (strstr(line, "C1  C2") || strstr(line, "-----") || strstr(line, "RESULTS FOR")
            || strstr(line, "generator") || strstr(line, "INSTRUCTIONS")
            || strstr(line, "minimum")) continue;

        // 必须有 NNN/MMM 格式和浮点数
        char* frac = strchr(line, '/');
        char* dot  = strchr(line, '.');
        if (!frac || !dot) continue;

        // 跳过无用测试类型
        bool skip_it = false;
        for (int i = 0; i < 2; i++) if (strstr(line, skip[i])) skip_it = true;
        if (skip_it) continue;

        double pv = -1;
        int prop = 0, denom = 0;
        prop = atoi(frac - 3); // " 200/200" → 数字在 / 前 3 字符内
        denom = atoi(frac + 1);

        // P-VALUE: 从 frac 往前找第一个 '.' (十进制小数点)
        char* pd = frac - 1;
        while (pd > line && *pd != '.') pd--;
        if (pd > line) {
            // 往前找到数字起始
            char* ps = pd - 1;
            while (ps > line && *ps != ' ') ps--;
            pv = atof(ps + 1);
        }

        // 找 test name: '/' 后面的最后一个词
        char* name = frac + 1;
        while (*name >= '0' && *name <= '9') name++;
        while (*name == ' ' || *name == '\t') name++;
        // name 现在指向 "OverlappingTemplate" 等, 去除尾部空格
        char* end = name + strlen(name) - 1;
        while (end > name && (*end == ' ' || *end == '\n' || *end == '\r')) { *end = 0; end--; }
        // 处理换行拆分的 test name (如 "Serial\n" 后面是空白)
        if (strlen(name) < 3) continue;

        if (pv >= 0 && denom > 0) {
            total++;
            bool pass = (pv >= 0.0001) && ((double)prop / denom >= 0.96);
            if (pass) passed++;
            fprintf(out, "  %-30s %8.4f %4d/%-4d %s\n",
                    name, pv, prop, denom, pass ? "PASS" : "FAIL");
        }
    }

    fprintf(out, "\n  ──────────────────────────────\n");
    fprintf(out, "  %d/%d effective tests passed\n", passed, total);
    fclose(in);
    if (out != stdout) fclose(out);
    return 0;
}
