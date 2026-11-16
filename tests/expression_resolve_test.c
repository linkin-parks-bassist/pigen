#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/expression_analysis.h"
#include "pigen/expression_resolve.h"
#include "pigen/type_resolve.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static pigen_syntax_expr_id parse(const pigen_preprocess_result *preprocessed,
	pigen_syntax_tree *syntax, size_t first, size_t after)
{
	pigen_syntax_error error = {0};
	pigen_syntax_expr_id result = INVALID_ID(pigen_syntax_expr_id);
	assert(pigen_parse_expression(&preprocessed->expanded, first, after,
		&syntax->expressions, &syntax->types, &result, &error));
	return result;
}

static pigen_symbol_id declare_signal(pigen_semantic_model *model,
	pigen_module_id module, pigen_scope_id scope, pigen_data_type_id type,
	pigen_source_span name, pigen_syntax_id syntax)
{
	pigen_symbol_id symbol;
	pigen_symbol_id shadowed;

	assert(pigen_symbol_declare(model, scope, PIGEN_SYMBOL_SIGNAL, type, name,
		name, &symbol, &shadowed) == PIGEN_DECLARE_OK);
	assert(pigen_signal_add(model, syntax, module, symbol, type,
		pigen_semantic_scalar_shape(model), INVALID_ID(pigen_expr_id),
		PIGEN_TRANSFER_TYPE_LOGIC, PIGEN_SEMANTIC_INTERNAL, name).index !=
		PIGEN_INVALID_ID);
	return symbol;
}

static uint64_t evaluate_width(const pigen_semantic_model *model,
	pigen_const_expr_id width)
{
	const pigen_const_expr *known = pigen_const_expr_get(model, width);
	const pigen_const_expr_id *children;
	uint64_t value;
	size_t i;

	assert(known);
	if (known->kind == PIGEN_CONST_EXPR_INTEGER) return known->as.integer;
	assert(known->kind == PIGEN_CONST_EXPR_WIDTH_SUM ||
		known->kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT ||
		known->kind == PIGEN_CONST_EXPR_WIDTH_MAXIMUM);
	children = pigen_const_expr_children(model, known->as.sequence.first_child,
		known->as.sequence.child_count);
	value = known->kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT ? 1 : 0;
	for (i = 0; i < known->as.sequence.child_count; i++)
	{
		uint64_t child = evaluate_width(model, children[i]);
		if (known->kind == PIGEN_CONST_EXPR_WIDTH_SUM) value += child;
		else if (known->kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT) value *= child;
		else if (child > value) value = child;
	}
	return value;
}

static uint64_t resolved_width(pigen_semantic_model *model,
	pigen_data_type_id type)
{
	return evaluate_width(model, pigen_data_type_packed_width(model, type));
}

static void check_size_casts(pigen_source_manager *sources,
    pigen_semantic_model *model, pigen_module_id module, pigen_scope_id parent)
{
    const char *names="cast_signed cast_unsigned cast_two";
    pigen_source_id source=pigen_source_add(sources,"cast_names",names,strlen(names));
    pigen_scope_id scope=pigen_scope_add(model,parent,(pigen_source_span){source,0,strlen(names)});
    pigen_data_type_id signed_type=pigen_data_type_sized_integral(model,8,PIGEN_SIGN_SIGNED,PIGEN_DATA_TYPE_STATE_FOUR);
    pigen_data_type_id unsigned_type=pigen_data_type_sized_integral(model,8,PIGEN_SIGN_UNSIGNED,PIGEN_DATA_TYPE_STATE_FOUR);
    pigen_data_type_id two_type=pigen_data_type_sized_integral(model,8,PIGEN_SIGN_SIGNED,PIGEN_DATA_TYPE_STATE_TWO);
    declare_signal(model,module,scope,signed_type,(pigen_source_span){source,0,11},(pigen_syntax_id){0});
    declare_signal(model,module,scope,unsigned_type,(pigen_source_span){source,12,25},(pigen_syntax_id){0});
    declare_signal(model,module,scope,two_type,(pigen_source_span){source,26,34},(pigen_syntax_id){0});
    const struct {const char *text; size_t width; pigen_signedness sign; pigen_state_domain state;} cases[]={
        {"12'(cast_signed)",12,PIGEN_SIGN_SIGNED,PIGEN_DATA_TYPE_STATE_FOUR},
        {"4'(cast_signed)",4,PIGEN_SIGN_SIGNED,PIGEN_DATA_TYPE_STATE_FOUR},
        {"12'(cast_unsigned)",12,PIGEN_SIGN_UNSIGNED,PIGEN_DATA_TYPE_STATE_FOUR},
        {"12'(cast_two)",12,PIGEN_SIGN_SIGNED,PIGEN_DATA_TYPE_STATE_TWO},
        {"(4+4)'(cast_two)",8,PIGEN_SIGN_SIGNED,PIGEN_DATA_TYPE_STATE_TWO},
        {"4' /* cast comment */ (cast_unsigned)",4,PIGEN_SIGN_UNSIGNED,PIGEN_DATA_TYPE_STATE_FOUR},
        {"8'(4'(cast_signed))",8,PIGEN_SIGN_SIGNED,PIGEN_DATA_TYPE_STATE_FOUR},
        {"4'(8'bx1z0_1010)",4,PIGEN_SIGN_UNSIGNED,PIGEN_DATA_TYPE_STATE_FOUR},
        {"0'(cast_signed)",0,0,0}, {"(-1)'(cast_signed)",0,0,0},
        {"cast_unsigned'(cast_signed)",0,0,0}, {"(cast_unsigned+1)'(cast_signed)",0,0,0},
        {"1025'(cast_signed)",0,0,0}, {"(8'bx)'(cast_signed)",0,0,0}
        ,{"$clog2('x)",0,0,0}, {"$clog2(cast_unsigned ^ '1)",0,0,0}
        ,{"$signed('1,'0)",0,0,0}, {"$unsigned()",0,0,0}
        ,{"peek('1,'0)",0,0,0}, {"unknown_call('1)",0,0,0}
        ,{"$clog2('1 ^ 65'd0)",0,0,0}
    };
    for(size_t i=0;i<sizeof cases/sizeof *cases;i++)
    {
        pigen_source_id input=pigen_source_add(sources,"size_cast",cases[i].text,strlen(cases[i].text));
        pigen_preprocess_result pre={0}; pigen_preprocess_error pe={0};
        pigen_syntax_tree syntax={0}; pigen_semantic_error error={0};
        pigen_resolve_policy policy={1024};
        assert(pigen_preprocess(sources,input,NULL,&pre,&pe)); syntax.expanded=&pre.expanded;
        pigen_syntax_expr_id written=parse(&pre,&syntax,0,pre.expanded.token_count-1);
        size_t before=model->expression_count,children=model->expression_child_count;
        pigen_expr_id result=pigen_resolve_expression(&syntax,model,scope,written,&policy,&error);
        const pigen_semantic_expr *known=pigen_expr_get(model,result);
        if(cases[i].width)
        {
            if(!known) fprintf(stderr,"size cast %s: %s\n",cases[i].text,error.message ? error.message : "no diagnostic");
            uint64_t width=0;
            assert(known && pigen_const_expr_evaluate_u64(model,
                pigen_data_type_packed_width(model,known->data_type),&width) && width==cases[i].width);
            assert(pigen_data_type_signedness(model,known->data_type)==cases[i].sign);
            assert(pigen_data_type_state_domain(model,known->data_type)==cases[i].state);
        }
        else
        {
            if(known || !error.message) fprintf(stderr,"invalid size/call %s: result=%d diagnostic=%s\n",cases[i].text,known!=NULL,error.message ? error.message : "none");
            assert(!known && error.message);
            assert(model->expression_count==before && model->expression_child_count==children);
        }
        pigen_free_syntax_expr_arena(&syntax.expressions);pigen_free_syntax_type_arena(&syntax.types);pigen_free_preprocess_result(&pre);
    }
}

static void check_empty_constant_fields(pigen_source_manager *sources,pigen_semantic_model *model,pigen_scope_id scope)
{
 const struct {const char *text;uint64_t expected,width;} cases[]={
  {"{{0{1'bx}},8'ha5}",0xa5,8},
  {"{(({0{1'bx}})),8'ha5}",0xa5,8},
  {"{{0{{0{1'bx}},1'b1}},8'ha5}",0xa5,8},
  {"{4'ha,{(2-2){8'hff}},4'h5}",0xa5,8},
  {"{2{{0{8'hff}},4'ha}}",0xaa,8},
  {"{8'hc3,{0{8'hff,8'h01}}}",0xc3,8},
  {"{{0{8'hff}},{0{8'h01}},8'h5a}",0x5a,8},
  {"{{0{unknown_name}},8'h5a}",0,0},
  {"{{0{cast_unsigned}},8'h5a}",0,0},
  {"{{0{unknown_call()}},8'h5a}",0,0},
  {"{0{8'ha5}}",0,0},
  {"{{0{8'ha5}}}",0,0},
  {"{{{0{8'ha5}}},8'h33}",0,0},
  {"{{0{8'ha5}},{0{8'h33}}}",0,0},
  {"{{(-1){8'hff}},8'h33}",0,0}
 };
 for(size_t i=0;i<sizeof cases/sizeof *cases;i++) for(size_t d=0;d<3;d++)
 {
  pigen_source_id input=pigen_source_add(sources,"empty_constant",cases[i].text,strlen(cases[i].text));
  pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};
  pigen_semantic_error error={0};pigen_resolve_policy policy={1024};
  assert(pigen_preprocess(sources,input,NULL,&pre,&pe));syntax.expanded=&pre.expanded;
  pigen_syntax_expr_id written=parse(&pre,&syntax,0,pre.expanded.token_count-1);
  size_t expressions=model->expression_count,children=model->expression_child_count,constraints=model->width_constraint_count;
  pigen_semantic_expr *published=model->expressions;
  pigen_analyzed_expr_arena arena={0};pigen_analyzed_expr_id value;
  pigen_literal_domain domain=d==2 ? PIGEN_LITERAL_DOMAIN_PIGEN : d ? PIGEN_LITERAL_DOMAIN_CASE : PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG;
  int ok=pigen_analyze_expression(&syntax,model,scope,written,1,domain,&policy,&arena,&value,&error);
  if(ok!=(cases[i].width!=0)) fprintf(stderr,"empty field %s: ok=%d error=%s\n",cases[i].text,ok,error.message ? error.message : "none");
  assert(ok==(cases[i].width!=0));
  assert(model->expression_count==expressions && model->expression_child_count==children && model->width_constraint_count==constraints && model->expressions==published);
  if(ok)
  {
   uint64_t actual,width;
   assert(pigen_const_expr_evaluate_u64(model,arena.nodes[value.index].constant,&actual) && actual==cases[i].expected);
   assert(pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,arena.nodes[value.index].data_type),&width) && width==cases[i].width);
   pigen_expr_id materialized=pigen_resolve_constant_expression(&syntax,model,scope,written,domain,&policy,&error);
   assert(materialized.index!=PIGEN_INVALID_ID && pigen_const_expr_evaluate_u64(model,pigen_expr_constant(model,materialized),&actual) && actual==cases[i].expected);
  }
  else assert(error.message);
  pigen_free_analyzed_expr_arena(&arena);pigen_free_syntax_expr_arena(&syntax.expressions);
  pigen_free_syntax_type_arena(&syntax.types);pigen_free_preprocess_result(&pre);
 }
}

