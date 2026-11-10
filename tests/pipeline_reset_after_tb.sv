module prafter_tb;
 logic clk=0,reset=1,clear=0,source_valid=0,source_ready,destination_valid,destination_ready=0;
 logic [7:0] source=8'h5a,destination;
 prafter dut(.*); always #5 clk=~clk;
 initial begin
  #12; reset=0; source_valid=1;
  do @(posedge clk); while(!source_ready);
  @(negedge clk); source_valid=0;
  begin int g=0; while(destination_valid!==1'b1 && g<10) begin @(posedge clk); g++; end end
  if(destination_valid!==1'b1 || destination!==8'h5a)$fatal(1,"preclear dv=%b d=%h",destination_valid,destination);
  destination_ready=1;
  @(posedge clk); #1;
  clear=1; @(posedge clk); @(negedge clk); clear=0;
  repeat(5) @(posedge clk); #1;
  if(destination_valid!==1'b0)$fatal(1,"stale pipeline packet after reset");
  $display("PASS conditional pipeline reset"); $finish;
 end
 initial #200 $fatal(1,"timeout");
endmodule
