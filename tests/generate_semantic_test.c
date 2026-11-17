#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pigen/resolve.h"
#include "pigen/expression_analysis.h"
#include "pigen/pipeline_identity.h"

static void check_combinational_cases(void)
{
    const char *text="module cases(input logic [7:0] value,output logic [7:0] result);"
        "always_comb begin uint[8] helper; helper=uint[8]'(value);"
        "unique case(value) 8'd1,8'd2: begin logic [7:0] value; value=8'd0;"
        "unique0 casez(value) 8'b???????1: result=8'd1; default result=8'd2; endcase end "
        "default: result=8'(helper); endcase while(helper) helper=0; repeat(helper) result=0; for(int i=-2,j=0;i<2;i++,j++) result=8'(i+j); for(int i=0;i<1;i++) result=0; for(;;) begin if(helper) continue; break; end do result=0; while(helper); forever break; end endmodule";
    pigen_source_manager sources={0};
    pigen_source_id input=pigen_source_add(&sources,"case.pigen",text,strlen(text));
    pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
    pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
    pigen_semantic_model model={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
    assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
    assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
    assert(pigen_resolve_semantics(&syntax,&model,&policy,&error));
    size_t cases=0; pigen_procedural_id selection={PIGEN_INVALID_ID};
    for(size_t i=0;i<model.procedural_count;i++)
    {
        const pigen_semantic_procedural *node=&model.procedural[i];
        if(node->kind<PIGEN_PROCEDURAL_CASE || node->kind>PIGEN_PROCEDURAL_CASEX) continue;
        cases++;
        assert(node->case_qualifier==(node->kind==PIGEN_PROCEDURAL_CASE ? PIGEN_CASE_UNIQUE : PIGEN_CASE_UNIQUE0));
        if(node->kind==PIGEN_PROCEDURAL_CASE) selection=(pigen_procedural_id){(uint32_t)i};
        const pigen_semantic_expr *selector=pigen_expr_get(&model,node->condition);
        uint64_t width; assert(selector && pigen_const_expr_evaluate_u64(&model,pigen_data_type_packed_width(&model,selector->data_type),&width) && width==8);
        for(size_t c=0;c<node->child_count;c++)
        {
            const pigen_semantic_procedural *item=&model.procedural[model.procedural_children[node->first_child+c].index];
            assert(item->kind==PIGEN_PROCEDURAL_CASE_ITEM && item->child_count==1);
            if(item->condition.index!=PIGEN_INVALID_ID)
            {
                const pigen_semantic_expr *label=pigen_expr_get(&model,item->condition);
                assert(label && pigen_const_expr_evaluate_u64(&model,pigen_data_type_packed_width(&model,label->data_type),&width) && width==8);
                assert(pigen_data_type_signedness(&model,label->data_type)==PIGEN_SIGN_UNSIGNED);
            }
        }
    }
    size_t loops=0;
    for(size_t i=0;i<model.procedural_count;i++)
    {
        const pigen_semantic_procedural *node=&model.procedural[i];
        if(node->kind!=PIGEN_PROCEDURAL_WHILE && node->kind!=PIGEN_PROCEDURAL_REPEAT) continue;
        loops++;assert(node->child_count==1);
        uint64_t width;
        assert(pigen_const_expr_evaluate_u64(&model,pigen_data_type_packed_width(&model,pigen_expr_get(&model,node->condition)->data_type),&width));
        assert(width==(node->kind==PIGEN_PROCEDURAL_WHILE ? 1 : 32));
        size_t before=model.procedural_count;
        assert(pigen_procedural_add(&model,node->process,node->kind,node->condition,(pigen_transfer_id){PIGEN_INVALID_ID},NULL,0,node->span).index==PIGEN_INVALID_ID);
        assert(model.procedural_count==before);
    }
    assert(loops==2);
    size_t for_count=0;
    for(size_t i=0;i<model.procedural_count;i++)
    {
        const pigen_semantic_procedural *node=&model.procedural[i];
        if(node->kind!=PIGEN_PROCEDURAL_FOR) continue;
        for_count++;assert(node->child_count==3 && node->condition.index!=PIGEN_INVALID_ID);
        const pigen_semantic_procedural *init=&model.procedural[model.procedural_children[node->first_child].index];
        const pigen_semantic_procedural *step=&model.procedural[model.procedural_children[node->first_child+1].index];
        assert(init->kind==PIGEN_PROCEDURAL_BLOCK && step->kind==PIGEN_PROCEDURAL_BLOCK);
        size_t before=model.procedural_count;
        assert(pigen_procedural_add(&model,node->process,PIGEN_PROCEDURAL_FOR,node->condition,
            (pigen_transfer_id){PIGEN_INVALID_ID},NULL,0,node->span).index==PIGEN_INVALID_ID);
        assert(model.procedural_count==before);
    }
    assert(for_count==3);
    size_t trailing=0,unbounded=0,exits=0;
    for(size_t i=0;i<model.procedural_count;i++)
    {
        const pigen_semantic_procedural *node=&model.procedural[i];
        if(node->kind==PIGEN_PROCEDURAL_DO_WHILE) {trailing++;assert(node->child_count==1 && node->condition.index!=PIGEN_INVALID_ID);}
        if(node->kind==PIGEN_PROCEDURAL_FOREVER) {unbounded++;assert(node->child_count==1 && node->condition.index==PIGEN_INVALID_ID);}
        if(node->kind==PIGEN_PROCEDURAL_BREAK || node->kind==PIGEN_PROCEDURAL_CONTINUE) {exits++;assert(!node->child_count && node->condition.index==PIGEN_INVALID_ID);}
    }
    assert(trailing==1 && unbounded==1 && exits==3);


    assert(cases==2 && selection.index!=PIGEN_INVALID_ID);
    pigen_semantic_procedural outer=model.procedural[selection.index];
    assert(outer.child_count==3);
    pigen_procedural_id first=model.procedural_children[outer.first_child],second=model.procedural_children[outer.first_child+1];
    assert(model.procedural_children[model.procedural[first.index].first_child].index==model.procedural_children[model.procedural[second.index].first_child].index);
    assert(!model.procedural[first.index].case_continuation && model.procedural[second.index].case_continuation==1);
    assert(pigen_procedural_add(&model,outer.process,PIGEN_PROCEDURAL_CASE,outer.condition,
        (pigen_transfer_id){PIGEN_INVALID_ID},&second,1,outer.span).index==PIGEN_INVALID_ID);
    pigen_procedural_id fallback=model.procedural_children[outer.first_child+2],duplicates[]={fallback,fallback};
    size_t count=model.procedural_count,children=model.procedural_child_count;
    assert(pigen_procedural_add(&model,outer.process,PIGEN_PROCEDURAL_CASE,outer.condition,(pigen_transfer_id){PIGEN_INVALID_ID},duplicates,2,outer.span).index==PIGEN_INVALID_ID);
    assert(pigen_procedural_add(&model,outer.process,PIGEN_PROCEDURAL_BLOCK,(pigen_expr_id){PIGEN_INVALID_ID},(pigen_transfer_id){PIGEN_INVALID_ID},&first,1,outer.span).index==PIGEN_INVALID_ID);
    assert(model.procedural_count==count && model.procedural_child_count==children);
    pigen_free_semantic_model(&model);pigen_free_syntax_tree(&syntax);
    pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_combinational_locals(void)
{
    const char *text="module locals(output uint[8] wire observed);"
        "always_comb begin localparam int W=8; typedef logic [W-1:0] word_t;"
        "word_t value; uint[8] temporary; int offset; offset=-1; value=W'(offset);"
        "temporary=uint[8]'(value); value+=8'd1; --temporary; temporary++; begin static word_t value; value=0; temporary|=uint[8]'(value); end "
        "observed=temporary; end endmodule";
    pigen_source_manager sources={0};
    pigen_source_id input=pigen_source_add(&sources,"locals.pigen",text,strlen(text));
    pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
    pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
    pigen_semantic_model model={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
    assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
    assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
    assert(pigen_resolve_semantics(&syntax,&model,&policy,&error));
    size_t blocks=0; pigen_symbol_id values[2];
    for(size_t i=0;i<model.scope_count;i++) if(model.scopes[i].is_procedural)
    {
        assert(blocks<2);
        values[blocks++]=pigen_symbol_lookup_text(&model,(pigen_scope_id){(uint32_t)i},"value",5);
    }
    assert(blocks==2 && values[0].index!=PIGEN_INVALID_ID && values[1].index!=PIGEN_INVALID_ID);
    assert(values[0].index!=values[1].index);
    assert(pigen_symbol_lookup_text(&model,model.modules[0].scope,"value",5).index==PIGEN_INVALID_ID);
    assert(pigen_symbol_lookup_text(&model,model.modules[0].scope,"temporary",9).index==PIGEN_INVALID_ID);
    assert(model.process_count==1 && model.processes[0].is_combinational);
    size_t updates=0;
    for(size_t i=0;i<model.transfer_count;i++)
    {
        const pigen_semantic_transfer *transfer=&model.transfers[i];
        const pigen_syntax_node *statement=pigen_syntax_get(&syntax,transfer->syntax);
        if(statement->kind==PIGEN_SYNTAX_BLOCKING_ASSIGNMENT &&
            (statement->as.nonblocking_assignment.compound || statement->as.nonblocking_assignment.increment))
        {
            updates++;
            int read_write=0;
            for(size_t u=0;u<transfer->signal_use_count;u++)
            {
                const pigen_transfer_signal_use *use=&model.transfer_signal_uses[transfer->first_signal_use+u];
                if((use->roles & (PIGEN_TRANSFER_SIGNAL_READ|PIGEN_TRANSFER_SIGNAL_WRITE))==
                    (PIGEN_TRANSFER_SIGNAL_READ|PIGEN_TRANSFER_SIGNAL_WRITE)) read_write=1;
            }
            assert(read_write);
        }
    }
    assert(updates==4);
    pigen_free_semantic_model(&model);pigen_free_syntax_tree(&syntax);
    pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_combinational_processes(void)
{
    for(int conflict=0;conflict<2;conflict++)
    {
        char text[2048];
        snprintf(text,sizeof text,
            "module comb(input wire clk,input wire reset,input uint[32] axi4_stream s[2],"
            "output uint[32] axi4_stream m[2],output bit[8] wire tags[2]);"
            "for(genvar i=0;i<2;i++) begin : lanes logic [7:0] value;"
            "always_comb begin value=reset ? 8'd0 : 8'(i+3);"
            "if(value[0]) begin value[0]=0; value=value | 8'd1; end else ;"
            "tags[i]=uint[8]'(value); end %s"
            "always @(posedge clk) if(!reset) m[i] <= s[i]; end endmodule",
            conflict ? "assign value=8'd0;" : "");
        pigen_source_manager sources={0};
        pigen_source_id input=pigen_source_add(&sources,"combinational.pigen",text,strlen(text));
        pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
        pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
        pigen_semantic_model model={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
        assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
        assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
        int ok=pigen_resolve_semantics(&syntax,&model,&policy,&error);
        if(!ok && !conflict) fprintf(stderr,"combinational: %s\n",error.message ? error.message : "none");
        assert(ok==!conflict);
        if(conflict) assert(error.message && strstr(error.message,"overlapping writers"));
        else
        {
            size_t processes=0;
            pigen_process_id owners[2];
            for(size_t i=0;i<model.process_count;i++) if(model.processes[i].is_combinational)
            {
                assert(processes<2); owners[processes++]=(pigen_process_id){(uint32_t)i};
                assert(model.processes[i].body.index<model.procedural_count);
                assert(model.procedural[model.processes[i].body.index].process.index==i);
            }
            assert(processes==2);
            for(size_t i=0;i<model.procedural_count;i++)
            {
                const pigen_semantic_procedural *node=&model.procedural[i];
                for(size_t c=0;c<node->child_count;c++)
                {
                    pigen_procedural_id child=model.procedural_children[node->first_child+c];
                    assert(child.index<i && model.procedural[child.index].process.index==node->process.index);
                }
            }
            size_t before=model.procedural_count,children=model.procedural_child_count;
            pigen_procedural_id foreign=model.processes[owners[1].index].body;
            assert(pigen_procedural_add(&model,owners[0],PIGEN_PROCEDURAL_BLOCK,
                (pigen_expr_id){PIGEN_INVALID_ID},(pigen_transfer_id){PIGEN_INVALID_ID},&foreign,1,
                (pigen_source_span){input,0,strlen(text)}).index==PIGEN_INVALID_ID);
            assert(model.procedural_count==before && model.procedural_child_count==children);
        }
        pigen_free_semantic_model(&model);pigen_free_syntax_tree(&syntax);
        pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
    }
}

static void check_continuous_concats(void)
{
    for(int conflict=0;conflict<2;conflict++)
    {
        char text[2048];
        snprintf(text,sizeof text,
            "module wiring(input wire clk,input wire reset,input uint[32] axi4_stream s[2],"
            "output uint[32] axi4_stream m[2],output bit[8] wire tags[2]);"
            "for(genvar i=0;i<2;i++) begin : lanes wire [7:0] alias;"
            "assign {alias[7:4],alias[3:0]}=8'(i+3);"
            "assign {tags[i][7:6],tags[i][5:4]}=alias[7:4];"
            "assign {tags[i][3:2],tags[i][1:0]}=alias[3:0];%s"
            "always @(posedge clk) if(!reset) m[i] <= s[i]; end endmodule",
            conflict ? "assign {alias[7:4],tags[i][3:0]}=8'd0;" : "");
        pigen_source_manager sources={0};
        pigen_source_id input=pigen_source_add(&sources,"continuous_concat.pigen",text,strlen(text));
        pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
        pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
        pigen_semantic_model model={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
        assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
        assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
        int ok=pigen_resolve_semantics(&syntax,&model,&policy,&error);
        if(!ok && !conflict) fprintf(stderr,"continuous concat: %s\n",error.message ? error.message : "none");
        assert(ok==!conflict);
        if(conflict) assert(error.message && strstr(error.message,"continuous assignment has overlapping writers"));
        else
        {
            size_t continuous=0;
            assert(model.transfer_count==8);
            for(size_t t=0;t<model.transfer_count;t++)
            {
                const pigen_semantic_transfer *transfer=&model.transfers[t];
                if(!model.processes[transfer->process.index].is_continuous) continue;
                continuous++;
                assert(pigen_lvalue_get(&model,transfer->destination)->kind==PIGEN_LVALUE_CONCATENATION);
                const pigen_transfer_signal_use *uses=pigen_transfer_signal_uses(&model,(pigen_transfer_id){(uint32_t)t});
                int writes=0;
                for(size_t u=0;u<transfer->signal_use_count;u++)
                {
                    assert(!(uses[u].roles & (PIGEN_TRANSFER_CONSUMER|PIGEN_TRANSFER_PRODUCER)));
                    writes |= !!(uses[u].roles & PIGEN_TRANSFER_SIGNAL_WRITE);
                }
                assert(writes);
            }
            assert(continuous==6);
        }
        pigen_free_semantic_model(&model);pigen_free_syntax_tree(&syntax);
        pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
    }
}

static void check_namespace(const char *body,int expected)
{
	char text[4096];
	snprintf(text,sizeof text,"module names(input wire clk,input wire reset); generate %s endgenerate endmodule",body);
	pigen_source_manager sources={0};
	pigen_source_id input=pigen_source_add(&sources,"generate_names.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
	pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
	int parsed=pigen_parse_syntax(&pre.expanded,&syntax,&se);
	if(!parsed) fprintf(stderr,"scoped parse: %s\n",se.message ? se.message : "none");
	assert(parsed);
	int ok=pigen_resolve_semantics(&syntax,&sem,&policy,&error);
	if(ok!=!!expected) fprintf(stderr,"namespace result %d: %s\n%s\n",ok,error.message ? error.message : "none",body);
	assert(ok==!!expected);
	if(expected==2)
	{
		assert(sem.generate_scope_count==2);
		assert(!sem.generate_scopes[0].is_iteration && sem.generate_scopes[1].is_iteration);
		assert(sem.scopes[sem.generate_scopes[1].scope.index].parent.index==sem.generate_scopes[0].scope.index);
	}
	if(!expected) assert(error.message && strstr(error.message,"generate block name conflicts"));
	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_namespaces(void)
{
	const char *endpoint="uint[32] buf unused;";
	char body[2048];
	snprintf(body,sizeof body,"if(0) begin : same %s end if(0) begin : same %s end",endpoint,endpoint);
	check_namespace(body,0);
	snprintf(body,sizeof body,"if(0) begin : same %s end else begin : same %s end",endpoint,endpoint);
	check_namespace(body,1);
	snprintf(body,sizeof body,"if(0) begin : same %s end else case(0) 0:begin : same %s end default:begin : same %s end endcase",endpoint,endpoint,endpoint);
	check_namespace(body,1);
	snprintf(body,sizeof body,"if(0) begin : hidden if(0) begin : same %s end if(0) begin : same %s end end",endpoint,endpoint);
	check_namespace(body,0);
	snprintf(body,sizeof body,"localparam same=1; if(0) begin : same %s end",endpoint);
	check_namespace(body,0);
	snprintf(body,sizeof body,"if(0) begin : same %s end localparam same=1;",endpoint);
	check_namespace(body,0);
	snprintf(body,sizeof body,"uint[8] wire same; if(0) begin : same %s end",endpoint);
	check_namespace(body,0);
	snprintf(body,sizeof body,"if(0) begin : same %s end for(genvar i=0;i<0;i++) begin : same %s end",endpoint,endpoint);
	check_namespace(body,0);
	snprintf(body,sizeof body,"if(0) begin : left if(0) begin : same %s end end if(0) begin : right if(0) begin : same %s end end",endpoint,endpoint);
	check_namespace(body,1);
	snprintf(body,sizeof body,"for(genvar i=0;i<0;i++) if(0) begin : same %s end if(0) begin : same %s end",endpoint,endpoint);
	check_namespace(body,1);
	snprintf(body,sizeof body,"for(genvar i=0;i<0;i++) begin if(0) begin : i %s end end",endpoint);
	check_namespace(body,0);
	snprintf(body,sizeof body,"if(0) for(genvar i=0;i<0;i++) begin : same %s end else begin : same %s end",endpoint,endpoint);
	check_namespace(body,1);
	snprintf(body,sizeof body,"if(1) for(genvar i=0;i<1;i++) begin : same %s end",endpoint);
	check_namespace(body,2);
	snprintf(body,sizeof body,"case(0) 0:for(genvar i=0;i<1;i++) begin : same %s end endcase",endpoint);
	check_namespace(body,2);
}

static void check_scoped_fsms(void)
{
	const char *text="module controllers(input wire clk,input wire reset,input uint[32] axi4_stream s[2],output uint[32] axi4_stream m[2]);"
		"for(genvar i=0;i<2;i++) begin : lanes localparam INDEX=i; "
		"fsm @(posedge clk) begin if(reset) goto running; state running: m[INDEX] <= s[INDEX]; end "
		"if(0) begin fsm @(posedge clk) begin state disabled: m[99] <= s[99]; end end end endmodule";
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"scoped_fsm.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
	pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
	int parsed=pigen_parse_syntax(&pre.expanded,&syntax,&se);
	if(!parsed) fprintf(stderr,"scoped parse: %s\n",se.message ? se.message : "none");
	assert(parsed);
	int ok=pigen_resolve_semantics(&syntax,&sem,&policy,&error);
	if(!ok) fprintf(stderr,"scoped fsm: %s\n",error.message ? error.message : "none");
	assert(ok && sem.process_count==2 && sem.transfer_count==4 && sem.generate_scope_count==2);
	pigen_symbol_id states[2];size_t count=0;
	for(size_t i=0;i<sem.signal_count;i++)
	{
		const pigen_semantic_signal *signal=&sem.signals[i];
		const pigen_syntax_node *node=pigen_syntax_get(&syntax,signal->syntax);
		if(!node || node->kind!=PIGEN_SYNTAX_FSM) continue;
		assert(count<2);states[count++]=signal->symbol;
		assert(sem.symbols[signal->symbol.index].scope.index!=sem.modules[0].scope.index);
	}
	assert(count==2 && states[0].index!=states[1].index);
	assert(sem.symbols[states[0].index].scope.index!=sem.symbols[states[1].index].scope.index);
	assert(sem.signals[pigen_symbol_signal(&sem,states[0]).index].syntax.index==
		sem.signals[pigen_symbol_signal(&sem,states[1]).index].syntax.index);
	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_scoped_pipelines(int reset_after)
{
	char text[4096];
	snprintf(text,sizeof text,"module pipes(input wire clk,input wire reset);"
		"for(genvar i=0;i<2;i++) begin : lanes localparam W=8+i; uint[W] buf ingress,egress; "
		"always @(posedge clk) begin if(reset) flush(ingress); %s pipeline pipe begin uint[W] value; "
		"stage begin %s value <= ingress; end yield value; endpipeline "
		"egress <= pipe; %s end end endmodule",
		reset_after ? "" : "if(reset) pipeline_reset(pipe);",
		reset_after ? "uint[W] wire observed;" : "uint[W] observed;",
		reset_after ? "if(reset) pipeline_reset(pipe);" : "");
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"scoped_pipeline.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
	pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
	assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
	int ok=pigen_resolve_semantics(&syntax,&sem,&policy,&error);
	if(!ok) fprintf(stderr,"scoped pipeline: %s\n",error.message ? error.message : "none");
	assert(ok && sem.process_count==2 && sem.generate_scope_count==2 && sem.action_count==4);
	for(size_t i=0;i<2;i++)
	{
		pigen_scope_id scope=sem.generate_scopes[i].scope;
		pigen_symbol_id symbol=pigen_symbol_lookup_text(&sem,scope,"pipe",4);
		pigen_signal_id signal=pigen_symbol_signal(&sem,symbol);
		assert(signal.index!=PIGEN_INVALID_ID && sem.signals[signal.index].direction==PIGEN_SEMANTIC_INTERNAL);
		uint64_t width=0;
		assert(pigen_const_expr_evaluate_u64(&sem,pigen_data_type_packed_width(&sem,sem.signals[signal.index].data_type),&width));
		assert(width==8+i);
		pigen_pipeline_identifier recovered;
		pigen_syntax_id id=sem.signals[signal.index].syntax;
		assert(!pigen_pipeline_identifier_of_in_scope(&sem,id,(pigen_module_id){0},scope,pigen_syntax_get(&syntax,id),&pre.expanded,&recovered));
		assert(recovered.signal.index==signal.index);
		assert(pigen_pipeline_identifier_of(&sem,id,(pigen_module_id){0},pigen_syntax_get(&syntax,id),&pre.expanded,&recovered));
	}
	for(size_t a=0;a<sem.action_count;a++)
	{
		pigen_semantic_action action=sem.actions[a];
		assert(action.process.index<2 && action.action==PIGEN_ACTION_FLUSH);
		pigen_scope_id expected=sem.generate_scopes[action.process.index].scope;
		pigen_scope_id actual=sem.symbols[sem.signals[action.signal.index].symbol.index].scope;
		while(pigen_scope_get(&sem,actual) && actual.index!=expected.index) actual=sem.scopes[actual.index].parent;
		assert(actual.index==expected.index);
	}
	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_foreach_pipeline_resets(int reset_after)
{
 char text[2048];
 snprintf(text,sizeof text,"module pipes(input wire clk,input wire reset,input wire[1:0] clear,"
  "input uint[32] axi4_stream buf s[0:1]);always @(posedge clk)foreach(s[lane])begin %s "
  "pipeline packet begin uint[32] first;uint[32] second;"
  "stage begin first<=s[lane].data;end stage begin uint[32] wire observed;second<=first;end "
  "yield second;endpipeline %s end endmodule",
  reset_after ? "" : "if(clear[lane])pipeline_reset(packet);",
  reset_after ? "if(clear[lane])pipeline_reset(packet);" : "");
 pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"foreach_reset.pigen",text,strlen(text));
 pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
 pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
 assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
 int ok=pigen_resolve_semantics(&syntax,&sem,&policy,&error);
 if(!ok)fprintf(stderr,"foreach pipeline reset: %s\n",error.message ? error.message : "none");
 assert(ok && sem.process_count==1 && sem.action_count==4);
 for(size_t a=0;a<sem.action_count;a++)
 {
  pigen_semantic_action action=sem.actions[a];
  assert(action.process.index==0 && action.action==PIGEN_ACTION_FLUSH);
  pigen_scope_id scope=sem.symbols[sem.signals[action.signal.index].symbol.index].scope;
  pigen_symbol_id lane=pigen_symbol_lookup_text(&sem,scope,"lane",4);
  assert(lane.index!=PIGEN_INVALID_ID && sem.symbols[lane.index].kind==PIGEN_SYMBOL_PARAMETER);
  int64_t coordinate;
  assert(pigen_const_expr_evaluate_i64(&sem,pigen_const_expr_intern_symbol(&sem,lane,
   sem.symbols[lane.index].data_type),&coordinate) && coordinate==(int64_t)(a/2));
 }
 pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

/* Retained native-fabric semantic witnesses are not in use. */
#if 0
static void check_array_fabrics(int generated,int fanin)
{
	char text[4096];
	snprintf(text,sizeof text,"module links(input wire clk,input wire reset,input uint[32] axi4[id=4] s[-2:-1][2],"
		"input uint[32] axi4[id=4] extra[-2:-1][2],output uint[32] axi4[id=5] m[-2:-1][2]); %s"
		"fabric begin s -> m; %s endfabric %s endmodule",
		generated ? "if(1) begin : selected " : "",fanin ? "extra -> m;" : "",generated ? "end" : "");
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"array_fabrics.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
	assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
	int ok=pigen_resolve_semantics(&syntax,&sem,&policy,&error);
	if(!ok) fprintf(stderr,"array fabric: %s\n",error.message ? error.message : "none");
	assert(ok && sem.protocol_fabric_edge_count==(fanin ? 8u : 4u));
	pigen_protocol_array_id source=pigen_symbol_protocol_array(&sem,pigen_symbol_lookup_text(&sem,sem.modules[0].scope,"s",1));
	pigen_protocol_array_id extra=pigen_symbol_protocol_array(&sem,pigen_symbol_lookup_text(&sem,sem.modules[0].scope,"extra",5));
	pigen_protocol_array_id target=pigen_symbol_protocol_array(&sem,pigen_symbol_lookup_text(&sem,sem.modules[0].scope,"m",1));
	for(size_t i=0;i<sem.protocol_fabric_edge_count;i++)
	{
		const pigen_semantic_protocol_fabric_edge *edge=&sem.protocol_fabric_edges[i];
		const pigen_semantic_protocol_array *array=pigen_protocol_array_get(&sem,i<4 ? source : extra);
		assert(edge->source.index==sem.protocol_array_elements[array->first_element+i%4].index);
		assert(edge->destination.index==sem.protocol_array_elements[sem.protocol_arrays[target.index].first_element+i%4].index);
		assert(edge->syntax.index==sem.protocol_fabric_edges[(i/4)*4].syntax.index);
	}
	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_array_selection(void)
{
	const char *text="module selection(input wire clk,input wire reset,input uint[32] axi4_stream s[-3:0][4:1],output uint[32] axi4_stream m[-3:0][4:1]);"
		"fabric begin s[$signed({{2{1'b1}},1'b0}):$signed({3{'1}})][$clog2({2{2'b11}})] -> m[(~1):(~0)][4]; s[$signed({3{'1}})][{2{1'b1}}] -> m[~0][3]; endfabric endmodule";
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"array_selection.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
	int resolved=pigen_resolve_semantics(&syntax,&sem,&policy,&error);
	if(!resolved) fprintf(stderr,"array selection: %s\n",error.message ? error.message : "none");
	assert(resolved);
	assert(sem.protocol_fabric_edge_count==3);
	pigen_syntax_id id=sem.protocol_fabric_edges[0].syntax;
	const pigen_syntax_node *edge=pigen_syntax_get(&syntax,id);
	pigen_protocol_array_view view;
	assert(pigen_resolve_protocol_array_view(&syntax,&sem,sem.modules[0].scope,edge->as.fabric_connection.left_expression,&policy,&view)==1);
	assert(view.dimension_count==1 && view.element_count==2);
	assert(view.dimensions[0].left==-2 && view.dimensions[0].right==-1);
	const pigen_semantic_protocol_array *array=pigen_protocol_array_get(&sem,view.array);
	assert(view.elements[0].index==sem.protocol_array_elements[array->first_element+4].index);
	assert(view.elements[1].index==sem.protocol_array_elements[array->first_element+8].index);
	const pigen_syntax_node *scalar=pigen_syntax_get(&syntax,sem.protocol_fabric_edges[2].syntax);
	size_t expressions=sem.expression_count,children=sem.expression_child_count,constraints=sem.width_constraint_count;
	pigen_protocol_endpoint_id endpoint=pigen_resolve_protocol_reference(&syntax,&sem,sem.modules[0].scope,scalar->as.fabric_connection.left_expression, &policy);
	assert(endpoint.index==sem.protocol_array_elements[array->first_element+9].index);
	pigen_protocol_array_view scalar_view;
	assert(pigen_resolve_protocol_array_view(&syntax,&sem,sem.modules[0].scope,scalar->as.fabric_connection.left_expression,&policy,&scalar_view)==1);
	assert(scalar_view.dimension_count==0 && scalar_view.element_count==1 && scalar_view.elements[0].index==endpoint.index);
	assert(sem.expression_count==expressions && sem.expression_child_count==children && sem.width_constraint_count==constraints);
	pigen_resolve_policy small_policy={2};
	assert(pigen_resolve_protocol_reference(&syntax,&sem,sem.modules[0].scope,scalar->as.fabric_connection.left_expression,&small_policy).index==PIGEN_INVALID_ID);
	assert(pigen_resolve_protocol_array_view(&syntax,&sem,sem.modules[0].scope,scalar->as.fabric_connection.left_expression,&small_policy,&(pigen_protocol_array_view){0})==-1);
	assert(sem.expression_count==expressions && sem.expression_child_count==children && sem.width_constraint_count==constraints);
	pigen_free_protocol_array_view(&scalar_view);
	pigen_free_protocol_array_view(&view);
	assert(!view.dimensions && !view.elements && !view.element_count);
	pigen_syntax_expr *index=&syntax.expressions.nodes[edge->as.fabric_connection.left_expression.index];
	pigen_syntax_expr *slice=&syntax.expressions.nodes[index->as.index.base.index];
	pigen_syntax_expr_id saved=slice->as.select.right;
	slice->as.select.right=index->as.index.index; /* [-2:4] exceeds the row bounds. */
	assert(pigen_resolve_protocol_array_view(&syntax,&sem,sem.modules[0].scope,edge->as.fabric_connection.left_expression,&policy,&view)==-1);
	assert(!view.dimensions && !view.elements && !view.element_count);
	slice->as.select.right=saved;

	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

#endif

static void check_array_construction(int pattern)
{
	char text[1024];
	snprintf(text,sizeof text,"module leaf(input wire clk,input wire reset,input uint[32] axi4_stream s%s); endmodule "
		"module construction(input wire clk,input wire reset,input uint[32] axi4_stream s[2][2]);"
		"leaf child(.clk(clk),.reset(reset),.s(%s)); endmodule",
        pattern==3 ? "[2][1]" : pattern==4 ? "[1][1][2]" : "[2][2]",
        pattern==3 ? "'{1:'{s[1][1]},default:s[0][0]}" :
        pattern==4 ? "'{default:s[1]}" :
		pattern==2 ? "'{0:'{0:s[1][0],1:s[1][1]},1:'{default:s[0][1],0:s[0][0]}}" :
		pattern ? "'{'{s[0][1],s[0][0]},'{s[1][1],s[1][0]}}" : "{s[0],s[1]}");
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"array_construction.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
	assert(pigen_resolve_semantics(&syntax,&sem,&policy,&error));
	pigen_syntax_expr_id expression={PIGEN_INVALID_ID};
	for(size_t i=0;i<syntax.expressions.node_count;i++)
		if(syntax.expressions.nodes[i].kind==(pattern>=2 ? PIGEN_SYNTAX_EXPR_RECORD : pattern ? PIGEN_SYNTAX_EXPR_ARRAY_PATTERN : PIGEN_SYNTAX_EXPR_CONCATENATION)) expression.index=(uint32_t)i;
	assert(expression.index!=PIGEN_INVALID_ID);
	pigen_protocol_array_view view;
	pigen_scope_id scope=sem.modules[1].scope;
	pigen_protocol_array_dimension dimensions[]={{1,0,2},{1,0,2},{0,0,1}};
    size_t rank=pattern==4 ? 3 : 2,expected=pattern>=3 ? 2 : 4;
    if(pattern==3) dimensions[1]=(pigen_protocol_array_dimension){0,0,1};
    if(pattern==4) {dimensions[0]=dimensions[1]=(pigen_protocol_array_dimension){0,0,1};dimensions[2]=(pigen_protocol_array_dimension){1,0,2};}
	assert(pigen_resolve_protocol_array_binding_to_shape(&syntax,&sem,scope,expression,dimensions,rank,&policy,&view)==1);
	assert(view.array.index==PIGEN_INVALID_ID && view.dimension_count==rank && view.element_count==expected);
	pigen_protocol_endpoint_id first=view.elements[0],last=view.elements[expected-1];
	assert(first.index!=last.index);
	pigen_free_protocol_array_view(&view);
	assert(pigen_resolve_protocol_array_binding(&syntax,&sem,scope,expression,rank+1,&policy,&view)==-1);
	assert(!view.elements && !view.dimensions && !view.element_count);
    if(pattern==4)
    {
        dimensions[0]=(pigen_protocol_array_dimension){1,0,2};
        assert(pigen_resolve_protocol_array_binding_to_shape(&syntax,&sem,scope,expression,dimensions,rank,&policy,&view)==1);
        assert(view.element_count==4 && view.elements[0].index==view.elements[2].index && view.elements[1].index==view.elements[3].index);
        pigen_free_protocol_array_view(&view);
        dimensions[0]=(pigen_protocol_array_dimension){0,0,1};
    }
	policy.maximum_generated_bits=expected-1;
	assert(pigen_resolve_protocol_array_binding_to_shape(&syntax,&sem,scope,expression,dimensions,rank,&policy,&view)==-1);
	assert(!view.elements && !view.dimensions && !view.element_count);
	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_array_replication(void)
{
	const char *text="module leaf(input wire clk,input wire reset,input uint[32] axi4_stream s[4]); endmodule "
		"module repeated(input wire clk,input wire reset,input uint[32] axi4_stream s[2]); "
		"localparam N=2; if(0) leaf child(.clk(clk),.reset(reset),.s('{N{s[1],s[0]}})); endmodule";
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"array_replication.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
	assert(pigen_resolve_semantics(&syntax,&sem,&policy,&error));
	pigen_syntax_expr_id expression={PIGEN_INVALID_ID};
	for(size_t i=0;i<syntax.expressions.node_count;i++)
		if(syntax.expressions.nodes[i].kind==PIGEN_SYNTAX_EXPR_ARRAY_PATTERN_REPLICATION) expression.index=(uint32_t)i;
	assert(expression.index!=PIGEN_INVALID_ID);
	pigen_protocol_array_dimension dimension={8,11,4};pigen_protocol_array_view view;
	assert(pigen_resolve_protocol_array_binding_to_shape(&syntax,&sem,sem.modules[1].scope,expression,&dimension,1,&policy,&view)==1);
	assert(view.element_count==4 && view.dimension_count==1);
	assert(view.elements[0].index!=view.elements[1].index);
	assert(view.elements[0].index==view.elements[2].index && view.elements[1].index==view.elements[3].index);
	pigen_free_protocol_array_view(&view);
	dimension.count=3;
	assert(pigen_resolve_protocol_array_binding_to_shape(&syntax,&sem,sem.modules[1].scope,expression,&dimension,1,&policy,&view)==-1);
	assert(!view.elements && !view.dimensions && !view.element_count);
	dimension.count=4;policy.maximum_generated_bits=3;
	assert(pigen_resolve_protocol_array_binding_to_shape(&syntax,&sem,sem.modules[1].scope,expression,&dimension,1,&policy,&view)==-1);
	assert(!view.elements && !view.dimensions && !view.element_count);
	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_external_names(void)
{
	const char *text="module names #(parameter genblk2=0)(input wire clk,input wire reset,input wire genblk02,input wire genblk002);"
		"if(0) begin : retained logic ordinary; end "
		"if(1) begin uint[32] buf a; end "
		"if(0) begin : named uint[32] buf disabled; end "
		"generate if(0) begin logic ordinary; end endgenerate "
		"for(genvar i=-1;i<2;i++) begin : lanes localparam genblk1=0; if(1) begin uint[32] buf local_bus; end end "
		"if(1) for(genvar j=0;j<2;j++) begin : inner uint[32] buf loop_bus; end "
		"if(0) begin uint[32] buf skipped; end else if(1) case(0) 0:begin uint[32] buf selected; end endcase "
		"if(0) begin : genblk0002 logic later; end initial begin : genblk00002 end "
		"if(0) begin : \\escaped.block logic raw; end if(0) begin : \\genblk000002 logic raw; end endmodule";
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"external_names.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
	pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
	if(!pigen_parse_syntax(&pre.expanded,&syntax,&se)) {fprintf(stderr,"external parse: %s\n",se.message);assert(0);}
	assert(syntax.generate_name_count==13);
	assert(!strcmp(syntax.generate_names[11].external_name,"escaped.block"));
	assert(!strcmp(syntax.generate_names[12].external_name,"genblk000002"));
	if(!pigen_resolve_semantics(&syntax,&sem,&policy,&error)) {fprintf(stderr,"external resolve: %s\n",error.message);assert(0);}
	const char *names[]={"genblk0000002","lanes","genblk01","lanes","genblk01","lanes","genblk01","genblk6","inner","inner","genblk7"};
	const size_t ordinals[]={2,5,1,5,1,5,1,6,1,1,7};
	assert(sem.generate_scope_count==sizeof names/sizeof *names);
	for(size_t i=0;i<sem.generate_scope_count;i++)
	{
		const pigen_semantic_generate_scope *scope=&sem.generate_scopes[i];
		assert(scope->external_name && !strcmp(scope->external_name,names[i]));
		assert(scope->external_ordinal==ordinals[i]);
		for(size_t j=0;j<syntax.generate_name_count;j++) assert(scope->external_name!=syntax.generate_names[j].external_name);
	}
	assert(pigen_symbol_lookup_text(&sem,sem.modules[0].scope,"genblk0000002",13).index==PIGEN_INVALID_ID);
	pigen_semantic_generate_scope malformed=sem.generate_scopes[0];
	malformed.external_ordinal=0;
	assert(pigen_generate_scope_add(&sem,&malformed).index==PIGEN_INVALID_ID);
	pigen_free_syntax_tree(&syntax);
	/* The semantic model owns these strings independently of parser templates. */
	for(size_t i=0;i<sem.generate_scope_count;i++) assert(!strcmp(sem.generate_scopes[i].external_name,names[i]));
	pigen_free_semantic_model(&sem);pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_external_names_are_not_source_paths(void)
{
	const char *text="module hidden(input wire clk,input wire reset,output uint[32] axi4_stream m);"
		"if(1) begin uint[32] buf private_bus; end "
		"always @(posedge clk) if(!reset) m<=genblk1.private_bus; endmodule";
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"external_lookup.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};
	pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));
	assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
	assert(syntax.generate_name_count==1 && !strcmp(syntax.generate_names[0].external_name,"genblk1"));
	assert(!pigen_resolve_semantics(&syntax,&sem,&policy,&error) && error.message);
	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_foreach(void)
{
	const char *text="module traversal(input logic [7:0] data,output logic [31:0] q); always_comb begin "
		"uint[8] mark; logic [7:0] a[-1:1][3:2]; int sum; mark=0; sum=0; "
		"foreach(a[i,j]) begin sum+=i+j; end q=32'(sum); end endmodule";
	pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"foreach.pigen",text,strlen(text));
	pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
	pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={1024};
	assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
	size_t loops=0,indices=0;
	for(size_t i=0;i<syntax.node_count;i++)
	{loops+=syntax.nodes[i].kind==PIGEN_SYNTAX_PROCEDURAL_FOREACH;indices+=syntax.nodes[i].kind==PIGEN_SYNTAX_FOREACH_INDEX;}
	assert(loops==1 && indices==2);
	assert(pigen_resolve_semantics(&syntax,&sem,&policy,&error));
	assert(sem.process_count==1 && sem.processes[0].is_combinational);
	assert(pigen_symbol_lookup_text(&sem,sem.modules[0].scope,"i",1).index==PIGEN_INVALID_ID);
	loops=0;indices=0;
	for(size_t i=0;i<sem.procedural_count;i++) if(sem.procedural[i].kind==PIGEN_PROCEDURAL_FOR)
	{
		loops++;const pigen_semantic_procedural *loop=&sem.procedural[i];assert(loop->child_count==3);
		const pigen_semantic_procedural *body=&sem.procedural[sem.procedural_children[loop->first_child+2].index];
		assert(body->kind==PIGEN_PROCEDURAL_BLOCK && body->child_count==3);
	}
	for(size_t i=0;i<sem.signal_count;i++)
	{
		const pigen_symbol *symbol=pigen_symbol_get(&sem,sem.signals[i].symbol);size_t n=0;
		const char *name=pigen_source_span_text(&sources,symbol->name,&n);
		if(n==1 && (name[0]=='i' || name[0]=='j'))
		{indices++;assert(pigen_data_type_signedness(&sem,sem.signals[i].data_type)==PIGEN_SIGN_SIGNED);assert(pigen_data_type_state_domain(&sem,sem.signals[i].data_type)==PIGEN_DATA_TYPE_STATE_FOUR);}
	}
	assert(loops==1 && indices==2);
	pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
}

static void check_native_foreach(void)
{
	const char *texts[]={
		"module visit(input wire clk,input wire reset,input bit[32] axi4_stream buf s[-2:0][3:2],output logic [31:0] q); always_comb begin "
		"int sum;sum=0;foreach(s[i,j]) begin localparam W=32;typedef logic [W-1:0] word_t;word_t piece;int carry;"
		"if(i==-2 && j==3) carry=0;piece=peek(s[i][j]).data;carry+=int'(piece);sum+=carry;end q=32'(sum);end endmodule",
		"module metadata(input wire clk,input wire reset,input bit[32] axi4_stream buf s[2],output logic [31:0] q);"
		"always_comb foreach(s[i]) begin if(i==0) q=0;q+=32'(i+1);end endmodule",
		"module bounded(input wire clk,input wire reset,input bit[32] axi4_stream buf s[6],output logic [31:0] q);"
		"always_comb begin q=0;foreach(s[i]) foreach(s[j]) q+=32'(i+j);end endmodule",
		"module endpoint_source(input wire clk,input wire reset,output bit[32] axi4_stream buf m[2]);endmodule "
		"module metadata_child(input wire clk,input wire reset,output logic [31:0] q);endpoint_source leaf(.clk(clk),.reset(reset));"
		"always_comb foreach(leaf.m[i]) begin if(i==1) q=0;q+=32'(i+1);end endmodule",
		"module metadata_forward(input wire clk,input wire reset,output logic [31:0] q);"
		"always_comb foreach(leaf.m[i]) begin if(i==1) q=0;q+=32'(i+1);end endpoint_source leaf(.clk(clk),.reset(reset));endmodule "
		"module endpoint_source(input wire clk,input wire reset,output bit[32] axi4_stream buf m[2]);endmodule",
		"module endpoint_source(input wire clk,input wire reset,output bit[32] axi4_stream buf m[2]);endmodule "
		"module metadata_generated(input wire clk,input wire reset,output logic [31:0] q);"
		"for(genvar g=0;g<1;g++) begin : bank endpoint_source leaf(.clk(clk),.reset(reset));end "
		"always_comb foreach(bank[0].leaf.m[i]) begin if(i==1) q=0;q+=32'(i+1);end endmodule"};
	for(size_t mode=0;mode<sizeof texts/sizeof *texts;mode++)
	{
		pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"native_foreach.pigen",texts[mode],strlen(texts[mode]));
		pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
		pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={mode==2 ? 32 : 1024};
		assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
		int resolved=pigen_resolve_semantics(&syntax,&sem,&policy,&error);
		if(mode==2) {assert(!resolved && error.message && strstr(error.message,"specialization limit"));}
		else
		{
			if(!resolved) fprintf(stderr,"native foreach semantic: %s\n",error.message ? error.message : "none");
			assert(resolved && sem.process_count==1 && sem.processes[0].is_combinational);
			assert(pigen_symbol_lookup_text(&sem,sem.modules[0].scope,"i",1).index==PIGEN_INVALID_ID);
			if(!mode)
			{
				size_t carries=0,pieces=0,aliases=0,coordinates=0;const int64_t expected[]={-2,-2,-1,-1,0,0};
				for(size_t i=0;i<sem.signal_count;i++)
				{const pigen_symbol *symbol=pigen_symbol_get(&sem,sem.signals[i].symbol);size_t n=0;const char *name=pigen_source_span_text(&sources,symbol->name,&n);carries+=n==5 && !memcmp(name,"carry",5);pieces+=n==5 && !memcmp(name,"piece",5);}
				assert(carries==1 && pieces==1);
				for(size_t i=0;i<sem.symbol_count;i++)
				{
					pigen_symbol symbol=sem.symbols[i];size_t n=0;const char *name=pigen_source_span_text(&sources,symbol.name,&n);
					if(symbol.kind==PIGEN_SYMBOL_PARAMETER && n==1 && name[0]=='i')
					{int64_t coordinate;assert(coordinates<6 && pigen_const_expr_evaluate_i64(&sem,pigen_const_expr_intern_symbol(&sem,(pigen_symbol_id){(uint32_t)i},symbol.data_type),&coordinate) && coordinate==expected[coordinates++]);}
					if(symbol.alias_of.index!=PIGEN_INVALID_ID)
					{
						aliases++;assert(pigen_symbol_lookup(&sem,symbol.scope,symbol.name).index==symbol.alias_of.index);
						assert(pigen_symbol_lookup_text(&sem,symbol.scope,name,n).index==symbol.alias_of.index);
						pigen_symbol_id alias;size_t before=sem.symbol_count;
						assert(pigen_symbol_bind_procedural_alias(&sem,symbol.scope,symbol.alias_of,&alias)==PIGEN_DECLARE_DUPLICATE && sem.symbol_count==before);
						assert(pigen_symbol_bind_procedural_alias(&sem,sem.modules[0].scope,symbol.alias_of,&alias)==PIGEN_DECLARE_INVALID && sem.symbol_count==before);
						if(symbol.kind==PIGEN_SYMBOL_SIGNAL)
						{
							assert(pigen_symbol_signal(&sem,(pigen_symbol_id){(uint32_t)i}).index==PIGEN_INVALID_ID);
							pigen_signal_id signal=pigen_symbol_signal(&sem,symbol.alias_of);
							sem.signals[signal.index].is_automatic=1;
							assert(pigen_symbol_bind_procedural_alias(&sem,symbol.scope,symbol.alias_of,&alias)==PIGEN_DECLARE_INVALID && sem.symbol_count==before);
							sem.signals[signal.index].is_automatic=0;
						}
					}
				}
				assert(aliases>=15 && coordinates==6);
			}
		}
		pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
	}
}

static void check_clocked_foreach_limits(void)
{
 const char *nested="module visit(input wire clk,input wire[3:0] values,output logic q);"
  "always @(posedge clk)foreach(values[i])foreach(values[j])q<=values[i]^values[j];endmodule";
 const char *separate="module visit(input wire clk,input wire[3:0] values,output logic q,output logic r);"
  "always @(posedge clk)foreach(values[i])q<=values[i];"
  "always @(posedge clk)foreach(values[i])r<=values[i];endmodule";
 for(size_t mode=0;mode<3;mode++)
 {
  const char *text=mode==2 ? separate : nested;
  pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"clocked_foreach.pigen",text,strlen(text));
  pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_syntax_error se={0};
  pigen_semantic_model sem={0};pigen_semantic_error error={0};pigen_resolve_policy policy={mode==2 ? 4 : mode==1 ? 20 : 19};
  assert(pigen_preprocess(&sources,input,NULL,&pre,&pe));assert(pigen_parse_syntax(&pre.expanded,&syntax,&se));
  int resolved=pigen_resolve_semantics(&syntax,&sem,&policy,&error);
  if(mode==0) assert(!resolved && error.message && strstr(error.message,"specialization limit"));
  else
  {
   if(!resolved)fprintf(stderr,"clocked foreach limit: %s\n",error.message ? error.message : "none");
   assert(resolved && sem.process_count==(mode==2 ? 2 : 1));
   assert(pigen_symbol_lookup_text(&sem,sem.modules[0].scope,"i",1).index==PIGEN_INVALID_ID);
   assert(pigen_symbol_lookup_text(&sem,sem.modules[0].scope,"j",1).index==PIGEN_INVALID_ID);
  }
  pigen_free_semantic_model(&sem);pigen_free_syntax_tree(&syntax);pigen_free_preprocess_result(&pre);pigen_free_sources(&sources);
 }
}

int main(void)
{
	check_foreach_pipeline_resets(0);
	check_foreach_pipeline_resets(1);
	check_clocked_foreach_limits();
	check_native_foreach();
	check_foreach();
	check_combinational_cases();
	check_combinational_locals();
	check_combinational_processes();
	check_continuous_concats();
	check_external_names();
	check_external_names_are_not_source_paths();
	check_array_construction(0);
	check_array_construction(1);
	check_array_construction(2);
	check_array_construction(3);
	check_array_construction(4);
	check_array_replication();
	/* Native fabric witnesses above are quarantined. */
	check_scoped_pipelines(0);
	check_scoped_pipelines(1);
	check_scoped_fsms();
	check_namespaces();
	const char *source =
		"module generated(input wire clk,input wire reset,input uint[32] axi4_lite s[4],output uint[32] axi4_lite m[4]);\n"
		"generate for(genvar i=0;i<4;i++) begin : lanes\n"
		"localparam K=i; uint[32] buf local_bus;\n"
		"always @(posedge clk) if(!reset) begin local_bus <= s[K].aw.addr; m[K].aw <= '{addr:local_bus,prot:0}; end\n"
		"if(0) begin : excluded uint[999999999] axi4_stream invalid_width; end\n"
		"end endgenerate endmodule\n";
	pigen_source_manager sources={0};
	pigen_source_id input=pigen_source_add(&sources,"generated.pigen",source,strlen(source));
	pigen_preprocess_result preprocessed={0}; pigen_preprocess_error preprocess_error={0};
	pigen_syntax_tree syntax={0}; pigen_syntax_error syntax_error={0};
	pigen_semantic_model model={0}; pigen_semantic_error error={0};
	pigen_resolve_policy policy={4096};
	assert(pigen_preprocess(&sources,input,NULL,&preprocessed,&preprocess_error));
	assert(pigen_parse_syntax(&preprocessed.expanded,&syntax,&syntax_error));
	assert(pigen_resolve_semantics(&syntax,&model,&policy,&error));
	assert(model.module_count==1 && model.generate_scope_count==4);
	assert(model.generate_item_count==12 && model.process_count==4 && model.transfer_count==8);
	pigen_symbol_id locals[4],iterators[4];
	for(size_t i=0;i<4;i++)
	{
		const pigen_semantic_generate_scope *scope=pigen_generate_scope_get(&model,(pigen_generate_scope_id){(uint32_t)i});
		assert(scope && scope->is_iteration && scope->index==(int64_t)i);
		assert(model.scopes[scope->scope.index].parent.index==model.modules[0].scope.index);
		iterators[i]=scope->iterator;
		const pigen_semantic_parameter *iterator=pigen_parameter_get(&model,pigen_symbol_parameter(&model,iterators[i]));
		int64_t value=-1;
		assert(iterator && iterator->is_local);
		assert(pigen_const_expr_evaluate_i64(&model,pigen_expr_constant(&model,iterator->value),&value) && value==(int64_t)i);
		locals[i]=pigen_symbol_lookup_text(&model,scope->scope,"local_bus",9);
		pigen_signal_id signal=pigen_symbol_signal(&model,locals[i]);
		assert(signal.index!=PIGEN_INVALID_ID && pigen_signal_get(&model,signal)->domain.index!=PIGEN_INVALID_ID);
		assert(pigen_symbol_lookup_text(&model,scope->scope,"invalid_width",13).index==PIGEN_INVALID_ID);
		assert(pigen_symbol_lookup_text(&model,scope->scope,"clk",3).index==
			pigen_symbol_lookup_text(&model,model.modules[0].scope,"clk",3).index);
		for(size_t p=0;p<i;p++) assert(locals[p].index!=locals[i].index && iterators[p].index!=iterators[i].index);
		pigen_semantic_generate_scope duplicate=*scope;
		assert(pigen_generate_scope_add(&model,&duplicate).index==PIGEN_INVALID_ID);
		pigen_semantic_generate_item bad={scope->syntax,scope->module,model.modules[0].scope,{(uint32_t)i}};
		assert(!pigen_generate_item_add(&model,&bad));
	}
	assert(pigen_symbol_lookup_text(&model,model.modules[0].scope,"local_bus",9).index==PIGEN_INVALID_ID);
	assert(pigen_symbol_lookup_text(&model,model.modules[0].scope,"i",1).index==PIGEN_INVALID_ID);
	assert(model.generate_scope_count==4 && model.generate_item_count==12);
	/* All earlier bindings remain immutable after later iterations resolve. */
	for(size_t i=0;i<4;i++)
	{
		const pigen_semantic_parameter *iterator=pigen_parameter_get(&model,pigen_symbol_parameter(&model,iterators[i]));
		int64_t value=-1;
		assert(pigen_const_expr_evaluate_i64(&model,pigen_expr_constant(&model,iterator->value),&value) && value==(int64_t)i);
	}
	pigen_free_semantic_model(&model); pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&preprocessed); pigen_free_sources(&sources);
	puts("PASS: independent generated scopes, immutable iteration bindings, local endpoints and selected items");
	return 0;
}
