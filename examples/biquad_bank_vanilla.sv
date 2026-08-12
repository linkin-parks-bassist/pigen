/* Plain Verilog-style reference for examples/biquad_bank.pigen.
 *
 * No structs, no interfaces, no Pigen syntax: named wires and registers only.
 * The price of doing ready/valid and an interleaved state bank by hand is what
 * Pigen is intended to remove, while keeping this implementation easy to read
 * beside the Pigen source. */
module biquad_pipeline_vanilla (
    input wire clk,
    input wire reset,
    input wire enable,

    input wire [2:0] req_in_handle,
    input wire signed [15:0] req_in_sample,
    input wire req_in_valid,
    output wire req_in_ready,

    output wire signed [15:0] bqd_out_sample,
    output wire bqd_out_valid,
    input wire bqd_out_ready,

    input wire [2:0] coef_write_handle,
    input wire signed [89:0] coef_write_coefs,
    input wire coef_write_valid,
    output wire coef_write_ready
);
    localparam signed [15:0] SAT_MAX = 16'sd32767;
    localparam signed [15:0] SAT_MIN = -16'sd32768;
    localparam signed [34:0] SAT_MAX_W = 35'sd32767;
    localparam signed [34:0] SAT_MIN_W = -35'sd32768;
    localparam signed [17:0] DEFAULT_B0 = 18'sd8192; // unity in Q2.12

    reg signed [89:0] coefficient_memory [0:7];
    reg signed [63:0] state_memory [0:7];
    reg [7:0] coefficient_valid;
    reg [7:0] state_valid;

    /* Stage 0: read the selected filter's coefficients and history. */
    reg [2:0] s0_handle;
    reg signed [17:0] s0_b0, s0_b1, s0_b2, s0_a1, s0_a2;
    reg signed [15:0] s0_x0, s0_x1, s0_x2, s0_y1, s0_y2;
    reg s0_valid;

    /* Stage 1: feed-forward and feedback multiply/accumulate. */
    reg [2:0] s1_handle;
    reg signed [34:0] s1_acc;
    reg signed [15:0] s1_x0, s1_x1, s1_y1, s1_y2;
    reg s1_valid;

    /* Stage 2 is an explicit elastic alignment register. */
    reg [2:0] s2_handle;
    reg signed [34:0] s2_acc;
    reg signed [15:0] s2_x0, s2_x1, s2_y1;
    reg s2_valid;

    /* Stage 3 is the output/writeback token. */
    reg [2:0] s3_handle;
    reg signed [15:0] s3_y, s3_x0, s3_x1, s3_y1;
    reg s3_valid;

    wire s3_ready = enable && (!s3_valid || bqd_out_ready);
    wire s2_ready = enable && (!s2_valid || s3_ready);
    wire s1_ready = enable && (!s1_valid || s2_ready);
    wire s0_ready = enable && (!s0_valid || s1_ready);

    wire signed [17:0] selected_b0 = coefficient_valid[req_in_handle] ? coefficient_memory[req_in_handle][89:72] : DEFAULT_B0;
    wire signed [17:0] selected_b1 = coefficient_valid[req_in_handle] ? coefficient_memory[req_in_handle][71:54] : 18'sd0;
    wire signed [17:0] selected_b2 = coefficient_valid[req_in_handle] ? coefficient_memory[req_in_handle][53:36] : 18'sd0;
    wire signed [17:0] selected_a1 = coefficient_valid[req_in_handle] ? coefficient_memory[req_in_handle][35:18] : 18'sd0;
    wire signed [17:0] selected_a2 = coefficient_valid[req_in_handle] ? coefficient_memory[req_in_handle][17:0]  : 18'sd0;
    wire signed [15:0] selected_x1 = state_valid[req_in_handle] ? state_memory[req_in_handle][63:48] : 16'sd0;
    wire signed [15:0] selected_x2 = state_valid[req_in_handle] ? state_memory[req_in_handle][47:32] : 16'sd0;
    wire signed [15:0] selected_y1 = state_valid[req_in_handle] ? state_memory[req_in_handle][31:16] : 16'sd0;
    wire signed [15:0] selected_y2 = state_valid[req_in_handle] ? state_memory[req_in_handle][15:0]  : 16'sd0;

    /* Widen each product before adding it: the names make the fixed-point
     * arithmetic obvious, unlike an enormous concatenated expression. */
    reg signed [34:0] ff_b0, ff_b1, ff_b2, fb_a1, fb_a2;
    reg signed [34:0] stage1_sum;
    reg signed [15:0] stage3_y;

    function signed [15:0] saturate_q2_12;
        input signed [34:0] value;
        reg signed [34:0] shifted;
        begin
            shifted = value >>> 12;
            if (shifted > SAT_MAX_W) saturate_q2_12 = SAT_MAX;
            else if (shifted < SAT_MIN_W) saturate_q2_12 = SAT_MIN;
            else saturate_q2_12 = shifted[15:0];
        end
    endfunction

    always @* begin
        ff_b0 = s0_b0 * s0_x0;
        ff_b1 = s0_b1 * s0_x1;
        ff_b2 = s0_b2 * s0_x2;
        fb_a1 = s0_a1 * s0_y1;
        fb_a2 = s0_a2 * s0_y2;
        stage1_sum = ff_b0 + ff_b1 + ff_b2 + fb_a1 + fb_a2;
        stage3_y = saturate_q2_12(s2_acc);
    end

    assign coef_write_ready = enable;
    assign req_in_ready = s0_ready;
    assign bqd_out_valid = enable && s3_valid;
    assign bqd_out_sample = s3_y;

    always @(posedge clk) begin
        if (reset) begin
            coefficient_valid <= 8'd0;
            state_valid <= 8'd0;
            s0_valid <= 1'b0;
            s1_valid <= 1'b0;
            s2_valid <= 1'b0;
            s3_valid <= 1'b0;
        end else if (enable) begin
            if (coef_write_valid) begin
                coefficient_memory[coef_write_handle] <= coef_write_coefs;
                coefficient_valid[coef_write_handle] <= 1'b1;
            end

            if (s0_ready) begin
                s0_valid <= req_in_valid;
                if (req_in_valid) begin
                    s0_handle <= req_in_handle;
                    s0_b0 <= selected_b0; s0_b1 <= selected_b1; s0_b2 <= selected_b2;
                    s0_a1 <= selected_a1; s0_a2 <= selected_a2;
                    s0_x0 <= req_in_sample;
                    s0_x1 <= selected_x1; s0_x2 <= selected_x2;
                    s0_y1 <= selected_y1; s0_y2 <= selected_y2;
                end
            end

            if (s1_ready) begin
                s1_valid <= s0_valid;
                if (s0_valid) begin
                    s1_handle <= s0_handle;
                    s1_acc <= stage1_sum;
                    s1_x0 <= s0_x0; s1_x1 <= s0_x1;
                    s1_y1 <= s0_y1; s1_y2 <= s0_y2;
                end
            end

            if (s2_ready) begin
                s2_valid <= s1_valid;
                if (s1_valid) begin
                    s2_handle <= s1_handle;
                    s2_acc <= s1_acc;
                    s2_x0 <= s1_x0; s2_x1 <= s1_x1; s2_y1 <= s1_y1;
                end
            end

            if (s3_ready) begin
                s3_valid <= s2_valid;
                if (s2_valid) begin
                    s3_handle <= s2_handle;
                    s3_y <= stage3_y;
                    s3_x0 <= s2_x0; s3_x1 <= s2_x1; s3_y1 <= s2_y1;
                end
            end

            if (s3_valid && bqd_out_ready) begin
                state_memory[s3_handle] <= {s3_x0, s3_x1, s3_y, s3_y1};
                state_valid[s3_handle] <= 1'b1;
            end
        end
    end
endmodule
