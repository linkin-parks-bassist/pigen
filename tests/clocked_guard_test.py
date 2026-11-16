#!/usr/bin/env python3
"""Exercise smoke-fixture guards as SV decisions, independently of spelling."""
from pathlib import Path
import sys
import tempfile
from native_protocol_test import ROOT, run


def main():
    tool=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'pigen'
    profiles=(
        ('always_block',('enable',),('destination',),'if(enable)e0=1;',True,
         'logic[7:0] ordinary_input=0;wire[7:0] ordinary_output;',
         '.ordinary_input(ordinary_input),.ordinary_output(ordinary_output)'),
        ('guarded_assignment',('enable',),('stage',),'if(enable)e0=1;',False,'',''),
        ('nested_guard',('outer_enable','inner_enable'),('first_stage','second_stage'),
         'if(outer_enable)if(inner_enable)begin e0=1;e1=1;end',False,'',''),
        ('dangling_else_guard',('outer_enable','inner_enable'),('first_stage','second_stage'),
         'if(outer_enable)if(inner_enable)e0=1;else e1=1;',False,'',''),
        ('else_guard',('select_first',),('first_stage','second_stage'),
         'if(select_first)e0=1;else e1=1;',False,'',''),
        ('else_if_guard',('first_select','second_select'),('first_stage','second_stage'),
         'if(first_select)e0=1;else if(second_select)e1=1;',False,'',''),
        ('accessor_guard',(),('destination',),'if(source_valid)e0=1;',True,'',''),
        ('reset_domain',('reset',),('destination',),'if(reset)begin end else e0=1;',True,'',''),
        ('output_port_reset',('reset',),('value',),'e0=1;',False,
         'logic[7:0] next_value=8\'h5a;wire[7:0] value;wire value_valid;logic value_ready=0;',
         '.next_value(next_value),.value(value),.value_valid(value_valid),.value_ready(value_ready)'),
        ('port_bram',('read_enable',),('bram_port',),'if(read_enable)e0=1;',False,
         'logic[3:0] read_address=0,write_address=0;logic[7:0] data_in=0;logic data_in_valid=0;wire data_in_ready;',
         '.read_address(read_address),.write_address(write_address),.data_in(data_in),.data_in_valid(data_in_valid),.data_in_ready(data_in_ready)'),
        ('clear_actions',('discard',),(), 'if(discard)e0=1;else e1=1;',False,'',''),
        ('accepts',(),('destination',),'e0=1;',True,'',''),
        ('conditional_transfer',('enable',),('destination',),'e0=1;',True,'',''),
    )
    with tempfile.TemporaryDirectory(prefix='pigen-clocked-guards-') as temporary:
        root=Path(temporary)
        for name,controls,destinations,decision,consuming,extra,extra_connections in profiles:
            directory=root/name;directory.mkdir()
            sv,tb,binary=[directory/filename for filename in ('design.sv','tb.sv','sim')]
            run([tool,ROOT/'tests'/f'{name}.pigen','-o',sv,'--no-diagram'],directory)
            declarations=''.join(f'logic {control}=0;' for control in controls if control!='reset')
            connections=['.clk(clk)','.reset(reset)']+[f'.{control}({control})' for control in controls if control!='reset']
            if extra_connections:connections.append(extra_connections)
            drive=''.join(f'{control}=four_state((trial>>{2*index})&3);' for index,control in enumerate(controls))
            if name=='always_block':drive+="ordinary_input=8'(trial+1);"
            forces=''.join(f'force dut.{destination}__pigen_in_ready=ready{index};'
                           for index,destination in enumerate(destinations))
            checks=''
            for index,destination in enumerate(destinations):
                expected=f'(e{index}&&ready{index}'+('&&source_valid' if consuming else '')+')'
                checks+=(f'if(dut.{destination}__pigen_in_valid!=={expected})'
                         f'$fatal(1,"{name} destination {index} guard/ready mismatch trial=%0d",trial);')
            if consuming:
                forces+='force dut.source__pigen_valid=source_valid;'
                checks+=('if(dut.source__pigen_out_ready!==(e0&&ready0&&source_valid))'
                         f'$fatal(1,"{name} consumed outside the selected ready transfer");')
            if name=='clear_actions':
                checks+=('if(dut.stage__pigen_force_invalid!==e0||dut.queue__pigen_clear!==e1)'
                         '$fatal(1,"validity action did not follow native if/else decision");')
            if name=='always_block':
                checks+='if(ordinary_output!==ordinary_input)$fatal(1,"ordinary always block changed");'
            if name=='output_port_reset':
                checks+=('if(ready0)begin if(reset)begin if(dut.value__pigen_packet_in!==8\'d0)$fatal(1,"reset payload");end '
                         'else if(dut.value__pigen_packet_in!==next_value)$fatal(1,"else payload for unknown reset");end ')
            when_event=name in ('accepts','conditional_transfer')
            cycles=256 if when_event else 128
            source_drive='source_valid=four_state((trial>>6)&3);' if when_event else 'source_valid=trial[6];'
            if when_event:
                checks+=('dut.did_transfer=0;clk=1;#1;e1=0;if(source_valid&&ready0)e1=1;'
                         'if(dut.did_transfer!==e1)$fatal(1,"when body did not follow known fire");clk=0;#1;')
            top='accepts_example' if name=='accepts' else name
            tb.write_text('module tb;logic clk=0,reset=0,source_valid=0,ready0=0,ready1=0,e0=0,e1=0;integer trial;'+
                          declarations+extra+f'{top} dut('+','.join(connections)+');'+
                          "function automatic logic four_state(input integer value);case(value)0:four_state=0;1:four_state=1;2:four_state=1'bx;default:four_state=1'bz;endcase endfunction "+
                          f'initial begin for(trial=0;trial<{cycles};trial++)begin '+drive+
                          source_drive+'ready0=trial[4];ready1=trial[5];e0=0;e1=0;'+decision+' '+
                          forces+'#1;'+checks+'end '+
                          f'$display("PASS {name}: {cycles} native guard, readiness and consumption decisions");$finish;end endmodule')
            run(['iverilog','-g2012','-s','tb','-o',binary,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
            print(run(['vvp',binary],directory).strip(),flush=True)


if __name__=='__main__':
    main()
