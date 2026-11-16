#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pigen/compile.h"

int main(void)
{
	const char *source="module route(input wire clk,input wire reset,"
		"input uint[32] axi4_stream[id=4,dest=2,last=1] s[2],"
		"output uint[32] axi4_stream[id=5,dest=2,last=1] m[3]);"
		"fabric begin s[0] -> m[0]; s[1] -> m[0]; endfabric "
		"for(genvar a=0;a<2;a++) begin : inputs for(genvar b=1;b<3;b++) begin : outputs "
		"fabric begin s[a] -> m[b]; endfabric end end endmodule";
	for(int borrowed=0;borrowed<2;borrowed++)
	{
		pigen_source_manager sources={0};pigen_compile_result result={0};pigen_compile_error error={0};
		assert(pigen_compile_source(borrowed ? &sources : NULL,"route.pigen",source,strlen(source),NULL,&result,&error)==0);
		assert(result.length && strstr(result.systemverilog,"pigen_axis_demux") && strstr(result.systemverilog,"pigen_axis_merge"));
		pigen_free_compile_result(&result);
		assert(pigen_compile_source(borrowed ? &sources : NULL,"discard.pigen",source,strlen(source),NULL,NULL,&error)==0);
		if(borrowed)
		{
			const pigen_source_file *retained=pigen_source_get(&sources,(pigen_source_id){0});
			assert(retained && retained->length==strlen(source) && !memcmp(retained->text,source,retained->length));
		}
		pigen_free_sources(&sources);
	}
	/* A lowered model exists before this unsupported fan-in size fails. */
	const char *failure="module failure(input wire clk,input wire reset,"
		"input uint[32] axi4_stream s[33],output uint[32] axi4_stream m);"
		"for(genvar i=0;i<33;i++) begin : inputs fabric begin s[i] -> m; endfabric end endmodule";
	pigen_compile_result result={0};pigen_compile_error error={0};
	assert(pigen_compile_source(NULL,"failure.pigen",failure,strlen(failure),NULL,&result,&error)!=0);
	assert(!result.systemverilog && error.message);
	pigen_free_compile_result(&result);
	puts("PASS native routing compiler ownership: borrowed/internal sources, retained/discarded output and lowered failure");
	return 0;
}
