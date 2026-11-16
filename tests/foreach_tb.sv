module foreach_tb;
reg [7:0] data;
wire [31:0] ordered,exits,skipped,nested,mixed;
foreach_core dut(.*);
reg [7:0] cells[-2:-1][3:1];
reg [1:0][2:0] packed_bits;
integer trial,i,j,k,sum,carry,stop;
reg [31:0] expected;
initial begin
 for(trial=0;trial<1024;trial=trial+1) begin
  data=8'(trial ^ (trial>>3));
  #1;
  foreach(cells[a,b]) cells[a][b]=8'(data+a*5+b);
  sum=0;carry=0;
  foreach(cells[a,b]) begin carry=carry+int'(cells[a][b]);sum=sum*3+carry+a*17+b;end
  expected=32'(sum);
  if(ordered!==expected) $fatal(1,"foreach ordered/static-local mismatch input=%h got=%h expected=%h",data,ordered,expected);
  // Independent explicit traversal and stop flags avoid sharing exit lowering.
  sum=0;stop=0;
  for(i=-2;i<=-1;i=i+1) for(j=3;j>=1;j=j-1) begin
   if(i==-1 && j==2) stop=1;
   if(!stop && !(i==-2 && j==2)) sum=sum*3+int'(cells[i][j]);
  end
  if(exits!==32'(sum)) $fatal(1,"foreach multidimensional break/continue mismatch");
  packed_bits=6'(data);sum=0;
  foreach(packed_bits[,b]) sum=sum*7+b+int'(packed_bits[1][b]);
  foreach(cells[a]) sum=sum*7+a;
  if(skipped!==32'(sum)) $fatal(1,"foreach packed/skipped/trailing dimension mismatch");
  sum=0;
  for(i=-2;i<=-1;i=i+1) for(j=3;j>=1;j=j-1) begin
   for(k=2;k>=0;k=k-1) if(k!=1) sum=sum+i*11+k;
   sum=sum+2;
  end
  if(nested!==32'(sum+17)) $fatal(1,"foreach nesting/shadowing/outer lifetime mismatch");
  sum=0;
  foreach(cells[,b,c]) sum=sum*3+b*11+c+int'(cells[-2][b][c]);
  if(mixed!==32'(sum)) $fatal(1,"foreach mixed unpacked/packed cardinality mismatch");
 end
 $display("PASS foreach: 1024 ordered/static-local, signed bounds, packed/skipped dimensions, nested scopes and exits");
 $finish;
end
endmodule
