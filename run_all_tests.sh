#!/bin/bash
# HLFSR-64 全量测试脚本
set -e
CXX="g++ -std=c++14 -O3 -march=native"
SRC="src/hlfsr64.cpp"
LDLIBS="-lssl -lcrypto"
mkdir -p benchmarks

echo "============================================"
echo "HLFSR-64 Full Test Suite"
echo "============================================"

# 1. quicktest
echo ""; echo "===== [1/9] quicktest ====="
$CXX src/test/quicktest.cpp $SRC -o quicktest.exe && ./quicktest.exe

# 2. crypto_test (diff + degree + linear screening)
echo ""; echo "===== [2/9] crypto_test (d=26, 10 trials) ====="
$CXX src/test/crypto_test.cpp $SRC -o crypto_test.exe && ./crypto_test.exe 26 10

# 3. bench8m (TV throughput sweep)
echo ""; echo "===== [3/9] bench8m (TV sweep 64KB-8MB) ====="
$CXX bench8m.cpp $SRC $LDLIBS -o bench8m.exe && ./bench8m.exe

# 4. bench_speed (openssl speed format)
echo ""; echo "===== [4/9] bench_speed (openssl speed format) ====="
$CXX bench_speed.cpp $SRC $LDLIBS -o bench_speed.exe && ./bench_speed.exe

# 5. bench_chacha (ChaCha20 AVX2 raw bandwidth)
echo ""; echo "===== [5/9] bench_chacha (ChaCha20 AVX2) ====="
$CXX bench_chacha.cpp $LDLIBS -o bench_chacha.exe && ./bench_chacha.exe

# 6. bench_keystream (keystream bandwidth, 64 MiB)
echo ""; echo "===== [6/9] bench_keystream (64 MiB keystream) ====="
$CXX src/test/bench_keystream.cpp $SRC $LDLIBS -o bench_keystream.exe && ./bench_keystream.exe

# 7. golomb_test (Golomb 3 postulates, 16 MiB)
echo ""; echo "===== [7/9] golomb_test (16 MiB) ====="
$CXX src/test/golomb_test.cpp $SRC $LDLIBS -o golomb_test.exe && ./golomb_test.exe

# 8. test_proto + bench_symmetric + bench_ecies
echo ""; echo "===== [8/9] protocol correctness ====="
$CXX src/protocol/test_proto.cpp src/protocol/hlfsr_stream.cpp $SRC $LDLIBS -o test_proto.exe && ./test_proto.exe

echo ""; echo "===== [9/9] protocol benchmarks ====="
$CXX src/test/bench_symmetric.cpp src/protocol/hlfsr_stream.cpp $SRC $LDLIBS -o bench_symmetric.exe && ./bench_symmetric.exe
$CXX src/test/bench_ecies.cpp src/protocol/hlfsr_stream.cpp $SRC $LDLIBS -o bench_ecies.exe && ./bench_ecies.exe

echo ""; echo "============================================"
echo "All fast/medium tests complete."
echo "Remaining (manual): linear_deep.exe, NIST STS"
echo "============================================"
