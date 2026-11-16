#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pigen/syntax.h"

static int span_is(const pigen_source_manager *sources, pigen_source_span span, const char *value)
{
	size_t length = 0;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && length == strlen(value) && !memcmp(text, value, length);
}

static int token_is(const pigen_expanded_source *expanded, pigen_token_id token, const char *value)
{
	size_t length = 0;
	const char *text = pigen_expanded_token_text(expanded, pigen_expanded_token_get(expanded, token), &length);
	return text && length == strlen(value) && !memcmp(text, value, length);
}

static void check(const char *body, int valid, int increment, int compound)
{
	const char *header = "module m(input wire clk,input wire reset,input uint[32] axi4_stream s[2],output uint[32] axi4_stream m[2]);\n";
	char text[8192];
	snprintf(text, sizeof text, "%s%s\nendmodule", header, body);
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "generate.pigen", text, strlen(text));
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_tree tree = {0};
	pigen_syntax_error error = {0};
	assert(pigen_preprocess(&sources, source, NULL, &preprocessed, &preprocess_error));
	int parsed = pigen_parse_syntax(&preprocessed.expanded, &tree, &error);
	if (parsed != valid) fprintf(stderr,"unexpected generate parse: %s\n%s\n",error.message ? error.message : "success",body);
	assert(parsed == valid);
	if (!valid) assert(error.message);
	else
	{
		size_t regions=0, loops=0, branches=0, blocks=0, processes=0, fabrics=0;
		for (size_t i=0;i<tree.node_count;i++)
		{
			const pigen_syntax_node *node=&tree.nodes[i];
			if (node->kind==PIGEN_SYNTAX_GENERATE_REGION)
			{
				regions++;
				assert(span_is(&sources,node->location.source_span,body));
			}
			if (node->kind==PIGEN_SYNTAX_GENERATE_BLOCK) blocks++;
			if (node->kind==PIGEN_SYNTAX_CLOCKED_PROCESS) processes++;
			if (node->kind==PIGEN_SYNTAX_FABRIC) fabrics++;
			if (node->kind==PIGEN_SYNTAX_GENERATE_IF)
			{
				branches++;
				assert(node->as.generate_if.has_else);
				const pigen_syntax_expr *condition=pigen_syntax_expr_get(&tree.expressions,node->as.generate_if.condition);
				assert(condition && span_is(&sources,condition->location.source_span,"i == 0"));
				const pigen_syntax_node *when_true=pigen_syntax_get(&tree,node->first_child);
				const pigen_syntax_node *when_false=pigen_syntax_get(&tree,when_true->next_sibling);
				assert(when_true->parent.index==i && when_false->parent.index==i);
			}
			if (node->kind==PIGEN_SYNTAX_GENERATE_FOR)
			{
				loops++;
				assert(token_is(&preprocessed.expanded,node->as.generate_for.iterator,"i"));
				assert(node->as.generate_for.declares_iterator);
				assert(node->as.generate_for.increment==increment);
				assert(node->as.generate_for.compound_step==!!compound);
				if (compound) assert(node->as.generate_for.compound_operator==(pigen_syntax_operator)(compound-1));
				const pigen_syntax_expr *initial=pigen_syntax_expr_get(&tree.expressions,node->as.generate_for.initial);
				const pigen_syntax_expr *condition=pigen_syntax_expr_get(&tree.expressions,node->as.generate_for.condition);
				assert(initial && span_is(&sources,initial->location.source_span,"0"));
				assert(condition && span_is(&sources,condition->location.source_span,"i < 2"));
				assert((node->as.generate_for.next_value.index==PIGEN_INVALID_ID)==(increment!=0));
				const pigen_syntax_node *block=pigen_syntax_get(&tree,node->first_child);
				assert(block && block->kind==PIGEN_SYNTAX_GENERATE_BLOCK);
				assert(token_is(&preprocessed.expanded,block->as.generate_block.name,"lanes"));
				assert(block->parent.index==i);
			}
		}
		assert(regions==1 && loops==1 && branches==1 && blocks==3 && processes==1 && fabrics==1);
	}
	pigen_free_syntax_tree(&tree);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
}

