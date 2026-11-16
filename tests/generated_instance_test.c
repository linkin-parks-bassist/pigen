#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pigen/resolve.h"
#include "pigen/rtl_lower.h"

/* Override constants belong to each selected lexical iteration. */
static void check_scoped_parameters(int selected)
{
	char text[1024];
	snprintf(text,sizeof text,
		"module parent(input wire clk,input wire reset,input uint[32] axi4_stream s[2]);"
		"genvar i; for(i=0;i<2;i++) begin : lanes "
		"case(%d ? {1'b1,190'b0,2'bxz} : 193'b0) {1'b1,190'b0,2'bxz}: leaf #(.TAG(lanes[i].i+1)) child(.clk(clk),.reset(reset),.s(s[i])); default:; endcase "
		"end endmodule "
		"module leaf #(parameter TAG=0)(input wire clk,input wire reset,input uint[32] axi4_stream s); endmodule",selected);
	pigen_source_manager sources={0};
	pigen_source_id input=pigen_source_add(&sources,"scoped_parameters.pigen",text,strlen(text));
	pigen_preprocess_result preprocessed={0};pigen_preprocess_error pe={0};
	pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&preprocessed,&pe));
	assert(pigen_parse_syntax(&preprocessed.expanded,&syntax,&se));
	int resolved=pigen_resolve_semantics(&syntax,&sem,&policy,&error);
	if(!resolved) fprintf(stderr,"scoped parameter resolution: %s\n",error.message ? error.message : "none");
	assert(resolved);
	pigen_symbol_id external=pigen_symbol_lookup_text(&sem,sem.modules[0].scope,"i",1);
	assert(external.index!=PIGEN_INVALID_ID && sem.symbols[external.index].kind==PIGEN_SYMBOL_GENVAR);
	assert(pigen_symbol_signal(&sem,external).index==PIGEN_INVALID_ID);
	if(sem.instance_count!=(selected ? 2u : 0u)) fprintf(stderr,"selected=%d count=%zu\n",selected,sem.instance_count);
	assert(sem.instance_count==(selected ? 2u : 0u));
	for(size_t i=0;i<sem.instance_count;i++)
	{
		pigen_scope_id instance_scope=sem.symbols[sem.instances[i].symbol.index].scope;
		pigen_symbol_id iteration=pigen_symbol_lookup_text(&sem,instance_scope,"i",1);
		assert(iteration.index!=external.index && sem.symbols[iteration.index].kind==PIGEN_SYMBOL_PARAMETER);
		pigen_module_id child=sem.instances[i].child_module;
		assert(sem.modules[child.index].specialization_of.index==1);
		assert(sem.modules[child.index].specialization_parent.index==0);
		size_t p=0;
		for(;p<sem.parameter_count;p++) if(sem.parameters[p].module.index==child.index && !sem.parameters[p].is_local) break;
		assert(p<sem.parameter_count);
		int64_t value=0;
		assert(pigen_const_expr_evaluate_i64(&sem,pigen_expr_constant(&sem,sem.parameters[p].value),&value));
		assert(value==(int64_t)i+1);
	}

	pigen_rtl_model rtl={0};pigen_rtl_lowering lowering={0};pigen_rtl_lowering_init(&lowering,&sem,&rtl);
	for(size_t m=0;m<sem.module_count;m++)
	{
		pigen_module_id module={(uint32_t)m};
		assert(!pigen_lower_rtl_transfers(&lowering,module));
		assert(!pigen_lower_rtl_generated_instances(&lowering,module));
		assert(!pigen_lower_rtl_endpoint_defaults(&lowering,module));
		assert(pigen_lower_rtl_module(&lowering,module).index!=PIGEN_INVALID_ID);
	}
	size_t constants=0;
	for(size_t i=0;i<rtl.object_count;i++) if(rtl.objects[i].kind==PIGEN_RTL_OBJECT_PARAMETER)
	{
		constants++;
		assert(!strcmp(rtl.objects[i].name,"TAG"));
		assert(pigen_rtl_expr_get(&rtl,rtl.objects[i].initial_value));
	}
	assert(constants==(selected ? 2u : 0u));
	pigen_rtl_lowering_free(&lowering);pigen_free_rtl_model(&rtl);

	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&preprocessed);pigen_free_sources(&sources);
}

