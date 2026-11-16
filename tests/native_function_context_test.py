#!/usr/bin/env python3
"""Native field arguments must work in accepted ordinary function contexts.

This is an acceptance regression, not an expected-failure test. It deliberately
checks ordinary buffered equivalents first so a shared language limitation
cannot be mistaken for a native AXI compatibility failure.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
from native_protocol_test import channels


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/native-function-contexts'


def command(args, directory):
    result = subprocess.run(list(map(str, args)), cwd=directory, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=90)
    with (directory / 'commands.log').open('a') as log:
        log.write('$ ' + ' '.join(map(str, args)) + '\n' + result.stdout)
    return result


def case(compiler, family, context):
    directory = BUILD / f'{family}-{context}'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'commands.log').write_text('')
    source, sv = directory / 'relay.pigen', directory / 'relay.sv'
    datatype = 'bit[32] buf' if family == 'ordinary' else f'uint[32] {family}'
    field = {'ordinary': 's', 'axi4_stream': 's.data'}.get(family, 's.w.data')
    transfer = 'm<=s;' if family in ('ordinary', 'axi4_stream') else 'm.w<=s.w;'
    call = f'temp<=inc({field});'
    body = {
        'continuous': f'assign temp=inc({field});',
        'combinational': f'always_comb temp=inc({field});',
        'clocked': f'always @(posedge clk) {call}',
        'mixed': f'always @(posedge clk) begin {transfer}{call}end',
    }[context]
    source.write_text(
        f'module relay(input wire clk,input wire reset,input {datatype} s,'
        f'output {datatype} m,output logic[31:0] temp);\n'
        'function automatic logic[31:0] inc(input logic[31:0] x);\n'
        "inc=x+32'd1;endfunction\n" + body + '\nendmodule\n')
    result = command([compiler, source, '-o', sv, '--no-diagram'], directory)
    if result.returncode:
        return f'{family}/{context}: compiler rejected an accepted function context\n{result.stdout}'
    result = command(['verilator', '--lint-only', '--top-module', 'relay',
                      sv, ROOT / 'rtl/pigen_primitives.sv'], directory)
    if result.returncode:
        return f'{family}/{context}: generated SV failed fatal lint\n{result.stdout}'
    print(f'PASS {family}/{context}: compile and fatal SV lint')
    return None


def transport(compiler, family, context, datatype, storage, child=False, global_helpers=False):
    tag = datatype.replace('[', '_').replace(']', '').replace(' ', '_')
    suffix = ('-child' if child else '') + ('-global' if global_helpers else '')
    directory = BUILD / f'flow-{family}-{context}-{tag}-{storage}{suffix}'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'commands.log').write_text('')
    source, sv, tb, sim = [directory / n for n in ('relay.pigen', 'relay.sv', 'tb.sv', 'sim')]
    stream = family == 'axi4_stream'
    endpoint = 'worker.m' if child else 's'
    field = endpoint + ('.data' if stream else '.w.data')
    transfer = f'm<={endpoint};' if stream else f'm.w<={endpoint}.w;'
    move = f'if(gate)begin {transfer}end '
    calls = f'temp<=observe({field});negative<=flag({field}<threshold);'
    if context == 'continuous':
        body = f'assign temp=observe({field});assign negative=flag({field}<threshold);always @(posedge clk){move}'
    elif context == 'combinational':
        body = f'always_comb begin temp=observe({field});negative=flag({field}<threshold);end always @(posedge clk){move}'
    elif context == 'clocked':
        body = f'always @(posedge clk)begin {calls}end always @(posedge clk){move}'
    else:
        body = f'always @(posedge clk)begin {move}{calls}end'
    prefix_source = 'typedef struct packed{logic[15:0] lo;logic[15:0] hi;} pair_t;\n' if datatype == 'pair_t' else ''
    if child:
        prefix_source += (f'module worker_module(input wire clk,input wire reset,input {datatype} {family} {storage} s,'
                          f'output {datatype} {family} {storage} m);always @(posedge clk)'
                          + ('m<=s;' if stream else 'm.w<=s.w;') + 'endmodule\n')
    observe = ('function automatic logic[31:0] observe(input pair_t s);observe={s.hi,s.lo};endfunction\n'
               if datatype == 'pair_t' else
               'function automatic logic[31:0] observe(input logic[31:0] s);observe={s[15:0],s[31:16]};endfunction\n')
    helpers = observe + 'function automatic logic flag(input logic value);flag=value;endfunction\n'
    if global_helpers:
        # A global body's typedef must remain local, while its formal shadows
        # the caller's native endpoint spelling just as a module helper does.
        helpers = helpers.replace('observe={s[15:0],s[31:16]};',
                                  'typedef logic[31:0] local_word;local_word copy;copy=s;observe={copy[15:0],copy[31:16]};')
        prefix_source += helpers
    source.write_text(prefix_source +
        f'module relay(input wire clk,input wire reset,input wire gate,input logic signed[31:0] threshold,input {datatype} {family} {storage} s,'
        f'output {datatype} {family} {storage} m,output logic[31:0] temp,output logic negative);\n'
        # The formal intentionally shadows the native endpoint spelling.
        + ('worker_module worker(.clk(clk),.reset(reset),.s(s));\n' if child else '')
        + ('' if global_helpers else helpers) + body + '\nendmodule\n')
    def checked(args):
        result = command(args, directory)
        assert result.returncode == 0, result.stdout
        return result.stdout
    checked([compiler, source, '-o', sv, '--no-diagram'])
    checked(['verilator', '--lint-only', '--top-module', 'relay', sv, ROOT / 'rtl/pigen_primitives.sv'])
    prefix = 't' if stream else 'w'
    extra = '' if stream else ",.s_wstrb(4'hf),.m_wstrb()" + (",.s_wlast(1'b1),.m_wlast()" if family == 'axi4' else '')
    signed = datatype.startswith('int') or 'signed' in datatype
    four_state = datatype.startswith('logic')
    expected_sign = '$signed(packet)<0' if signed else "1'b0"
    tb.write_text(f'''module tb;
logic clk=0,reset=1,gate=0;always #5 clk=~clk;
logic[31:0] payload=0;logic offered=0,ready=0;wire accepted,valid;
wire[31:0] result,temp;wire negative;
relay dut(.clk(clk),.reset(reset),.gate(gate),.threshold(32'sd0),.s_{prefix}data(payload),.s_{prefix}valid(offered),.s_{prefix}ready(accepted),
.m_{prefix}data(result),.m_{prefix}valid(valid),.m_{prefix}ready(ready),.temp(temp),.negative(negative){extra});
logic[31:0] packet;integer sent=0,received=0,epoch;
always @(posedge clk)if(!reset)begin
 if(offered&&accepted)sent++;
 if(valid&&ready)received++;
end
initial begin
 for(epoch=0;epoch<24;epoch++)begin
  @(negedge clk);reset=1;gate=0;offered=0;ready=0;
  repeat(3)@(negedge clk);
  if(valid!==0)$fatal(1,"reset left a retained packet");
  packet=32'h8123abcd+32'h10403081*32'(epoch);
  ''' + ("if(epoch%3==1)packet[5]=1'bx;if(epoch%3==2)packet[9]=1'bz;" if four_state else '') + f'''
  reset=0;payload=packet;offered=1;
  do @(posedge clk);while(!accepted);
  @(negedge clk);offered=0;payload=~packet;
  // The physical input is deliberately changed after acceptance. A raw-pin
  // replacement, or an observation which consumes the token, must fail.
  repeat(5)@(negedge clk);
  if(temp!=={{packet[15:0],packet[31:16]}}||negative!==({expected_sign}))
   $fatal(1,"function lost stored DATA, signedness or four-state bits");
  if(valid!==0)$fatal(1,"observation unexpectedly produced a packet");
  gate=1;wait(valid===1);@(negedge clk);gate=0;
  repeat(4)begin
   if(!valid||result!==packet)$fatal(1,"function observation consumed or corrupted the packet");
   @(negedge clk);
  end
  // Alternate queued reset and explicit drain, checking both lifecycles.
  if(epoch%2==0)begin ready=1;@(posedge clk);@(negedge clk);ready=0;end
 end
 if(sent!=24||received!=12)$fatal(1,"independent function-context count mismatch %0d %0d",sent,received);
 $display("PASS retained native function transport");$finish;
end
initial begin #100000;$fatal(1,"retained function transport timeout");end
endmodule
''')
    checked(['iverilog', '-g2012', '-s', 'tb', '-o', sim, sv, tb, ROOT / 'rtl/pigen_primitives.sv'])
    assert 'PASS retained native function transport' in checked(['vvp', sim])
    print(f'PASS {family}/{context}/{datatype}/{storage}/child={child}/global={global_helpers}: stored observations, stalls, reset and independent pin counts')


def lexical_and_negative(compiler, family):
    field = 's.data' if family == 'axi4_stream' else 's.w.data'
    header = ('module relay(input wire clk,input wire reset,input wire other,'
              f'input uint[32] {family} s,output logic[31:0] temp);')
    function = "function automatic logic[31:0] inc(input logic[31:0] x);inc=x+32'd1;endfunction\n"
    positives = {
        'formal_record_shadow': ('typedef struct packed{logic[31:0] data;} local_t;\n',
            'function automatic logic[31:0] inc(input local_t s);inc=s.data+32\'d1;endfunction\n',
            f'assign temp=inc({field});'),
        'block_record_shadow': ('typedef struct packed{logic[31:0] data;} local_t;\n', function,
            "always @(posedge clk)begin:worker local_t s;s.data=32'h8123abcd;temp<=inc(s.data);end"),
        'event_clock_shadow': ('', function,
            f"always @(posedge clk)begin:worker logic clk;clk=1'b0;temp<=inc({field});end"),
        'macro_argument': (f'`define FIELD {field}\n', function, 'assign temp=inc(`FIELD);'),
        'captured_field': ('', function + f'function automatic logic[31:0] captured();captured=inc({field});endfunction\n',
            'assign temp=captured();'),
        'local_index': ('', function, f'always_comb begin integer coordinate;coordinate=3;temp=inc({{24\'b0,{field}[coordinate +: 8]}});end'),
    }
    negatives = {
        'continuous_write': (f'assign {field}=inc(32\'d1);', 'payload writes require a structured transfer'),
        'clocked_write': (f'always @(posedge clk){field}<=inc(32\'d1);', 'payload writes require a structured transfer'),
        'concat_write': (f'always @(posedge clk){{{field},temp}}<=inc(32\'d1);', 'payload writes require a structured transfer'),
        'increment_write': (f'always @(posedge clk)begin temp<=inc({field});++{field};end', 'payload writes require a structured transfer'),
        'argument_postincrement': (f'always @(posedge clk)temp<=inc({field}++);', 'payload writes require a structured transfer'),
        'argument_preincrement': (f'always @(posedge clk)temp<=inc(++{field});', 'payload writes require a structured transfer'),
        'argument_compound_write': (f'always @(posedge clk)temp<=inc(({field}+=32\'d1));', 'payload writes require a structured transfer'),
        'argument_grouped_increment': (f'always @(posedge clk)temp<=inc(({field})++);', 'payload writes require a structured transfer'),
        'negedge': (f'always @(negedge clk)temp<=inc({field});', 'bound rising-edge clock'),
        'other_clock': (f'always @(posedge other)temp<=inc({field});', 'across clock domains'),
        'async_reset': (f'always @(posedge clk or posedge reset)temp<=inc({field});', 'bound rising-edge clock'),
    }
    for name, (prefix, routine, body) in positives.items():
        directory = BUILD / f'{family}-{name}'
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'commands.log').write_text('')
        source, sv = directory / 'relay.pigen', directory / 'relay.sv'
        source.write_text(prefix + header + routine + body + '\nendmodule\n')
        for args in ([compiler, source, '-o', sv, '--no-diagram'],
                     ['verilator', '--lint-only', '--top-module', 'relay', sv, ROOT / 'rtl/pigen_primitives.sv']):
            result = command(args, directory)
            assert result.returncode == 0, f'{family}/{name}: {result.stdout}'
    for name, (body, diagnostic) in negatives.items():
        directory = BUILD / f'{family}-{name}'
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'commands.log').write_text('')
        source, sv = directory / 'relay.pigen', directory / 'relay.sv'
        source.write_text(header + function + body + '\nendmodule\n')
        result = command([compiler, source, '-o', sv, '--no-diagram'], directory)
        assert result.returncode != 0 and diagnostic in result.stdout, f'{family}/{name}: {result.stdout}'
    print(f'PASS {family}: lexical/formal shadows, macro and captured arguments, local indices, write/timing negatives')


def channel_observations(compiler, family):
    """Independently specified pins cover every channel and enabled field."""
    directory = BUILD / f'all-fields-{family}'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'commands.log').write_text('')
    source, sv, tb, sim = [directory / n for n in ('relay.pigen', 'relay.sv', 'tb.sv', 'sim')]
    schema = channels(family, 32)
    config = {'axi4_stream': 'keep=1,strb=1,last=1,id=4,dest=3,user=7',
              'axi4_lite': 'addr=40',
              'axi4': 'addr=40,id=4,awuser=3,wuser=7,buser=5,aruser=11,ruser=13'}[family]
    functions, observations, transfers, declarations, connections = [], [], [], [], []
    inputs, outputs, initialize, corrupt = [], [], [], []
    field_count = 0
    for lane, (channel, fields) in enumerate(schema.items()):
        incoming, outgoing = ('m', 's') if channel in ('b', 'r') else ('s', 'm')
        selection = 'beat' if channel == 't' else channel
        transfers.append(f'{outgoing}.{selection}<={incoming}.{selection};')
        connections.extend([f'.{incoming}_{channel}valid(offered[{lane}])',
                            f'.{incoming}_{channel}ready(accepted[{lane}])',
                            f'.{outgoing}_{channel}valid(valid[{lane}])',
                            f'.{outgoing}_{channel}ready(1\'b0)'])
        for field, width in fields:
            name = f'{channel}_{field}'
            functions.append(f'function automatic logic[{width-1}:0] observe_{name}(input logic[{width-1}:0] value);'
                             f'observe_{name}=~value;endfunction\n')
            observations.append(f'observe_{name}({incoming}.{selection}.{field})')
            declarations.append(f'logic[{width-1}:0] input_{name};wire[{width-1}:0] output_{name};')
            connections.extend([f'.{incoming}_{channel}{field}(input_{name})',
                                f'.{outgoing}_{channel}{field}(output_{name})'])
            inputs.append(f'input_{name}')
            outputs.append(f'output_{name}')
            initialize.append(f"input_{name}={width}'(64'h8123456789abcdef ^ (64'(epoch)*64'h1020304050607081) ^ 64'd{field_count});")
            if field == 'data':
                initialize.append(f"if(epoch%3==1)input_{name}[5]=1'bx;if(epoch%3==2)input_{name}[9]=1'bz;")
            corrupt.append(f'input_{name}=~input_{name};')
            field_count += 1
    bits = sum(width for fields in schema.values() for _, width in fields)
    source.write_text(f'module relay(input wire clk,input wire reset,input wire gate,'
                      f'input logic[31:0] {family}[{config}] fifo[3] s,'
                      f'output logic[31:0] {family}[{config}] fifo[3] m,output logic[{bits-1}:0] snapshot);\n'
                      + ''.join(functions) + 'assign snapshot={' + ','.join(observations) + '};\n'
                      + 'always @(posedge clk)if(gate)begin ' + ''.join(transfers) + 'end\nendmodule\n')
    lanes = len(schema)
    tb.write_text(f'''module tb;logic clk=0,reset=1,gate=0;always #5 clk=~clk;
logic[{lanes-1}:0] offered=0;wire[{lanes-1}:0] accepted,valid;logic[{lanes-1}:0] sampled;
wire[{bits-1}:0] snapshot;logic[{bits-1}:0] packet;integer epoch;
{''.join(declarations)}
relay dut(.clk(clk),.reset(reset),.gate(gate),.snapshot(snapshot),{','.join(connections)});
initial begin
 for(epoch=0;epoch<16;epoch++)begin
  @(negedge clk);reset=1;gate=0;offered=0;repeat(3)@(negedge clk);
  if(valid!==0)$fatal(1,"channel reset left valid packets");
  {''.join(initialize)}
  packet={{{','.join(inputs)}}};reset=0;offered='1;
  while(offered!==0)begin @(posedge clk);sampled=offered&accepted;@(negedge clk);offered=offered&~sampled;end
  {''.join(corrupt)}
  repeat(5)@(negedge clk);
  if(snapshot!==~packet||valid!==0)$fatal(1,"retained field mapping or stored observation mismatch");
  gate=1;wait(valid==='1);@(negedge clk);gate=0;
  repeat(4)begin
   if(valid!=='1||{{{','.join(outputs)}}}!==packet)$fatal(1,"field observation consumed or changed a channel packet");
   @(negedge clk);
  end
 end
 $display("PASS all retained channel fields");$finish;
end
initial begin #100000;$fatal(1,"all-field observation timeout");end endmodule
''')
    for args in ([compiler, source, '-o', sv, '--no-diagram'],
                 ['verilator', '--lint-only', '--top-module', 'relay', sv, ROOT / 'rtl/pigen_primitives.sv'],
                 ['iverilog', '-g2012', '-s', 'tb', '-o', sim, sv, tb, ROOT / 'rtl/pigen_primitives.sv'],
                 ['vvp', sim]):
        result = command(args, directory)
        assert result.returncode == 0, f'{family}/all-fields: {result.stdout}'
    assert 'PASS all retained channel fields' in result.stdout
    print(f'PASS {family}: {field_count} independently specified channel fields, stored function arguments, X/Z, stalls and queued reset')


def main():
    global BUILD
    compiler = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'pigen'
    BUILD.mkdir(parents=True, exist_ok=True)
    BUILD = Path(tempfile.mkdtemp(prefix=compiler.name + '-', dir=BUILD))
    print(f'Artifacts: {BUILD}', flush=True)
    failures = []
    for family in ('ordinary', 'axi4_stream', 'axi4_lite', 'axi4'):
        for context in ('continuous', 'combinational', 'clocked', 'mixed'):
            failure = case(compiler, family, context)
            if failure:
                failures.append(failure)
        if family == 'ordinary' and failures:
            raise AssertionError('ordinary compatibility baseline failed:\n' + '\n'.join(failures))
    if failures:
        raise AssertionError(f'{len(failures)} native compatibility failures:\n' + '\n'.join(failures))
    for family in ('axi4_stream', 'axi4_lite', 'axi4'):
        channel_observations(compiler, family)
        lexical_and_negative(compiler, family)
        for context in ('continuous', 'combinational', 'clocked', 'mixed'):
            for datatype, storage in (('uint[32]', 'buf'), ('int[32]', 'fifo[3]'), ('logic signed[31:0]', 'skid'), ('pair_t', 'buf')):
                transport(compiler, family, context, datatype, storage)
            transport(compiler, family, context, 'logic signed[31:0]', 'fifo[3]', child=True)
            transport(compiler, family, context, 'logic signed[31:0]', 'fifo[3]', global_helpers=True)


if __name__ == '__main__':
    main()
