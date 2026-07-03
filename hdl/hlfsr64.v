// hlfsr64.v — HLFSR-64 Verilog (single-cycle per step)
// 综合: vivado / yosys, 64x64 乘法器自动推断
// 此版: 简化 init, 仅演示 next() 数据通路
// km 由外部 HKDF 派生, 直接写入 lfsr/matrix 寄存器

`timescale 1ns / 1ps

module hlfsr64 (
    input  wire        clk,
    input  wire        rst_n,
    // 状态加载 (外部完成 init, 写入初始 lfsr/matrix/idx)
    input  wire        load,
    input  wire [63:0] lfsr_in [0:7],
    input  wire [7:0]  matrix_in [0:63],
    input  wire [8:0]  idx_in,
    // 运行
    input  wire        next_req,
    output wire [63:0] keystream,
    output wire        done
);

    // ============================================================
    // 多项式
    // ============================================================
    wire [63:0] POLY [0:7];
    assign POLY[0] = 64'h4800203343401101;
    assign POLY[1] = 64'h0416001300480117;
    assign POLY[2] = 64'h58000C0310100803;
    assign POLY[3] = 64'h00A0090940648023;
    assign POLY[4] = 64'h484302010C340003;
    assign POLY[5] = 64'h801D001006412901;
    assign POLY[6] = 64'h04429288080A1021;
    assign POLY[7] = 64'h2000022052D01213;

    localparam [63:0] K1 = 64'h9E3779B97F4A7C15;
    localparam [63:0] K2 = 64'hBF58476D1CE4E5B9;

    // ============================================================
    // 寄存器
    // ============================================================
    reg [63:0] lfsr [0:7];
    reg [7:0]  matrix [0:63];
    reg [8:0]  idx;
    reg        busy;

    // ============================================================
    // 单步组合逻辑
    // ============================================================
    wire [2:0] face = idx[2:0];
    wire [2:0] row  = idx[5:3];
    wire [2:0] col  = idx[8:6];
    wire [5:0] addr = {face, row};

    wire [7:0]  mask_byte = matrix[addr];
    wire        curbit    = mask_byte[col];  // bit-select

    // Galois 推进 + 全 XOR (组合逻辑, 8 路并行)
    wire [63:0] lfsr_new [0:7];
    genvar gi;
    generate
        for (gi = 0; gi < 8; gi = gi + 1) begin : galois
            assign lfsr_new[gi] = (lfsr[gi] << 1) ^ (POLY[gi] & {64{lfsr[gi][63]}});
        end
    endgenerate

    wire [63:0] vx_raw = lfsr_new[0] ^ lfsr_new[1] ^ lfsr_new[2] ^ lfsr_new[3]
                       ^ lfsr_new[4] ^ lfsr_new[5] ^ lfsr_new[6] ^ lfsr_new[7];

    // ROTL33
    wire [63:0] vx_rot = (vx_raw << 33) | (vx_raw >> 31);
    wire [63:0] vx     = vx_rot ^ vx_raw;

    // mask 乘子 + 级联乘法
    wire [63:0] mk  = (mask_byte * K2) | 64'd1;
    wire [63:0] raw = (vx * mk) * K1;

    // 输出
    assign keystream = raw ^ {64{curbit}};
    assign done = busy;

    // 反馈
    wire [63:0] fb     = raw[15:0] * K2;
    wire [7:0]  fb_lo  = fb[7:0];
    wire [7:0]  fb_hi  = fb[15:8];
    wire [2:0]  nb_face = face ^ 3'd1;
    wire [5:0]  nb_addr = {nb_face, row};

    // ROL8: (x << col) | (x >> (8-col))
    wire [7:0] matrix_cur = matrix[addr];
    wire [7:0] matrix_rol = (matrix_cur << col) | (matrix_cur >> (8 - col));

    // 更新值
    wire [7:0] matrix_new  = matrix_rol ^ fb_lo;
    wire [7:0] matrix_nb   = matrix[nb_addr] ^ fb_hi;

    // ============================================================
    // 时序逻辑
    // ============================================================
    integer i;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < 8; i = i + 1) lfsr[i] <= 64'd0;
            for (i = 0; i < 64; i = i + 1) matrix[i] <= 8'd0;
            idx  <= 9'd0;
            busy <= 1'b0;
        end else begin
            if (load) begin
                for (i = 0; i < 8; i = i + 1) lfsr[i] <= lfsr_in[i];
                for (i = 0; i < 64; i = i + 1) matrix[i] <= matrix_in[i];
                idx  <= idx_in;
                busy <= 1'b0;
            end else if (next_req && !busy) begin
                for (i = 0; i < 8; i = i + 1) lfsr[i] <= lfsr_new[i];
                matrix[addr]    <= matrix_new;
                matrix[nb_addr] <= matrix_nb;
                idx  <= (idx + 1) & 9'h1FF;
                busy <= 1'b1;
            end else begin
                busy <= 1'b0;
            end
        end
    end

endmodule
