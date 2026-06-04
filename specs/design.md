HLFSR-64 技术规格书

1. 设计概述

名称：HLFSR-64（Hierarchical LFSR with 64-bit output）

设计目的：构造一个密钥驱动的自修改流密码核心。密钥后处理生成256位内部状态(bitmap)，该状态同时作为数据和指令存储器。加密过程中，一个8位比特游标(idx)在bitmap上滑动，取当前位作为输出翻转控制(curbit)，取当前字节内从当前位开始的4位作为基底选择(sel)，驱动16条基底LFSR之一产生64位输出。每步执行bitmap中的指令，动态改写bitmap自身和控制流。64位输出宽度用于提高加密带宽，后续可演进为纯流密码处理机。


2. 顶层参数

内部状态(bitmap)：256位(32字节)
基底LFSR：16条 × 64位
输出宽度：64比特/步
指令集：16条，4位操作码 + 4位参数
指令执行：每步执行
控制寄存器：idx(8位,0~255)、curbyte(8位)、curbit(1位)
密钥输入：任意长度，预处理后256位
Nonce/keystream_init：由调用方外部传入
多项式：16条基底LFSR的多项式见 specs/polynomials.md，均为经过本原性验证的64次本原多项式（权重7）


3. 组件定义

bitmap：256位内部状态数组，按比特编号0~255，按字节寻址(0~31)。由密钥后处理生成，加密过程中被指令动态修改。

idx：8位比特指针，取值范围0~255，模256循环。每步默认+1，受步进和跳转指令改变。

curbit：bitmap[idx]，即idx当前所指的那1位。用于控制输出翻转。

curbyte：idx所在字节的8位，即bitmap[idx/8]的完整8位。高4位为操作码，低4位为参数。每步执行。

sel0：基底选择信号。byte_idx = idx / 8，bit_pos = idx % 8，sel0 = (bitmap[byte_idx] >> bit_pos) & 0x0F。从idx所指位开始，在同一字节内向下取4位。若bit_pos > 4，有效位不足4位，低位补零。

sel1：第二基底选择信号。从同一字节的 bit_pos−4 位置开始向下取 4 位。若 bit_pos < 4，高位不足 4 位时低位补零（即 sel1 = 0）。取法同 sel0，偏移 4 位。

基底LFSR：16条独立的64位LFSR，编号0~15。多项式作为系统参数预设。每步所有16条LFSR各自移位一次。取 sel0 和 sel1 两条 LFSR 的输出异或后，乘以常数 0x9E3779B97F4A7C15 进行位混合，再与 {64{curbit}} 异或得到最终输出。


4. 每步操作

1. curbit = bitmap[idx]
2. sel0 = (bitmap[idx/8] >> (idx % 8)) & 0x0F
3. sel1 = (bitmap[idx/8] >> ((idx % 8) − 4)) & 0x0F（同字节，高位不足4位时低位补零）
4. 全部16条LFSR各移位一次
5. raw = (L_sel0 ^ L_sel1) × 0x9E3779B97F4A7C15（乘性混合，打破字内位关联）
6. output = raw XOR {64{curbit}}
7. 将curbyte作为指令执行
8. idx按当前指令更新，或默认 idx = (idx + 1) mod 256


5. 指令集

指令格式：高4位=操作码，低4位=参数。所有16条指令统一为4+4结构。