static void check_runtime_empty_analysis(pigen_source_manager *sources,pigen_semantic_model *model,pigen_module_id module,pigen_scope_id parent)
{
 const char *names="empty_data empty_ignored";
 pigen_source_id declared=pigen_source_add(sources,"empty_names",names,strlen(names));
 pigen_scope_id scope=pigen_scope_add(model,parent,(pigen_source_span){declared,0,strlen(names)});
 pigen_data_type_id type=pigen_data_type_sized_integral(model,8,PIGEN_SIGN_UNSIGNED,PIGEN_DATA_TYPE_STATE_FOUR);
 declare_signal(model,module,scope,type,(pigen_source_span){declared,0,10},(pigen_syntax_id){0});
 declare_signal(model,module,scope,type,(pigen_source_span){declared,11,24},(pigen_syntax_id){0});
 const struct {const char *text;uint64_t width;size_t evaluations;} cases[]={
  {"{empty_data,empty_ignored}",16,0},
  {"{{0{empty_ignored}},empty_data}",8,1},
  {"{empty_data,{0{empty_ignored}}}",8,1},
  {"{empty_data[7:4],{0{empty_ignored}},empty_data[3:0]}",8,1},
  {"{2{{0{empty_ignored}},empty_data}}",16,1},
  {"{({0{empty_ignored}}),empty_data,{0{empty_data}}}",8,2},
  {"({{0{empty_ignored}},empty_data})+8'd1",8,1},
  {"uint[8]'({{0{empty_ignored}},empty_data})",8,1},
  {"{{0{empty_ignored}},8'h5a}",8,1},
  {"{0{empty_ignored}}",0,0},
  {"{{0{empty_ignored}}}",0,0},
  {"{{{0{empty_ignored}}},empty_data}",0,0},
  {"{{0{missing}},empty_data}",0,0},
  {"{{empty_ignored{1'b0}},empty_data}",0,0},
  {"{{(-1){empty_ignored}},empty_data}",0,0}
 };
 for(size_t i=0;i<sizeof cases/sizeof *cases;i++) for(size_t d=0;d<2;d++)
 {
  const char *text=cases[i].text;pigen_source_id input=pigen_source_add(sources,"runtime_empty_analysis",text,strlen(text));
  pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_semantic_error error={0};pigen_resolve_policy policy={1024};
  assert(pigen_preprocess(sources,input,NULL,&pre,&pe));syntax.expanded=&pre.expanded;
  pigen_syntax_expr_id written=parse(&pre,&syntax,0,pre.expanded.token_count-1);
  size_t expressions=model->expression_count,children=model->expression_child_count,constraints=model->width_constraint_count;
  pigen_semantic_expr *published=model->expressions;
  pigen_analyzed_expr_arena arena={0};pigen_analyzed_expr_id analyzed;
  int ok=pigen_analyze_expression(&syntax,model,scope,written,0,d ? PIGEN_LITERAL_DOMAIN_CASE : PIGEN_LITERAL_DOMAIN_PIGEN,cases[i].width && !cases[i].evaluations ? NULL : &policy,&arena,&analyzed,&error);
  if(ok!=(cases[i].width!=0)) fprintf(stderr,"runtime empty analysis %s ok=%d error=%s\n",text,ok,error.message ? error.message : "none");
  assert(ok==(cases[i].width!=0));
  assert(model->expression_count==expressions && model->expression_child_count==children && model->width_constraint_count==constraints && model->expressions==published);
  if(ok)
  {
   uint64_t width;size_t evaluations=0;
   /* Pigen numerical addition retains its carry; CASE addition keeps SV width. */
   uint64_t expected_width=cases[i].width+(!d && strstr(text,"+8'd1") ? 1 : 0);
   int width_ok=pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,arena.nodes[analyzed.index].data_type),&width);
   if(!width_ok || width!=expected_width) fprintf(stderr,"runtime empty width %s domain=%zu got=%llu expected=%llu\n",text,d,(unsigned long long)width,(unsigned long long)expected_width);
   assert(width_ok && width==expected_width);
   assert(arena.nodes[analyzed.index].constant.index==PIGEN_INVALID_ID);
   for(size_t n=0;n<arena.node_count;n++) if(arena.nodes[n].form==PIGEN_ANALYZED_EVALUATED_VALUE)
   {
    const pigen_analyzed_expr *v=&arena.nodes[n];evaluations++;
    assert(v->constant.index==PIGEN_INVALID_ID && v->data_type.index==arena.nodes[v->as.evaluation.value.index].data_type.index);
    assert(v->as.evaluation.evaluated.index<arena.node_count && (v->as.evaluation.before==0 || v->as.evaluation.before==1));
   }
   assert(evaluations==cases[i].evaluations);
   pigen_expr_id materialized=(d ? pigen_resolve_case_expression : pigen_resolve_expression)(&syntax,model,scope,written,&policy,&error);
   if(materialized.index==PIGEN_INVALID_ID) fprintf(stderr,"runtime empty materialization %s: %s\n",text,error.message ? error.message : "none");
   assert(materialized.index!=PIGEN_INVALID_ID && pigen_expr_get(model,materialized)->data_type.index==arena.nodes[analyzed.index].data_type.index);
   assert(pigen_expr_constant(model,materialized).index==PIGEN_INVALID_ID);
  }
  else assert(error.message);
  pigen_free_analyzed_expr_arena(&arena);pigen_free_syntax_expr_arena(&syntax.expressions);
  pigen_free_syntax_type_arena(&syntax.types);pigen_free_preprocess_result(&pre);
 }
}

static void check_assignment_fills(pigen_source_manager *sources,pigen_semantic_model *model,pigen_scope_id scope)
{
	const char *texts[]={"'0","('1)","'x","'Z"};
	for(size_t t=0;t<4;t++) for(size_t two=0;two<2;two++)
	{
		pigen_source_id input=pigen_source_add(sources,"assignment_fill",texts[t],strlen(texts[t]));
		pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};
		pigen_semantic_error error={0};pigen_resolve_policy policy={1024};
		assert(pigen_preprocess(sources,input,NULL,&pre,&pe));syntax.expanded=&pre.expanded;
		pigen_syntax_expr_id written=parse(&pre,&syntax,0,pre.expanded.token_count-1);
		pigen_data_type_id target=pigen_data_type_sized_integral(model,129,PIGEN_SIGN_SIGNED,
			two ? PIGEN_DATA_TYPE_STATE_TWO : PIGEN_DATA_TYPE_STATE_FOUR);
		pigen_expr_id result=pigen_resolve_assignment_value(&syntax,model,scope,written,target,&policy,&error);
		const pigen_semantic_expr *known=pigen_expr_get(model,result);
		assert(known && known->kind==PIGEN_EXPR_BITS && known->data_type.index==target.index && known->as.bits.state_count==129);
		pigen_bit_state expected=t==0 ? PIGEN_BIT_ZERO : t==1 ? PIGEN_BIT_ONE : two ? PIGEN_BIT_ZERO : t==2 ? PIGEN_BIT_X : PIGEN_BIT_Z;
		for(size_t i=0;i<129;i++) assert(model->literal_states[known->as.bits.first_state+i]==expected);
		size_t before=model->expression_count,children=model->expression_child_count;
		policy.maximum_generated_bits=128;
		assert(pigen_resolve_assignment_value(&syntax,model,scope,written,target,&policy,&error).index==PIGEN_INVALID_ID);
		assert(model->expression_count==before && model->expression_child_count==children);
		pigen_free_syntax_expr_arena(&syntax.expressions);pigen_free_syntax_type_arena(&syntax.types);pigen_free_preprocess_result(&pre);
	}
}

