# HLFSR-64 构建与验证
# 需要 MSYS2 MinGW64 shell 环境 (PATH 含 mingw64/bin)
CXX      = g++
CXXFLAGS = -std=c++14 -O2 -march=native
LDLIBS   = -lssl -lcrypto
SRC      = src/hlfsr64.cpp
TEST     = src/test

.PHONY: build test bench golomb all clean

build:
	$(CXX) $(CXXFLAGS) -c $(SRC) -o /tmp/hlfsr64.o

test:
	$(CXX) $(CXXFLAGS) $(TEST)/quicktest.cpp $(SRC) -o /tmp/qt && /tmp/qt

bench:
	$(CXX) $(CXXFLAGS) $(TEST)/bench_keystream.cpp $(SRC) $(LDLIBS) -o /tmp/bench && /tmp/bench

golomb:
	$(CXX) $(CXXFLAGS) $(TEST)/golomb_test.cpp $(SRC) $(LDLIBS) -o /tmp/golomb && /tmp/golomb

protocol:
	$(CXX) $(CXXFLAGS) src/protocol/test_proto.cpp src/protocol/hlfsr_stream.cpp $(SRC) $(LDLIBS) -o /tmp/proto && /tmp/proto

all: test bench golomb

clean:
	rm -f /tmp/hlfsr64.o /tmp/qt /tmp/bench /tmp/golomb /tmp/proto
