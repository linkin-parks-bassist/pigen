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
 puts("PASS specialization fixed point and limits");
 return 0;
}