static void check_cast_fills(pigen_source_manager *sources,pigen_semantic_model *model,pigen_scope_id scope)
{
	const struct {const char *cast,*expected;size_t width;} cases[]={
		{"8'('1)","8'hff",8},{"8'(('0))","8'h00",8},
		{"8'('x)","8'hxx",8},{"8'('Z)","8'hzz",8},
		{"8'(4'('1))","8'h0f",8},{"4'(8'('1))","4'hf",4},
		{"uint[32]'('1)","32'hffffffff",32},{"int[17]'(('1))","17'h1ffff",17},
		{"bit[129]'('x)","129'h0",129},{"logic[129]'('z)","129'hz",129},
		{"8'(1'b1 ? '1 : '0)","8'hff",8},{"8'(1'b0 ? '1 : '0)","8'h00",8},
		{"8'(1'bx ? '1 : '0)","8'hxx",8},
		{"bit[16]'(1'bx ? '1 : '0)","16'h0",16},
		{"uint[16]'(1'bx ? '1 : '0)","16'h0",16}
		,{"int[16]'(1'b0 ? '1 : 8'sh80)","16'h0080",16}
		,{"uint[8]'('1 ^ 8'ha5)","8'h5a",8}
		,{"8'(~'0)","8'hff",8}
		,{"bit[16]'('x | 16'h0080)","16'h0080",16}
		,{"uint[8]'((8'ha5 ^ '1)[7:4])","8'h05",8}
		,{"uint[8]'(8'ha5['1])","8'h00",8}
		,{"8'ha5[('1 & 32'd3):0]","4'h5",4}
		,{"uint[8]'(('1 & 8'h40) ? 8'h55 : 8'haa)","8'h55",8}
		,{"uint[8]'((logic[0:7]'(8'ha5))[0:3])","8'h0a",8}
		,{"uint[8]'((logic[0:7]'(8'ha5))[2+:3])","8'h04",8}
		,{"uint[8]'((logic[0:7]'(8'ha5))[4-:3])","8'h04",8}
		,{"uint[8]'(8'ha5[6+:2])","8'h02",8}
		,{"uint[8]'(8'ha5[6-:2])","8'h01",8}
		,{"uint[8]'((logic[-2:-9]'(8'ha5))[-2:-5])","8'h0a",8}
		,{"uint[8]'((logic[2][4]'(8'ha5))[1])","8'h0a",8}
		,{"logic[4]'(8'hxz[7:4])","4'hx",4}
		,{"logic[4]'(8'hxz[3:0])","4'hz",4}
		,{"logic[4]'(8'ha5[9:6])","4'bxx10",4}
		,{"logic[4]'((logic[2][4]'(8'ha5))[1'bx])","4'hx",4}
		,{"logic[2]'((logic[64'sh8000000000000001:64'sh8000000000000000]'(2'b10))[64'sh8000000000000000-:2])","2'b0x",2}
		,{"logic[2]'((logic[64'sh7ffffffffffffffe:64'sh7fffffffffffffff]'(2'b10))[64'sh7fffffffffffffff+:2])","2'b0x",2}
		,{"16'($signed('1))","16'hffff",16}
		,{"16'($unsigned('1))","16'h0001",16}
		,{"16'($signed('1 ^ 8'h7f))","16'hff80",16}
		,{"16'($unsigned('1 ^ 8'h7f))","16'h0080",16}
		,{"uint[8]'($clog2('1 ^ 8'h00))","8'h08",8}
		,{"uint[8]'($clog2('1))","8'h00",8}
		,{"$unsigned(bit[8]'('1))","8'hff",8}
		,{"$signed(bit[8]'('1))","8'hff",8}
		,{"uint[16]'(peek('1))","16'h0001",16}
		,{"uint[8]'(peek('1 ^ 8'ha5))","8'h5a",8}
		,{"{3{2'b10}}","6'b101010",6}
		,{"{2{1'b1,2'b01}}","6'b101101",6}
		,{"{2{{2{'1}},1'b0}}","6'b110110",6}
		,{"16'($signed({3{'1}}))","16'hffff",16}
		,{"16'($unsigned({3{'1}}))","16'h0007",16}
		,{"{(1+2){('1 ^ 2'b01)}}","6'b101010",6}
		,{"{2{1'bx ? '1 : '0}}","2'bxx",2}
		,{"({2{4'ha}})[5:2]","4'ha",4}
		,{"uint[64]'(2147483647+1)","64'h0000000080000000",64}
	};
	for(size_t i=0;i<sizeof cases/sizeof *cases;i++)
	{
		pigen_const_expr_id values[2];
		for(size_t side=0;side<2;side++)
		{
			const char *text=side ? cases[i].expected : cases[i].cast;
			pigen_source_id input=pigen_source_add(sources,"cast_fill",text,strlen(text));
			pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};
			pigen_semantic_error error={0};pigen_resolve_policy policy={1024};
			assert(pigen_preprocess(sources,input,NULL,&pre,&pe));syntax.expanded=&pre.expanded;
			pigen_syntax_expr_id written=parse(&pre,&syntax,0,pre.expanded.token_count-1);
			pigen_expr_id result=pigen_resolve_expression(&syntax,model,scope,written,&policy,&error);
			if(result.index==PIGEN_INVALID_ID) fprintf(stderr,"cast fill %s: %s\n",text,error.message ? error.message : "missing result");
			assert(result.index!=PIGEN_INVALID_ID);
			if(!side && (!strncmp(text,"$unsigned(bit[",14) || !strncmp(text,"$signed(bit[",12)))
				assert(pigen_data_type_state_domain(model,pigen_expr_get(model,result)->data_type)==PIGEN_DATA_TYPE_STATE_TWO);
			uint64_t width=0;
			assert(pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,pigen_expr_get(model,result)->data_type),&width) && width==cases[i].width);
			values[side]=pigen_expr_constant(model,result);
			/* Structural constants must use the same value without publishing
			 * or relocating the semantic expression arena. */
			if(!side && !strstr(text,"peek("))
			{
				size_t expressions=model->expression_count,children=model->expression_child_count,constraints=model->width_constraint_count;
				pigen_semantic_expr *published=model->expressions;
				pigen_analyzed_expr_arena arena={0};pigen_analyzed_expr_id analyzed;
				int analyzed_ok=pigen_analyze_expression(&syntax,model,scope,written,1,PIGEN_LITERAL_DOMAIN_CASE,&policy,&arena,&analyzed,&error);
				if(!analyzed_ok) fprintf(stderr,"pure constant failed %s: %s\n",text,error.message ? error.message : "none");
				assert(analyzed_ok);
				int same=0;
				if(!pigen_const_expr_case_equal(model,arena.nodes[analyzed.index].constant,values[side],cases[i].width,0,&same) || !same)
					fprintf(stderr,"analyzed constant mismatch %s\n",text);
				assert(same && model->expression_count==expressions && model->expression_child_count==children && model->width_constraint_count==constraints && model->expressions==published);
				pigen_free_analyzed_expr_arena(&arena);
				pigen_expr_id constant=pigen_resolve_constant_expression(&syntax,model,scope,written,PIGEN_LITERAL_DOMAIN_CASE,&policy,&error);
				if(constant.index==PIGEN_INVALID_ID) fprintf(stderr,"constant materialization failed %s: %s\n",text,error.message ? error.message : "none");
				assert(constant.index!=PIGEN_INVALID_ID);
				same=0;
				int matched=pigen_const_expr_case_equal(model,pigen_expr_constant(model,constant),values[side],cases[i].width,0,&same);
				if(!matched || !same) fprintf(stderr,"materialized constant mismatch %s\n",text);
				assert(matched && same);
			}
			pigen_free_syntax_expr_arena(&syntax.expressions);pigen_free_syntax_type_arena(&syntax.types);pigen_free_preprocess_result(&pre);
		}
		int equal=0;
		int compared=pigen_const_expr_case_equal(model,values[0],values[1],cases[i].width,0,&equal);
		if(!compared || !equal) fprintf(stderr,"cast constant mismatch %s expected %s compared=%d equal=%d\n",cases[i].cast,cases[i].expected,compared,equal);
		assert(compared && equal);
		const pigen_const_expr *selected=pigen_const_expr_get(model,values[0]);
		while(selected && selected->kind==PIGEN_CONST_EXPR_CONVERSION) selected=pigen_const_expr_get(model,selected->as.conversion.operand);
		uint64_t expected_value,actual_value;
		if(selected && (selected->kind==PIGEN_CONST_EXPR_INDEX || selected->kind==PIGEN_CONST_EXPR_SELECT) &&
			pigen_const_expr_evaluate_u64(model,values[1],&expected_value))
			assert(pigen_const_expr_evaluate_u64(model,values[0],&actual_value) && actual_value==expected_value);
	}
}

