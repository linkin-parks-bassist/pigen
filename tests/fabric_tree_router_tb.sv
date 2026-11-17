module fabric_tree_router_tb;
 logic clk=0,reset=1;
 logic[2:0]in_valid=0,in_ready,out_valid,out_ready=0;
 logic[71:0]packet_in=0,packet_out;
 int cycle=0,sent0=0,sent2=0,seen0=0,seen2=0,run_length=0,last_source=-1;
 logic took0=0,took2=0,held=0;logic[23:0]saved;
 pigen_tree_router #(.PAYLOAD_T(logic[23:0]),.PATH_W(3))dut(.*);
 always #5 clk=~clk;
 function automatic logic[23:0]packet(input int source,index);
  return (24'(source==0?6:5)<<21)|(24'(index)<<4)|24'(source);
 endfunction
 function automatic logic[23:0]expected(input int source,index);
  return (24'(source==0?3:6)<<21)|(24'(index)<<4)|24'(source);
 endfunction
 initial begin repeat(3)@(negedge clk);reset=0;end
 always @(negedge clk)if(!reset)begin
  cycle++;out_ready[1]=cycle>=50&&(cycle%7>=2);
  if(!in_valid[0]||took0)begin in_valid[0]=sent0<97;packet_in[0+:24]=packet(0,sent0);end
  if(!in_valid[2]||took2)begin in_valid[2]=cycle>=30&&sent2<97;packet_in[48+:24]=packet(2,sent2);end
 end
 always @(posedge clk)if(!reset)begin
  if(out_valid[0]||out_valid[2])$fatal(1,"unexpected router destination");
  if(held&&(!out_valid[1]||packet_out[24+:24]!==saved))$fatal(1,"stalled grant changed on contender arrival");
  held=out_valid[1]&&!out_ready[1];saved=packet_out[24+:24];
  if(cycle>70&&seen0<90&&seen2<90&&!out_valid[1])$fatal(1,"saturated router inserted bubble");
  if(out_valid[1]&&out_ready[1])begin
   if(packet_out[24+:4]==0)begin
    if(packet_out[24+:24]!==expected(0,seen0))$fatal(1,"source zero route/data/order");seen0++;
   end else if(packet_out[24+:4]==2)begin
    if(packet_out[24+:24]!==expected(2,seen2))$fatal(1,"source two route/data/order");seen2++;
   end else $fatal(1,"unknown source");
   if(seen0<90&&seen2<90)begin
    if(last_source==int'(packet_out[24+:4]))run_length++;else run_length=1;
    last_source=int'(packet_out[24+:4]);if(run_length>2)$fatal(1,"contender starvation");
   end
  end
  took0=in_valid[0]&&in_ready[0];took2=in_valid[2]&&in_ready[2];
  if(took0)sent0++;if(took2)sent2++;
  if(seen0==97&&seen2==97)begin $display("PASS three-port router: stalled-grant stability, rotation, FIFO order, round-robin service and saturated throughput");$finish;end
  if(cycle>2000)$fatal(1,"router timeout");
 end
endmodule
