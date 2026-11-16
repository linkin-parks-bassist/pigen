#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pigen/specialize.h"

int main(void)
{
 const char *source = "module child(input logic clk, input bit[8] buf i, output bit[8] buf o); always_ff @(posedge clk) o<=i; endmodule\n"
  "module top(input logic clk, input bit[8] buf i, output bit[8] buf o); child c(clk,i,o); endmodule\n";
 const char *error = NULL;
 size_t offset = 0;
 char *fixed = pigen_specialize_fixed_point(source, strlen(source), 16, 100000, &error, &offset);
 assert(fixed && !error);
 assert(strstr(fixed, ".clk(clk)"));
 char *again = pigen_specialize_parameterized_instances(fixed, strlen(fixed), &error, &offset);
 assert(!again && !error);
 again = pigen_specialize_fixed_point(fixed, strlen(fixed), 0, 100000, &error, &offset);
 assert(again && !strcmp(again,fixed));
 free(again);
 free(fixed);
 assert(!pigen_specialize_fixed_point(source, strlen(source), 0, 100000, &error, &offset));
 assert(error && strstr(error,"rewrite-round limit"));
 assert(!pigen_specialize_fixed_point(source, strlen(source), 16, 1, &error, &offset));
 assert(error && strstr(error,"size limit"));
 assert(!pigen_specialize_fixed_point(source, strlen(source), 16, strlen(source), &error, &offset));
 assert(error && strstr(error,"size limit"));
 /* Module parameters apply outside, while inline genvars own their loop
  * controls and bodies. The single-item outer loop includes the inner loop. */
 const char *shadow = "module bank #(parameter i=4,parameter j=5)(input uint[i] buf s);\n"
  "generate for(genvar i=0;i<2;i++) for(genvar j=0;j<2;j++) begin : lanes\n"
  "uint[8] wire value; assign value=uint[8]'(i+j); end\n"
  "if(i==4) begin : after uint[j] wire outside; end endgenerate endmodule\n"
  "module top(input uint[4] buf s); bank #(.i(4),.j(5)) child(.s(s)); endmodule\n";
 fixed = pigen_specialize_fixed_point(shadow,strlen(shadow),16,100000,&error,&offset);
 assert(fixed && !error);
 assert(strstr(fixed,"module __pigen_spec_bank_0"));
 assert(strstr(fixed,"input uint[4] buf s"));
 assert(strstr(fixed,"for(genvar i=0;i<2;i++) for(genvar j=0;j<2;j++)"));
 assert(strstr(fixed,"uint[8]'(i+j)"));
 assert(strstr(fixed,"if(4==4) begin : after uint[5] wire outside"));
 free(fixed);
 /* Native root calls preserve typed parameter syntax for semantic specialization. */
 const char *native = "module native_bank #(parameter W=32)(input uint[W] axi4_stream s); endmodule\n"
  "module native_top(input uint[64] axi4_stream s); native_bank #(.W(64)) child(.s(s)); endmodule\n";
 fixed=pigen_specialize_fixed_point(native,strlen(native),16,100000,&error,&offset);
 assert(fixed && !error && !strstr(fixed,"__pigen_spec_native_bank"));
 assert(strstr(fixed,"native_bank #(.W(64)) child"));
 assert(strstr(fixed,"input uint[W] axi4_stream s"));
 free(fixed);
 const char *native_alias = "typedef bit[32] axi4_lite[addr=40] bus_t; typedef bus_t copy_t;\n"
  "module alias_bank #(parameter N=2)(input copy_t s); endmodule\n"
  "module top(input copy_t s); alias_bank #(.N(3)) child(.s(s)); endmodule\n";
 fixed=pigen_specialize_fixed_point(native_alias,strlen(native_alias),16,100000,&error,&offset);
 assert(fixed && !error && !strstr(fixed,"__pigen_spec_alias_bank"));
 assert(strstr(fixed,"alias_bank #(.N(3)) child"));
 free(fixed);
 const char *ordinary_shadow = "typedef bit[32] axi4_lite bus_t;\n"
  "module shadow_bank #(parameter W=8)(input uint[W] buf s);\n"
  "typedef logic[7:0] bus_t; bus_t value; endmodule\n"
  "module top(input uint[8] buf s); shadow_bank #(.W(8)) child(.s(s)); endmodule\n";
 fixed=pigen_specialize_fixed_point(ordinary_shadow,strlen(ordinary_shadow),16,100000,&error,&offset);
 assert(fixed && !error && strstr(fixed,"module __pigen_spec_shadow_bank_0"));
 free(fixed);
 puts("PASS specialization fixed point, limits, native aliases and lexical typedef shadows");
 return 0;
}