static void check_source_references(const char *family)
{
	char text[4096],protocol[256];
	int stream=!strcmp(family,"axi4_stream");
	snprintf(protocol,sizeof protocol,"uint[32] %s[%s]",family,stream ? "id=2,last=1" : !strcmp(family,"axi4") ? "addr=40,id=2,awuser=3" : "addr=40");
	snprintf(text,sizeof text,"module mapped(input wire clk,input wire reset,input uint[8] wire flag,input %s s[-1:0],output %s m[-1:0]);"
		"if(1) begin for(genvar i=-1;i<1;i++) begin : lanes uint[32] buf local_bus;"
		"leaf child(.clk(clk),.reset(reset),.s(first.m),.m(m[i]),.tap(flag)); leaf first(.clk(clk),.reset(reset),.s(s[i]),.tap(flag)); end end endmodule "
		"module leaf(input wire clk,input wire reset,input %s s,output %s m,input uint[8] wire tap); always @(posedge clk) if(!reset) begin %s end endmodule",
		protocol,protocol,protocol,protocol,stream ? "m<=s;" : "m.aw<=s.aw;m.w<=s.w;s.b<=m.b;m.ar<=s.ar;s.r<=m.r;");
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"source_references.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
	pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
	assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
	assert(pigen_resolve_semantics(&syntax,&sem,&policy,&error));
	pigen_rtl_model rtl={0};pigen_rtl_lowering lowering={0};pigen_rtl_lowering_init(&lowering,&sem,&rtl);
	for(size_t m=0;m<sem.module_count;m++)
	{
		pigen_module_id id={(uint32_t)m};
		assert(!pigen_lower_rtl_transfers(&lowering,id));
		assert(!pigen_lower_rtl_endpoint_defaults(&lowering,id));
		assert(pigen_lower_rtl_module(&lowering,id).index!=PIGEN_INVALID_ID);
	}
	char local[128],pin[128],alias[128],child_pin[128];
	snprintf(local,sizeof local,"genblk1.lanes[-1].local_bus");
	snprintf(pin,sizeof pin,"s[-1].%s.%s",stream ? "beat" : "aw",stream ? "data" : "addr");
	snprintf(alias,sizeof alias,"genblk1.lanes[-1].child.s.%s",stream ? "beat" : "aw");
	snprintf(child_pin,sizeof child_pin,"genblk1.lanes[-1].child.s.%s.%s",stream ? "beat" : "aw",stream ? "data" : "addr");
	int scope_found=0,local_found=0,pin_found=0,child_found=0,alias_found=0,child_pin_found=0;
	pigen_rtl_object_id parent_object={PIGEN_INVALID_ID},alias_object={PIGEN_INVALID_ID},sibling_object={PIGEN_INVALID_ID},flag_object={PIGEN_INVALID_ID};
	pigen_rtl_instance_id tap_instance={PIGEN_INVALID_ID};
	for(size_t i=0;i<rtl.source_reference_count;i++)
	{
		const pigen_rtl_source_reference *ref=pigen_rtl_source_reference_get(&rtl,i);
		assert(ref && ref->source_path && ref->emitted_name);
		if(ref->role==PIGEN_RTL_SOURCE_SCOPE && !strcmp(ref->source_path,"genblk1.lanes[-1]")) scope_found++;
		if(ref->role==PIGEN_RTL_SOURCE_VALUE && !strcmp(ref->source_path,local)) local_found++;
		if(ref->role==PIGEN_RTL_SOURCE_FIELD && !strcmp(ref->source_path,pin)) pin_found++;
		if(ref->role==PIGEN_RTL_SOURCE_INSTANCE && !strcmp(ref->source_path,"genblk1.lanes[-1].child")) child_found++;
		if(ref->role==PIGEN_RTL_SOURCE_VALUE && !strcmp(ref->source_path,alias)) {alias_found++;alias_object=ref->object;}
		if(ref->module.index==0 && ref->role==PIGEN_RTL_SOURCE_VALUE && !strcmp(ref->source_path,stream ? "s[-1].beat" : "s[-1].aw")) parent_object=ref->object;
		if(ref->role==PIGEN_RTL_SOURCE_VALUE && !strcmp(ref->source_path,stream ? "genblk1.lanes[-1].first.m.beat" : "genblk1.lanes[-1].first.m.aw")) sibling_object=ref->object;
		if(ref->role==PIGEN_RTL_SOURCE_INSTANCE_PORT && !strcmp(ref->source_path,"genblk1.lanes[-1].child.tap")) tap_instance=ref->instance;
		if(ref->module.index==0 && ref->role==PIGEN_RTL_SOURCE_VALUE && !strcmp(ref->source_path,"flag")) flag_object=ref->object;
		if(ref->object.index!=PIGEN_INVALID_ID) assert(!strcmp(ref->emitted_name,rtl.objects[ref->object.index].name));
		if(ref->role==PIGEN_RTL_SOURCE_INSTANCE) assert(!strcmp(ref->emitted_name,rtl.instances[ref->instance.index].name));
        if(ref->role==PIGEN_RTL_SOURCE_INSTANCE_PORT && !strcmp(ref->source_path,child_pin))
        {
            const char *suffix=stream ? ".s_tdata" : ".s_awaddr";
            const pigen_rtl_instance *target=&rtl.instances[ref->instance.index];
            assert(!strncmp(ref->emitted_name,target->name,strlen(target->name)));
            assert(!strcmp(ref->emitted_name+strlen(target->name),suffix));child_pin_found++;
        }
	}
	assert(scope_found==1 && local_found==1 && pin_found==1 && child_found==1 && alias_found==1 && child_pin_found==1);
	assert(parent_object.index!=PIGEN_INVALID_ID && alias_object.index!=PIGEN_INVALID_ID);
	/* Bound collection elements may use typed projection equations; retain
	 * both source views even when they have distinct implementation objects. */
	assert(sibling_object.index!=PIGEN_INVALID_ID);
	assert(tap_instance.index!=PIGEN_INVALID_ID && flag_object.index!=PIGEN_INVALID_ID);
	const pigen_rtl_instance *tap=&rtl.instances[tap_instance.index];int connected=0;
	for(size_t c=0;c<tap->connection_records.count;c++)
	{
		const pigen_rtl_connection *connection=&rtl.connections[tap->connection_records.first+c];
		if(strcmp(connection->port,"tap")) continue;
		const pigen_rtl_expr *value=pigen_rtl_expr_get(&rtl,connection->value);
		assert(value && value->kind==PIGEN_RTL_EXPR_OBJECT && value->object.index==flag_object.index);connected=1;
	}
	assert(connected);
	size_t modules_before=rtl.module_count;
	size_t before=rtl.source_reference_count;
	for(size_t m=0;m<sem.module_count;m++) assert(pigen_lower_rtl_module(&lowering,(pigen_module_id){(uint32_t)m}).index!=PIGEN_INVALID_ID);
	assert(rtl.source_reference_count==before && rtl.module_count==modules_before);
	pigen_rtl_source_reference bad=rtl.source_references[0];bad.emitted_name="conflicting_name";
	assert(pigen_rtl_source_reference_add(&rtl,&bad)==SIZE_MAX && rtl.source_reference_count==before);
	bad=rtl.source_references[0];bad.module.index=PIGEN_INVALID_ID;
	assert(pigen_rtl_source_reference_add(&rtl,&bad)==SIZE_MAX && rtl.source_reference_count==before);
    for(size_t i=0;i<rtl.source_reference_count;i++) if(rtl.source_references[i].object.index!=PIGEN_INVALID_ID)
    {
        bad=rtl.source_references[i];bad.module.index=bad.module.index ? 0 : 1;
        assert(pigen_rtl_source_reference_add(&rtl,&bad)==SIZE_MAX && rtl.source_reference_count==before);break;
    }
	assert(!pigen_rtl_source_reference_get(&rtl,SIZE_MAX));
	pigen_rtl_lowering_free(&lowering);pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
	for(size_t i=0;i<rtl.source_reference_count;i++) assert(rtl.source_references[i].source_path[0] && rtl.source_references[i].emitted_name[0]);
	pigen_free_rtl_model(&rtl);
}