操作码 助记符 类别 语义
0000  IncB   算术   bitmap[(idx/8+1)%32] += param (模256)
0001  Copb   位操作 bitmap[(idx/8+1)%32] ^= (1 << param)
0010  DecB   算术   bitmap[(idx/8+1)%32] -= param (模256)
0011  CopO   位操作 bitmap[(idx/8+1)%32] ^= (1 << (7-param))
0100  StpB   步进   idx += param × 8
0101  Stpb   步进   idx += param
0110  RStpB  步进   idx -= param × 8
0111  RStpb  步进   idx -= param
1000  JmpBL  跳转   idx = (curbyte[7] × 128) + (param × 8)
1001  JmpBR  跳转   idx = (curbyte[0] × 128) + (param × 8)
1010  XorB   逻辑   bitmap[(idx/8+1)%32] ^= param
1011  AndB   逻辑   bitmap[(idx/8+1)%32] &= param
1100  OrB    逻辑   bitmap[(idx/8+1)%32] |= param
1101  SwapB  数据   bitmap[(idx/8+1)%32]高低4位交换，idx += 8
1110  CurB   自修改 curbyte ^= bitmap[(idx/8+offset) mod 32]，写回bitmap[idx/8]
1111  NotB   自修改 bitmap[(idx/8+offset) mod 32] = ~bitmap[(idx/8+offset) mod 32]，idx += 8


指令详细说明：

操作对象：除CurB操作自身所在字节外，其余指令操作对象为idx下一个字节，即 (idx/8+1) mod 32。

IncB/DecB：对目标字节进行加减法，模256。

Copb/CopO：翻转目标字节的指定位。Copb正向索引(param=0翻转bit0)，CopO反向索引(param=0翻转bit7)。

StpB/Stpb：idx向前移动。StpB以字节为单位(param×8比特)，Stpb以比特为单位。

RStpB/RStpb：idx向后移动。RStpB以字节为单位，RStpb以比特为单位。

JmpBL：idx跳转到 curbyte[7]×128 + param×8。curbyte[7]决定半区(0=左半0~127，1=右半128~255)，param决定半区内字节偏移(0~15)，乘以8转换为比特地址。无条件跳转。

JmpBR：idx跳转到 curbyte[0]×128 + param×8。curbyte[0]决定半区，param决定半区内字节偏移。无条件跳转。

XorB/AndB/OrB：对目标字节进行按位逻辑操作。param零扩展为8位后参与运算。

SwapB：交换目标字节的高低4位，执行后强制 idx += 8。

CurB：将param视为4位有符号数(0~7为正，8~15为负即-8~-1)，计算 offset = sign_extend(param)，读取 bitmap[(idx/8+offset) mod 32]，异或到curbyte，写回bitmap[idx/8](修改自身所在字节)。窗口覆盖前后共16字节。

NotB：使用与CurB相同的有符号偏移，对 bitmap[(idx/8+offset) mod 32] 按位取反，写回原处，执行后强制 idx += 8。

默认行为：非指令步或指令未改变idx时，idx = (idx + 1) mod 256。


6. 初始化

纯对称场景：
Nonce和keystream_init由调用方外部传入（Nonce 8~16字节，同一Key下不可重复）。
bitmap     = SHA-256(Nonce || Key || 0x00)
lfsr_seed  = SHA-256(Nonce || Key || 0x01)
idx_init   = lfsr_seed[0]
16 条基底 LFSR 初态由 lfsr_seed 滑动窗口填入：LFSR[i] 取 lfsr_seed[(i×2) mod 32 .. (i×2+7) mod 32]，循环取用。

PK体系场景：
HLFSR作为ECIES等混合加密体系的对称部分，接收上层协议派生的32字节会话密钥直接作为bitmap。Nonce和keystream_init同样由上层传入。


7. 常数时间实现要求

以下操作必须无分支：
- 取curbit和sel
- 指令译码(16条指令全部计算结果，掩码选择)
- 跳转(使用掩码选择目标idx)
- 写回bitmap(使用掩码控制是否写入)

8. 实现约束

- 零平台依赖：核心库仅使用标准 C/C++（C99 或 C++11 以上），不依赖 x86 特定指令、OS 特定 API、或第三方库
- 以库形式提供：头文件 + 实现文件，可被其他项目直接引用。对外暴露 init / keystream / 状态查询接口
- polyselect.cpp 是开发期工具，不进入库代码


9. 使用约束

同一密钥下Nonce不可重复使用。