#!/usr/bin/env python3
"""Independent standard-pin transport checks for native AXI endpoint arrays."""
from pathlib import Path
import re
import shutil
import tempfile
from native_protocol_test import ROOT, channels, run, ordinary_package_source, foreign_axi_module


def case(directory, family, mode, native_alias=False, type_keys=False, package=False, ordinary_package=False, external=False, data=32, qualifier=None, clocked_foreach=None, compiler=None):
    case_oracle = ""
    if package: native_alias = True
    authored_mode = mode
    requested_mode = mode
    pipeline_exit=bool(clocked_foreach and clocked_foreach.startswith('pipeline_'))
    coordinate_modes={'query_coordinate_direct':'direct','query_coordinate_scalar':'scalar_binding',
                      'query_coordinate_projection':'projection','query_coordinate_fabric':'fabric_routed',
                      'query_coordinate_fabric_child':'fabric_child',
                      'coordinate_direct':'direct','coordinate_scalar':'scalar_binding',
                      'coordinate_projection':'projection','coordinate_fabric':'fabric_routed',
                      'coordinate_fabric_child':'fabric_child',
                      'fill_coordinate_direct':'direct','fill_coordinate_scalar':'scalar_binding',
                      'fill_coordinate_projection':'projection','fill_coordinate_fabric':'fabric_routed',
                      'fill_coordinate_fabric_child':'fabric_child',
                      'empty_coordinate_direct':'direct','empty_coordinate_scalar':'scalar_binding',
                      'empty_coordinate_projection':'projection','empty_coordinate_fabric':'fabric_routed',
                      'empty_coordinate_fabric_child':'fabric_child',
                      'replicated_coordinate_direct':'direct','replicated_coordinate_scalar':'scalar_binding',
                      'replicated_coordinate_projection':'projection','replicated_coordinate_fabric':'fabric_routed',
                      'replicated_coordinate_fabric_child':'fabric_child'}
    if mode in coordinate_modes: mode=requested_mode=coordinate_modes[mode]
    if mode in ('generate_foreach','generate_named_foreach','generate_named_comb'): mode=requested_mode='generate_comb'
    if mode=='generate_static_init': mode=requested_mode='generate_comb'
    if mode in ('generate_external', 'generate_external_local'): mode = 'generate_process'
    if mode in ('generate_fsm', 'generate_fsm_implicit', 'generate_pipeline', 'generate_pipeline_implicit', 'generate_pipeline_hierarchy'): mode = 'generate_process'
    if mode == 'generate_external_reuse': mode = 'scope_relative'
    if mode == 'generate_external_shadow': mode = 'generate_shadow'
    if mode == 'retained_external': mode = 'direct'
    implicit_modes = {'implicit_process': 'generate_process', 'implicit_fabric': 'generate_fabric',
                      'implicit_child': 'generate_parameters_nested', 'implicit_block': 'scope_block',
                      'implicit_named': 'scope_relative', 'implicit_external': 'generate_external_reuse',
                      'implicit_retained': 'retained_external', 'implicit_if': 'generate_process', 'implicit_conditional_loop': 'generate_parameters_nested',
                      'implicit_named_block': 'generate_process', 'implicit_unbraced': 'generate_parameters_nested'}
    case_modes = ('case_generate', 'case_generate_implicit', 'case_generate_default', 'case_generate_xz',
                  'case_generate_signed', 'case_generate_wide', 'case_generate_ternary',
                  'case_generate_none', 'case_generate_priority', 'case_generate_conditional')
    if mode in case_modes:
        mode = requested_mode = 'generate_parameters'
    if mode in implicit_modes:
        mode = implicit_modes[mode]
        requested_mode = mode
        if mode == 'generate_external_reuse': mode = 'scope_relative'
        if mode == 'retained_external': mode = 'direct'

    fields_by_channel = channels(family, data)
    options = {'axi4_stream': 'id=4,dest=3,user=7,strb=1,keep=1,last=1',
               'axi4_lite': 'addr=40',
               'axi4': 'addr=40,id=4,awuser=3,wuser=7,buser=5,aruser=11,ruser=13'}[family]
    dtype = 'int[32]' if mode == 'generate_signed' else 'bit[4][8]' if mode == 'generate_packed' or authored_mode in ('generate_foreach','generate_named_foreach') else 'uint[32]'
    if not data:
        assert family == 'axi4_stream'
        dtype = 'void'
        options = options.replace('strb=1,keep=1,', '')
    protocol = f'{dtype} {family}[{options}]'
    coordinates = [(-2, 1), (-2, 0), (-1, 1), (-1, 0)]
    shape = '[-2:-1][2]'
    ports = f'input wire clk,input wire reset,input {protocol} fifo[3] s{shape},output {protocol} skid m{shape}'
    if pipeline_exit:
        assert mode in ('direct','aggregate','projection','parameter','fabric_child')
        ports+=',input logic pause'
    if mode in ('generate_assign', 'generate_assign_implicit', 'generate_assign_named', 'generate_assign_child', 'generate_comb', 'generate_comb_implicit', 'generate_comb_child', 'generate_constants', 'generate_elastic', 'generate_parameters', 'generate_parameters_positional', 'generate_parameters_typed', 'generate_parameters_nested', 'scope_constants', 'scope_elastic'):
        ports += f',output uint[8] wire tags{shape}'
        if mode in ('generate_elastic','scope_elastic'): ports += f',output bit wire tag_valid{shape}'
    if requested_mode == 'retained_external': ports += f',output uint[8] wire tags{shape}'
    assignments = []
    for a, b in coordinates:
        for channel in fields_by_channel:
            member = 'beat' if channel == 't' else channel
            source, destination = ('m', 's') if channel in ('b', 'r') else ('s', 'm')
            assignments.append(f'{destination}[{a}][{b}]' + ('' if channel == 't' else f'.{member}') + f' <= {source}[{a}][{b}]' + ('' if channel == 't' else f'.{member}') + ';')
    body = 'always @(posedge clk) if (!reset) begin\n' + '\n'.join(assignments) + '\nend\n'
    pipeline_types=''
    if clocked_foreach:
        statements=[]
        for channel in fields_by_channel:
            member='beat' if channel=='t' else channel
            incoming,outgoing=('m','s') if channel in ('b','r') else ('s','m')
            statement=f'{outgoing}[i][j].{member}<={incoming}[i][j].{member};'
            if pipeline_exit:
                typename=f'pipeline_record_{channel}'
                fields=fields_by_channel[channel]
                pipeline_types+='typedef struct packed {'+''.join(f'bit[{bits-1}:0] {field};' for field,bits in reversed(fields))+'} '+typename+';\n'
                width=sum(bits for _,bits in fields)
                statement=(f'pipeline packet_{channel} begin {typename} first;{typename} second;'
                           f'stage begin first<={typename}\'({incoming}[i][j].{member});end '
                           'stage begin second<=first;end yield second;endpipeline '
                           f'{outgoing}[i][j].{member}<=uint[{width}]\'(packet_{channel});')
            if clocked_foreach=='qualified':
                statement=f'unique case(i) -2:begin {statement} end default:begin {statement} end endcase'
            statements.append(statement)
        if pipeline_exit:
            exit_kind=clocked_foreach.removeprefix('pipeline_').removesuffix('_after')
            exit_statement='if(pause)'+('goto idle;' if exit_kind=='goto' else exit_kind+';')
            if clocked_foreach.endswith('_after'):statements.append(exit_statement)
            else:statements.insert(0,exit_statement)
        iteration=('foreach(s[i]) foreach(s[,j])' if clocked_foreach=='nested' else 'foreach(s[i,j])')
        if clocked_foreach=='exits':
            first_channel=next(iter(fields_by_channel))
            member='beat' if first_channel=='t' else first_channel
            field=fields_by_channel[first_channel][0][0]
            iteration=('foreach(s[i,j]) foreach(s[k])')
            statements.insert(0,f'if(k==-2 && peek(s[i][j].{member}.{field})[0])continue;')
            statements.append('break;')
        loop=iteration+' begin\n'+'\n'.join(statements)+'\nend\n'
        body=('fsm @(posedge clk) begin if(reset)goto run;state run:begin '+loop+
              ('end state idle:goto run;end\n' if pipeline_exit else 'end end\n')
              if clocked_foreach=='fsm' or (pipeline_exit and exit_kind=='goto') else 'always @(posedge clk)if(!reset) '+loop)
    source = pipeline_types+f'module native_array({ports});\n{body}endmodule\n'
    if mode.startswith('generate_'):
        lines = []
        for channel in fields_by_channel:
            member = 'beat' if channel == 't' else channel
            incoming, outgoing = ('m', 's') if channel in ('b', 'r') else ('s', 'm')
            lines.append(f'{outgoing}[a][b].{member} <= {incoming}[a][b].{member};')
        source = f'module native_array({ports});\n'
        if mode in ('generate_assign', 'generate_assign_implicit', 'generate_assign_named', 'generate_assign_child', 'generate_comb', 'generate_comb_implicit', 'generate_comb_child'):
            source += "wire [7:0] mapped; assign mapped=8'h55;\n"
        source += 'generate for(genvar a=-2;a<=-1;a++) begin : rows\n'
        source += 'for(genvar b=1;b>=0;b-=1) begin : columns\n'
        if mode in ('generate_comb','generate_comb_implicit','generate_comb_child'):
            if mode == 'generate_comb_child': source += 'logic [7:0] marker;\n'
            source += "always_comb begin localparam int W=8; typedef logic [W-1:0] word_t; word_t mapped; uint[8] alias; int offset; logic [7:0] iteration;\n"
            source += "offset=-1; mapped=reset ? 8'd0 : W'(a*4+b+offset+1);\n"
            source += "begin static word_t mapped; mapped=8'h5a; alias=uint[8]'(mapped ^ 8'h5a); end\n"
            source += "alias|=uint[8]'(mapped); ++alias; alias--;\n"
            source += "iteration=0; for(int iteration=0;iteration<2;iteration++) begin alias^=8\'h5a; end repeat(2) alias^=8\'h33;\n"
            source += "for(iteration=0;iteration<4;iteration++) begin if(iteration==1) continue; if(iteration==3) break; alias^=8'h17; end\n"
            source += "iteration=0; do begin iteration++; if(iteration==1) continue; alias^=8'h33; end while(iteration<3);\n"
            source += "iteration=0; forever begin iteration++; if(iteration==1) continue; if(iteration==4) break; alias^=8'h55; end\n"
            source += "unique casez(alias[1:0]) default: ; 2'b?1: begin alias[0]=0; alias|=8'd1; end endcase\n"
            source += "priority case(alias[1:0]) 2'b00,2'b10: begin alias^=8'h5a; alias^=8'h5a; end default ; endcase\n"
            source += "unique0 case(alias[0]) 1'b0,1'b1: ; endcase\n"
            source += "unique if(alias[0]) alias[0]=1; else if(!alias[0]) alias[0]=0; else alias[0]=0;\n"
            source += "unique0 if(alias[0]) alias[0]=1; else begin if(!alias[0]) alias[0]=0; end\n"
            source += "priority if(alias[0]) alias[0]=1; else alias[0]=0;\n"
            source += "{marker,leaf.tag}={8'hca,alias};\n" if mode == 'generate_comb_child' else "tags[a][b]=uint[8]'(alias);\n"
            source += 'end\n'
        elif mode in ('generate_assign', 'generate_assign_implicit', 'generate_assign_named', 'generate_assign_child'):
            if mode == 'generate_assign_named': source += 'if(1) begin : wiring\n'
            size = "SZ" if mode == 'generate_assign_named' else "(a+10)" if mode == 'generate_assign_implicit' else "8"
            if mode == 'generate_assign_named': source += 'localparam SZ=a+10;\n'
            source += f"wire [7:0] mapped, alias; assign mapped=reset ? 8'd0 : {size}'(a*4+b), alias=mapped ^ 8'h5a;\n"
            if mode == 'generate_assign_named': source += 'end\n'
            alias = 'wiring.alias' if mode == 'generate_assign_named' else 'alias'
            if mode == 'generate_assign_child':
                source += "wire [7:0] marker; assign {marker,leaf.tag}={8'hca,mapped};\n"
            else:
                source += f"assign {{tags[a][b][7:6],tags[a][b][5:4]}}={alias}[7:4] ^ 4'h5; assign {{tags[a][b][3:2],tags[a][b][1:0]}}={alias}[3:0] ^ 4'ha;\n"
            source += 'if(0) begin wire [7:0] unused; assign unused=missing_name; end\n'
        if mode in ('generate_assign_child','generate_comb_child'):
            source += 'scalar_leaf leaf(.clk(clk),.reset(reset),.s(s[a][b]),.m(m[a][b]),.seen(tags[a][b]));\n'
        elif mode in ('generate_instance', 'generate_siblings', 'generate_child_fabric', 'generate_projections', 'generate_constants', 'generate_elastic', 'generate_signed', 'generate_packed', 'generate_parameters', 'generate_parameters_positional', 'generate_parameters_typed', 'generate_parameters_nested'):
            controls = '.clk(clk),.reset(reset)'
            if mode in ('generate_instance', 'generate_constants', 'generate_elastic', 'generate_signed', 'generate_packed', 'generate_parameters', 'generate_parameters_positional', 'generate_parameters_typed', 'generate_parameters_nested'):
                extra = ",.tag(uint[8]'(a*4+b)),.seen(tags[a][b])" if mode == 'generate_constants' else ',.seen(tags[a][b])' if mode.startswith('generate_parameters') else ''
                if mode in ('generate_elastic','scope_elastic'):
                    source += "uint[8] buf token; always @(posedge clk) if(!reset) token <= uint[8]'(a*4+b);\n"
                    extra = ',.extra_in(token)'
                configuration = '#(.TAG(a*4+b),.W(32)) ' if mode.startswith('generate_parameters') else ''
                if mode == 'generate_parameters_typed': configuration = "#(.TAG(int[8]'(256+a*4+b)),.W(32)) "
                if mode == 'generate_parameters_positional': configuration = '#(a*4+b,,32) '
                source += f'scalar_leaf {configuration}leaf({controls},.s(s[a][b]),.m(m[a][b]){extra});\n'
                if mode in ('generate_elastic','scope_elastic'):
                    source += 'assign tags[a][b]=peek(leaf.extra_out); assign tag_valid[a][b]=valid(leaf.extra_out);\n'
            elif mode == 'generate_siblings':
                source += f'scalar_leaf second({controls},.s(first.m),.m(m[a][b]));\n'
                source += f'scalar_leaf first({controls},.s(s[a][b]));\n'
            elif mode == 'generate_child_fabric':
                source += f'scalar_leaf second({controls},.m(m[a][b]));\n'
                source += f'scalar_leaf first({controls},.s(s[a][b]));\n'
                source += 'fabric begin first.m -> second.s; endfabric\n'
            else:
                source += f'scalar_leaf leaf({controls});\n'
                transfers = []
                for channel in fields_by_channel:
                    member = 'beat' if channel == 't' else channel
                    incoming, outgoing = ('m', 's') if channel in ('b', 'r') else ('s', 'm')
                    transfers += [f'leaf.{incoming}.{member} <= {incoming}[a][b].{member};',
                                  f'{outgoing}[a][b].{member} <= leaf.{outgoing}.{member};']
                source += 'always @(posedge clk) if(!reset) begin\n' + '\n'.join(transfers) + '\nend\n'
        elif mode == 'generate_fabric':
            source += 'fabric begin s[a][b] -> m[a][b]; endfabric\n'
        else:
            body = 'always @(posedge clk) if(!reset) begin\n' + '\n'.join(lines) + '\nend\n'
            source += 'if(a == -2 && b == 1) begin : selected\n' + body + 'end else begin : other\n' + body + 'end\n'
        source += 'if(0) begin : excluded uint[999999999] axi4_stream dead; native_array #(.INVALID(missing)) recursive_child(.clk(clk),.reset(reset)); end\n'
        source += 'end end endgenerate\nendmodule\n'
        if mode in ('generate_assign_implicit','generate_comb_implicit'):
            source = source.replace('generate for(', 'for(', 1).replace('endgenerate', '', 1)
        if mode in ('generate_comb_child','generate_assign_child', 'generate_instance', 'generate_siblings', 'generate_child_fabric', 'generate_projections', 'generate_constants', 'generate_elastic', 'generate_signed', 'generate_packed', 'generate_parameters', 'generate_parameters_positional', 'generate_parameters_typed', 'generate_parameters_nested'):
            leaf_ports = f'input wire clk,input wire reset,input {protocol} s,output {protocol} m'
            if mode in ('generate_constants','generate_assign_child','generate_comb_child'): leaf_ports += ',input uint[8] wire tag,output uint[8] wire seen'
            if mode.startswith('generate_parameters'): leaf_ports += ',output uint[8] wire seen'
            if mode in ('generate_elastic','scope_elastic'): leaf_ports += ',input uint[8] buf extra_in,output uint[8] buf extra_out'
            configuration = '#(parameter TAG=0,parameter NEXT=TAG+1,parameter W=32,parameter DEPTH=2)' if mode.startswith('generate_parameters') else ''
            if mode == 'generate_parameters_typed': configuration = configuration.replace('parameter TAG=0', 'parameter int[8] TAG=0')
            if mode.startswith('generate_parameters'): leaf_ports = leaf_ports.replace('uint[32]', 'uint[W]')
            leaf = f'module scalar_leaf {configuration}({leaf_ports});\n'
            if mode.startswith('generate_parameters'): leaf += "assign seen=uint[8]'(NEXT-1);\n"
            if mode in ('generate_constants','generate_assign_child','generate_comb_child'): leaf += 'assign seen=tag;\n'
            leaf += 'always @(posedge clk) if(!reset) begin\n'
            if mode in ('generate_elastic','scope_elastic'): leaf += 'extra_out <= extra_in;\n'
            for channel in fields_by_channel:
                member = 'beat' if channel == 't' else channel
                incoming, outgoing = ('m', 's') if channel in ('b', 'r') else ('s', 'm')
                leaf += f'{outgoing}.{member} <= {incoming}.{member};\n'
            leaf += 'end\nendmodule\n'
            if mode == 'generate_parameters_nested':
                helper = leaf.replace('module scalar_leaf ', 'module nested_helper ')
                leaf = f'module scalar_leaf {configuration}({leaf_ports});\n'
                leaf += 'nested_helper #(.TAG(NEXT-1),.W(W)) core(.clk(clk),.reset(reset),.s(s),.m(m),.seen(seen)); endmodule\n'
                leaf += helper
            source += leaf

        if mode == 'generate_shadow':
            source = source.replace('module native_array(', 'module shadow_bank #(parameter a=49,parameter b=24)(')
            source = source.replace('uint[32]', 'uint[a+15]').replace('addr=40', 'addr=b+17').replace('user=7', 'user=b-16')
            source += f'module native_array({ports}); shadow_bank #(.a(17),.b(23)) bank(.clk(clk),.reset(reset),.s(s),.m(m)); endmodule\n'
    elif mode.startswith('fabric_'):
        arrow = '>' if mode == 'fabric_direct' else '->'
        if mode == 'fabric_child':
            source = source.replace('module native_array(', 'module array_leaf(')
            source += f'module native_array({ports});\n'
            source += 'array_leaf second(.clk(clk),.reset(reset),.m(m));\n'
            source += 'array_leaf first(.clk(clk),.reset(reset),.s(s));\n'
            left, right = 'first.m', 'second.s'
        else:
            source = f'module native_array({ports});\n'
            left, right = 's', 'm'
        source += 'fabric begin\n'
        for a, b in coordinates:
            source += f'{left}[{a}][{b}] {arrow} {right}[{a}][{b}];\n'
        source += 'endfabric\nendmodule\n'
    elif mode == 'projection':
        source = source.replace('module native_array(', 'module array_leaf(')
        parent = []
        for a, b in coordinates:
            for channel in fields_by_channel:
                member = 'beat' if channel == 't' else channel
                source_name, destination = ('m', 's') if channel in ('b', 'r') else ('s', 'm')
                parent += [f'leaf.{source_name}[{a}][{b}].{member} <= {source_name}[{a}][{b}].{member};',
                           f'{destination}[{a}][{b}].{member} <= leaf.{destination}[{a}][{b}].{member};']
        source += f'module native_array({ports});\narray_leaf leaf(.clk(clk),.reset(reset));\n'
        source += 'always @(posedge clk) if(!reset) begin\n' + '\n'.join(parent) + '\nend\nendmodule\n'
    elif mode in ('aggregate', 'positional', 'siblings', 'parameter'):
        source = source.replace('module native_array(', 'module array_leaf(')
        source += f'module native_array({ports});\n'
        if mode == 'parameter':
            source = source.replace('module array_leaf(', 'module array_leaf #(parameter LANES=3)(', 1)
            first, second = source.split('module native_array(', 1)
            source = first.replace('[-2:-1][2]', '[-2:-1][LANES]') + 'module native_array(' + second
            source += 'array_leaf #(.LANES(2)) leaf(.clk(clk),.reset(reset),.s(s),.m(m));\n'
        elif mode == 'aggregate':
            source += 'array_leaf leaf(.clk(clk),.reset(reset),.s(s),.m(m));\n'
        elif mode == 'positional':
            source += 'array_leaf leaf(clk,reset,s,m);\n'
        else:
            source += 'array_leaf second(.clk(clk),.reset(reset),.s(first.m),.m(m));\n'
            source += 'array_leaf first(.clk(clk),.reset(reset),.s(s));\n'
        source += 'endmodule\n'
    elif mode == 'scalar_binding':
        scalar_assignments = []
        for channel in fields_by_channel:
            member = 'beat' if channel == 't' else channel
            incoming, outgoing = ('m', 's') if channel in ('b', 'r') else ('s', 'm')
            scalar_assignments.append(f'{outgoing}.{member} <= {incoming}.{member};')
        source = f'module scalar_leaf(input wire clk,input wire reset,input {protocol} s,output {protocol} m);\n'
        source += 'always @(posedge clk) if(!reset) begin\n' + '\n'.join(scalar_assignments) + '\nend\nendmodule\n'
        source += f'module native_array({ports});\n'
        for lane, (a, b) in enumerate(coordinates):
            source += f'scalar_leaf lane{lane}(.clk(clk),.reset(reset),.s(s[{a}][{b}]),.m(m[{a}][{b}]));\n'
        source += 'endmodule\n'
    if mode.startswith('scope_'):
        source = f'module native_array({ports});\n'
        source += 'generate for(genvar a=-2;a<=-1;a++) begin : rows for(genvar b=1;b>=0;b--) begin : columns\n'
        if mode == 'scope_fabric':
            source += 'scalar_leaf first(.clk(clk),.reset(reset),.s(s[a][b])); scalar_leaf second(.clk(clk),.reset(reset),.m(m[a][b]));\n'
        elif mode == 'scope_elastic':
            source += 'uint[8] buf token; scalar_leaf child(.clk(clk),.reset(reset),.extra_in(token));\n'
        elif mode == 'scope_block':
            source += 'if(b==1) begin : active scalar_leaf child(.clk(clk),.reset(reset)); end else begin : active scalar_leaf child(.clk(clk),.reset(reset)); end\n'
        else: source += 'scalar_leaf child(.clk(clk),.reset(reset));\n'
        if mode == 'scope_constants': source += 'localparam TAG=a*4+b;\n'
        source += 'end end endgenerate\n'
        transfers = []
        for lane,(a,b) in enumerate(coordinates):
            path = f'rows[{a}].columns[{b}]'
            if mode == 'scope_block': path += '.active'
            if mode == 'scope_elastic':
                transfers.append(f"{path}.token <= uint[8]'(rows[{a}].a*4+{path}.b);")
                source += f'assign tags[{a}][{b}]=peek({path}.child.extra_out); assign tag_valid[{a}][{b}]=valid({path}.child.extra_out);\n'
            if mode == 'scope_constants':
                source += f"assign tags[{a}][{b}]=uint[8]'({path}.b==1 ? {path}.TAG : rows[{a}].a*4+{path}.b);\n"
            if mode == 'scope_fabric':
                source += f'fabric begin {path}.first.m -> {path}.second.s; endfabric\n'
                continue
            if mode in ('scope_binding', 'scope_array_binding'):
                output_binding = f',.m(m[{a}][{b}])' if mode == 'scope_binding' else ''
                source += f'scalar_leaf final_{lane}(.clk(clk),.reset(reset),.s({path}.child.m){output_binding});\n'
                if mode == 'scope_binding':
                    source += f'fabric begin s[{a}][{b}] -> {path}.child.s; endfabric\n'
                    continue
            for channel in fields_by_channel:
                member = 'beat' if channel == 't' else channel
                incoming,outgoing = ('m','s') if channel in ('b','r') else ('s','m')
                suffix = '[0]' if mode in ('scope_array', 'scope_array_binding') else ''
                if mode == 'scope_array_binding':
                    if channel in ('b','r'):
                        transfers += [f'final_{lane}.m[0].{member} <= m[{a}][{b}].{member};',f's[{a}][{b}].{member} <= {path}.child.s[0].{member};']
                    else:
                        transfers += [f'{path}.child.s[0].{member} <= s[{a}][{b}].{member};',f'm[{a}][{b}].{member} <= final_{lane}.m[0].{member};']
                else:
                    transfers += [f'{path}.child.{incoming}{suffix}.{member} <= {incoming}[{a}][{b}].{member};',
                                  f'{outgoing}[{a}][{b}].{member} <= {path}.child.{outgoing}{suffix}.{member};']
        if mode == 'scope_relative':
            transfers = []
            for channel in fields_by_channel:
                member = 'beat' if channel == 't' else channel
                incoming,outgoing = ('m','s') if channel in ('b','r') else ('s','m')
                transfers += [f'rows[u].columns[v].child.{incoming}.{member} <= {incoming}[u][v].{member};',
                              f'{outgoing}[u][v].{member} <= rows[u].columns[v].child.{outgoing}.{member};']
            source += 'generate for(genvar u=-2;u<=-1;u++) begin : routes for(genvar v=1;v>=0;v--) begin : cells\n'
            source += 'always @(posedge clk) if(!reset) begin\n' + '\n'.join(transfers) + '\nend end end endgenerate\n'
        elif transfers: source += 'always @(posedge clk) if(!reset) begin\n' + '\n'.join(transfers) + '\nend\n'
        source += 'endmodule\n'
        shape_leaf = '[2]' if mode in ('scope_array','scope_array_binding') else ''
        extra_ports = ',input uint[8] buf extra_in,output uint[8] buf extra_out' if mode == 'scope_elastic' else ''
        source += f'module scalar_leaf(input wire clk,input wire reset,input {protocol} s{shape_leaf},output {protocol} m{shape_leaf}{extra_ports});\n'
        source += 'always @(posedge clk) if(!reset) begin\n'
        if mode == 'scope_elastic': source += 'extra_out <= extra_in;\n'
        for element in ('[0]','[1]') if shape_leaf else ('',):
            for channel in fields_by_channel:
                member = 'beat' if channel == 't' else channel
                incoming,outgoing = ('m','s') if channel in ('b','r') else ('s','m')
                source += f'{outgoing}{element}.{member} <= {incoming}{element}.{member};\n'
        source += 'end endmodule\n'
    if requested_mode in ('generate_external','generate_external_local','generate_external_reuse'):
        source = source.replace('for(genvar a=', 'for(a=').replace('for(genvar b=', 'for(b=')
        if requested_mode == 'generate_external_local':
            source = source.replace('generate for(a=', 'genvar a; generate for(a=',1)
            source = source.replace('begin : rows\n', 'begin : rows genvar b;\n',1)
        else:
            source = source.replace('generate for(a=', 'genvar a,b; generate for(a=',1)
        if requested_mode == 'generate_external_reuse':
            source = source.replace('for(genvar u=', 'for(u=').replace('for(genvar v=', 'for(v=')
            source = re.sub(r'\bu\b','a',source)
            source = re.sub(r'\bv\b','b',source)
        mode = requested_mode
    if requested_mode == 'generate_external_shadow':
        source = source.replace('generate for(genvar a=', 'generate begin : configuration genvar a,b; for(a=',1)
        source = source.replace('for(genvar b=', 'for(b=',1)
        source = source.replace('end end endgenerate', 'end end end endgenerate',1)
        mode = requested_mode
    if requested_mode == 'retained_external':
        retained = "genvar a,b; generate for(a=-2;a<=-1;a++) begin : tags_rows for(b=1;b>=0;b--) begin : tags_columns assign tags[a][b]=8'(a*4+b); end end endgenerate\n"
        source = source.replace('endmodule',retained+'endmodule',1)
        mode = requested_mode
    selection_modes = ('array_row_binding','array_slice_binding','array_slice_up','array_slice_down',
                       'array_column_binding','array_rebased_binding','array_slice_fabric','array_proper_slice','array_row_generate','array_slice_generate',
                       'array_concat_scalar','array_concat_mixed','array_concat_rows','array_concat_child',
                       'array_pattern_scalar','array_pattern_rows','array_pattern_nested','array_pattern_generate',
                       'array_keyed_flat','array_keyed_nested','array_keyed_default','array_keyed_generate','array_default_scalar','array_default_subarray','array_default_generate',
                       'array_repeat_scalar','array_repeat_rows','array_repeat_nested','array_repeat_generate','array_fill_slice','array_replicated_slice','array_empty_slice','array_query_slice')
    if authored_mode in selection_modes:
        def selected_leaf(name, dimensions, coords):
            leaf = f'module {name}(input wire clk,input wire reset,input {protocol} s{dimensions},output {protocol} m{dimensions}); always @(posedge clk) if(!reset) begin\n'
            for coordinate in coords:
                suffix = ''.join(f'[{i}]' for i in coordinate)
                for ch in fields_by_channel:
                    member = 'beat' if ch=='t' else ch
                    incoming,outgoing = ('m','s') if ch in ('b','r') else ('s','m')
                    leaf += f'{outgoing}{suffix}.{member} <= {incoming}{suffix}.{member};\n'
            return leaf + 'end endmodule\n'
        if authored_mode in ('array_fill_slice','array_replicated_slice','array_empty_slice','array_query_slice'):
            source=selected_leaf('selected_leaf','[8:9]',[(8,),(9,)])
            source+=f'module native_array({ports}); '
            for name,row in (('first',"$signed(~('1 & 32'd1))"),('second',"$signed('1)")):
                source+=f"selected_leaf {name}(.clk(clk),.reset(reset),.s(s[{row}][$unsigned('1):'0]),.m(m[{row}][$unsigned('1):'0])); "
            source+='endmodule\n'
            if authored_mode=='array_query_slice':
                source=source.replace("$signed(~('1 & 32'd1))","$left(s,1)").replace("$signed('1)","$right(s,1)").replace("$unsigned('1):'0","($size(s[$left(s,1)],1)-1):$right(s,2)")
            if authored_mode=='array_empty_slice':
                source=source.replace("$signed(~('1 & 32'd1))","$signed({{0{8'hff}},3'b110})").replace("$signed('1)","$signed({3'b111,{0{1'bx}}})").replace("$unsigned('1):'0","{1'b1,{0{8'h00}}}:{{0{8'hff}},1'b0}")
            if authored_mode=='array_replicated_slice':
                source=source.replace("$signed(~('1 & 32'd1))","$signed({{2{1'b1}},1'b0})").replace("$signed('1)","$signed({3{'1}})").replace("$unsigned('1):'0","{1{{1{1'b1}}}}:({2{1'b1}} ^ 2'b11)")
        elif authored_mode.startswith('array_repeat_'):
            generated=authored_mode=='array_repeat_generate'
            scalar=authored_mode=='array_repeat_scalar' or generated
            dims,coords=('[8:11]',[(i,) for i in range(8,12)]) if scalar and not generated else ('[8:9]',[(8,),(9,)]) if generated else ('[8:9][5:4]',[(8,5),(8,4),(9,5),(9,4)])
            source=selected_leaf('selected_leaf',dims,coords)
            source+=f'module native_array({ports}); localparam ONCE=1; '
            if generated: source+='for(genvar row=-2;row<=-1;row++) begin : rows '
            def construction(port):
                if generated: items=f'{port}[row][1],{port}[row][0]'
                elif scalar: items=','.join(f'{port}[{a}][{b}]' for a,b in coordinates)
                elif authored_mode=='array_repeat_rows': items=f'{port}[-2],{port}[-1]'
                else: items=','.join("'{ONCE{"+f'{port}[{a}][1],{port}[{a}][0]'+'}}' for a in (-2,-1))
                return "'{(ONCE+0){"+items+'}}'
            source+=f'selected_leaf child(.clk(clk),.reset(reset),.s({construction("s")}),.m({construction("m")})); '
            if generated: source+='end '
            source+='endmodule\n'
        elif authored_mode in ('array_default_scalar','array_default_generate'):
            source=selected_leaf('selected_leaf','[8:9][5:5]',[(8,5),(9,5)])
            source+=f'module native_array({ports}); '
            generated=authored_mode=='array_default_generate'
            if generated: source+='localparam BASE=8; for(genvar col=1;col>=0;col--) begin : columns '
            for col in (('col',) if generated else (1,0)):
                def construction(port):
                    return "'{"+('BASE' if generated else '8')+":'{"+f'{port}[-2][{col}]'+"},default:("+f'{port}[-1][{col}]'+')}'
                source+=f'selected_leaf col{col}(.clk(clk),.reset(reset),.s({construction("s")}),.m({construction("m")})); '
            if generated: source+='end '
            source+='endmodule\n'
        elif authored_mode == 'array_default_subarray':
            source=selected_leaf('selected_leaf','[8:8][5:5][3:4]',[(8,5,3),(8,5,4)])
            source+=f'module native_array({ports}); '
            for row in (-2,-1):
                def construction(port): return "'{default:"+f'{port}[{row}]'+'}'
                source+=f'selected_leaf row{abs(row)}(.clk(clk),.reset(reset),.s({construction("s")}),.m({construction("m")})); '
            source+='endmodule\n'
        elif authored_mode in ('array_keyed_flat','array_keyed_nested','array_keyed_default'):
            nested = authored_mode == 'array_keyed_nested'
            dims,coords = ('[-8:-7][5:4]',[(-8,5),(-8,4),(-7,5),(-7,4)]) if nested else ('[8:11]',[(i,) for i in range(8,12)])
            source = selected_leaf('selected_leaf',dims,coords)
            def construction(port):
                if nested:
                    return "'{-7:'{4:" + f'{port}[-1][0],5:{port}[-1][1]' + "},-8:'{4:" + f'{port}[-2][0],5:{port}[-2][1]' + '}}'
                entries = [f'{8+i}:{port}[{a}][{b}]' for i,(a,b) in enumerate(coordinates)]
                if authored_mode == 'array_keyed_default': entries[3] = f'default:{port}[-1][0]'
                return "'{" + ','.join(entries[i] for i in (3,1,0,2)) + '}'
            source += f'module native_array({ports}); selected_leaf child(.clk(clk),.reset(reset),.s({construction("s")}),.m({construction("m")})); endmodule\n'
        elif authored_mode == 'array_keyed_generate':
            source = selected_leaf('selected_leaf','[8:9]',[(8,),(9,)])
            source += f'module native_array({ports}); localparam BASE=8; for(genvar row=-2;row<=-1;row++) begin : rows '
            source += "selected_leaf child(.clk(clk),.reset(reset),.s('{BASE+1:s[row][0],BASE:s[row][1]}),.m('{BASE+1:m[row][0],BASE:m[row][1]})); end endmodule\n"
        elif authored_mode in ('array_pattern_scalar','array_pattern_rows','array_pattern_nested'):
            rows = authored_mode != 'array_pattern_scalar'
            dims,coords = ('[8:9][5:4]',[(8,5),(8,4),(9,5),(9,4)]) if rows else ('[8:11]',[(i,) for i in range(8,12)])
            source = selected_leaf('selected_leaf',dims,coords)
            def construction(port):
                items = [f'{port}[{a}][{b}]' for a,b in coordinates]
                if rows: items = [f'{port}[-2]',f'{port}[-1]']
                if authored_mode == 'array_pattern_nested':
                    items = ["'{" + f'{port}[{a}][1],{port}[{a}][0]' + '}' for a in (-2,-1)]
                return "'{" + ','.join(items) + '}'
            source += f'module native_array({ports}); selected_leaf child(.clk(clk),.reset(reset),.s({construction("s")}),.m({construction("m")})); endmodule\n'
        elif authored_mode == 'array_pattern_generate':
            source = selected_leaf('selected_leaf','[8:9]',[(8,),(9,)])
            source += f'module native_array({ports}); for(genvar row=-2;row<=-1;row++) begin : rows '
            source += "selected_leaf child(.clk(clk),.reset(reset),.s('{s[row][1],s[row][0]}),.m('{m[row][1],m[row][0]})); end endmodule\n"
        elif authored_mode in ('array_concat_scalar','array_concat_mixed','array_concat_rows'):
            rows = authored_mode == 'array_concat_rows'
            dims,coords = ('[8:9][5:4]',[(8,5),(8,4),(9,5),(9,4)]) if rows else ('[8:11]',[(i,) for i in range(8,12)])
            source = selected_leaf('selected_leaf',dims,coords)
            def construction(port):
                items = [f'{port}[{a}][{b}]' for a,b in coordinates]
                if rows: items = [f'{port}[-2]',f'{port}[-1]']
                if authored_mode == 'array_concat_mixed': items = [f'{port}[-2][1]',f'{port}[-2][0:0]',f'{port}[-1]']
                return '{' + ','.join(items) + '}'
            source += f'module native_array({ports}); selected_leaf child(.clk(clk),.reset(reset),.s({construction("s")}),.m({construction("m")})); endmodule\n'
        elif authored_mode == 'array_concat_child':
            source = selected_leaf('selected_leaf','[8:11]',[(i,) for i in range(8,12)])
            source += f'module native_array({ports}); selected_leaf first(.clk(clk),.reset(reset)); selected_leaf last(.clk(clk),.reset(reset),.s({{first.m[8:9],first.m[10],first.m[11]}})); '
            source += 'fabric begin ' + ' '.join(f's[{a}][{b}] > first.s[{8+i}]; last.m[{8+i}] > m[{a}][{b}];' for i,(a,b) in enumerate(coordinates))
            source += 'endfabric endmodule\n'
        elif authored_mode == 'array_rebased_binding':
            source = selected_leaf('selected_leaf','[8:9][5:4]',[(8,5),(8,4),(9,5),(9,4)])
            source += f'module native_array({ports}); selected_leaf child(.clk(clk),.reset(reset),.s(s),.m(m)); endmodule\n'
        elif authored_mode == 'array_slice_fabric':
            source = f'module native_array({ports}); fabric begin '
            source += ' '.join(f's[{a}][1:0] -> m[{a}][1:0];' for a in (-2,-1)) + 'endfabric endmodule\n'
        elif authored_mode == 'array_proper_slice':
            source = selected_leaf('wide_leaf','[9:6]',[(9,),(8,),(7,),(6,)])
            source += selected_leaf('selected_leaf','[8:9]',[(8,),(9,)])
            source += f'module native_array({ports}); wide_leaf first(.clk(clk),.reset(reset)); wide_leaf last(.clk(clk),.reset(reset)); '
            for k,selection in enumerate(('9:8','7:6')):
                source += f'selected_leaf mid{k}(.clk(clk),.reset(reset),.s(first.m[{selection}]),.m(last.s[{selection}])); '
            source += 'fabric begin ' + ' '.join(f's[{a}][{b}] > first.s[{9-i}]; last.m[{9-i}] > m[{a}][{b}];' for i,(a,b) in enumerate(coordinates))
            source += 'endfabric endmodule\n'
        else:
            source = selected_leaf('selected_leaf','[8:9]',[(8,),(9,)])
            source += f'module native_array({ports}); '
            if authored_mode in ('array_row_generate','array_slice_generate'):
                tail = '[0+:2]' if authored_mode == 'array_slice_generate' else ''
                source += f'for(genvar row=-2;row<=-1;row++) begin : rows selected_leaf child(.clk(clk),.reset(reset),.s(s[row]{tail}),.m(m[row]{tail})); end '
            elif authored_mode == 'array_column_binding':
                for b in (1,0):
                    source += f'selected_leaf col{b}(.clk(clk),.reset(reset),.s(s[-2:-1][{b}]),.m(m[-2:-1][{b}])); '
            else:
                tail = {'array_row_binding':'','array_slice_binding':'[1:0]','array_slice_up':'[0+:2]','array_slice_down':'[1-:2]'}[authored_mode]
                for i,a in enumerate((-2,-1)):
                    source += f'selected_leaf row{i}(.clk(clk),.reset(reset),.s(s[{a}]{tail}),.m(m[{a}]{tail})); '
            source += 'endmodule\n'
    if authored_mode in ('fabric_array_direct','fabric_array_routed','fabric_array_child','fabric_array_generated','fabric_array_scoped_child','fabric_array_standalone'):
        arrow = '>' if authored_mode == 'fabric_array_direct' else '->'
        source = f'module native_array({ports}); fabric begin s {arrow} m; endfabric endmodule\n'
        if authored_mode == 'fabric_array_generated':
            source = f'module native_array({ports}); if(1) begin : selected fabric begin s -> m; endfabric end endmodule\n'
        if authored_mode in ('fabric_array_child','fabric_array_scoped_child'):
            source = f'module leaf({ports}); fabric begin s -> m; endfabric endmodule\n'
            source += f'module native_array({ports}); leaf child(.clk(clk),.reset(reset)); fabric begin s -> child.s; child.m -> m; endfabric endmodule\n'
            if authored_mode == 'fabric_array_scoped_child':
                source = source.replace('leaf child(.clk(clk),.reset(reset)); fabric', 'for(genvar k=0;k<1;k++) begin : banks leaf child(.clk(clk),.reset(reset)); end fabric')
                source = source.replace('s -> child.s; child.m -> m;', 's -> banks[0].child.s; banks[0].child.m -> m;')
        if authored_mode == 'fabric_array_standalone':
            source = f'fabric array_router({ports}) begin s -> m; endfabric\n'
            source += f'module native_array({ports}); array_router child(.clk(clk),.reset(reset),.s(s),.m(m)); endmodule\n'

    if authored_mode in ('generate_pipeline', 'generate_pipeline_implicit', 'generate_pipeline_hierarchy'):
        source = f'module native_array({ports}); generate for(genvar a=-2;a<=-1;a++) begin : rows '
        source += 'for(genvar b=1;b>=0;b--) begin : columns localparam DATA_W=32;\n'
        for ch, fields in fields_by_channel.items():
            member = 'beat' if ch == 't' else ch
            incoming, outgoing = ('m','s') if ch in ('b','r') else ('s','m')
            declarations = ''.join(f'bit [{"DATA_W-1" if field == "data" else "DATA_W/8-1" if field in ("strb","keep") else bits-1}:0] {field};' for field,bits in reversed(fields))
            source += f'typedef struct packed {{{declarations}}} record_{ch};\n'
            source += f'always @(posedge clk) begin {qualifier+" " if qualifier else ""}if(reset) pipeline_reset(pipe_{ch}); else begin '
            source += f'pipeline pipe_{ch} begin record_{ch} first; record_{ch} final; '
            source += f'stage begin first <= record_{ch}\'({incoming}[a][b].{member}); end '
            source += f"stage begin final <= record_{ch}'("+'{{0{first}},first}); end yield final; endpipeline '
            width = '+'.join('DATA_W' if field=='data' else 'DATA_W/8' if field in ('strb','keep') else str(bits) for field,bits in fields)
            source += f"{outgoing}[a][b].{member} <= uint[{width}]'(pipe_{ch}); end end\n"
        source += 'end end endgenerate endmodule\n'
        if authored_mode == 'generate_pipeline_implicit': source=re.sub(r'\bgenerate\b|\bendgenerate\b','',source)
        if authored_mode == 'generate_pipeline_hierarchy':
            source = source.replace(f'module native_array({ports});', f"module pipeline_leaf #(parameter DATA_W=64)({ports.replace('uint[32]','uint[DATA_W]')});",1)
            source = source.replace('localparam DATA_W=32;','')
            source += f'module native_array({ports}); pipeline_leaf #(.DATA_W(32)) child(.clk(clk),.reset(reset),.s(s),.m(m)); endmodule\n'

    if authored_mode in ('generate_fsm', 'generate_fsm_implicit'):
        def fsm_body(match):
            return ('uint[8] reg delay; fsm @(posedge clk) begin '
                    'if(reset) begin delay <= 0; goto waiting; end '
                    f'state waiting: {qualifier+" " if qualifier else ""}if(delay < b+1) delay <= delay+1; else goto running; '
                    'state running: begin\n' + match[1] + '\nend end\n')
        source, count = re.subn(r'always @\(posedge clk\) if\(!reset\) begin\n(.*?)\nend\n',
                                fsm_body, source, flags=re.S)
        assert count == 2
        if authored_mode == 'generate_fsm_implicit': source = re.sub(r'\bgenerate\b|\bendgenerate\b', '', source)
    if authored_mode in ('implicit_if','implicit_named_block'):
        wrapper = 'if(1) begin : enabled' if authored_mode == 'implicit_if' else 'begin : enabled'
        source = source.replace('generate for(genvar a=',wrapper+' for(genvar a=',1)
        tail = 'end else begin : disabled uint[999999999] axi4_stream dead; end' if authored_mode == 'implicit_if' else 'end'
        source = source.replace('end end endgenerate','end end '+tail+' endgenerate',1)
    if authored_mode == 'implicit_conditional_loop':
        source = source.replace('generate for(genvar a=', 'generate if(1) for(genvar a=', 1)
    if authored_mode == 'implicit_unbraced':
        source = source.replace('begin : rows\n','',1).replace('end end endgenerate','end endgenerate',1)
    if authored_mode in implicit_modes:
        source = re.sub(r'\bgenerate\b|\bendgenerate\b', '', source)
        # Keep the underlying scoreboard/observation mode, while printing the authored case name.
    if authored_mode in case_modes:
        child = next(line for line in source.splitlines() if line.startswith('scalar_leaf #('))
        dead = 'begin : unused uint[999999999] axi4_stream impossible; native_array #(.INVALID(missing)) recursive_child(.clk(clk),.reset(reset)); end'
        body = 'begin : selected ' + child + ' end'
        selector,labels = 'b','0,1'
        if authored_mode == 'case_generate_default':
            selection = f'case(b+8) default: {body} 0,1: {dead} endcase'
        elif authored_mode == 'case_generate_xz':
            selection = f"case(4'b10xz) 4'b10zx: {dead} 4'b10xz: {body} default: {dead} endcase"
        elif authored_mode == 'case_generate_signed':
            selection = f"case(8'shff) 32'shffffffff: {dead} 32'sh000000ff: {body} 8'd0: {dead} default: {dead} endcase"
        elif authored_mode == 'case_generate_wide':
            bits = '1' + '0'*190 + 'xz'
            selection = f"case(193'b{bits}) {{1'b1,190'b0,2'bxz}}: {body} default: {dead} endcase"
        elif authored_mode == 'case_generate_conditional':
            selection = f"case(b ? {{1'b1,190'b0,2'bxz}} : 193'b0) 193'b0, {{1'b1,190'b0,2'bxz}}: {body} default:{dead} endcase"
        elif authored_mode == 'case_generate_ternary':
            selection = f'case(b) b==0 ? 0 : 3, b==1 ? 1 : 4: {body} default {dead} endcase'
        elif authored_mode == 'case_generate_none':
            selection = child + f' case(5) 0,1: {dead} endcase'
        elif authored_mode == 'case_generate_priority':
            selection = f'case(b) default: {dead} 0,1: {body} 0,1: {dead} endcase'
        else:
            selection = f'case({selector}) default: {dead} {labels}: {body} endcase'
        oracle = selection.replace(body,'begin oracle=1; end').replace(dead,'begin oracle=0; end').replace(child,'oracle=1;')
        case_oracle = 'integer b,oracle; initial begin for(b=0;b<2;b++) begin oracle=-1; ' + oracle + '; if(oracle!==1) $fatal(1,"native SV case oracle disagrees with expected branch"); end end\n'
        source = source.replace(child,selection,1)
        if authored_mode == 'case_generate_implicit': source = re.sub(r'\bgenerate\b|\bendgenerate\b','',source)
    if authored_mode=='generate_static_init':
        data_member='beat' if family=='axi4_stream' else 'w'
        declarations="typedef struct packed {logic[3:0] hi;logic[3:0] lo;} init_t; static init_t record_seed='{hi:4'h5,lo:4'ha}; localparam uint[8] FIRST=90, SECOND=FIRST; localparam logic[7:0] COMPARE=(8'hff+8'h1)==8'h0 ? 8'h55 : 8'haa; static uint[8] seed=SECOND; "
        declarations+=f'static integer payload_bits=$bits(s[a][b].{data_member}.data);'
        source=source.replace('logic [7:0] iteration;','logic [7:0] iteration;'+declarations)
        source=source.replace("alias|=uint[8]'(mapped);", "alias|=uint[8]'(mapped); alias^=uint[8]'({record_seed.hi,record_seed.lo} ^ seed ^ COMPARE ^ 8'h55); alias+=uint[8]'(payload_bits-32);")
    if authored_mode=='generate_named_comb':
        source=source.replace('always_comb begin ', 'always_comb begin : observe ')
        source=source.replace("begin static word_t mapped; mapped=8'h5a; alias=uint[8]'(mapped ^ 8'h5a); end", "begin : sample static word_t mapped; mapped=8'h5a; alias=uint[8]'(sample.mapped ^ 8'h5a); end : sample")
        source=source.replace("alias|=uint[8]'(mapped);", "alias|=uint[8]'(mapped); alias^=uint[8]'(sample.mapped ^ 8'h5a);")
    if authored_mode in ('generate_foreach','generate_named_foreach'):
        source=source.replace('logic [7:0] iteration;', 'logic [7:0] iteration; logic [1:0][1:0] foreach_bits;')
        data_member='beat' if family=='axi4_stream' else 'w'
        loops="foreach_bits=0; foreach(foreach_bits[ii,jj]) alias^=uint[8]'(8'h5a); foreach(foreach_bits[,jj]) alias^=uint[8]'(8'h33); "
        loops+=f"foreach(s[a][b].{data_member}.data[ii,jj]) alias^=uint[8]'(8'h7e); "
        if authored_mode=='generate_named_foreach':
            loops=loops.replace(f"foreach(s[a][b].{data_member}.data[ii,jj]) alias^=uint[8]'(8'h7e);", f"foreach(s[a][b].{data_member}.data[ii,jj]) begin : sample static logic[7:0] zero=0; alias^=uint[8]'(8'h7e ^ sample.zero); end : sample")
        source=source.replace('iteration=0; for',loops+'iteration=0; for',1)
    if authored_mode in coordinate_modes:
        # Independent scoreboard still uses literal physical coordinates.
        if authored_mode.startswith('query_coordinate_'):
            source=re.sub(r'\b([sm])\[(-2|-1)\]\[(1|0)\]',
                          lambda match: match[1]+'['+('($dimensions(s)==2 ? $left(s,1) : 99)' if match[2]=='-2' else '($low(s,1)+$size(s,1)-1)')+']['+('($dimensions(s[$left(s,1)])==1 ? $left(s,$dimensions(s)) : 99)' if match[3]=='1' else '($dimensions(s[$left(s,1)][$left(s,2)])==0 ? $right(s,$unpacked_dimensions(s)) : 99)')+']',source)
        elif authored_mode.startswith('fill_coordinate_'):
            source=re.sub(r'\b([sm])\[(-2|-1)\]\[(1|0)\]',
                          lambda match: match[1]+'['+("$signed(~('1 & 32'd1))" if match[2]=='-2' else "$signed('1)")+']['+("$unsigned('1)" if match[3]=='1' else "$clog2('1)")+']',source)
        elif authored_mode.startswith('empty_coordinate_'):
            source=re.sub(r'\b([sm])\[(-2|-1)\]\[(1|0)\]',
                          lambda match: match[1]+'['+("$signed({{0{8'hff}},3'b110})" if match[2]=='-2' else "$signed({3'b111,{0{1'bx}}})")+']['+("{1'b1,{0{8'h00}}}" if match[3]=='1' else "{{0{8'hff}},1'b0}")+']',source)
        elif authored_mode.startswith('replicated_coordinate_'):
            source=re.sub(r'\b([sm])\[(-2|-1)\]\[(1|0)\]',
                          lambda match: match[1]+'['+("$signed({{2{1'b1}},1'b0})" if match[2]=='-2' else "$signed({3{'1}})")+']['+("{1{{1{1'b1}}}}" if match[3]=='1' else "({2{1'b1}} ^ 2'b11)")+']',source)
        else:
            source=re.sub(r'\b([sm])\[(-2|-1)\]\[(1|0)\]',
                          lambda match: match[1]+'[~'+('1' if match[2]=='-2' else '0')+'][~'+('(-2)' if match[3]=='1' else '(-1)')+']',source)
    type_definitions = ''
    if type_keys:
        native_alias = True
        if type_keys in ('record', 'record_alias'):
            type_definitions = ('typedef struct packed {bit[16] hi; bit[16] lo;} word_t;\n'
                                'typedef struct packed {bit[16] hi; bit[16] lo;} other_word_t;\n'
                                'typedef word_t copied_word_t;\n')
            replacement = protocol.replace(dtype, 'word_t')
            source = source.replace(protocol, replacement)
            protocol = replacement
        wrong_payload = {'state':'logic[31:0]', 'sign':'int[32]', 'shape':'bit[4][8]',
                         'bounds':'bit[0:31]', 'record':'other_word_t'}.get(type_keys)
        if wrong_payload:
            type_definitions += f'typedef {wrong_payload} {family}[{options}] wrong_t;\n'
        elif type_keys == 'contract':
            wrong_options = options.replace('id=4,dest=3', 'id=3,dest=4') if family == 'axi4_stream' else options.replace('addr=40', 'addr=41')
            type_definitions += f'typedef {dtype} {family}[{wrong_options}] wrong_t;\n'
        elif type_keys == 'ordinary':
            type_definitions += f'typedef {dtype} wrong_t;\n'
        elif type_keys == 'equivalent':
            type_definitions += f'typedef bit[32] {family}[{options}] equivalent_t;\n'
        elif type_keys == 'record_alias':
            type_definitions += f'typedef copied_word_t {family}[{options}] equivalent_t;\n'
        for port in ('s', 'm'):
            if type_keys == 'nested':
                source = re.sub(r'4:(' + port + r'\[-[12]\]\[0\])', r'copied_bus_t:\1', source)
            elif type_keys == 'descent':
                source = source.replace(f'default:({port}[-1]', f'copied_bus_t:({port}[-1]')
            elif type_keys == 'generated':
                source = source.replace(f'BASE+1:{port}[row][0]', f'copied_bus_t:{port}[row][0]')
            elif type_keys == 'covered':
                source = source.replace(f'.{port}(\'{{', f'.{port}(\'{{copied_bus_t:unresolved_endpoint,')
            else:
                value = f'{port}[-1][0]'
                key = 'equivalent_t' if type_keys in ('equivalent', 'record_alias') else 'copied_bus_t'
                entries = f'{key}:{value}'
                if type_keys == 'last': entries = 'native_bus_t:unresolved_endpoint,' + entries
                if type_keys == 'default': entries = 'default:unresolved_endpoint,' + entries
                if type_keys in ('state','sign','shape','bounds','record','contract','ordinary'):
                    entries += ',wrong_t:unresolved_endpoint'
                if type_keys == 'primitive': entries += ',bit:unresolved_endpoint'
                source = source.replace(f'11:{value}', entries)
    if native_alias:
        if mode.startswith('generate_parameters'):
            source = source.replace(protocol.replace(dtype, 'uint[W]'), 'copied_bus_t')
        source = (f'typedef {protocol} native_bus_t;\ntypedef native_bus_t copied_bus_t;\n' +
                  source.replace(protocol, 'copied_bus_t'))
    source = type_definitions + source
    if package:
        first = re.search(r'\b(?:module|fabric)\s+\w+', source).start()
        prefix, source = source[:first], source[first:]
        names = re.findall(r'}?\s+(\w+)\s*;', prefix)
        for name in names:
            if name.endswith('_t'):
                source = re.sub(r'\b'+name+r'\b', 'bus_defs::'+name, source)
        if package in ('import_unit','import_header'):
            source = source.replace('bus_defs::','')
        if package == 'import_unit':
            source = 'import bus_defs::*;\n' + source
        elif package == 'import_header':
            source = re.sub(r'\bmodule (\w+)\s*', lambda m: f'module {m[1]} import bus_defs::*; ', source)
        elif package == 'import_generated':
            source = re.sub(r'begin\s*:\s*(\w+)', r'begin : \1 import bus_defs::*; ', source)
            source = source.replace('bus_defs::copied_bus_t:', 'copied_bus_t:')
        exported = ''
        if package == 'export':
            members = ','.join('bus_defs::'+name for name in names if name.endswith('_t'))
            exported = 'package exported_buses; export *::*; import '+members+'; endpackage\n'
            source = source.replace('bus_defs::','exported_buses::')
        source = 'package bus_defs;\n' + prefix + 'endpackage\n' + exported + source
    if ordinary_package:
        source=ordinary_package_source(source,32,ordinary_package)
    foreign_sources = []
    if external:
        assert mode in ('aggregate', 'positional', 'projection', 'fabric_child', 'scalar_binding')
        child = 'scalar_leaf' if mode == 'scalar_binding' else 'array_leaf'
        source, replaced = re.subn(r'module ' + child + r'([^;]*);.*?endmodule',
                                   lambda m: 'extern module ' + child + m[1] + ';', source, count=1, flags=re.S)
        assert replaced == 1
        scalar = foreign_axi_module(directory, family, 32, 'buf', 'clk', 'reset', False, False)
        if mode == 'scalar_binding':
            scalar.write_text(scalar.read_text().replace('module native_leaf ', 'module scalar_leaf ', 1))
            foreign_sources = [scalar]
        else:
            pins = ['input wire clk', 'input wire reset']
            instances = []
            for lane, (a, b) in enumerate(coordinates):
                suffix = f'm{-a}_{b}'
                bindings = ['.clk(clk)', '.reset(reset)']
                for channel, fields in fields_by_channel.items():
                    incoming = 'm' if channel in ('b', 'r') else 's'
                    for endpoint in ('s', 'm'):
                        for field, bits in fields + [('valid', 1), ('ready', 1)]:
                            direction = 'input' if (endpoint == incoming) != (field == 'ready') else 'output'
                            pin = f'{endpoint}_{suffix}_{channel}{field}'
                            pins.append(f'{direction} wire [{bits-1}:0] {pin}')
                            bindings.append(f'.{endpoint}_{channel}{field}({pin})')
                instances.append(f'native_leaf lane{lane}(' + ','.join(bindings) + ');')
            ip = directory / 'foreign_array.sv'
            ip.write_text('module array_leaf(' + ','.join(pins) + ');\n' + '\n'.join(instances) + '\nendmodule\n')
            foreign_sources = [scalar, ip]
    pigen, sv, tb, sim = [directory / name for name in ('array.pigen', 'array.sv', 'tb.sv', 'sim')]
    if pipeline_exit:
        source=source.replace('(.clk(clk),.reset(reset),','(.clk(clk),.reset(reset),.pause(pause),')
    pigen.write_text(source)
    run([compiler if compiler else ROOT/'pigen', pigen, '-o', sv, '--no-diagram'], directory)
    run(['verilator', '--lint-only', '--top-module', 'native_array', sv, ROOT/'rtl/pigen_primitives.sv', *foreign_sources], directory)
    connections = ['.clk(clk)', '.reset(reset)']
    declarations, init, drive, check, clear, done, capacity = [], [], [], [], [], [], []
    if pipeline_exit:
        declarations.append('logic pause=1;integer pause_cycles=0;')
        connections.append('.pause(pause)')
        drive.append('pause_cycles++;pause=epoch==0||(pause_cycles%11)<4;')
    for lane, (a, b) in enumerate(coordinates):
        suffix = f'm{-a}_{b}'
        for channel, fields in fields_by_channel.items():
            tag = f'l{lane}_{channel}'
            incoming, outgoing = ('m', 's') if channel in ('b', 'r') else ('s', 'm')
            width = sum(bits for _, bits in fields)
            for endpoint in ('s', 'm'):
                for field, bits in fields + [('valid', 1), ('ready', 1)]:
                    pin = f'{endpoint}_{suffix}_{channel}{field}'
                    declarations.append(f'logic [{bits-1}:0] {pin};')
                    connections.append(f'.{pin}({pin})')
            def pin(endpoint, field):
                return f'{endpoint}_{suffix}_{channel}{field}'
            inp = '{' + ','.join(pin(incoming, field) for field, _ in fields) + '}'
            out = '{' + ','.join(pin(outgoing, field) for field, _ in fields) + '}'
            iv, ir, ov, ready = pin(incoming, 'valid'), pin(incoming, 'ready'), pin(outgoing, 'valid'), pin(outgoing, 'ready')
            declarations += [f'logic [{width-1}:0] expected_{tag}[0:256], held_{tag};',
                             f'integer sent_{tag}=0,got_{tag}=0; logic stalled_{tag}=0;']
            init += [f'{iv}=0; {ready}=0;'] + [f'{pin(incoming, field)}=0;' for field, _ in fields]
            clear.append(f'sent_{tag}=0; got_{tag}=0; stalled_{tag}=0;')
            # Different lane/channel stall schedules exercise independent ownership.
            drive += [f'{ready}=epoch==0 ? 0 : (($random(seed)&15)>{lane+2});',
                      f'if(!{iv} || {ir}) begin {iv}=sent_{tag}<257 && (($random(seed)&3)!=0);']
            drive += [f"{pin(incoming, field)}={bits}'({{{(bits+31)//32}{{$random(seed)}}}});" for field, bits in fields]
            drive += ['end']
            check += [f'if(stalled_{tag} && (!{ov} || {out} !== held_{tag})) $fatal(1,"{tag} changed under stall");',
                      f'stalled_{tag}={ov}&&!{ready}; held_{tag}={out};',
                      f'if({iv}&&{ir}) begin expected_{tag}[sent_{tag}]={inp}; sent_{tag}++; end',
                      f'if({ov}&&{ready}) begin if(got_{tag}>=sent_{tag} || {out} !== expected_{tag}[got_{tag}]) $fatal(1,"{tag} lane field/order mismatch"); got_{tag}++; end']
            done.append(f'got_{tag}==257')
            capacity.append(f'if(sent_{tag}==0 || got_{tag}!=0) $fatal(1,"{tag} queued-reset witness missing");')
            if pipeline_exit and mode=='direct':
                earlier=(clocked_foreach.endswith('_after') and
                         (exit_kind=='continue' or lane==0))
                capacity.append(f'if(sent_{tag}{"<=" if earlier else "!="}{3 if incoming=="s" else 2})'
                                f'$fatal(1,"{tag} pipeline effects disagreed with exit ordering/ingress capacity");')
    if mode in ('generate_assign', 'generate_assign_implicit', 'generate_assign_named', 'generate_assign_child', 'generate_comb', 'generate_comb_implicit', 'generate_comb_child', 'generate_constants', 'generate_elastic', 'generate_parameters', 'generate_parameters_positional', 'generate_parameters_typed', 'generate_parameters_nested', 'scope_constants', 'scope_elastic', 'retained_external'):
        declarations.append('wire bit [7:0] tags[-2:-1][1:0];')
        connections.append('.tags(tags)')
        if mode in ('generate_elastic','scope_elastic'):
            declarations.append('wire bit tag_valid[-2:-1][1:0];')
            connections.append('.tag_valid(tag_valid)')
        for a, b in coordinates:
            guard = f'tag_valid[{a}][{b}] && ' if mode in ('generate_elastic','scope_elastic') else ''
            check.append(f"if({guard}tags[{a}][{b}] !== 8'd{(a*4+b)&255}) $fatal(1,\"generated constant connection mismatch\");")
            if mode in ('generate_elastic','scope_elastic'): done.append(f'tag_valid[{a}][{b}]')
    tb.write_text('module tb; logic clk=0,reset=1; integer seed=3371,epoch=0; always #5 clk=~clk;\n' +
                  '\n'.join(declarations) + '\n' + case_oracle + '\nnative_array dut(' + ','.join(connections) + ');\n' +
                  'initial begin ' + '\n'.join(init) + '\nrepeat(4) @(negedge clk); reset=0; repeat(100) @(negedge clk);\n' +
                  '\n'.join(capacity) + '\nreset=1; repeat(3) @(negedge clk); epoch=1; reset=0; end\n' +
                  'always @(negedge clk) if(!reset) begin\n' + '\n'.join(drive) + '\nend\n' +
                  'always @(posedge clk) if(reset) begin\n' + '\n'.join(clear) + '\nend else begin\n' + '\n'.join(check) +
                  '\nif(epoch==1 && ' + ' && '.join(done) + ') begin $display("PASS independent AXI array lanes"); $finish; end\nend\n' +
                  'initial begin #100000; $fatal(1,"AXI array timeout"); end endmodule\n')
    if ordinary_package=='forward_call':
        tb.write_text(tb.read_text().replace('module tb;', 'module tb; import configuration::compute; initial begin #1;if(configuration::normalize(3)!==16 || compute(3)!==16)$fatal(1,"forward package helper mismatch");end',1))
    if qualifier or 'unique if(' in source or clocked_foreach=='qualified':
        run(['verilator', '--binary', '--assert', '--timing', '--top-module', 'tb',
             '--Mdir', directory/'obj', '-o', sim, '-j', '2', sv, tb,
             ROOT/'rtl/pigen_primitives.sv', *foreign_sources], directory)
        output=run([sim],directory)
    else:
        run(['iverilog', '-g2012', '-s', 'tb', '-o', sim, sv, tb, ROOT/'rtl/pigen_primitives.sv', *foreign_sources], directory)
        output=run(['vvp', sim], directory)
    assert 'PASS independent AXI array lanes' in output
    print(f'PASS native array {family} {authored_mode} type_keys={type_keys}: {257*4*len(fields_by_channel)} stalled records and queued reset')


