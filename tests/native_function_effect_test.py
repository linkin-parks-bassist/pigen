#!/usr/bin/env python3
"""Writable subroutine actuals must not bypass native payload ownership.

Acceptance regression: readonly input/const-ref calls must compile; output,
inout and ref actuals targeting native fields must reject before publication.
"""
from pathlib import Path
import sys
import tempfile
from native_function_context_test import ROOT, command


def extended(compiler, root, family, failures):
    field = 's.data' if family == 'axi4_stream' else 's.w.data'
    readonly = 'function automatic logic[31:0] f(input logic[31:0] x);f=x;endfunction\n'
    writable = 'function automatic logic[31:0] f(output logic[31:0] x);x=42;f=7;endfunction\n'
    mixed = 'function automatic logic[31:0] f(output logic[31:0] a,input logic[31:0] x);a=42;f=x;endfunction\n'
    cases = {
        'inherited_input': ('', 'function automatic logic[31:0] f(input logic[31:0] a,x);f=a^x;endfunction\n', f'temp<=f(scratch,{field});', False),
        'inherited_output': ('', 'function automatic logic[31:0] f(output logic[31:0] a,logic[31:0] x);a=1;x=2;f=3;endfunction\n', f'temp<=f(scratch,{field});', True),
        'explicit_input': ('', mixed, f'temp<=f(scratch,{field});', False),
        'const_ref_inherited': ('', 'function automatic logic[31:0] f(const ref logic[31:0] a,x);f=a^x;endfunction\n', f'temp<=f(scratch,{field});', False),
        'ref_after_const': ('', 'function automatic logic[31:0] f(const ref logic[31:0] a,ref logic[31:0] x);x=a;f=x;endfunction\n', f'temp<=f(scratch,{field});', True),
        'implicit_ansi_input': ('', 'function automatic logic[31:0] f(x);f={31\'d0,x};endfunction\n', f'temp<=f({field}[0]);', False),
        'old_declarations_input': ('', 'function automatic logic[31:0] f;input logic[31:0] x;f=x;endfunction\n', f'temp<=f({field});', False),
        'old_declarations_output': ('', 'function automatic logic[31:0] f;output logic[31:0] x;x=42;f=7;endfunction\n', f'temp<=f({field});', True),
        'named_input': ('', mixed, f'temp<=f(.x({field}),.a(scratch));', False),
        'named_output': ('', 'function automatic logic[31:0] f(input logic[31:0] a,output logic[31:0] x);x=a;f=7;endfunction\n', f'temp<=f(.x({field}),.a(scratch));', True),
        'writable_array_index': ('', writable, f'temp<=f(values[{field}[1:0]]);', False),
        'writable_concat': ('', writable, f'temp<=f({{{field}[15:0],scratch[15:0]}});', True),
        'writable_signed_cast': ('', writable, f'temp<=f($signed({field}));', True),
        'writable_size_cast': ('', writable, f'temp<=f(32\'({field}));', True),
        'writable_folded_binary': ('', writable, f'temp<=f({field}+32\'d0);', True),
        'readonly_signed_cast': ('', readonly, f'temp<=f($signed({field}));', False),
        'readonly_size_cast': ('', readonly, f'temp<=f(32\'({field}));', False),
        'task_input': ('', 'task automatic take(input logic[31:0] x);temp=x;endtask\n', f'take({field});', False),
        'task_output': ('', 'task automatic take(output logic[31:0] x);x=42;endtask\n', f'take({field});', True),
        'unknown_callee': ('', '', f'temp<=external_helper({field});', 'known retained subroutine signature'),
        'system_read': ('', '', f'temp<=$signed({field});', False),
        'system_write': ('', '', f'temp<=$random({field});', 'known retained subroutine signature'),
    }
    for direction in ('input', 'output'):
        function = readonly if direction == 'input' else writable
        package = 'package helpers;typedef logic[31:0] word_t;\n' + function + 'endpackage\n'
        cases['qualified_' + direction] = (package, '', f'temp<=helpers::f({field});', direction == 'output')
        cases['import_' + direction] = (package, 'import helpers::f;\n', f'temp<=f({field});', direction == 'output')
    # Local declarations outrank enclosing wildcard candidates; a body-local
    # import instead chooses the inner routine. Emitted calls must agree.
    cases['local_write_over_wildcard'] = ('package helpers;typedef logic[31:0] word_t;\n' + readonly + 'endpackage\n',
                                        'import helpers::*;\n' + writable, f'temp<=f({field});', True)
    cases['local_read_over_wildcard'] = ('package helpers;typedef logic[31:0] word_t;\n' + writable + 'endpackage\n',
                                       'import helpers::*;\n' + readonly, f'temp<=f({field});', False)
    cases['inner_import_write'] = ('package helpers;typedef logic[31:0] word_t;\n' + writable + 'endpackage\n',
                                  readonly, f'import helpers::f;temp<=f({field});', True)
    cases['inner_import_read'] = ('package helpers;typedef logic[31:0] word_t;\n' + readonly + 'endpackage\n',
                                 writable, f'import helpers::f;temp<=f({field});', False)
    macro = ('`define ROUTINES ' + readonly.strip() + ' ' + writable.replace(' f(', ' g(').replace('f=7', 'g=7').strip() + '\n'
             'package helpers;typedef logic[31:0] word_t;`ROUTINES\nendpackage\n')
    cases['macro_definitions_read'] = (macro, '', f'temp<=helpers::f({field});', False)
    cases['macro_definitions_write'] = (macro, '', f'temp<=helpers::g({field});', True)
    cases['macro_definitions_import'] = (macro, 'import helpers::f;\n', f'temp<=f({field});', False)
    cases['macro_named_actual'] = ('`define CALL(X) f(.x(X))\n', readonly, f'temp<=`CALL({field});', False)
    for direction, routine in (('read', readonly), ('write', writable)):
        cases['child_' + direction] = ('module worker;\n' + routine + 'endmodule\n',
                                       'worker inst();\n', f'temp<=inst.f({field});', direction == 'write')
        cases['type_shadow_' + direction] = ('typedef logic[31:0] axi4_lite f;\n',
                                            routine, f'temp<=f({field});', direction == 'write')
    cases['forward_type_shadow_read'] = ('typedef logic[31:0] axi4_lite f;\n', readonly, f'temp<=f({field});', False)
    cases['global_read'] = (readonly, '', f'temp<=f({field});', False)
    cases['global_write'] = (writable, '', f'temp<=f({field});', True)
    cases['global_const_ref'] = (readonly.replace('input', 'const ref'), '', f'temp<=f({field});', False)
    cases['global_named_read'] = (readonly, '', f'temp<=f(.x({field}));', False)
    cases['global_old_read'] = ('function automatic logic[31:0] f;input logic[31:0] x;f=x;endfunction\n', '', f'temp<=f({field});', False)
    cases['global_task_read'] = ('task automatic take(input logic[31:0] x,output logic[31:0] y);y=x;endtask\n', '', f'take({field},temp);', False)
    cases['global_task_write'] = ('task automatic take(output logic[31:0] x);x=42;endtask\n', '', f'take({field});', True)
    cases['global_unit_read'] = (readonly, writable, f'temp<=$unit::f({field});', False)
    cases['global_unit_write'] = (writable, readonly, f'temp<=$unit::f({field});', True)
    cases['global_local_read'] = (writable, readonly, f'temp<=f({field});', False)
    cases['global_local_write'] = (readonly, writable, f'temp<=f({field});', True)
    cases['global_forward_read'] = ('', '', f'temp<=f({field});', False)
    global_macro = ('`define ROUTINES ' + readonly.strip() + ' ' + writable.replace(' f(', ' g(').replace('f=7', 'g=7').strip() + '\n`ROUTINES\n')
    cases['global_macro_read'] = (global_macro, '', f'temp<=f({field});', False)
    cases['global_macro_write'] = (global_macro, '', f'temp<=g({field});', True)
    for name, (prefix, routine, body, rejected) in cases.items():
        directory = root / (family + '-' + name)
        directory.mkdir()
        source, sv = directory / 'probe.pigen', directory / 'probe.sv'
        dtype = 'helpers::word_t' if 'package helpers;' in prefix else 'logic[31:0]'
        before, after = ('', routine) if name.startswith('forward_') else (routine, '')
        source.write_text(prefix + 'module probe(input wire clk,input wire reset,'
                          f'input {dtype} {family} s,output logic[31:0] temp);\n'
                          'logic[31:0] scratch;logic[31:0] values[0:3];\n' + before
                          + 'always @(posedge clk)begin ' + body + 'end\n' + after + 'endmodule\n'
                          + (readonly if name == 'global_forward_read' else ''))
        sentinel = '// preserve previous output\n'
        sv.write_text(sentinel)
        result = command([compiler, source, '-o', sv, '--no-diagram'], directory)
        if rejected:
            diagnostic = rejected if isinstance(rejected, str) else 'writable subroutine arguments'
            if result.returncode == 0 or sv.read_text() != sentinel or diagnostic not in result.stdout:
                failures.append(f'{family}/{name}: expected {diagnostic}\n{result.stdout}')
        elif result.returncode:
            failures.append(f'{family}/{name}: readonly actual rejected\n{result.stdout}')
        else:
            result = command(['verilator', '--lint-only', '--top-module', 'probe', sv,
                              ROOT / 'rtl/pigen_primitives.sv'], directory)
            if result.returncode:
                failures.append(f'{family}/{name}: generated SV failed lint\n{result.stdout}')


