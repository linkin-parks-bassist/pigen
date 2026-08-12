/* Conventional SystemVerilog reference for biquad_bank.pigen.
 *
 * This is intentionally explicit: every ready/valid channel, pipeline stage,
 * coefficient RAM access, and state writeback is visible. It implements the
 * same five-coefficient recurrence and eight-slot state bank as the Pigen
 * example, but without Pigen syntax or generated primitives. */
`define DATA_WIDTH 16
`define COEF_WIDTH 18
`define N_SLOTS 8
`define HANDLE_WIDTH $clog2(`N_SLOTS)

typedef struct packed {
    logic [`HANDLE_WIDTH-1:0] handle;
    logic signed [`DATA_WIDTH-1:0] sample;
} vanilla_biquad_req_t;

typedef struct packed {
    logic signed [`DATA_WIDTH-1:0] sample;
} vanilla_biquad_out_t;

typedef struct packed {
    logic [`HANDLE_WIDTH-1:0] handle;
    logic signed [`COEF_WIDTH*5-1:0] coefs;
} vanilla_biquad_coef_write_req_t;

module biquad_pipeline_vanilla (
    input  logic clk,
    input  logic reset,
    input  logic enable,
    input  vanilla_biquad_req_t req_in,
    input  logic req_in_valid,
    output logic req_in_ready,
    output vanilla_biquad_out_t bqd_out,
    output logic bqd_out_valid,
    input  logic bqd_out_ready,
    input  vanilla_biquad_coef_write_req_t coef_write_port,
    input  logic coef_write_port_valid,
    output logic coef_write_port_ready
);
    typedef logic [`HANDLE_WIDTH-1:0] handle_t;
    typedef logic signed [`COEF_WIDTH-1:0] coef_t;
    typedef logic signed [`DATA_WIDTH-1:0] sample_t;
    typedef logic signed [34:0] acc_t;

    typedef struct packed {
        handle_t handle;
        coef_t b0, b1, b2, a1, a2;
        sample_t x0, x1, x2, y1, y2;
    } stage0_t;
    typedef struct packed {
        handle_t handle;
        acc_t acc;
        sample_t x0, x1, y1, y2;
    } stage1_t;
    typedef struct packed {
        handle_t handle;
        sample_t y;
        sample_t x0, x1, y1;
    } stage3_t;

    logic signed [`COEF_WIDTH*5-1:0] coef_mem [`N_SLOTS-1:0];
    logic signed [`DATA_WIDTH*4-1:0] state_mem [`N_SLOTS-1:0];
    logic [`N_SLOTS-1:0] coefs_valid, state_valid;

    stage0_t s0, s0_next;
    stage1_t s1, s1_next, s2, s2_next;
    stage3_t s3, s3_next;
    logic s0_valid, s1_valid, s2_valid, s3_valid;
    logic s0_ready, s1_ready, s2_ready, s3_ready;
    sample_t sat_y;

    function automatic sample_t saturate(input acc_t value);
        acc_t shifted;
        begin
            shifted = value >>> 12;
            if (shifted > 32767) saturate = 16'sd32767;
            else if (shifted < -32768) saturate = -16'sd32768;
            else saturate = shifted[15:0];
        end
    endfunction

    assign coef_write_port_ready = enable;
    assign s3_ready = enable && (!s3_valid || bqd_out_ready);
    assign s2_ready = enable && (!s2_valid || s3_ready);
    assign s1_ready = enable && (!s1_valid || s2_ready);
    assign s0_ready = enable && (!s0_valid || s1_ready);
    assign req_in_ready = s0_ready;
    assign bqd_out_valid = enable && s3_valid;
    assign bqd_out.sample = s3.y;

    always_comb begin
        coef_t b0, b1, b2, a1, a2;
        sample_t x1, x2, y1, y2;
        {b0, b1, b2, a1, a2} = coefs_valid[req_in.handle] ?
            coef_mem[req_in.handle] : {18'sd8192, 18'sd0, 18'sd0, 18'sd0, 18'sd0};
        {x1, x2, y1, y2} = state_valid[req_in.handle] ? state_mem[req_in.handle] : '0;
        s0_next = '{req_in.handle, b0, b1, b2, a1, a2, req_in.sample, x1, x2, y1, y2};
        s1_next = '{s0.handle, s0.b0*s0.x0 + s0.b1*s0.x1 + s0.b2*s0.x2 +
                    s0.a1*s0.y1 + s0.a2*s0.y2, s0.x0, s0.x1, s0.y1, s0.y2};
        s2_next = s1;
        sat_y = saturate(s2.acc);
        s3_next = '{s2.handle, sat_y, s2.x0, s2.x1, s2.y1};
    end

    always_ff @(posedge clk) begin
        if (reset) begin
            s0_valid <= 1'b0; s1_valid <= 1'b0; s2_valid <= 1'b0; s3_valid <= 1'b0;
            coefs_valid <= '0; state_valid <= '0;
        end else if (enable) begin
            if (coef_write_port_valid) begin
                coef_mem[coef_write_port.handle] <= coef_write_port.coefs;
                coefs_valid[coef_write_port.handle] <= 1'b1;
            end
            if (s0_ready) begin s0_valid <= req_in_valid; if (req_in_valid) s0 <= s0_next; end
            if (s1_ready) begin s1_valid <= s0_valid; if (s0_valid) s1 <= s1_next; end
            if (s2_ready) begin s2_valid <= s1_valid; if (s1_valid) s2 <= s2_next; end
            if (s3_ready) begin s3_valid <= s2_valid; if (s2_valid) s3 <= s3_next; end
            if (s3_valid && bqd_out_ready) begin
                state_mem[s3.handle] <= {s3.x0, s3.x1, s3.y, s3.y1};
                state_valid[s3.handle] <= 1'b1;
            end
        end
    end
endmodule