def negatives(root):
    for family in ('axi4_stream', 'axi4_lite', 'axi4'):
        protocol = f'uint[32] {family}'
        channel = '' if family == 'axi4_stream' else '.aw'
        header = f'module bad(input wire clk,input wire reset,input wire idx,input {protocol} s[2],output {protocol} m[2]);'
        wiring = f'always @(posedge clk) m[0]{channel} <= s[0]{channel}; '
        examples = {
            'comb_break_root': header + "logic value; always_comb begin value=valid(s[0]); break; end " + wiring + 'endmodule',
            'comb_continue_case': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb case(idx) default: continue; endcase " + wiring + 'end endmodule',
            'comb_do_missing_tail': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb do value=0; " + wiring + 'end endmodule',
            'comb_do_missing_semicolon': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb do value=0; while(idx) " + wiring + 'end endmodule',
            'comb_do_buffered': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; logic value; always_comb do value=0; while(token); " + wiring + 'end endmodule',
            'comb_for_scope': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb begin for(int j=0;j<2;j++) value=0; value=j; end " + wiring + 'end endmodule',
            'comb_for_step_nonblocking': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb for(int j=0;j<2;j<=j+1) value=0; " + wiring + 'end endmodule',
            'comb_for_empty_variable': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb for(int j;j<2;j++) value=0; " + wiring + 'end endmodule',
            'comb_for_trailing_comma': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb for(int j=0,;j<2;j++) value=0; " + wiring + 'end endmodule',
            'comb_for_header': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb for(int j=0;j<2) value=0; " + wiring + 'end endmodule',
            'comb_for_buffered': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; logic value; always_comb for(int j=0;token;j++) value=0; " + wiring + 'end endmodule',
            'comb_while_buffered': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; logic value; always_comb while(token) value=0; " + wiring + 'end endmodule',
            'comb_repeat_buffered': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; logic value; always_comb repeat(token) value=0; " + wiring + 'end endmodule',
            'comb_loop_nonintegral': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb while(1.5) value=0; " + wiring + 'end endmodule',
            'comb_loop_empty_control': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb repeat() value=0; " + wiring + 'end endmodule',
            'comb_loop_missing_control': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb while idx value=0; " + wiring + 'end endmodule',
            'comb_case_qualifier_chain_root': header + "logic value; always_comb priority unique case(idx) 0: value=0; default: value=valid(s[0]); endcase " + wiring + 'endmodule',
            'comb_case_duplicate_default': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb case(idx) default: value=0; default: value=1; endcase " + wiring + 'end endmodule',
            'comb_case_missing_colon': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb case(idx) 1'b1 value=0; endcase " + wiring + 'end endmodule',
            'comb_case_unterminated': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb case(idx) 1'b1: value=0; " + wiring + 'end endmodule',
            'comb_case_buffered_selector': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; logic value; always_comb case(token) 0: value=0; default: value=1; endcase " + wiring + 'end endmodule',
            'comb_case_buffered_label': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; logic value; always_comb case(idx) token: value=0; default: value=1; endcase " + wiring + 'end endmodule',
            'comb_case_buffered_write': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; always_comb case(idx) 0: token=0; default: token=1; endcase " + wiring + 'end endmodule',
            'comb_case_array_selector': header + "for(genvar i=0;i<2;i++) begin logic [7:0] values[2]; logic value; always_comb case(values) 0: value=0; default: value=1; endcase " + wiring + 'end endmodule',
            'comb_if_qualifier_chain': header + "for(genvar i=0;i<2;i++) begin logic value; always_comb unique priority if(idx) value=0; else value=1; " + wiring + 'end endmodule',
            'comb_update_numerical_raw_division': header + "for(genvar i=0;i<2;i++) begin always_comb begin uint[8] value; value=1; value/=8'd2; end " + wiring + 'end endmodule',
            'comb_update_numerical_raw_remainder': header + "for(genvar i=0;i<2;i++) begin always_comb begin uint[8] value; value=1; value%=8'd2; end " + wiring + 'end endmodule',
            'comb_assign_numerical_raw_division': header + "for(genvar i=0;i<2;i++) begin always_comb begin uint[8] value; value=1; value=value/8'd2; end " + wiring + 'end endmodule',
            'comb_update_numerical_division': header + "for(genvar i=0;i<2;i++) begin always_comb begin uint[8] value; value=1; value/=2; end " + wiring + 'end endmodule',
            'comb_update_numerical_remainder': header + "for(genvar i=0;i<2;i++) begin always_comb begin uint[8] value; value=1; value%=2; end " + wiring + 'end endmodule',
            'comb_update_width_limit': header + "for(genvar i=0;i<2;i++) begin always_comb begin uint[8] value; value=1; value<<=4096; end " + wiring + 'end endmodule',
            'comb_update_buffered': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; always_comb token+=1; " + wiring + 'end endmodule',
            'comb_update_buffered_read': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; logic [7:0] value; always_comb begin value=0; value+=token; end " + wiring + 'end endmodule',
            'comb_update_readonly': header + "for(genvar i=0;i<2;i++) begin always_comb idx++; " + wiring + 'end endmodule',
            'comb_update_missing_rhs': header + "for(genvar i=0;i<2;i++) begin logic [7:0] value; always_comb value+=; " + wiring + 'end endmodule',
            'comb_update_array': header + "for(genvar i=0;i<2;i++) begin logic [7:0] value[2]; always_comb value+=1; " + wiring + 'end endmodule',
            'comb_update_side_effect': header + "for(genvar i=0;i<2;i++) begin logic [7:0] value; uint[3] slot; always_comb begin slot=0; value[slot++]++; end " + wiring + 'end endmodule',
            'comb_local_net': header + "for(genvar i=0;i<2;i++) begin always_comb begin wire [7:0] local_value; local_value=0; end " + wiring + 'end endmodule',
            'comb_local_automatic_net': header + "for(genvar i=0;i<2;i++) begin always_comb begin automatic wire [7:0] local_value; end " + wiring + 'end endmodule',
            'comb_local_initializer': header + "for(genvar i=0;i<2;i++) begin always_comb begin logic [7:0] local_value=8'd0; end " + wiring + 'end endmodule',
            'comb_local_buffered': header + "for(genvar i=0;i<2;i++) begin always_comb begin uint[8] buf local_value; local_value=0; end " + wiring + 'end endmodule',
            'comb_local_late': header + "for(genvar i=0;i<2;i++) begin logic [7:0] value; always_comb begin value=0; logic [7:0] local_value; local_value=0; end " + wiring + 'end endmodule',
            'comb_local_escape': header + "for(genvar i=0;i<2;i++) begin always_comb begin begin logic [7:0] local_value; local_value=0; end local_value=1; end " + wiring + 'end endmodule',
            'comb_local_duplicate': header + "for(genvar i=0;i<2;i++) begin always_comb begin logic [7:0] value; logic [7:0] value; value=0; end " + wiring + 'end endmodule',
            'comb_local_bad_parameter': header + "for(genvar i=0;i<2;i++) begin always_comb begin localparam W=8,missing; end " + wiring + 'end endmodule',
            'generate_comb_buffered': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; always_comb token=0; " + wiring + 'end endmodule',
            'generate_comb_condition': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; logic [7:0] value; always_comb begin value=0; if(token) value=1; end " + wiring + 'end endmodule',
            'generate_comb_overlap': header + "for(genvar i=0;i<2;i++) begin logic [7:0] value; always_comb value=8'd0; assign value=8'd1; " + wiring + 'end endmodule',
            'generate_comb_nba': header + "for(genvar i=0;i<2;i++) begin logic [7:0] value; always_comb value<=8'd0; " + wiring + 'end endmodule',
            'generate_concat_overlap': header + "for(genvar i=0;i<2;i++) begin wire [7:0] aa,bb; assign {aa[7:4],bb[3:0]}=8'd0; assign {bb[3:0],aa[3:0]}=8'd0; " + wiring + 'end endmodule',
            'generate_concat_buffered': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; wire [7:0] aa; assign {aa,token}=16'd0; " + wiring + 'end endmodule',
            'generate_concat_readonly': header + "for(genvar i=0;i<2;i++) begin wire [7:0] aa; assign {aa,idx}=9'd0; " + wiring + 'end endmodule',
            'generate_concat_index': header + "for(genvar i=0;i<2;i++) begin uint[1] buf token; wire [7:0] aa,bb; assign {aa[token],bb}=9'd0; " + wiring + 'end endmodule',
            'generate_size_zero': header + "for(genvar i=0;i<2;i++) begin wire [7:0] observation; assign observation=0'(i); " + wiring + 'end endmodule',
            'generate_size_negative': header + "for(genvar i=0;i<2;i++) begin wire [7:0] observation; assign observation=(-1)'(i); " + wiring + 'end endmodule',
            'generate_size_runtime': header + "for(genvar i=0;i<2;i++) begin wire [7:0] observation; assign observation=idx'(i); " + wiring + 'end endmodule',
            'generate_size_excessive': header + "for(genvar i=0;i<2;i++) begin wire [7:0] observation; assign observation=999999999'(i); " + wiring + 'end endmodule',
            'generate_assign_overlap': header + "for(genvar i=0;i<2;i++) begin wire [7:0] local_value; assign local_value=8'd1,local_value[3:0]=4'd0; " + wiring + 'end endmodule',
            'generate_assign_buffered': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; assign token=8'd0; " + wiring + 'end endmodule',
            'generate_assign_input': header + "for(genvar i=0;i<2;i++) begin assign idx=1'b0; " + wiring + 'end endmodule',
            'generate_assign_unobserved': header + "for(genvar i=0;i<2;i++) begin uint[8] buf token; wire [7:0] local_value; assign local_value=token; " + wiring + 'end endmodule',
            'out_of_bounds': header + f'always @(posedge clk) m[0]{channel} <= s[2]{channel}; endmodule',
            'out_of_bounds_computed': header + f'always @(posedge clk) m[0]{channel} <= s[~(-4)]{channel}; endmodule',
            'unknown_computed': header + f'always @(posedge clk) m[0]{channel} <= s[~1\'bx]{channel}; endmodule',
            'query_invalid_dimension': header + f"always @(posedge clk) m[0]{channel} <= s[$left(s,0)]{channel}; endmodule",
            'query_unknown_dimension': header + f"always @(posedge clk) m[0]{channel} <= s[$left(s,'x)]{channel}; endmodule",
            'query_runtime_dimension': header + f"always @(posedge clk) m[0]{channel} <= s[$left(s,idx)]{channel}; endmodule",
            'query_rank_arity': header + f"always @(posedge clk) m[0]{channel} <= s[$dimensions(s,1)]{channel}; endmodule",
            'unknown_fill_coordinate': header + f"always @(posedge clk) m[0]{channel} <= s['x]{channel}; endmodule",
            'unknown_call_coordinate': header + f"always @(posedge clk) m[0]{channel} <= s[$unsigned('x)]{channel}; endmodule",
            'runtime_fill_coordinate': header + f"always @(posedge clk) m[0]{channel} <= s['1 & idx]{channel}; endmodule",
            'signed_coordinate_bounds': header + f"always @(posedge clk) m[0]{channel} <= s[$signed('1)]{channel}; endmodule",
            'filled_coordinate_bounds': header + f"always @(posedge clk) m[0]{channel} <= s[$unsigned('1 ^ 2'b00)]{channel}; endmodule",
            'runtime_call_coordinate': header + f"always @(posedge clk) m[0]{channel} <= s[$unsigned(idx)]{channel}; endmodule",
            'malformed_call_coordinate': header + f"always @(posedge clk) m[0]{channel} <= s[$unsigned('1,'0)]{channel}; endmodule",
            'replicated_coordinate_bounds': header + f"always @(posedge clk) m[0]{channel} <= s[{{2{{1'b1}}}}]{channel}; endmodule",
            'replicated_coordinate_unknown': header + f"always @(posedge clk) m[0]{channel} <= s[{{2{{1'bx}}}}]{channel}; endmodule",
            'replicated_coordinate_runtime_count': header + f"always @(posedge clk) m[0]{channel} <= s[{{idx{{1'b0}}}}]{channel}; endmodule",
            'empty_coordinate_zero_only': header + f"always @(posedge clk) m[0]{channel} <= s[{{{{0{{1'b0}}}}}}]{channel}; endmodule",
            'empty_coordinate_nested_zero_only': header + f"always @(posedge clk) m[0]{channel} <= s[{{{{{{0{{1'b0}}}}}},1'b0}}]{channel}; endmodule",
            'empty_coordinate_unknown_body': header + f"always @(posedge clk) m[0]{channel} <= s[{{{{0{{missing}}}},1'b0}}]{channel}; endmodule",
            'empty_coordinate_runtime_body': header + f"always @(posedge clk) m[0]{channel} <= s[{{{{0{{idx}}}},1'b0}}]{channel}; endmodule",
            'replicated_coordinate_zero_count': header + f"always @(posedge clk) m[0]{channel} <= s[{{0{{1'b0}}}}]{channel}; endmodule",
            'replicated_coordinate_negative_count': header + f"always @(posedge clk) m[0]{channel} <= s[{{(-1){{1'b0}}}}]{channel}; endmodule",
            'replicated_coordinate_excessive_count': header + f"always @(posedge clk) m[0]{channel} <= s[{{1025{{1'b0}}}}]{channel}; endmodule",
            'runtime_index': header + f'always @(posedge clk) m[0]{channel} <= s[idx]{channel}; endmodule',
            'missing_index': header + f'always @(posedge clk) m[0]{channel} <= s{channel}; endmodule',
            'generate_runtime_condition': header + f'generate if(idx) begin : lanes always @(posedge clk) m[0]{channel} <= s[0]{channel}; end endgenerate endmodule',
            'generate_cycle': header + f'generate for(genvar i=0;i<2;i^=1) begin : lanes always @(posedge clk) m[i]{channel} <= s[i]{channel}; end endgenerate endmodule',
            'generate_no_progress': header + f'generate for(genvar i=0;i<2;i=i) begin : lanes always @(posedge clk) m[i]{channel} <= s[i]{channel}; end endgenerate endmodule',
            'genvar_duplicate': header + 'genvar i,i; endmodule',
            'genvar_runtime': header + "genvar i; uint[1] wire observation; assign observation=uint[1]\'(i); endmodule",
            'genvar_wrong_kind': header + f'localparam i=0; generate for(i=0;i<2;i++) begin : lanes always @(posedge clk) m[i]{channel} <= s[i]{channel}; end endgenerate endmodule',
            'genvar_nested_reuse': header + f'genvar i; generate for(i=0;i<2;i++) begin : lanes for(i=0;i<2;i++) begin : inner always @(posedge clk) m[i]{channel} <= s[i]{channel}; end end endgenerate endmodule',
            'generate_missing_genvar': header + f'generate for(i=0;i<2;i++) begin : lanes always @(posedge clk) m[i]{channel} <= s[i]{channel}; end endgenerate endmodule',
            'generate_self_initialization': header + f'localparam i=0; generate for(genvar i=i;i<2;i++) begin : lanes always @(posedge clk) m[i]{channel} <= s[i]{channel}; end endgenerate endmodule',
            'partial_rank': header.replace('s[2]', 's[2][2]') + f'always @(posedge clk) m[0]{channel} <= s[0]{channel}; endmodule',
            'zero_count': header.replace('s[2]', 's[0]') + 'endmodule',
            'fabric_out_of_bounds': header + 'fabric begin s[2] -> m[0]; endfabric endmodule',
            'fabric_runtime_index': header + 'fabric begin s[idx] -> m[0]; endfabric endmodule',
            'duplicate_lane': header + 'fabric begin s[0] -> m[0]; s[0] -> m[1]; endfabric endmodule',
        }
        leaf = f'module leaf(input wire clk,input wire reset,input {protocol} s[2]); endmodule\n'
        for label, pattern in {
            'zero':"'{0{s[0],s[1]}}", 'negative':"'{(-1){s[0],s[1]}}",
            'runtime':"'{idx{s[0],s[1]}}", 'unknown':"'{2'bx{s[0],s[1]}}",
            'huge':"'{64'sh7fffffffffffffff{s[0],s[1]}}",
            'size':"'{1{s[0]}}", 'ownership':"'{2{s[0]}}",
            'flatten':"'{1{s}}", 'mixed':"'{1{s[0]},s[1]}",
            'empty':"'{2{}}", 'unterminated':"'{2{s[0]}",
        }.items():
            examples['pattern_repeat_'+label]=leaf+header+f'leaf child(.clk(clk),.reset(reset),.s({pattern})); endmodule'
        parent = f'module bad(input wire clk,input wire reset,input {protocol} s[2]);'
        examples.update({
            'default_scalar_ownership': leaf.replace('s[2]','s[2][2]') + parent + "leaf child(.clk(clk),.reset(reset),.s('{default:s[0]})); endmodule",
            'default_subarray_ownership': leaf.replace('s[2]','s[2][2][2]') + parent.replace('s[2]','s[2][2]') + "leaf child(.clk(clk),.reset(reset),.s('{default:s[0]})); endmodule",
            'default_wrong_subarray': leaf.replace('s[2]','s[1][1][3]') + parent.replace('s[2]','s[2][2]') + "leaf child(.clk(clk),.reset(reset),.s('{default:s[0]})); endmodule",
            'default_explicit_scalar': leaf.replace('s[2]','s[2][1]') + parent + "leaf child(.clk(clk),.reset(reset),.s('{1:s[0],default:s[1]})); endmodule",
            'keyed_missing': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s('{1:s[1]})); endmodule",
            'keyed_duplicate': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s('{1:s[1],0:s[0],(1-1):s[0]})); endmodule",
            'keyed_default_duplicate': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s('{default:s[1],default:s[0]})); endmodule",
            'keyed_out_of_bounds': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s('{2:s[1],0:s[0]})); endmodule",
            'keyed_runtime': leaf + parent.replace('input wire reset,','input wire reset,input wire idx,') + "leaf child(.clk(clk),.reset(reset),.s('{idx:s[1],default:s[0]})); endmodule",
            'keyed_default_ownership': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s('{default:s[0]})); endmodule",
            'keyed_mixed': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s('{1:s[1],s[0]})); endmodule",
            'pattern_size': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s('{s[0]})); endmodule",
            'pattern_duplicate': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s('{s[0],s[0]})); endmodule",
            'pattern_ordinary': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s('{s[0],32'd0})); endmodule",
            'pattern_flatten': leaf + parent.replace('s[2]', 's[2][1]') + "leaf child(.clk(clk),.reset(reset),.s('{s[0],s[1]})); endmodule",
            'pattern_nested_size': leaf.replace('s[2]', 's[2][2]') + parent.replace('s[2]', 's[2][2]') + "leaf child(.clk(clk),.reset(reset),.s('{'{s[0][0]},'{s[1][1],s[1][0]}})); endmodule",
            'pattern_protocol': leaf.replace('uint[32]', 'uint[64]') + parent + "leaf child(.clk(clk),.reset(reset),.s('{s[0],s[1]})); endmodule",
            'pattern_role': leaf.replace(f'input {protocol}', f'output {protocol}') + parent + "leaf child(.clk(clk),.reset(reset),.s('{s[0],s[1]})); endmodule",
            'concat_size': leaf + parent + 'leaf child(.clk(clk),.reset(reset),.s({s[0]})); endmodule',
            'concat_duplicate': leaf + parent + 'leaf child(.clk(clk),.reset(reset),.s({s[0],s[0]})); endmodule',
            'concat_ordinary': leaf + parent + "leaf child(.clk(clk),.reset(reset),.s({s[0],32'd0})); endmodule",
            'concat_nested': leaf + parent + 'leaf child(.clk(clk),.reset(reset),.s({{s[0]},{s[1]}})); endmodule',
            'concat_tail': leaf.replace('s[2]', 's[2][2]') + parent.replace('s[2]', 's[2][3]') + 'leaf child(.clk(clk),.reset(reset),.s({s[1],s[0]})); endmodule',
            'concat_rank': leaf.replace('s[2]', 's[2][2]') + parent + 'leaf child(.clk(clk),.reset(reset),.s({s[0],s[1]})); endmodule',
            'concat_protocol': leaf.replace('uint[32]', 'uint[64]') + parent + 'leaf child(.clk(clk),.reset(reset),.s({s[0],s[1]})); endmodule',
            'concat_role': leaf.replace(f'input {protocol}', f'output {protocol}') + parent + 'leaf child(.clk(clk),.reset(reset),.s({s[0],s[1]})); endmodule',
            'array_bounds': leaf.replace('s[2]', 's[0:2]') + parent + 'leaf child(.clk(clk),.reset(reset),.s(s)); endmodule',
            'array_size': leaf.replace('s[2]', 's[3]') + parent + 'leaf child(.clk(clk),.reset(reset),.s(s)); endmodule',
            'array_protocol': leaf.replace('uint[32]', 'uint[64]') + parent + 'leaf child(.clk(clk),.reset(reset),.s(s)); endmodule',
            'array_role': leaf.replace(f'input {protocol}', f'output {protocol}') + parent + 'leaf child(.clk(clk),.reset(reset),.s(s)); endmodule',
            'ordinary_array': leaf + parent.replace(protocol, 'uint[32] buf') + 'leaf child(.clk(clk),.reset(reset),.s(s)); endmodule',
        })
        scalar_leaf = f'module leaf(input wire clk,input wire reset,input {protocol} s,output {protocol} m); endmodule\n'
        generate_header = header.replace('input wire idx,', 'input wire idx,input wire other_clk,')
        binding = 'leaf child(.clk(clk),.reset(reset),.s(s[i]),.m(m[i]));'
        def generated(child, content):
            return child + generate_header + 'generate for(genvar i=0;i<2;i++) begin : lanes ' + content + ' end endgenerate endmodule'
        examples.update({
            'generated_contract': generated(scalar_leaf.replace('uint[32]', 'uint[64]'), binding),
            'generated_role': generated(scalar_leaf.replace(f'input {protocol} s', f'output {protocol} s'), binding),
            'generated_ordinary': generated(scalar_leaf, binding).replace(f'input {protocol} s[2]', 'input uint[32] buf s[2]'),
            'generated_clock': generated(scalar_leaf, binding.replace('.clk(clk)', '.clk(other_clk)')),
            'generated_ownership': generated(scalar_leaf, binding + binding.replace('child(', 'duplicate(')),
            'generated_cycle': generate_header + 'generate for(genvar i=0;i<2;i++) begin : lanes bad child(.clk(clk),.reset(reset)); end endgenerate endmodule',
        })
        parameter_leaf = scalar_leaf.replace('module leaf(', 'module leaf #(parameter W=32)(').replace('uint[32]', 'uint[W]')
        def configured(arguments):
            return binding.replace('leaf child(', f'leaf #({arguments}) child(')
        examples.update({
            'generated_parameter_contract': generated(parameter_leaf, configured('.W(64)')),
            'generated_parameter_runtime': generated(parameter_leaf, configured('.W(idx)')),
            'generated_parameter_unknown': generated(parameter_leaf, configured('.MISSING(i)')),
            'generated_parameter_duplicate': generated(parameter_leaf, configured('.W(32),.W(32)')),
            'generated_parameter_empty_duplicate': generated(parameter_leaf, configured('.W(),.W(32)')),
            'generated_parameter_mix': generated(parameter_leaf, configured('.W(32),32')),
            'generated_parameter_excess': generated(parameter_leaf, configured('32,64')),
            'generated_parameter_localparam': generated(parameter_leaf.replace('endmodule', 'localparam FIXED=1; endmodule'), configured('.FIXED(2)')),
            'generated_parameter_cycle': generate_header.replace('module bad(', 'module bad #(parameter N=1)(') +
                'generate if(N) begin : recursion bad #(.N(N)) child(.clk(clk),.reset(reset)); end endgenerate endmodule',
        })
        scope_source = scalar_leaf + header + 'generate for(genvar i=0;i<2;i++) begin : lanes leaf child(.clk(clk),.reset(reset),.s(s[i])); end endgenerate '
        def scoped(link):
            return scope_source + f'fabric begin {link} -> m[0]; endfabric endmodule'
        examples.update({
            'scope_runtime': scoped('lanes[idx].child.m'),
            'scope_bounds': scoped('lanes[2].child.m'),
            'scope_negative_bounds': scoped('lanes[-1].child.m'),
            'scope_missing_index': scoped('lanes.child.m'),
            'scope_extra_index': scoped('lanes[0][0].child.m'),
            'scope_missing_member': scoped('lanes[0].absent.m'),
            'scope_no_ancestor_fallback': scoped('lanes[0].s'),
            'scope_unselected': scalar_leaf + header + 'generate if(0) begin : absent leaf child(.clk(clk),.reset(reset)); end endgenerate fabric begin absent.child.m -> m[0]; endfabric endmodule',
            'scope_conditional_loop': scalar_leaf + header + 'if(1) for(genvar i=0;i<2;i++) begin : lanes leaf child(.clk(clk),.reset(reset)); end fabric begin lanes[0].child.m -> m[0]; endfabric endmodule',
            'scope_scalar_index': scalar_leaf + header + 'generate if(1) begin : single leaf child(.clk(clk),.reset(reset)); end endgenerate fabric begin single[0].child.m -> m[0]; endfabric endmodule',
            'scope_ownership': scope_source + 'fabric begin lanes[0].child.m -> m[0]; lanes[(2-2)].child.m -> m[1]; endfabric endmodule',
        })
        for label in ('generate_runtime_condition','generate_cycle','generate_no_progress',
                      'generate_missing_genvar','generate_self_initialization','generated_parameter_cycle',
                      'generated_parameter_runtime','scope_unselected'):
            examples['implicit_'+label] = re.sub(r'\bgenerate\b|\bendgenerate\b','',examples[label])
        examples['implicit_case'] = header + f'case(idx) 1: begin always @(posedge clk) m[0]{channel} <= s[0]{channel}; end endcase endmodule'
        case_transfer = f'always @(posedge clk) m[0]{channel} <= s[0]{channel};'
        examples.update({
            'case_runtime_label': header + f'case(0) idx: begin {case_transfer} end endcase endmodule',
            'case_duplicate_default': header + f'case(0) default: begin {case_transfer} end default:; endcase endmodule',
            'case_wildcard_generate': header + f'casez(0) 0: begin {case_transfer} end endcase endmodule',
            'case_missing_end': header + f'case(0) 0: begin {case_transfer} end endmodule',
        })
        hidden = f'if(0) begin : collision {protocol} unused; end '
        examples.update({
            'generate_hidden_name': header + hidden + hidden + 'endmodule',
            'generate_hidden_port_name': header + hidden.replace(': collision', ': s') + 'endmodule',
            'generate_hidden_parameter_name': header + 'localparam collision=0; ' + hidden + 'endmodule',
            'generate_nested_hidden_name': header + 'if(0) begin : outer ' + hidden + hidden + 'end endmodule',
            'generate_cross_region_name': header + 'generate ' + hidden + 'endgenerate generate ' + hidden + 'endgenerate endmodule',
        })
        examples.update({
            'generated_fsm_goto': header + 'for(genvar i=0;i<2;i++) begin fsm @(posedge clk) begin state active: goto missing; end end endmodule',
            'generated_fsm_clock': header + 'for(genvar i=0;i<2;i++) begin uint[8] buf token; fsm @(posedge token) begin state active: goto active; end end endmodule',
        })
        examples.update({
            'generated_pipeline_reset_missing': header + 'for(genvar i=0;i<2;i++) begin always @(posedge clk) pipeline_reset(missing); end endmodule',
            'generated_pipeline_buffered_local': header + 'for(genvar i=0;i<2;i++) begin always @(posedge clk) begin pipeline pipe begin uint[8] value; stage begin uint[8] buf temporary; value <= 0; end yield value; endpipeline end end endmodule',
        })
        examples.update({
            'fabric_collection_bounds': header.replace('m[2]','m[0:1]') + 'fabric begin s -> m; endfabric endmodule',
            'fabric_collection_rank': header.replace('m[2]','m[1][2]') + 'fabric begin s -> m; endfabric endmodule',
            'fabric_collection_size': header.replace('m[2]','m[3]') + 'fabric begin s -> m; endfabric endmodule',
            'fabric_collection_scalar': header + 'fabric begin s -> m[0]; endfabric endmodule',
            'fabric_collection_reuse': header + 'fabric begin s -> m; s[0] -> m[0]; endfabric endmodule',
            'fabric_collection_owner': header + f'fabric begin s -> m; endfabric always @(posedge clk) m[0]{channel} <= s[0]{channel}; endmodule',
        })
        sliced_leaf = f'module selected_leaf(input wire clk,input wire reset,input {protocol} s[2]); endmodule\n'
        for label, selection in {'runtime':'s[idx]', 'bounds':'s[3]', 'slice_bounds':'s[2:1]',
                                  'slice_direction':'s[0:1]', 'slice_width':'s[0+:0]',
                                  'slice_runtime_width':'s[0+:idx]', 'slice_overflow':"s[64'sh7fffffffffffffff+:2]", 'slice_underflow':"s[64'sh8000000000000000-:2]", 'multiple_slices':'s[1:0][1:0]'}.items():
            collection_header = header.replace('s[2]','s[2][2]') if label in ('runtime','bounds','multiple_slices') else header
            examples['array_selection_'+label] = (sliced_leaf.replace('s[2]', 's[2][2]') if label=='multiple_slices' else sliced_leaf) + collection_header + f'selected_leaf child(.clk(clk),.reset(reset),.s({selection})); endmodule'
        examples['array_selection_overlap'] = sliced_leaf + header + 'selected_leaf first(.clk(clk),.reset(reset),.s(s)); selected_leaf second(.clk(clk),.reset(reset),.s(s[1:0])); endmodule'
        for label, source in examples.items():
            directory = root/f'negative_{family}_{label}'
            directory.mkdir()
            pigen, sv = directory/'bad.pigen', directory/'bad.sv'
            pigen.write_text(source)
            sv.write_text('preserved output\n')
            output = run([ROOT/'pigen', pigen, '-o', sv, '--no-diagram'], directory, expect=False)
            assert output.strip(), label
            assert sv.read_text() == 'preserved output\n', label
            if label == 'pattern_repeat_ownership':
                assert 'signal has nonexclusive consumers' in output, output
            if label in ('generated_cycle', 'generated_parameter_cycle'):
                assert 'recursive module instantiation cannot be elaborated' in output, output
            if label in ('implicit_case', 'case_runtime_label'):
                assert 'case generate requires integral constant selector and labels' in output, output
            if label.startswith('generate_hidden_') or label in ('generate_nested_hidden_name', 'generate_cross_region_name'):
                assert 'generate block name conflicts' in output, output
            if label == 'generate_comb_condition':
                assert 'always_comb buffered conditions require peek' in output, output
            if label == 'generate_comb_nba':
                assert 'always_comb requires supported blocking assignments' in output, output
            if label in ('generate_assign_overlap','generate_concat_overlap','generate_comb_overlap'):
                assert 'continuous assignment has overlapping writers' in output, output
            if label in ('generate_assign_buffered','generate_concat_buffered','generate_comb_buffered'):
                assert 'continuous assignment cannot produce a buffered token' in output, output
            if label in ('generate_assign_input','generate_concat_readonly'):
                assert 'requires a writable static projection' in output, output
            if label in ('generate_assign_unobserved','generate_concat_index'):
                assert 'continuous buffered payload observation requires peek' in output, output
            if label == 'genvar_runtime':
                assert 'unknown value name' in output, output
            if label == 'generate_self_initialization':
                assert 'generate initializer cannot reference its loop index' in output, output
            if label == 'generate_missing_genvar':
                assert 'generate loop iterator requires a visible genvar declaration' in output, output
            if label == 'generate_runtime_condition':
                assert 'generate condition must be a known constant' in output, output
            if label in ('generate_cycle', 'generate_no_progress'):
                assert 'generate loop does not terminate' in output, output
    print('PASS native array negative indices, shapes, contracts, roles and ownership')


def type_key_negatives(root):
    for family, options in [('axi4_stream','last=1'),('axi4_lite','addr=40'),('axi4','addr=40,id=4')]:
        protocol = f'uint[32] {family}[{options}]'
        wrong = f'uint[32] axi4_stream[last=0]' if family != 'axi4_stream' else 'uint[32] axi4_lite[addr=40]'
        prefix = (f'typedef {protocol} bus_t; typedef {wrong} wrong_t; typedef uint[32] word_t;\n'
                  f'module leaf(input wire clk,input wire reset,input {protocol} s[2],output {protocol} m[2]); '
                  'fabric begin s > m; endfabric endmodule\n')
        header = f'module bad(input wire clk,input wire reset,input {protocol} s[2],input {wrong} other,output {protocol} m[2]); bit[32] buf plain;\n'
        cases = [
            ('unmatched', "'{wrong_t:s[0]}", 'matching rank'),
            ('ordinary', "'{word_t:s[0]}", 'matching rank'),
            ('unknown', "'{unknown_type:s[0]}", 'matching rank'),
            ('duplicates', "'{bus_t:s[0]}", 'nonexclusive consumers'),
            ('row', "'{0:s[0],bus_t:s}", 'matching rank'),
            ('duplicate_index', "'{0:s[0],0:s[1],bus_t:s[1]}", 'matching rank'),
            ('plain', "'{0:s[0],bus_t:plain}", 'matching rank'),
            ('wrong_value', "'{0:s[0],bus_t:other}", 'complete compatible protocol contract'),
        ]
        for name, pattern, diagnostic in cases:
            source = root/f'type_key_bad_{family}_{name}.pigen'
            output = source.with_suffix('.sv')
            source.write_text(prefix + header + f'leaf child(.clk(clk),.reset(reset),.s({pattern}),.m(m)); endmodule\n')
            output.write_text('preserve output\n')
            result = run([ROOT/'pigen',source,'-o',output,'--no-diagram'],root,expect=False)
            assert diagnostic in result, result
            assert output.read_text() == 'preserve output\n'


def qualified_fsm_generate_tests(root):
    for family in ('axi4_stream', 'axi4_lite', 'axi4'):
        for qualifier in ('unique','unique0','priority'):
            for mode in ('generate_fsm','generate_fsm_implicit'):
                directory=root/f'qualified_{family}_{qualifier}_{mode}'
                directory.mkdir()
                case(directory,family,mode,native_alias=True,qualifier=qualifier)


def qualified_pipeline_tests(root):
    for family in ('axi4_stream', 'axi4_lite', 'axi4'):
        for qualifier,mode in (('unique','generate_pipeline'),
                               ('unique0','generate_pipeline_implicit'),
                               ('priority','generate_pipeline_hierarchy')):
            directory=root/f'qualified_pipeline_{family}_{qualifier}'
            directory.mkdir()
            case(directory,family,mode,native_alias=True,qualifier=qualifier)


def void_collection_tests(root):
    for mode, keys in [('direct', False), ('aggregate', False), ('scalar_binding', False),
                       ('fabric_child', False), ('array_row_binding', False),
                       ('array_slice_fabric', False), ('array_keyed_flat', True), ('array_keyed_generate', 'generated')]:
        directory = root / f'void_collection_{mode}'
        directory.mkdir()
        case(directory, 'axi4_stream', mode, type_keys=keys, data=0)


def external_collection_tests(root):
    for family in ('axi4_stream', 'axi4_lite', 'axi4'):
        for mode in ('aggregate', 'positional', 'projection', 'fabric_child', 'scalar_binding'):
            directory = root / f'external_{family}_{mode}'
            directory.mkdir()
            case(directory, family, mode, external=True)


def main():
    root = Path(tempfile.mkdtemp(prefix='pigen-native-arrays-'))
    try:
        qualified_fsm_generate_tests(root)
        qualified_pipeline_tests(root)
        void_collection_tests(root)
        external_collection_tests(root)
        negatives(root)
        type_key_negatives(root)
        for family in ('axi4_stream', 'axi4_lite', 'axi4'):
            for kind,mode in [('basic','array_keyed_flat'),('nested','array_keyed_nested'),
                              ('generated','array_keyed_generate'),(False,'fabric_array_standalone')]:
                directory=root/f'export_{family}_{mode}'
                directory.mkdir()
                case(directory,family,mode,type_keys=kind,package='export')
            for ordinary,mode in [('mixed','fabric_child'),('mixed_transitive','array_keyed_generate'),('retained_export','array_keyed_generate'),('forward_call','array_keyed_generate')]:
                directory=root/f'ordinary_{ordinary}_{family}_{mode}'
                directory.mkdir()
                case(directory,family,mode,ordinary_package=ordinary,
                     type_keys='generated' if mode=='array_keyed_generate' else False)
            for ordinary,mode in [('qualified','direct'),('header','fabric_child'),
                                  ('unit','fabric_array_standalone'),('transitive','array_keyed_generate')]:
                directory=root/f'ordinary_{family}_{mode}'
                directory.mkdir()
                case(directory,family,mode,ordinary_package=ordinary,
                     type_keys='generated' if mode=='array_keyed_generate' else False)
            for package, kind, mode in [('import_unit','basic','array_keyed_flat'),('import_header','nested','array_keyed_nested'),
                                        ('import_generated','generated','array_keyed_generate'),('import_unit',False,'fabric_array_standalone')]:
                directory = root/f'{package}_{family}_{kind or "standalone"}'
                directory.mkdir()
                case(directory,family,mode,type_keys=kind,package=package)
            for kind in ('basic','nested','generated','state','record','record_alias'):
                mode = {'nested':'array_keyed_nested','generated':'array_keyed_generate'}.get(kind,'array_keyed_flat')
                directory = root/f'package_{family}_{kind}'
                directory.mkdir()
                case(directory,family,mode,type_keys=kind,package=True)
            for kind in ('basic','last','default','covered','contract','state','sign','shape','bounds','ordinary','primitive','equivalent','record','record_alias','nested','descent','generated'):
                mode = {'nested':'array_keyed_nested','descent':'array_default_scalar','generated':'array_keyed_generate'}.get(kind,'array_keyed_flat')
                directory = root/f'type_key_{family}_{kind}'
                directory.mkdir()
                case(directory, family, mode, type_keys=kind)
            for mode in ('generate_parameters_nested', 'fabric_array_generated', 'array_keyed_generate'):
                directory = root/f'alias_{family}_{mode}'
                directory.mkdir()
                case(directory, family, mode, native_alias=True)
            for mode in ('query_coordinate_direct','query_coordinate_scalar','query_coordinate_projection','query_coordinate_fabric','query_coordinate_fabric_child','array_query_slice','generate_static_init','empty_coordinate_direct','empty_coordinate_scalar','empty_coordinate_projection','empty_coordinate_fabric','empty_coordinate_fabric_child','array_empty_slice','generate_foreach','generate_named_comb','generate_named_foreach','replicated_coordinate_direct','replicated_coordinate_scalar','replicated_coordinate_projection','replicated_coordinate_fabric','replicated_coordinate_fabric_child','array_replicated_slice','fill_coordinate_direct', 'fill_coordinate_scalar', 'fill_coordinate_projection', 'fill_coordinate_fabric', 'fill_coordinate_fabric_child', 'array_fill_slice', 'coordinate_direct', 'coordinate_scalar', 'coordinate_projection', 'coordinate_fabric', 'coordinate_fabric_child', 'direct', 'scalar_binding', 'projection', 'aggregate', 'positional', 'siblings', 'parameter', 'array_row_binding', 'array_slice_binding', 'array_slice_up', 'array_slice_down', 'array_column_binding', 'array_rebased_binding', 'array_slice_fabric', 'array_proper_slice', 'array_row_generate', 'array_slice_generate', 'array_concat_scalar', 'array_concat_mixed', 'array_concat_rows', 'array_concat_child', 'array_pattern_scalar', 'array_pattern_rows', 'array_pattern_nested', 'array_pattern_generate', 'array_keyed_flat', 'array_keyed_nested', 'array_keyed_default', 'array_keyed_generate', 'array_default_scalar', 'array_default_subarray', 'array_default_generate', 'array_repeat_scalar', 'array_repeat_rows', 'array_repeat_nested', 'array_repeat_generate', 'fabric_direct', 'fabric_routed', 'fabric_child', 'fabric_array_direct', 'fabric_array_routed', 'fabric_array_child', 'fabric_array_generated', 'fabric_array_scoped_child', 'fabric_array_standalone', 'generate_process', 'generate_assign', 'generate_assign_implicit', 'generate_assign_named', 'generate_assign_child', 'generate_comb', 'generate_comb_implicit', 'generate_comb_child', 'generate_fsm', 'generate_fsm_implicit', 'generate_pipeline', 'generate_pipeline_implicit', 'generate_pipeline_hierarchy', 'generate_fabric', 'generate_instance', 'generate_siblings', 'generate_child_fabric', 'generate_projections', 'generate_constants', 'generate_elastic', 'generate_signed', 'generate_packed', 'generate_shadow', 'generate_parameters', 'generate_parameters_positional', 'generate_parameters_typed', 'generate_parameters_nested', 'scope_child', 'scope_fabric', 'scope_binding', 'scope_array', 'scope_block', 'scope_constants', 'scope_array_binding', 'scope_elastic', 'scope_relative', 'generate_external', 'generate_external_local', 'generate_external_reuse', 'generate_external_shadow', 'retained_external', 'implicit_process', 'implicit_fabric', 'implicit_child', 'implicit_block', 'implicit_named', 'implicit_external', 'implicit_retained', 'implicit_if', 'implicit_named_block', 'implicit_unbraced', 'implicit_conditional_loop', 'case_generate', 'case_generate_implicit', 'case_generate_default', 'case_generate_xz', 'case_generate_signed', 'case_generate_wide', 'case_generate_ternary', 'case_generate_none', 'case_generate_priority', 'case_generate_conditional'):
                directory = root/f'{family}_{mode}'
                directory.mkdir()
                case(directory, family, mode)
        example = root/'generated_examples.sv'
        run([ROOT/'pigen', ROOT/'examples/native_axi_generated.pigen', '-o', example, '--no-diagram'], root)
        for top in ('native_axis_generated', 'native_axil_generated', 'native_axi4_generated', 'native_axis_child_bank', 'native_axis_configured_bank', 'native_axis_named_bank', 'native_axis_selected_bank', 'native_axis_fsm_bank', 'native_axis_pipeline_bank', 'native_axis_array_link', 'native_axis_row_bank', 'native_axis_joined_ports', 'native_axis_pattern_bank', 'native_axis_keyed_ports', 'native_axis_observed_bank'):
            run(['verilator', '--lint-only', '--top-module', top, example, ROOT/'rtl/pigen_primitives.sv'], root)
        print('PASS parameterized native AXI generate examples')
    except BaseException:
        print(f'Native array evidence retained in {root}')
        raise
    else:
        shutil.rmtree(root)


if __name__ == '__main__':
    main()