static void check_bits(pigen_source_manager *sources,pigen_semantic_model *model,
 pigen_module_id module,pigen_scope_id parent)
{
 const char *name="bits_data";
 pigen_source_id names=pigen_source_add(sources,"bits_names",name,strlen(name));
 pigen_scope_id scope=pigen_scope_add(model,parent,(pigen_source_span){names,0,strlen(name)});
 pigen_data_type_id type=pigen_data_type_sized_integral(model,13,PIGEN_SIGN_UNSIGNED,PIGEN_DATA_TYPE_STATE_FOUR);
 declare_signal(model,module,scope,type,(pigen_source_span){names,0,strlen(name)},(pigen_syntax_id){0});
 pigen_symbol_id alias,shadowed;
 const char *alias_name="bits_alias";
 pigen_source_id alias_source=pigen_source_add(sources,"bits_alias_name",alias_name,strlen(alias_name));
 assert(pigen_symbol_declare(model,scope,PIGEN_SYMBOL_TYPEDEF,type,
  (pigen_source_span){alias_source,0,strlen(alias_name)},(pigen_source_span){alias_source,0,strlen(alias_name)},&alias,&shadowed)==PIGEN_DECLARE_OK);
 const char *array_name="bits_array";
 pigen_source_id array_source=pigen_source_add(sources,"bits_array_name",array_name,strlen(array_name));
 pigen_symbol_id array=declare_signal(model,module,scope,type,(pigen_source_span){array_source,0,strlen(array_name)},(pigen_syntax_id){0});
 pigen_data_type_id integer=pigen_data_type_unsized_integer(model);
 pigen_shape_dimension dimensions[2]={
  {.form=PIGEN_SHAPE_DIMENSION_COUNT,.as.count=pigen_const_expr_intern_integer(model,3,integer)},
  {.form=PIGEN_SHAPE_DIMENSION_RANGE,.as.range={pigen_const_expr_intern_integer(model,5,integer),pigen_const_expr_intern_integer(model,2,integer)}}};
 model->signals[model->symbols[array.index].object.signal.index].shape=pigen_shape_intern(model,dimensions,2);
 const struct {const char *text;uint64_t value;int valid;} cases[]={
  {"$bits(peek(bits_data))",13,1},{"$bits(peek(bits_data)+1)",32,1},
  {"$bits(valid(bits_data))",1,1},{"$bits(ready(bits_data))",1,1},
  {"$bits(accepts(bits_data,bits_data))",1,1},{"$size(peek(bits_array),2)",4,1},
  {"$bits(peek('1))",1,1},{"$size(bits_array,peek(2))",4,1},
  {"$bits(valid(1))",0,0},{"$bits(ready(bits_data+1))",0,0},
  {"$bits(peek())",0,0},{"$bits(peek(bits_data,1))",0,0},
  {"$bits(accepts(bits_data+1,bits_data))",0,0},
  {"$bits(accepts(bits_data,bits_data+1))",0,0},
  {"$bits(accepts(bits_data,(bits_data)))",0,0},

  {"$dimensions(bits_array)",3,1},{"$unpacked_dimensions(bits_array)",2,1},
  {"$size(bits_array)",3,1},{"$left(bits_array)",0,1},{"$right(bits_array)",2,1},
  {"$increment(bits_array)",UINT64_MAX,1},{"$increment(bits_array,2)",1,1},
  {"$size(bits_array,2)",4,1},{"$left(bits_array,2)",5,1},{"$right(bits_array,2)",2,1},
  {"$low(bits_array,2)",2,1},{"$high(bits_array,2)",5,1},{"$size(bits_array,3)",13,1},
  {"$dimensions(bits_array[bits_data])",2,1},{"$unpacked_dimensions(bits_array[bits_data])",1,1},
  {"$size(bits_array[bits_data],2)",13,1},{"$dimensions(bits_data[bits_data])",0,1},
  {"$dimensions(logic)",0,1},{"$dimensions(integer)",1,1},{"$dimensions(int)",1,1},
  {"$left(int)",31,1},{"$right(int)",0,1},{"$size(int[17])",17,1},
  {"$dimensions(logic[3:0][2:1])",2,1},{"$left(logic[3:0][2:1],2)",2,1},
  {"$right(logic[3:0][2:1],2)",1,1},{"$size(logic[3:0][2:1],2)",2,1},
  {"$dimensions(bits_alias[2])",2,1},{"$left(bits_alias[2],2)",12,1},
  {"$size(bits_alias[2],1)",2,1},{"$unpacked_dimensions(bits_alias[2])",0,1},
  {"$dimensions(struct packed {logic a;})",1,1},{"$size(struct packed {logic a;})",1,1},
  {"$size(logic)",UINT64_C(1)<<63,1},{"$size(bits_array,0)",UINT64_C(1)<<63,1},
  {"$size(bits_array,4)",UINT64_C(1)<<63,1},{"$size(bits_array,-1)",UINT64_C(1)<<63,1},
  {"$size(bits_array,'x)",UINT64_C(1)<<63,1},{"$size(bits_array,bits_data)",0,0},
  {"$dimensions(bits_array,1)",0,0},{"$unpacked_dimensions(logic,1)",0,0},
  {"$size()",0,0},{"$size(bits_array,1,2)",0,0},{"$size(missing_name)",0,0},
  {"$bits(logic,1)",0,0},

  {"$bits(bits_array)",156,1},{"$bits(bits_array[bits_data])",52,1},
  {"$bits(bits_data[bits_data])",1,1},{"$bits(bits_alias)",13,1},{"$bits(bits_alias[2])",26,1},
  {"$bits(bits_alias signed)",13,1},{"$bits(bits_data[2])",1,1},
  {"$bits(bits_data)",13,1},{"$bits(bits_data + 1)",32,1},
  {"$bits(1)",32,1},{"$bits(8'hxz)",8,1},{"$bits('1)",1,1},
  {"$bits(logic[7:0])",8,1},{"$bits(int)",32,1},{"$bits(int[17])",17,1},
  {"$bits(struct packed {logic[3:0] a; logic[8:0] b;})",13,1},
  {"$bits({bits_data,8'd0})",21,1},{"$bits($bits(bits_data))",32,1},
  {"$bits(bits_data[4:2])",3,1},{"$bits(missing_name)",0,0},
  {"$bits()",0,0},{"$bits(bits_data,1)",0,0}
 };
 const char *malformed[]={"$bits(struct packed {logic a;}) +","$size(logic,)","$bits(logic,)","$dimensions(logic,)"};
 for(size_t bad_index=0;bad_index<sizeof malformed/sizeof *malformed;bad_index++)
 {
  const char *bad=malformed[bad_index];
  pigen_source_id input=pigen_source_add(sources,"bad_bits_query",bad,strlen(bad));
  pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};
  pigen_syntax_error error={0};pigen_syntax_expr_id written;
  assert(pigen_preprocess(sources,input,NULL,&pre,&pe));syntax.expanded=&pre.expanded;
  assert(!pigen_parse_expression(&pre.expanded,0,pre.expanded.token_count-1,&syntax.expressions,&syntax.types,&written,&error));
  assert(!syntax.expressions.node_count && !syntax.expressions.child_count);
  assert(!syntax.types.node_count && !syntax.types.argument_count && !syntax.types.member_count);
  pigen_free_syntax_expr_arena(&syntax.expressions);pigen_free_syntax_type_arena(&syntax.types);pigen_free_preprocess_result(&pre);
 }
 for(size_t i=0;i<sizeof cases/sizeof *cases;i++)
 {
  pigen_source_id input=pigen_source_add(sources,"bits_query",cases[i].text,strlen(cases[i].text));
  pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};
  pigen_semantic_error error={0};pigen_resolve_policy policy={1024};
  assert(pigen_preprocess(sources,input,NULL,&pre,&pe));syntax.expanded=&pre.expanded;
  pigen_syntax_expr_id written=parse(&pre,&syntax,0,pre.expanded.token_count-1);
  size_t before=model->expression_count,children=model->expression_child_count,constraints=model->width_constraint_count;
  const pigen_semantic_expr *pointer=model->expressions;
  pigen_analyzed_expr_arena arena={0};pigen_analyzed_expr_id id;
  int ok=pigen_analyze_expression(&syntax,model,scope,written,1,PIGEN_LITERAL_DOMAIN_PIGEN,&policy,&arena,&id,&error);
  if(ok!=cases[i].valid) fprintf(stderr,"bits mismatch %s: %s\n",cases[i].text,error.message ? error.message : "no diagnostic");
  assert(ok==cases[i].valid);
  assert(before==model->expression_count && children==model->expression_child_count && constraints==model->width_constraint_count && pointer==model->expressions);
  if(ok)
  {
   const pigen_analyzed_expr *known=pigen_analyzed_expr_get(&arena,id);int64_t value;
   int evaluated=pigen_const_expr_evaluate_i64(model,known->constant,&value);
   if(cases[i].value==(UINT64_C(1)<<63)) assert(!evaluated);
   else {
    if(!evaluated || value!=(int64_t)(int32_t)(uint32_t)cases[i].value)
     fprintf(stderr,"inquiry value %s: evaluated=%d value=%lld expected=%lld\n",cases[i].text,evaluated,(long long)value,(long long)(int64_t)(int32_t)(uint32_t)cases[i].value);
    assert(evaluated && value==(int64_t)(int32_t)(uint32_t)cases[i].value);
   }
   assert(resolved_width(model,known->data_type)==32);
   assert(pigen_data_type_signedness(model,known->data_type)==PIGEN_SIGN_SIGNED);
   pigen_expr_id resolved=pigen_resolve_expression(&syntax,model,scope,written,&policy,&error);
   assert(resolved.index!=PIGEN_INVALID_ID);
   assert(model->expression_count==before+1); /* Operand is never materialized. */
  }
  pigen_free_analyzed_expr_arena(&arena);pigen_free_syntax_expr_arena(&syntax.expressions);
  pigen_free_syntax_type_arena(&syntax.types);pigen_free_preprocess_result(&pre);
 }
 const char *runtime[]={"$size(bits_array,bits_data)","$size(bits_array,('1 & bits_data))","$left(bits_data[0],bits_data)","$size(bits_array,peek(bits_data))","$size(bits_array,valid(bits_data))","$size(bits_array,accepts(bits_data,bits_data))","$size(bits_array,bits_array)"};
 for(size_t i=0;i<sizeof runtime/sizeof *runtime;i++)
 {
  pigen_source_id input=pigen_source_add(sources,"runtime_inquiry",runtime[i],strlen(runtime[i]));
  pigen_preprocess_result pre={0};pigen_preprocess_error pe={0};pigen_syntax_tree syntax={0};pigen_semantic_error error={0};
  pigen_resolve_policy policy={1024};assert(pigen_preprocess(sources,input,NULL,&pre,&pe));syntax.expanded=&pre.expanded;
  pigen_syntax_expr_id written=parse(&pre,&syntax,0,pre.expanded.token_count-1);
  size_t before=model->expression_count,children=model->expression_child_count,constraints=model->width_constraint_count;
  const pigen_semantic_expr *pointer=model->expressions;
  pigen_analyzed_expr_arena arena={0};pigen_analyzed_expr_id id;
  int ok=pigen_analyze_expression(&syntax,model,scope,written,0,PIGEN_LITERAL_DOMAIN_PIGEN,&policy,&arena,&id,&error);
  assert(ok==(i<6));
  assert(before==model->expression_count && children==model->expression_child_count && constraints==model->width_constraint_count && pointer==model->expressions);
  if(ok)
  {
   const pigen_analyzed_expr *known=pigen_analyzed_expr_get(&arena,id);
   assert(known->constant.index==PIGEN_INVALID_ID && resolved_width(model,known->data_type)==32);
   size_t boundaries=0;for(size_t n=0;n<arena.node_count;n++) boundaries+=arena.nodes[n].form==PIGEN_ANALYZED_CASE_VALUE;
   assert(boundaries==(i==3 ? 2u : 1u));
   assert(pigen_resolve_expression(&syntax,model,scope,written,&policy,&error).index!=PIGEN_INVALID_ID);
  }
  pigen_free_analyzed_expr_arena(&arena);pigen_free_syntax_expr_arena(&syntax.expressions);
  pigen_free_syntax_type_arena(&syntax.types);pigen_free_preprocess_result(&pre);
 }
}

static void check_anonymous_records(pigen_source_manager *sources,
	pigen_semantic_model *model, pigen_scope_id scope)
{
	const char *text = "struct packed {uint[16] hi;struct packed {uint[8] tag;int[8] value;} low;}";
	pigen_data_type_id previous = INVALID_ID(pigen_data_type_id);
	for (size_t iteration = 0; iteration < 2; iteration++)
	{
		pigen_source_id source = pigen_source_add(sources, "anonymous_record", text, strlen(text));
		pigen_preprocess_result pre = {0}; pigen_preprocess_error pe = {0};
		pigen_syntax_tree syntax = {0}; pigen_syntax_error se = {0};
		pigen_semantic_error error = {0}; pigen_syntax_type_id written; size_t after;
		assert(pigen_preprocess(sources, source, NULL, &pre, &pe));
		syntax.expanded = &pre.expanded;
		assert(pigen_parse_type_prefix(&pre.expanded, 0, pre.expanded.token_count-1,
			&syntax.expressions, &syntax.types, &written, &after, &se));
		assert(after == pre.expanded.token_count-1);
		pigen_data_type_id type = pigen_resolve_type(&syntax, model, scope, written, &error);
		uint64_t bits;
		assert(type.index != PIGEN_INVALID_ID && !error.message);
		assert(pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,type),&bits) && bits == 32);
		size_t symbols = model->symbol_count, scopes = model->scope_count, members = model->struct_member_count;
		assert(pigen_resolve_type(&syntax, model, scope, written, &error).index == type.index);
		assert(model->symbol_count == symbols && model->scope_count == scopes && model->struct_member_count == members);
		pigen_source_span low = {source, (size_t)(strstr(text,"low;")-text), (size_t)(strstr(text,"low;")-text)+3};
		pigen_source_span value = {source, (size_t)(strstr(text,"value;")-text), (size_t)(strstr(text,"value;")-text)+5};
		const pigen_semantic_struct_member *member = pigen_struct_member_for_type(model, type, low);
		assert(member && member->offset == 0);
		assert(pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,member->data_type),&bits) && bits == 16);
		member = pigen_struct_member_for_type(model, member->data_type, value);
		assert(member && member->offset == 0);
		assert(pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,member->data_type),&bits) && bits == 8);
		assert(pigen_data_type_signedness(model, member->data_type) == PIGEN_SIGN_SIGNED);
		pigen_data_type_id integer=pigen_data_type_unsized_integer(model);
		pigen_packed_dimension dimension={pigen_const_expr_intern_integer(model,1,integer),
			pigen_const_expr_intern_integer(model,0,integer)};
		pigen_data_type_id array=pigen_data_type_alias(model,pigen_data_type_alias_symbol(model,type),
			pigen_data_type_alias_target(model,type),PIGEN_SIGN_UNSIGNED,&dimension,1);
		assert(!pigen_struct_member_for_type(model,array,low));
		assert(pigen_struct_member_for_type(model,pigen_data_type_packed_element(model,array),low));
		pigen_source_span keyword = {source,0,6};
		assert(pigen_symbol_lookup(model,scope,keyword).index == PIGEN_INVALID_ID);
		if (previous.index != PIGEN_INVALID_ID)
			assert(pigen_data_type_matches(model,previous,type) == 0);
		previous = type;
		pigen_free_syntax_expr_arena(&syntax.expressions);
		pigen_free_syntax_type_arena(&syntax.types);
		pigen_free_preprocess_result(&pre);
	}
}

