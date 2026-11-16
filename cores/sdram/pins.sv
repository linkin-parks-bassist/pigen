// Electrical boundary only. The board supplies a qualified SDRAM clock and holds
// CKE high. Route dq_out/dq_oe through the target FPGA's bidirectional I/O cells.
module pigen_sdram_dq_pins (
    input wire [15:0] dq_out,
    input wire dq_oe,
    output wire [15:0] dq_in,
    inout wire [15:0] dq
);
    assign dq = dq_oe ? dq_out : 16'bz;
    assign dq_in = dq;
endmodule
