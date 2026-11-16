// Ordinary SystemVerilog IP compiled alongside the generated wrapper.
module external_axis_register #(parameter integer DATA_W=32)(
    input wire clk, input wire reset,
    input wire [DATA_W-1:0] s_axis_tdata,
    input wire s_axis_tvalid, output wire s_axis_tready,
    output wire [DATA_W-1:0] m_axis_tdata,
    output wire m_axis_tvalid, input wire m_axis_tready);
    reg [DATA_W-1:0] data_q;
    reg valid_q;
    assign s_axis_tready = !reset && (!valid_q || m_axis_tready);
    assign m_axis_tdata = data_q;
    assign m_axis_tvalid = !reset && valid_q;
    always @(posedge clk) begin
        if (reset) begin data_q <= 0; valid_q <= 0; end
        else if (s_axis_tready) begin
            valid_q <= s_axis_tvalid;
            if (s_axis_tvalid) data_q <= s_axis_tdata;
        end
    end
endmodule