static void check_parameter_instances(void)
{
	const char *text =
		"module parent(input wire clk,input uint[32] axi4_stream s[2]);\n"
		"genvar unused_a,unused_b; generate for(genvar i=0;i<2;i++) begin : lanes\n"
		"leaf #(.TAG(i+3),.WIDTH($clog2(i+2))) named(.clk(clk),.s(s[i]));\n"
		"leaf #(fn(i,2),{i,2}) positional(clk,fn(i,2));\n"
		"leaf #() defaults(.clk(clk)); end endgenerate endmodule\n"
		"module leaf #(parameter TAG=0,parameter WIDTH=32)(input wire clk,input uint[32] axi4_stream s); endmodule\n";
	pigen_source_manager sources={0};
	pigen_source_id source=pigen_source_add(&sources,"parameter_instances.pigen",text,strlen(text));
	pigen_preprocess_result preprocessed={0};pigen_preprocess_error pe={0};
	pigen_syntax_tree tree={0};pigen_syntax_error error={0};
	assert(pigen_preprocess(&sources,source,NULL,&preprocessed,&pe));
	if(!pigen_parse_syntax(&preprocessed.expanded,&tree,&error))
		fprintf(stderr,"parameter instance syntax: %s\n",error.message);
	assert(!error.message);
	size_t instances=0, parameters=0, ports=0, genvars=0;
	const char *expected[]={"i+3","$clog2(i+2)","fn(i,2)","{i,2}"};
	for(size_t n=0;n<tree.node_count;n++)
	{
		const pigen_syntax_node *node=&tree.nodes[n];
		if(node->kind==PIGEN_SYNTAX_GENVAR)
		{
			assert(genvars<2 && token_is(&preprocessed.expanded,node->as.genvar.name,genvars ? "unused_b" : "unused_a"));
			assert(tree.nodes[node->parent.index].kind==PIGEN_SYNTAX_MODULE);
			genvars++;
		}
		if(node->kind==PIGEN_SYNTAX_INSTANCE)
		{
			instances++;
			assert(token_is(&preprocessed.expanded,node->as.instance.module,"leaf"));
			assert(node->as.instance.has_parameters);
			assert(tree.nodes[node->parent.index].kind==PIGEN_SYNTAX_GENERATE_BLOCK);
		}
		if(node->kind==PIGEN_SYNTAX_PARAMETER_CONNECTION)
		{
			assert(parameters<4);
			assert(tree.nodes[node->parent.index].kind==PIGEN_SYNTAX_INSTANCE);
			const pigen_syntax_expr *value=pigen_syntax_expr_get(&tree.expressions,node->as.connection.value);
			assert(value && span_is(&sources,value->location.source_span,expected[parameters]));
			if(parameters<2) assert(token_is(&preprocessed.expanded,node->as.connection.port,parameters ? "WIDTH" : "TAG"));
			else assert(node->as.connection.port.index==PIGEN_INVALID_ID);
			parameters++;
		}
		if(node->kind==PIGEN_SYNTAX_CONNECTION) ports++;
	}
	assert(instances==3 && parameters==4 && ports==5 && genvars==2);
	pigen_free_syntax_tree(&tree);pigen_free_preprocess_result(&preprocessed);pigen_free_sources(&sources);
}

static void check_case_generate(void)
{
	const char *body="case(SEL) 0, (SEL ? 1 : 2): begin : alternative uint[32] axi4_stream unused; end "
		"2: if(1) begin : selected leaf #(.TAG(SEL)) child(.clk(clk)); end "
		"default begin : fallback fabric begin s[0] -> m[0]; endfabric end endcase";
	for(int explicit_region=0;explicit_region<2;explicit_region++)
	{
		char text[2048];
		snprintf(text,sizeof text,"module parent #(parameter SEL=2)(input wire clk,input wire reset,input uint[32] axi4_stream s[2],output uint[32] axi4_stream m[2]); %s%s%s endmodule",
			explicit_region ? "generate " : "",body,explicit_region ? " endgenerate" : "");
		pigen_source_manager sources={0};pigen_source_id input=pigen_source_add(&sources,"case_generate.pigen",text,strlen(text));
		pigen_preprocess_result preprocessed={0};pigen_preprocess_error pe={0};pigen_syntax_tree tree={0};pigen_syntax_error error={0};
		assert(pigen_preprocess(&sources,input,NULL,&preprocessed,&pe));
		if(!pigen_parse_syntax(&preprocessed.expanded,&tree,&error)) fprintf(stderr,"case generate syntax: %s\n",error.message);
		assert(!error.message);
		size_t cases=0,items=0,labels=0,defaults=0;
		const char *expected[]={"0","(SEL ? 1 : 2)","2"};
		for(size_t i=0;i<tree.node_count;i++)
		{
			const pigen_syntax_node *node=&tree.nodes[i];
			if(node->kind==PIGEN_SYNTAX_GENERATE_CASE)
			{
				cases++;assert(span_is(&sources,node->location.source_span,body));
				assert(tree.nodes[node->parent.index].kind==PIGEN_SYNTAX_GENERATE_REGION);
				const pigen_syntax_expr *selector=pigen_syntax_expr_get(&tree.expressions,node->as.generate_case.selector);
				assert(selector && span_is(&sources,selector->location.source_span,"SEL"));
			}
			if(node->kind==PIGEN_SYNTAX_GENERATE_CASE_ITEM)
			{
				items++;defaults+=node->as.generate_case_item.is_default;
				assert(tree.nodes[node->parent.index].kind==PIGEN_SYNTAX_GENERATE_CASE);
			}
			if(node->kind==PIGEN_SYNTAX_GENERATE_CASE_LABEL)
			{
				assert(labels<3 && tree.nodes[node->parent.index].kind==PIGEN_SYNTAX_GENERATE_CASE_ITEM);
				const pigen_syntax_expr *value=pigen_syntax_expr_get(&tree.expressions,node->as.generate_case_label.value);
				assert(value && span_is(&sources,value->location.source_span,expected[labels++]));
			}
		}
		assert(cases==1 && items==3 && labels==3 && defaults==1);
		pigen_free_syntax_tree(&tree);pigen_free_preprocess_result(&preprocessed);pigen_free_sources(&sources);
	}
}