int main(void)
{
	check_source_references("axi4_stream");
	check_source_references("axi4_lite");
	check_source_references("axi4");
	check_scoped_parameters(1);
	check_scoped_parameters(0);
	const char *text =
		"module parent(input wire clk,input wire reset,input int[32] axi4_stream s[2],output int[32] axi4_stream m[2],output uint[8] wire tags[2]);\n"
		"generate for(genvar i=0;i<2;i++) begin : lanes\n"
		"leaf child(.clk(clk),.reset(reset),.s(s[i]),.m(m[i]),.tag(uint[8]'(i)),.seen(tags[i]));\n"
		"end endgenerate endmodule\n"
		"module leaf(input wire clk,input wire reset,input int[32] axi4_stream s,output int[32] axi4_stream m,input uint[8] wire tag,output uint[8] wire seen);\n"
		"assign seen=tag; always @(posedge clk) if(!reset) m <= s; endmodule\n";
	pigen_source_manager sources={0};
	pigen_source_id input=pigen_source_add(&sources,"generated_instances.pigen",text,strlen(text));
	pigen_preprocess_result preprocessed={0};pigen_preprocess_error pe={0};
	pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};
	pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&preprocessed,&pe));
	assert(pigen_parse_syntax(&preprocessed.expanded,&syntax,&se));
	assert(pigen_resolve_semantics(&syntax,&sem,&policy,&error));
	assert(sem.instance_count==2 && sem.generate_scope_count==2);
	assert(sem.instances[0].syntax.index==sem.instances[1].syntax.index);
	assert(sem.instances[0].symbol.index!=sem.instances[1].symbol.index);
	assert(sem.symbols[sem.instances[0].symbol.index].scope.index!=sem.symbols[sem.instances[1].symbol.index].scope.index);
	pigen_rtl_model rtl={0};pigen_rtl_lowering lowering={0};
	pigen_rtl_lowering_init(&lowering,&sem,&rtl);
	for(size_t m=0;m<sem.module_count;m++)
	{
		pigen_module_id module={(uint32_t)m};
		assert(!pigen_lower_rtl_transfers(&lowering,module));
		assert(!pigen_lower_rtl_generated_instances(&lowering,module));
		assert(!pigen_lower_rtl_endpoint_defaults(&lowering,module));
		assert(pigen_lower_rtl_module(&lowering,module).index!=PIGEN_INVALID_ID);
	}
	size_t count=0;const char *names[2]={NULL,NULL};
	for(size_t i=0;i<rtl.instance_count;i++)
	{
		const pigen_rtl_instance *instance=&rtl.instances[i];
		if(!instance->definition) continue;
		assert(count<2 && !strcmp(instance->definition,"leaf"));
		assert(instance->semantic_instance.index<sem.instance_count);
		assert(instance->primitive==PIGEN_RTL_PRIMITIVE_INVALID);
		assert(instance->connection_records.count==10);
		int data=0,valid=0,tag=0;
		for(size_t c=0;c<instance->connection_records.count;c++)
		{
			const pigen_rtl_connection *connection=&rtl.connections[instance->connection_records.first+c];
			assert(connection->port && pigen_rtl_expr_get(&rtl,connection->value));
			data+=!strcmp(connection->port,"s_tdata") || !strcmp(connection->port,"m_tdata");
			valid+=!strcmp(connection->port,"s_tvalid") || !strcmp(connection->port,"m_tvalid");
			tag+=!strcmp(connection->port,"tag");
			if(!strcmp(connection->port,"s_tready") || !strcmp(connection->port,"m_tdata"))
				assert(connection->direction==PIGEN_SEMANTIC_OUTPUT);
			if(!strcmp(connection->port,"tag")) assert(connection->direction==PIGEN_SEMANTIC_INPUT);
			if(!strcmp(connection->port,"m_tdata"))
				assert(pigen_rtl_type_get(&rtl,pigen_rtl_expr_get(&rtl,connection->value)->type)->signedness==PIGEN_SIGN_SIGNED);

		}
		assert(data==2 && valid==2 && tag==1);
		names[count++]=instance->name;
	}
	assert(count==2 && strcmp(names[0],names[1]));
	size_t before=rtl.instance_count;
	assert(!pigen_lower_rtl_generated_instances(&lowering,(pigen_module_id){0}));
	assert(rtl.instance_count==before);
	pigen_rtl_lowering_free(&lowering);pigen_free_semantic_model(&sem);
	pigen_free_syntax_tree(&syntax);pigen_free_preprocess_result(&preprocessed);pigen_free_sources(&sources);
	/* Definition/instance/port names belong to the RTL model. */
	for(size_t i=0;i<rtl.instance_count;i++) if(rtl.instances[i].definition)
		assert(!strcmp(rtl.instances[i].definition,"leaf"));
	pigen_free_rtl_model(&rtl);
	puts("PASS: scoped generated instances publish owned RTL definitions, names and typed connections");
	return 0;
}
