module int_ports_sv_tb;
  int a = 41, y;
  int_ports dut(.a(a), .y(y));
  initial begin
    #1;
    if (y !== 42) $fatal(1, "int port value lost: %0d", y);
    $display("PASS int ports preserved");
    $finish;
  end
endmodule