int main(void)
{
	check_parameter_instances();
	check_case_generate();
	const struct { const char *text; int increment; int compound; } steps[]={
		{"i++",1,0}, {"++i",1,0}, {"i--",-1,0}, {"--i",-1,0}, {"i = i + 1",0,0},
		{"i += 1",0,PIGEN_SYNTAX_OP_ADD+1}, {"i -= 1",0,PIGEN_SYNTAX_OP_SUBTRACT+1},
		{"i *= 2",0,PIGEN_SYNTAX_OP_MULTIPLY+1}, {"i /= 2",0,PIGEN_SYNTAX_OP_DIVIDE+1},
		{"i %= 3",0,PIGEN_SYNTAX_OP_MODULO+1}, {"i <<= 1",0,PIGEN_SYNTAX_OP_SHIFT_LEFT+1},
		{"i >>>= 1",0,PIGEN_SYNTAX_OP_ARITH_SHIFT_RIGHT+1}, {"i >>= 1",0,PIGEN_SYNTAX_OP_SHIFT_RIGHT+1},
		{"i <<<= 1",0,PIGEN_SYNTAX_OP_ARITH_SHIFT_LEFT+1},
		{"i &= 3",0,PIGEN_SYNTAX_OP_BITWISE_AND+1}, {"i |= 1",0,PIGEN_SYNTAX_OP_BITWISE_OR+1},
		{"i ^= 1",0,PIGEN_SYNTAX_OP_BITWISE_XOR+1}
	};
	for (size_t i=0;i<sizeof steps/sizeof *steps;i++)
	{
		char body[2048];
		snprintf(body,sizeof body,"generate for(genvar i = 0; i < 2; %s) begin : lanes\n"
			"if(i == 0) begin : first\nalways @(posedge clk) if(!reset) m[i] <= s[i];\nend : first\n"
			"else begin : second\nfabric begin s[i] -> m[i]; endfabric\nend : second\n"
			"end : lanes endgenerate",steps[i].text);
		check(body,1,steps[i].increment,steps[i].compound);
		char implicit[2048];
		size_t prefix=strlen("generate "),suffix=strlen(" endgenerate");
		snprintf(implicit,sizeof implicit,"%.*s",(int)(strlen(body)-prefix-suffix),body+prefix);
		check(implicit,1,steps[i].increment,steps[i].compound);
	}
	const char *invalid[]={
        "if(1) begin uint[32] axi4_stream bus; assign ; end",
        "if(1) begin uint[32] axi4_stream bus; assign a=1,; end",
        "if(1) begin uint[32] axi4_stream bus; assign a=1,b=; end",
        "if(1) begin uint[32] axi4_stream bus; assign a=1,b; end",
		"case(1) default: begin uint[32] axi4_stream a; end default:; endcase",
		"case(1) ,: begin uint[32] axi4_stream a; end endcase",
		"case(1) 1 begin uint[32] axi4_stream a; end endcase",
		"case(1) 1: begin uint[32] axi4_stream a; end",
		"casez(1) 1: begin uint[32] axi4_stream a; end endcase",
		"for(genvar i=0;i<2;j++) begin : lanes uint[32] axi4_stream middle; end",
		"if(1) begin : selected uint[32] axi4_stream middle; end : wrong",
		"if(1) begin uint[32] axi4_stream middle;",
		"genvar ;",
		"genvar i=0;",
		"genvar i,;",
		"genvar i,,j;",
		"genvar i j;",
		"generate for(genvar i=0;i<2;i++) begin : lanes leaf #(.TAG i) child(.s(s[i])); end endgenerate",
		"generate for(genvar i=0; i<2; j++) begin : lanes uint[32] axi4_stream local_bus; end endgenerate",
		"generate for(genvar i=0; ; i++) begin : lanes uint[32] axi4_stream local_bus; end endgenerate",
		"generate for(genvar i=0; i<2) begin : lanes uint[32] axi4_stream local_bus; end endgenerate",
		"generate for(genvar i=0; i<2; i++) begin : lanes uint[32] axi4_stream local_bus; end : wrong endgenerate",
		"generate if(1) begin uint[32] axi4_stream local_bus; endgenerate",
		"generate if(1) begin uint[32] axi4_stream local_bus; end",
		"generate generate uint[32] axi4_stream local_bus; endgenerate endgenerate"
	};
	for(size_t i=0;i<sizeof invalid/sizeof *invalid;i++) check(invalid[i],0,0,0);
	char nested[7000]="generate ";
	for(size_t i=0;i<130;i++) strcat(nested,"if(1) begin ");
	strcat(nested,"uint[32] axi4_stream local_bus; ");
	for(size_t i=0;i<130;i++) strcat(nested,"end ");
	strcat(nested,"endgenerate");
	check(nested,0,0,0);
	puts("PASS: structured generate regions, loops, branches, scoped bodies and original spans");
	return 0;
}