def main():
    compiler = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'pigen'
    root = ROOT / 'build/native-function-effects'
    root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=compiler.name + '-', dir=root))
    print(f'Artifacts: {root}', flush=True)
    failures = []
    for family in ('axi4_stream', 'axi4_lite', 'axi4'):
        field = 's.data' if family == 'axi4_stream' else 's.w.data'
        for direction in ('input', 'const ref', 'output', 'inout', 'ref'):
            writable = direction in ('output', 'inout', 'ref')
            directory = root / (family + '-' + direction.replace(' ', '-'))
            directory.mkdir()
            source, sv = directory / 'probe.pigen', directory / 'probe.sv'
            source.write_text(
                'module probe(input wire clk,input wire reset,'
                f'input logic[31:0] {family} s,output logic[31:0] temp);\n'
                f'function automatic logic[31:0] f({direction} logic[31:0] x);'
                + ("x=32'd42;" if writable else '')
                + "f=x+32'd1;endfunction\nalways @(posedge clk)temp<=f(" + field + ');\nendmodule\n')
            sentinel = '// preserve previous output\n'
            sv.write_text(sentinel)
            result = command([compiler, source, '-o', sv, '--no-diagram'], directory)
            if writable:
                if result.returncode == 0 or sv.read_text() != sentinel:
                    failures.append(f'{family}/{direction}: native writable actual was accepted or output changed')
            elif result.returncode:
                failures.append(f'{family}/{direction}: readonly call rejected\n{result.stdout}')
            else:
                result = command(['verilator', '--lint-only', '--top-module', 'probe', sv,
                                  ROOT / 'rtl/pigen_primitives.sv'], directory)
                if result.returncode:
                    failures.append(f'{family}/{direction}: readonly generated SV failed lint\n{result.stdout}')
        extended(compiler, root, family, failures)
    if failures:
        raise AssertionError('\n'.join(failures))
    print('PASS native readonly and writable actual directions across all AXI families')


if __name__ == '__main__':
    main()