int main(void)
{
	const char text[] =
		"width left\n"
		"left + width\n"
		"left == width\n"
		"width + width\n"
		"8'hA5[width]\n"
		"left[width]\n"
		"8'hA5[6:2]\n"
		"8'hA5[width +: 3]\n"
		"left[width -: 3]\n"
		"left[left:3]\n"
		"left[width +: left]\n"
		"{8'hA5, 8'h5A}\n"
		"{left, width}\n"
		"{width, left}\n"
		"word_t aliased\n"
		"aliased + aliased\n"
		"aliased == aliased\n"
		"(sa + sb) * sc\n"
		"ua + 1\n"
		"sa + ua\n"
		"uint[8]'(sa)\n"
		"bit[8]'(ua)\n"
		"8'hff + ua\n"
		"uint[8]'(ua)\n"
		"uint[8]'(bp)\n"
		"bit[8]'(8'hff)\n"
		"ua << sh\n"
		"ua << ds\n"
		"ua << 4294967296\n"
		"ua << 18446744073709551615\n"
		"ua << 18446744073709551616\n"
		"ds ** 18446744073709551616\n"
		"unknown_t[8]'(8'hff)\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "expressions.pigen",
		text, strlen(text));
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_tree syntax = {0};
	pigen_semantic_model model;
	pigen_scope_id scope;
	pigen_module_id module;
	pigen_data_type_id unsized_integer_data_type;
	pigen_data_type_id boolean_type;
	pigen_data_type_id aliased_type;
	pigen_binary_resolution binary_resolution;
	pigen_symbol_id module_symbol;
	pigen_symbol_id width;
	pigen_symbol_id left;
	pigen_symbol_id word_type_symbol;
	pigen_symbol_id aliased;
	pigen_symbol_id signed_a;
	pigen_symbol_id signed_b;
	pigen_symbol_id signed_c;
	pigen_symbol_id unsigned_a;
	pigen_symbol_id vector_parameter;
	pigen_symbol_id shift_amount;
	pigen_symbol_id symbolic_shift_amount;
	pigen_signal_id left_signal;
	pigen_signal_id aliased_signal;
	pigen_symbol_id shadowed;
	pigen_syntax_expr_id runtime_syntax;
	pigen_syntax_expr_id comparison_syntax;
	pigen_syntax_expr_id constant_syntax;
	pigen_syntax_expr_id constant_index_syntax;
	pigen_syntax_expr_id runtime_index_syntax;
	pigen_syntax_expr_id constant_range_syntax;
	pigen_syntax_expr_id constant_indexed_syntax;
	pigen_syntax_expr_id runtime_select_syntax;
	pigen_syntax_expr_id invalid_range_syntax;
	pigen_syntax_expr_id invalid_width_syntax;
	pigen_syntax_expr_id constant_concat_syntax;
	pigen_syntax_expr_id runtime_concat_syntax;
	pigen_syntax_expr_id swapped_concat_syntax;
	pigen_syntax_expr_id aliased_add_syntax;
	pigen_syntax_expr_id aliased_compare_syntax;
	pigen_syntax_expr_id chained_syntax;
	pigen_syntax_expr_id literal_add_syntax;
	pigen_syntax_expr_id mixed_add_syntax;
	pigen_syntax_expr_id integer_cast_syntax;
	pigen_syntax_expr_id vector_cast_syntax;
	pigen_syntax_expr_id invalid_mixed_syntax;
	pigen_syntax_expr_id identity_cast_syntax;
	pigen_syntax_expr_id constant_cast_syntax;
	pigen_syntax_expr_id vector_resize_cast_syntax;
	pigen_syntax_expr_id invalid_cast_syntax;
	pigen_syntax_expr_id oversized_shift_syntax;
	pigen_syntax_expr_id symbolic_shift_syntax;
	pigen_syntax_expr_id symbolic_power_syntax;
	pigen_syntax_expr_id enormous_shift_syntax[3];
	pigen_expr_id runtime;
	pigen_expr_id comparison;
	pigen_expr_id constant;
	pigen_expr_id constant_index;
	pigen_expr_id runtime_index;
	pigen_expr_id constant_range;
	pigen_expr_id constant_indexed;
	pigen_expr_id runtime_select;
	pigen_expr_id constant_concat;
	pigen_expr_id runtime_concat;
	pigen_expr_id swapped_concat;
	pigen_expr_id aliased_add;
	pigen_expr_id aliased_compare;
	pigen_expr_id intrinsic;
	pigen_expr_id literal_add;
	pigen_expr_id mixed_add;
	pigen_expr_id integer_cast;
	pigen_expr_id vector_cast;
	pigen_expr_id identity_cast;
	pigen_expr_id runtime_constant_cast;
	pigen_expr_id constant_cast;
	pigen_data_type_id signed_8;
	pigen_data_type_id unsigned_8;
	pigen_const_expr_id width_8;
	pigen_const_expr_id width_16;
	pigen_const_expr_id width_24;
	pigen_const_expr_id width_11;
	pigen_const_expr_id symbolic_width;
	pigen_data_type_id signed_16;
	pigen_data_type_id signed_24;
	pigen_data_type_id unsigned_16;
	pigen_data_type_id unsigned_11;
	pigen_data_type_id symbolic_unsigned;
	pigen_resolve_policy policy = {.maximum_generated_bits = 1024};
	pigen_semantic_error semantic_error = {0};
	const pigen_semantic_expr *known;
	const pigen_semantic_expr *left_read;
	const pigen_semantic_expr *width_read;

	assert(pigen_preprocess(&sources, source, NULL, &preprocessed,
		&preprocess_error));
	syntax.expanded = &preprocessed.expanded;
	pigen_semantic_init(&model, &sources);
	model.compilation_scope = pigen_scope_add(&model,
		INVALID_ID(pigen_scope_id),
		(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0});
	assert(pigen_symbol_declare(&model, model.compilation_scope,
		PIGEN_SYMBOL_MODULE, INVALID_ID(pigen_data_type_id),
		(pigen_source_span){source, 0, 5},
		(pigen_source_span){source, 0, strlen(text)}, &module_symbol, NULL) ==
		PIGEN_DECLARE_OK);
	scope = pigen_scope_add(&model, model.compilation_scope,
		(pigen_source_span){source, 0, strlen(text)});
	module = pigen_module_add(&model, (pigen_syntax_id){0}, module_symbol,
		scope, (pigen_source_span){source, 0, strlen(text)});
	assert(module.index != PIGEN_INVALID_ID);
	unsized_integer_data_type = pigen_data_type_unsized_integer(&model);
	boolean_type = pigen_data_type_boolean(&model);
	assert(pigen_data_type_resolve_binary_operation(&model, PIGEN_BINARY_ADD,
		unsized_integer_data_type, unsized_integer_data_type, &binary_resolution));
	assert(binary_resolution.left_conversion.kind == PIGEN_CONVERSION_IDENTITY);
	assert(binary_resolution.left_conversion.source_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.left_conversion.target_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.right_conversion.kind == PIGEN_CONVERSION_IDENTITY);
	assert(binary_resolution.right_conversion.source_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.right_conversion.target_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.operation.operator == PIGEN_BINARY_ADD);
	assert(binary_resolution.operation.left_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.operation.right_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.operation.result_data_type.index ==
		unsized_integer_data_type.index);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_PARAMETER,
		unsized_integer_data_type, (pigen_source_span){source, 0, 5},
		(pigen_source_span){source, 0, 5}, &width, &shadowed) ==
		PIGEN_DECLARE_OK);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_SIGNAL,
		unsized_integer_data_type, (pigen_source_span){source, 6, 10},
		(pigen_source_span){source, 6, 10}, &left, &shadowed) ==
		PIGEN_DECLARE_OK);
	left_signal = pigen_signal_add(&model, (pigen_syntax_id){1}, module, left,
		unsized_integer_data_type, pigen_semantic_scalar_shape(&model),
		INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_LOGIC,
		PIGEN_SEMANTIC_INTERNAL, (pigen_source_span){source, 6, 10});
	assert(left_signal.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_TYPEDEF,
		unsized_integer_data_type,
		(pigen_source_span){source,
			(size_t)(strstr(text, "word_t") - text),
			(size_t)(strstr(text, "word_t") - text) + strlen("word_t")},
		(pigen_source_span){source,
			(size_t)(strstr(text, "word_t") - text),
			(size_t)(strstr(text, "word_t") - text) + strlen("word_t")},
		&word_type_symbol, &shadowed) == PIGEN_DECLARE_OK);
	aliased_type = pigen_data_type_alias(&model, word_type_symbol,
		unsized_integer_data_type, PIGEN_SIGN_IMPLICIT, NULL, 0);
	assert(aliased_type.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_SIGNAL,
		aliased_type,
		(pigen_source_span){source,
			(size_t)(strstr(text, "aliased") - text),
			(size_t)(strstr(text, "aliased") - text) + strlen("aliased")},
		(pigen_source_span){source,
			(size_t)(strstr(text, "aliased") - text),
			(size_t)(strstr(text, "aliased") - text) + strlen("aliased")},
		&aliased, &shadowed) == PIGEN_DECLARE_OK);
	aliased_signal = pigen_signal_add(&model, (pigen_syntax_id){2}, module,
		aliased, aliased_type, pigen_semantic_scalar_shape(&model),
		INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_LOGIC,
		PIGEN_SEMANTIC_INTERNAL,
		(pigen_source_span){source,
			(size_t)(strstr(text, "aliased") - text),
			(size_t)(strstr(text, "aliased") - text) + strlen("aliased")});
	assert(aliased_signal.index != PIGEN_INVALID_ID);
	width_8 = pigen_const_expr_intern_integer(&model, 8,
		unsized_integer_data_type);
	signed_8 = pigen_data_type_signed_integer(&model, width_8);
	unsigned_8 = pigen_data_type_unsigned_integer(&model, width_8);
	width_16 = pigen_const_expr_intern_integer(&model, 16,
		unsized_integer_data_type);
	width_24 = pigen_const_expr_intern_integer(&model, 24,
		unsized_integer_data_type);
	signed_16 = pigen_data_type_signed_integer(&model, width_16);
	signed_24 = pigen_data_type_signed_integer(&model, width_24);
	unsigned_16 = pigen_data_type_unsigned_integer(&model, width_16);
	width_11 = pigen_const_expr_intern_integer(&model, 11,
		unsized_integer_data_type);
	unsigned_11 = pigen_data_type_unsigned_integer(&model, width_11);
	signed_a = declare_signal(&model, module, scope, signed_8,
		(pigen_source_span){source, (size_t)(strstr(text, "sa + sb") - text),
			(size_t)(strstr(text, "sa + sb") - text) + 2},
		(pigen_syntax_id){3});
	signed_b = declare_signal(&model, module, scope, signed_8,
		(pigen_source_span){source, (size_t)(strstr(text, "sb) *") - text),
			(size_t)(strstr(text, "sb) *") - text) + 2},
		(pigen_syntax_id){4});
	signed_c = declare_signal(&model, module, scope, signed_8,
		(pigen_source_span){source, (size_t)(strstr(text, "sc\n") - text),
			(size_t)(strstr(text, "sc\n") - text) + 2},
		(pigen_syntax_id){5});
	unsigned_a = declare_signal(&model, module, scope, unsigned_8,
		(pigen_source_span){source, (size_t)(strstr(text, "ua + 1") - text),
			(size_t)(strstr(text, "ua + 1") - text) + 2},
		(pigen_syntax_id){6});
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_PARAMETER,
		pigen_data_type_from_spelling(&model,
			(pigen_source_span){source,
				(size_t)(strstr(text, "bit") - text),
				(size_t)(strstr(text, "bit") - text) + strlen("bit")},
			PIGEN_SIGN_UNSIGNED,
			&(pigen_data_type_argument){PIGEN_DATA_TYPE_ARGUMENT_COUNT,
				{.count = width_8}}, 1),
		(pigen_source_span){source,
			(size_t)(strstr(text, "bp)") - text),
			(size_t)(strstr(text, "bp)") - text) + 2},
		(pigen_source_span){source,
			(size_t)(strstr(text, "bp)") - text),
			(size_t)(strstr(text, "bp)") - text) + 2},
		&vector_parameter, &shadowed) == PIGEN_DECLARE_OK);
	shift_amount = declare_signal(&model, module, scope, unsigned_11,
		(pigen_source_span){source,
			(size_t)(strstr(text, "sh\n") - text),
			(size_t)(strstr(text, "sh\n") - text) + 2},
		(pigen_syntax_id){7});
	symbolic_width = pigen_const_expr_intern_symbol(&model, width,
		unsized_integer_data_type);
	symbolic_unsigned = pigen_data_type_unsigned_integer(&model,
		symbolic_width);
	{
		pigen_integer_id negative_two = pigen_integer_negate(&model,
			pigen_integer_intern_u64(&model, 2));
		pigen_data_type_id exact_negative_two =
			pigen_data_type_exact_integer(&model, negative_two);
		pigen_data_type_id symbolic_signed = pigen_data_type_signed_integer(
			&model, symbolic_width);
		pigen_const_expr_id promoted_width =
			pigen_const_expr_intern_width_sum(&model,
				(pigen_const_expr_id[]){symbolic_width,
					pigen_const_expr_intern_integer(&model, 1,
						unsized_integer_data_type)}, 2);
		pigen_data_type_id promoted_signed = pigen_data_type_signed_integer(
			&model, promoted_width);
		pigen_data_type_id exact_zero = pigen_data_type_exact_integer(&model,
			pigen_integer_intern_u64(&model, 0));
		pigen_data_type_id exact_one = pigen_data_type_exact_integer(&model,
			pigen_integer_intern_u64(&model, 1));
		pigen_const_expr_id result_width;

		assert(!pigen_data_type_conversion_is_valid(&model,
			(pigen_conversion){PIGEN_CONVERSION_INTEGER_PROMOTION,
				symbolic_unsigned, symbolic_signed}));
		assert(pigen_data_type_conversion_is_valid(&model,
			(pigen_conversion){PIGEN_CONVERSION_INTEGER_PROMOTION,
				symbolic_unsigned, promoted_signed}));
		assert(pigen_data_type_resolve_binary_operation(&model,
			PIGEN_BINARY_ADD, symbolic_unsigned, exact_negative_two,
			&binary_resolution));
		assert(pigen_data_type_numerical_interpretation(&model,
			binary_resolution.operation.result_data_type) ==
			PIGEN_NUMERICAL_SIGNED_INTEGER);
		result_width = pigen_data_type_packed_width(&model,
			binary_resolution.operation.result_data_type);
		assert(pigen_const_expr_get(&model, result_width)->kind ==
			PIGEN_CONST_EXPR_NUMERICAL_RANGE_WIDTH);
		assert(pigen_const_expr_is_symbolic(&model, result_width));
		assert(pigen_data_type_resolve_binary_operation(&model,
			PIGEN_BINARY_MULTIPLY, symbolic_signed, exact_zero,
			&binary_resolution));
		assert(binary_resolution.operation.result_data_type.index ==
			pigen_data_type_unsigned_integer(&model,
				pigen_const_expr_intern_integer(&model, 1,
					unsized_integer_data_type)).index);
		assert(pigen_data_type_resolve_binary_operation(&model,
			PIGEN_BINARY_MULTIPLY, symbolic_signed, exact_one,
			&binary_resolution));
		assert(binary_resolution.operation.result_data_type.index ==
			symbolic_signed.index);
		assert(pigen_data_type_resolve_binary_operation(&model,
			PIGEN_BINARY_ADD, symbolic_unsigned, exact_zero,
			&binary_resolution));
		assert(binary_resolution.operation.result_data_type.index ==
			symbolic_unsigned.index);
	}
	symbolic_shift_amount = declare_signal(&model, module, scope,
		symbolic_unsigned,
		(pigen_source_span){source,
			(size_t)(strstr(text, "ds\n") - text),
			(size_t)(strstr(text, "ds\n") - text) + 2},
		(pigen_syntax_id){8});
	(void)signed_a;
	(void)signed_b;
	(void)signed_c;
	(void)unsigned_a;
	(void)vector_parameter;
	(void)shift_amount;
	(void)symbolic_shift_amount;

	/* Expanded token extents exclude the EOF token. */
	runtime_syntax = parse(&preprocessed, &syntax, 2, 5);
	comparison_syntax = parse(&preprocessed, &syntax, 5, 8);
	constant_syntax = parse(&preprocessed, &syntax, 8, 11);
	constant_index_syntax = parse(&preprocessed, &syntax, 11, 15);
	runtime_index_syntax = parse(&preprocessed, &syntax, 15, 19);
	constant_range_syntax = parse(&preprocessed, &syntax, 19, 25);
	constant_indexed_syntax = parse(&preprocessed, &syntax, 25, 31);
	runtime_select_syntax = parse(&preprocessed, &syntax, 31, 37);
	invalid_range_syntax = parse(&preprocessed, &syntax, 37, 43);
	invalid_width_syntax = parse(&preprocessed, &syntax, 43, 49);
	constant_concat_syntax = parse(&preprocessed, &syntax, 49, 54);
	runtime_concat_syntax = parse(&preprocessed, &syntax, 54, 59);
	swapped_concat_syntax = parse(&preprocessed, &syntax, 59, 64);
	aliased_add_syntax = parse(&preprocessed, &syntax, 66, 69);
	aliased_compare_syntax = parse(&preprocessed, &syntax, 69, 72);
	chained_syntax = parse(&preprocessed, &syntax, 72, 79);
	literal_add_syntax = parse(&preprocessed, &syntax, 79, 82);
	mixed_add_syntax = parse(&preprocessed, &syntax, 82, 85);
	integer_cast_syntax = parse(&preprocessed, &syntax, 85, 93);
	vector_cast_syntax = parse(&preprocessed, &syntax, 93, 101);
	invalid_mixed_syntax = parse(&preprocessed, &syntax, 101, 104);
	identity_cast_syntax = parse(&preprocessed, &syntax, 104, 112);
	constant_cast_syntax = parse(&preprocessed, &syntax, 112, 120);
	vector_resize_cast_syntax = parse(&preprocessed, &syntax, 120, 128);
	invalid_cast_syntax = parse(&preprocessed, &syntax, 146, 154);
	oversized_shift_syntax = parse(&preprocessed, &syntax, 128, 131);
	symbolic_shift_syntax = parse(&preprocessed, &syntax, 131, 134);
	enormous_shift_syntax[0] = parse(&preprocessed, &syntax, 134, 137);
	enormous_shift_syntax[1] = parse(&preprocessed, &syntax, 137, 140);
	enormous_shift_syntax[2] = parse(&preprocessed, &syntax, 140, 143);
	symbolic_power_syntax = parse(&preprocessed, &syntax, 143, 146);
	{
		pigen_analyzed_expr_arena analyzed_arena = {0};
		pigen_analyzed_expr_id analyzed;
		const pigen_syntax_expr *addition = pigen_syntax_expr_get(
			&syntax.expressions, literal_add_syntax);
		size_t expression_count = model.expression_count;

		assert(pigen_analyze_expression(&syntax, &model, scope,
			integer_cast_syntax, 0, PIGEN_LITERAL_DOMAIN_PIGEN, &policy,
			&analyzed_arena, &analyzed,
			&semantic_error));
		assert(analyzed.index != PIGEN_INVALID_ID);
		assert(model.expression_count == expression_count);
		pigen_free_analyzed_expr_arena(&analyzed_arena);
		assert(addition && addition->kind == PIGEN_SYNTAX_EXPR_BINARY);
		assert(pigen_analyze_expression(&syntax, &model, scope,
			addition->as.binary.right, 1, PIGEN_LITERAL_DOMAIN_PIGEN, NULL,
			&analyzed_arena, &analyzed, &semantic_error));
		assert(pigen_data_type_numerical_interpretation(&model,
			pigen_analyzed_expr_get(&analyzed_arena, analyzed)->data_type) ==
			PIGEN_NUMERICAL_EXACT_INTEGER);
		pigen_free_analyzed_expr_arena(&analyzed_arena);
		assert(pigen_analyze_expression(&syntax, &model, scope,
			addition->as.binary.right, 1, PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG,
			NULL, &analyzed_arena, &analyzed, &semantic_error));
		assert(pigen_analyzed_expr_get(&analyzed_arena, analyzed)->data_type.index ==
			unsized_integer_data_type.index);
		pigen_free_analyzed_expr_arena(&analyzed_arena);
		{
			pigen_expr_id runtime_literal = pigen_resolve_expression(&syntax,
				&model, scope, addition->as.binary.right, &policy, &semantic_error);
			pigen_expr_id constant_literal = pigen_resolve_constant_expression(
				&syntax, &model, scope, addition->as.binary.right,
				PIGEN_LITERAL_DOMAIN_PIGEN, &policy, &semantic_error);
			const pigen_semantic_expr *runtime_known = pigen_expr_get(&model,
				runtime_literal);
			const pigen_semantic_expr *constant_known = pigen_expr_get(&model,
				constant_literal);

			assert(runtime_known && constant_known);
			assert(runtime_known->kind == PIGEN_EXPR_EXACT_INTEGER);
			assert(constant_known->kind == runtime_known->kind);
			assert(constant_known->data_type.index == runtime_known->data_type.index);
			assert(pigen_expr_constant(&model, constant_literal).index ==
				pigen_expr_constant(&model, runtime_literal).index);
		}
	}

	intrinsic = pigen_resolve_expression(&syntax, &model, scope,
		chained_syntax, &policy, &semantic_error);
	known = pigen_expr_get(&model, intrinsic);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(resolved_width(&model, known->data_type) == 17);
	{
		pigen_expr_id narrowed = pigen_resolve_assignment_value(&syntax, &model,
			scope, chained_syntax, signed_16, &policy, &semantic_error);
		pigen_expr_id widened = pigen_resolve_assignment_value(&syntax, &model,
			scope, chained_syntax, signed_24, &policy, &semantic_error);
		const pigen_semantic_expr *narrowed_known = pigen_expr_get(&model,
			narrowed);
		const pigen_semantic_expr *widened_known = pigen_expr_get(&model,
			widened);

		assert(narrowed_known && narrowed_known->kind == PIGEN_EXPR_CONVERSION);
		assert(narrowed_known->data_type.index == signed_16.index);
		assert(narrowed_known->as.conversion.conversion.kind ==
			PIGEN_CONVERSION_INTEGER_RESIZE);
		assert(resolved_width(&model, pigen_expr_get(&model,
			narrowed_known->as.conversion.operand)->data_type) == 17);
		assert(widened_known && widened_known->kind == PIGEN_EXPR_CONVERSION);
		assert(widened_known->data_type.index == signed_24.index);
		assert(widened_known->as.conversion.conversion.kind ==
			PIGEN_CONVERSION_INTEGER_RESIZE);
		{
			size_t expression_count = model.expression_count;
			assert(pigen_resolve_assignment_value(&syntax, &model, scope,
			chained_syntax, unsigned_16, &policy, &semantic_error).index ==
				PIGEN_INVALID_ID);
			assert(model.expression_count == expression_count);
		}
	}
	{
		known = pigen_expr_get(&model, intrinsic);
		const pigen_semantic_expr *group = pigen_expr_get(&model,
			known->as.binary.left);
		const pigen_semantic_expr *addition;

		assert(group && group->kind == PIGEN_EXPR_GROUP);
		addition = pigen_expr_get(&model, group->as.group.operand);
		assert(addition && addition->kind == PIGEN_EXPR_BINARY);
		assert(resolved_width(&model, addition->data_type) == 9);
	}

	literal_add = pigen_resolve_expression(&syntax, &model, scope,
		literal_add_syntax, &policy, &semantic_error);
	known = pigen_expr_get(&model, literal_add);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(resolved_width(&model, known->data_type) == 9);
	{
		const pigen_semantic_expr *left_operand = pigen_expr_get(&model,
			known->as.binary.left);
		const pigen_semantic_expr *right_operand = pigen_expr_get(&model,
			known->as.binary.right);
		const pigen_semantic_expr *exact;

		assert(left_operand && left_operand->kind == PIGEN_EXPR_SYMBOL);
		assert(right_operand && right_operand->kind == PIGEN_EXPR_CONVERSION);
		assert(right_operand->as.conversion.conversion.kind ==
			PIGEN_CONVERSION_EXACT_INTEGER);
		exact = pigen_expr_get(&model, right_operand->as.conversion.operand);
		assert(exact && exact->kind == PIGEN_EXPR_EXACT_INTEGER);
	}

	mixed_add = pigen_resolve_expression(&syntax, &model, scope,
		mixed_add_syntax, &policy, &semantic_error);
	known = pigen_expr_get(&model, mixed_add);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(resolved_width(&model, known->data_type) == 10);
	assert(pigen_expr_get(&model, known->as.binary.left)->kind ==
		PIGEN_EXPR_CONVERSION);
	assert(pigen_expr_get(&model, known->as.binary.left)->as.conversion.
		conversion.kind == PIGEN_CONVERSION_INTEGER_RESIZE);
	assert(pigen_expr_get(&model, known->as.binary.right)->kind ==
		PIGEN_EXPR_CONVERSION);
	assert(pigen_expr_get(&model, known->as.binary.right)->as.conversion.
		conversion.kind == PIGEN_CONVERSION_INTEGER_PROMOTION);

	integer_cast = pigen_resolve_expression(&syntax, &model, scope,
		integer_cast_syntax, &policy, &semantic_error);
	known = pigen_expr_get(&model, integer_cast);
	assert(known && known->kind == PIGEN_EXPR_CONVERSION);
	assert(known->as.conversion.conversion.kind ==
		PIGEN_CONVERSION_INTEGER_REINTERPRET);
	assert(known->data_type.index == unsigned_8.index);
	assert(pigen_lvalue_resolve(&model, integer_cast).index == PIGEN_INVALID_ID);

	vector_cast = pigen_resolve_expression(&syntax, &model, scope,
		vector_cast_syntax, &policy, &semantic_error);
	known = pigen_expr_get(&model, vector_cast);
	assert(known && known->kind == PIGEN_EXPR_CONVERSION);
	assert(known->as.conversion.conversion.kind ==
		PIGEN_CONVERSION_INTEGER_TO_VECTOR);

	identity_cast = pigen_resolve_expression(&syntax, &model, scope,
		identity_cast_syntax, &policy, &semantic_error);
	known = pigen_expr_get(&model, identity_cast);
	assert(known && known->kind == PIGEN_EXPR_SYMBOL);
	assert(known->as.symbol.index == unsigned_a.index);

	runtime_constant_cast = pigen_resolve_expression(&syntax, &model, scope,
		constant_cast_syntax, &policy, &semantic_error);
	constant_cast = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_cast_syntax, PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG, &policy,
		&semantic_error);
	known = pigen_expr_get(&model, runtime_constant_cast);
	assert(known && known->kind == PIGEN_EXPR_CONVERSION);
	assert(known->as.conversion.conversion.kind ==
		PIGEN_CONVERSION_VECTOR_TO_INTEGER);
	{
		const pigen_semantic_expr *constant_known = pigen_expr_get(&model,
			constant_cast);
		assert(constant_known && constant_known->kind == PIGEN_EXPR_CONVERSION);
		assert(constant_known->as.conversion.conversion.kind ==
			known->as.conversion.conversion.kind);
		assert(pigen_expr_get(&model, constant_known->as.conversion.operand)->kind ==
			PIGEN_EXPR_SYMBOL);
		assert(pigen_expr_get(&model, known->as.conversion.operand)->kind ==
			PIGEN_EXPR_SYMBOL);
	}
	/* Raw-vector casts are legal, including conversion of an SV bit literal
	 * into a Pigen bit count. Pin value and width independently. */
	{
		pigen_expr_id resized = pigen_resolve_expression(&syntax, &model, scope,
			vector_resize_cast_syntax, &policy, &semantic_error);
		const pigen_semantic_expr *value = pigen_expr_get(&model, resized);
		uint64_t bits = 0, cast_width = 0;
		assert(value && value->kind == PIGEN_EXPR_CONVERSION);
		assert(pigen_const_expr_evaluate_u64(&model, pigen_data_type_packed_width(&model, value->data_type), &cast_width) && cast_width == 8);
		assert(pigen_const_expr_evaluate_u64(&model, value->constant, &bits) && bits == 255);
	}
	{
		size_t expression_count = model.expression_count;
		const pigen_syntax_expr *cast_syntax = pigen_syntax_expr_get(
			&syntax.expressions, invalid_cast_syntax);
		semantic_error = (pigen_semantic_error){0};
		assert(pigen_resolve_expression(&syntax, &model, scope,
			invalid_cast_syntax, &policy, &semantic_error).index ==
			PIGEN_INVALID_ID);
		assert(model.expression_count == expression_count);
		assert(cast_syntax);
		assert(semantic_error.span.source.index == cast_syntax->location.source_span.source.index);
		assert(semantic_error.span.start >= cast_syntax->location.source_span.start);
		assert(semantic_error.span.end <= cast_syntax->location.source_span.end);
		assert(semantic_error.span.start < semantic_error.span.end);
	}
	{
		size_t expression_count = model.expression_count;
		const pigen_syntax_expr *shift_syntax = pigen_syntax_expr_get(
			&syntax.expressions, oversized_shift_syntax);
		semantic_error = (pigen_semantic_error){0};
		assert(pigen_resolve_expression(&syntax, &model, scope,
			oversized_shift_syntax, &policy, &semantic_error).index ==
			PIGEN_INVALID_ID);
		assert(model.expression_count == expression_count);
		assert(shift_syntax);
		assert(semantic_error.span.start ==
			shift_syntax->as.binary.operator_location.source_span.start);
		assert(semantic_error.span.end ==
			shift_syntax->as.binary.operator_location.source_span.end);
	}
	{
		size_t constraint_count = model.width_constraint_count;
		pigen_expr_id shifted = pigen_resolve_expression(&syntax, &model, scope,
			symbolic_shift_syntax, &policy, &semantic_error);
		const pigen_width_constraint *constraint;

		assert(shifted.index != PIGEN_INVALID_ID);
		assert(model.width_constraint_count == constraint_count + 1);
		constraint = pigen_width_constraint_get(&model, constraint_count);
		assert(constraint);
		assert(constraint->maximum_bits == 1024);
		assert(constraint->width.index == pigen_data_type_packed_width(&model,
			pigen_expr_get(&model, shifted)->data_type).index);
	}
	{
		size_t constraint_count = model.width_constraint_count;
		pigen_expr_id powered = pigen_resolve_expression(&syntax, &model, scope,
			symbolic_power_syntax, &policy, &semantic_error);
		const pigen_width_constraint *constraint;
		pigen_const_expr_id result_width;

		assert(powered.index != PIGEN_INVALID_ID);
		result_width = pigen_data_type_packed_width(&model,
			pigen_expr_get(&model, powered)->data_type);
		assert(pigen_const_expr_get(&model, result_width)->kind ==
			PIGEN_CONST_EXPR_NUMERICAL_RANGE_WIDTH);
		assert(model.width_constraint_count == constraint_count + 1);
		constraint = pigen_width_constraint_get(&model, constraint_count);
		assert(constraint && constraint->maximum_bits == 1024);
		assert(constraint->width.index == result_width.index);
	}
	{
		size_t i;

		for (i = 0; i < 3; i++)
		{
			const pigen_syntax_expr *shift_syntax = pigen_syntax_expr_get(
				&syntax.expressions, enormous_shift_syntax[i]);
			size_t expression_count = model.expression_count;

			semantic_error = (pigen_semantic_error){0};
			assert(pigen_resolve_expression(&syntax, &model, scope,
				enormous_shift_syntax[i], &policy, &semantic_error).index ==
				PIGEN_INVALID_ID);
			assert(model.expression_count == expression_count);
			assert(semantic_error.message &&
				strstr(semantic_error.message, "exceeds width limit"));
			assert(shift_syntax && semantic_error.span.start ==
				shift_syntax->as.binary.operator_location.source_span.start);
		}
	}

	{
		size_t expression_count = model.expression_count;
		const pigen_syntax_expr *invalid_syntax = pigen_syntax_expr_get(
			&syntax.expressions, invalid_mixed_syntax);
		/* Mixed based-literal/ordinary arithmetic is supported now, so
		 * this expression resolves to a binary operation instead of an
		 * error. */
		semantic_error = (pigen_semantic_error){0};
		{
			pigen_expr_id mixed = pigen_resolve_expression(&syntax, &model,
				scope, invalid_mixed_syntax, &policy, &semantic_error);
			const pigen_semantic_expr *mixed_known = pigen_expr_get(&model,
				mixed);

			assert(mixed.index != PIGEN_INVALID_ID);
			assert(mixed_known && mixed_known->kind == PIGEN_EXPR_BINARY);
			assert(mixed_known->data_type.index != PIGEN_INVALID_ID);
		}
		assert(model.expression_count >= expression_count);
		assert(invalid_syntax);
	}

	runtime = pigen_resolve_expression(&syntax, &model, scope,
		runtime_syntax, &policy, NULL);
	known = pigen_expr_get(&model, runtime);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == unsized_integer_data_type.index);
	assert(known->as.binary.operation.operator == PIGEN_BINARY_ADD);
	assert(known->as.binary.operation.left_data_type.index ==
		unsized_integer_data_type.index);
	assert(known->as.binary.operation.right_data_type.index ==
		unsized_integer_data_type.index);
	assert(known->as.binary.operation.result_data_type.index ==
		unsized_integer_data_type.index);
	left_read = pigen_expr_get(&model, known->as.binary.left);
	width_read = pigen_expr_get(&model, known->as.binary.right);
	assert(left_read && left_read->kind == PIGEN_EXPR_SYMBOL);
	assert(left_read->as.symbol.index == left.index);
	assert(pigen_expr_constant(&model, known->as.binary.left).index ==
		PIGEN_INVALID_ID);
	assert(width_read && width_read->kind == PIGEN_EXPR_SYMBOL);
	assert(width_read->as.symbol.index == width.index);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, known->as.binary.right)) != NULL);
	assert(pigen_expr_constant(&model, runtime).index == PIGEN_INVALID_ID);
	assert(pigen_resolve_constant_expression(&syntax, &model, scope,
		runtime_syntax, PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG, &policy, NULL).index ==
		PIGEN_INVALID_ID);

	comparison = pigen_resolve_expression(&syntax, &model, scope,
		comparison_syntax, &policy, NULL);
	known = pigen_expr_get(&model, comparison);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == boolean_type.index);
	assert(known->as.binary.operation.operator == PIGEN_BINARY_EQUAL);
	assert(known->as.binary.operation.result_data_type.index ==
		boolean_type.index);
	assert(pigen_expr_constant(&model, comparison).index == PIGEN_INVALID_ID);

	constant = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_syntax, PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG, &policy, NULL);
	known = pigen_expr_get(&model, constant);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == unsized_integer_data_type.index);
	{
		const pigen_const_expr *constant_known = pigen_const_expr_get(&model,
			pigen_expr_constant(&model, constant));
		assert(constant_known &&
			constant_known->kind == PIGEN_CONST_EXPR_BINARY);
		assert(constant_known->as.binary.operation.operator ==
			PIGEN_BINARY_ADD);
		assert(constant_known->as.binary.operation.left_data_type.index ==
			known->as.binary.operation.left_data_type.index);
		assert(constant_known->as.binary.operation.right_data_type.index ==
			known->as.binary.operation.right_data_type.index);
		assert(constant_known->as.binary.operation.result_data_type.index ==
			known->as.binary.operation.result_data_type.index);
	}

	constant_index = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_index_syntax, PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG, &policy, NULL);
	known = pigen_expr_get(&model, constant_index);
	assert(known && known->kind == PIGEN_EXPR_INDEX);
	assert(known->data_type.index == boolean_type.index);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_index))->kind ==
		PIGEN_CONST_EXPR_INDEX);

	runtime_index = pigen_resolve_expression(&syntax, &model, scope,
		runtime_index_syntax, &policy, NULL);
	known = pigen_expr_get(&model, runtime_index);
	assert(known && known->kind == PIGEN_EXPR_INDEX);
	assert(known->data_type.index == boolean_type.index);
	assert(pigen_expr_constant(&model, runtime_index).index ==
		PIGEN_INVALID_ID);

	constant_range = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_range_syntax, PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG, &policy, NULL);
	known = pigen_expr_get(&model, constant_range);
	assert(known && known->kind == PIGEN_EXPR_SELECT);
	assert(known->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_range))->kind ==
		PIGEN_CONST_EXPR_SELECT);

	constant_indexed = pigen_resolve_constant_expression(&syntax, &model,
		scope, constant_indexed_syntax, PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG,
		&policy, NULL);
	known = pigen_expr_get(&model, constant_indexed);
	assert(known && known->kind == PIGEN_EXPR_SELECT);
	assert(known->as.select.kind == PIGEN_SEMANTIC_SELECT_INDEXED_UP);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_indexed))->kind ==
		PIGEN_CONST_EXPR_SELECT);

	runtime_select = pigen_resolve_expression(&syntax, &model, scope,
		runtime_select_syntax, &policy, NULL);
	known = pigen_expr_get(&model, runtime_select);
	assert(known && known->kind == PIGEN_EXPR_SELECT);
	assert(known->as.select.kind == PIGEN_SEMANTIC_SELECT_INDEXED_DOWN);
	assert(pigen_expr_constant(&model, runtime_select).index ==
		PIGEN_INVALID_ID);
	assert(pigen_resolve_expression(&syntax, &model, scope,
		invalid_range_syntax, &policy, NULL).index == PIGEN_INVALID_ID);
	assert(pigen_resolve_expression(&syntax, &model, scope,
		invalid_width_syntax, &policy, NULL).index == PIGEN_INVALID_ID);

	constant_concat = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_concat_syntax, PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG, &policy, NULL);
	known = pigen_expr_get(&model, constant_concat);
	assert(known && known->kind == PIGEN_EXPR_CONCATENATION);
	assert(known->as.sequence.child_count == 2);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_concat))->kind ==
		PIGEN_CONST_EXPR_CONCATENATION);

	runtime_concat = pigen_resolve_expression(&syntax, &model, scope,
		runtime_concat_syntax, &policy, NULL);
	swapped_concat = pigen_resolve_expression(&syntax, &model, scope,
		swapped_concat_syntax, &policy, NULL);
	known = pigen_expr_get(&model, runtime_concat);
	assert(known && known->kind == PIGEN_EXPR_CONCATENATION);
	assert(known->data_type.index ==
		pigen_expr_get(&model, swapped_concat)->data_type.index);
	assert(pigen_expr_constant(&model, runtime_concat).index ==
		PIGEN_INVALID_ID);
	{
		const pigen_expr_id *children = pigen_expr_children(&model,
			known->as.sequence.first_child, known->as.sequence.child_count);
		assert(children && known->as.sequence.child_count == 2);
		assert(pigen_expr_get(&model, children[0])->as.symbol.index ==
			left.index);
		assert(pigen_expr_get(&model, children[1])->as.symbol.index ==
			width.index);
	}

	aliased_add = pigen_resolve_expression(&syntax, &model, scope,
		aliased_add_syntax, &policy, NULL);
	known = pigen_expr_get(&model, aliased_add);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == aliased_type.index);
	aliased_compare = pigen_resolve_expression(&syntax, &model, scope,
		aliased_compare_syntax, &policy, NULL);
	known = pigen_expr_get(&model, aliased_compare);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == boolean_type.index);
	{
		const char count_text[] = "int[0]";
		pigen_source_id count_source = pigen_source_add(&sources,
			"count.pigen", count_text, strlen(count_text));
		pigen_preprocess_result count_preprocessed = {0};
		pigen_syntax_tree count_syntax = {0};
		pigen_syntax_error syntax_error = {0};
		pigen_syntax_type_id count_type = INVALID_ID(pigen_syntax_type_id);
		size_t after;

		assert(pigen_preprocess(&sources, count_source, NULL,
			&count_preprocessed, &preprocess_error));
		count_syntax.expanded = &count_preprocessed.expanded;
		assert(pigen_parse_type_prefix(&count_preprocessed.expanded, 0,
			count_preprocessed.expanded.token_count - 1,
			&count_syntax.expressions, &count_syntax.types, &count_type, &after,
			&syntax_error));
		assert(after == count_preprocessed.expanded.token_count - 1);
		semantic_error = (pigen_semantic_error){0};
		assert(pigen_resolve_type(&count_syntax, &model, scope, count_type,
			&semantic_error).index == PIGEN_INVALID_ID);
		assert(!strcmp(semantic_error.message,
			"type count must be a positive integer"));
		pigen_free_syntax_type_arena(&count_syntax.types);
		pigen_free_syntax_expr_arena(&count_syntax.expressions);
		pigen_free_preprocess_result(&count_preprocessed);
	}

	check_bits(&sources,&model,module,scope);
	check_size_casts(&sources,&model,module,scope);
	check_assignment_fills(&sources,&model,scope);
	check_cast_fills(&sources,&model,scope);
	check_empty_constant_fields(&sources,&model,scope);
	check_runtime_empty_analysis(&sources,&model,module,scope);
	check_anonymous_records(&sources,&model,scope);
	pigen_free_semantic_model(&model);
	pigen_free_syntax_expr_arena(&syntax.expressions);
	pigen_free_syntax_type_arena(&syntax.types);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
	puts("PASS: runtime and constant expressions share typed resolution");
	return 0;
}
