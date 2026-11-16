module jobs_tb;
 logic clk=0,reset=1;
 logic signed [8:0] request=0; logic request_valid=0,request_ready;
 logic signed [16:0] result; logic result_valid,result_ready=0;
 integer requests[0:11]='{5,8,3,12,2,7,4,10,1,6,9,0};
 integer sent=0,received=0,cycle=0,logfile;
 job_engine dut(.*);
 always #5 clk=~clk;
 always @(negedge clk) begin
  cycle=cycle+1;
  reset=cycle<3;
  if(!reset) begin
   request_valid=sent<12;
   if(sent<12) request=requests[sent];
   result_ready=cycle>=110 && cycle%9>2;
  end
 end
 always @(posedge clk) if(!reset) begin
  if(request_valid && request_ready) begin
   $fwrite(logfile,"%0d,request,%0d,%0d\n",cycle,sent,request);
   sent=sent+1;
  end
  if(result_valid && result_ready) begin
   if(received>=sent || result!==((requests[received]*(requests[received]+1))/2))
    $fatal(1,"job %0d result %0d mismatch",received,result);
   $fwrite(logfile,"%0d,result,%0d,%0d\n",cycle,received,result);
   received=received+1;
  end
  if(received==12) begin
   $display("PASS: all 12 queued jobs completed in order under backpressure");
   $fclose(logfile);$finish;
  end
  if(cycle>600) $fatal(1,"timeout sent=%0d received=%0d",sent,received);
 end
 initial begin
  logfile=$fopen("events.csv","w");$fwrite(logfile,"cycle,event,job,value\n");
  $dumpfile("jobs.vcd");$dumpvars(0,jobs_tb);
 end
endmodule
