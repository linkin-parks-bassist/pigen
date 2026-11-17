#include <assert.h>
#include <string.h>
#include "pigen/compile.h"

static const char *design=
 "module leaf(input wire clk,input wire reset,input bit[16] buf i,output bit[16] buf o);"
 "always @(posedge clk)o<=i;endmodule "
 "module network(input wire clk,input wire reset);leaf a(clk,reset);leaf b(clk,reset);"
 "fabric begin a.o -> b.i;endfabric endmodule";

int main(void)
{
 for(int owned=0;owned<2;owned++)for(int returned=0;returned<2;returned++) {
  pigen_source_manager sources={0};pigen_compile_result result={0};pigen_compile_error error={0};
  assert(pigen_compile_source(owned ? NULL:&sources,"fabric-api.pigen",design,strlen(design),NULL,returned ? &result:NULL,&error)==0);
  if(returned) {
   assert(result.systemverilog && strstr(result.systemverilog,"pigen_tree_router"));
   assert(result.fabric_diagrams.count==1);
   assert(!strcmp(result.fabric_diagrams.items[0].name,"network"));
   assert(strstr(result.fabric_diagrams.items[0].svg,"a.o"));
   pigen_free_compile_result(&result);
   assert(!result.systemverilog && !result.length && !result.fabric_diagrams.count && !result.fabric_diagrams.items);
   pigen_free_compile_result(&result);
  }
  pigen_free_sources(&sources);
 }
 pigen_compile_result failed;memset(&failed,0xa5,sizeof failed);
 const char *bad="module unfinished;";pigen_compile_error error={0};
 assert(pigen_compile_source(NULL,"bad.pigen",bad,strlen(bad),NULL,&failed,&error)!=0);
 assert(!failed.systemverilog && !failed.length && !failed.fabric_diagrams.items && !failed.fabric_diagrams.count);
 pigen_free_compile_result(&failed);
 return 0;
}
