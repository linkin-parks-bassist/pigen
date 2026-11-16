#!/usr/bin/env python3
"""Compile native cores, run independent protocol checkers, retain reproducible evidence."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]

def run(argv, cwd, log):
    result = subprocess.run([str(x) for x in argv], cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    with log.open('a') as stream:
        stream.write('$ ' + ' '.join(map(str, argv)) + '\n' + result.stdout)
    if result.returncode:
        raise RuntimeError(f'{argv[0]} failed ({result.returncode}); see {log}\n{result.stdout}')
    return result.stdout

def check_native_loop_control(directory, generated, original, log):
    # Use the authored algorithm directly as native SV, with explicit widths
    # and array direction, rather than reproducing compiler control lowering.
    start=original.index('module loop_control(')
    reference=original[start:original.index('\nendmodule',start)+len('\nendmodule')]
    reference=reference.replace('module loop_control(', 'module native_loop_control(').replace('results[10]', 'results[9:0]')
    reference=reference.replace('peek(value)', 'value').replace('if(i==value[2:0])', "if(i==32'(value[2:0]))")
    reference=reference.replace('while(counter<value[2:0])', "while(counter<8'(value[2:0]))")
    reference=reference.replace('while(selector)', 'while(|selector)')
    reference=reference.replace('accumulator+=10;', "accumulator+=8'd10;").replace('case(counter) 2:', "case(counter) 8'd2:")
    native=directory/'native_loop_control.sv';native.write_text(reference)
    bench=directory/'native_loop_control_tb.sv'
    bench.write_text("""module native_loop_control_tb;
logic [7:0] value=0;
wire [7:0] actual[9:0],expected[9:0];
loop_control compiled(value,actual);
native_loop_control reference_model(value,expected);
initial begin
    for(integer n=0;n<256;n++) begin
        value=8'(n);#1;
        for(integer k=0;k<10;k++) if(actual[k]!==expected[k])
            $fatal(1,"native loop mismatch byte=%0d result=%0d",n,k);
    end
    $display("PASS native SV loop control parity: 256 values");$finish;
end
endmodule
""")
    build=directory/'native-loop-build'
    run(['verilator','--binary','--timing','--top-module','native_loop_control_tb','--Mdir',build]+generated+[native,bench],ROOT,log)
    assert 'PASS native SV loop control parity' in run([build/'Vnative_loop_control_tb'],directory,log)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=ROOT/'build/cores')
    args = parser.parse_args()
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    compiler = ROOT/'pigen'
    cases = []
    for div in (4,5,8,16,31):
        cases.append((f'uart-div{div}', 'uart_tb', [('uart/tx.pigen', {'CLKS_PER_BIT':div}),
                      ('uart/rx.pigen', {'CLKS_PER_BIT':div})], {'DIV':div}, []))
    for bits in range(5,10):
        for parity in range(3):
            for stops in (1,2):
                for div in (4,7):
                    config=dict(CLKS_PER_BIT=div,DATA_BITS=bits,PARITY=parity,STOP_BITS=stops)
                    cases.append((f'uart-{bits}{"neo"[parity]}{stops}-div{div}', 'uart_framing_tb',
                                  [('uart/tx.pigen',config),('uart/rx.pigen',config)],
                                  dict(DIV=div,BITS=bits,MODE=parity,STOPS=stops),[]))
    for mode in range(4):
        for half in (2,4,7):
            cases.append((f'spi-mode{mode}-half{half}', 'spi_tb',
                          [('spi/controller.pigen', {'HALF_PERIOD':half,'CPOL':mode//2,'CPHA':mode%2})],
                          {'HALF':half,'POL':mode//2,'PHA':mode%2}, []))
    for mode in range(4):
        for half in (61,73,107):
            cases.append((f'spi-target-mode{mode}-half{half}', 'spi_target_tb',
                          [('spi/target.pigen', {'CPOL':mode//2,'CPHA':mode%2})],
                          {'HALF':half,'POL':mode//2,'PHA':mode%2}, []))
    for address in (8,82,119):
        for half,setup in ((83,2),(107,4),(173,9)):
            cases.append((f'i2c-target-addr{address}-half{half}', 'i2c_target_tb',
                          [('i2c/target.pigen', {'ADDRESS':address,'SETUP_CYCLES':setup})],
                          {'ADDR':address,'HALF':half,'SETUP':setup}, []))
    for half in (2,4,9):
        cases.append((f'i2c-half{half}', 'i2c_tb', [('i2c/byte_controller.pigen',{'HALF_PERIOD':half})], {'HALF':half}, []))
    for half in (2,4,7):
        cases.append((f'i2s-half{half}', 'i2s_tb', [('i2s/controller.pigen',{'HALF_PERIOD':half})], {'HALF':half}, []))
    cases += [('when-value','when_value_tb',[],{},[]),('transfer-flow','transfer_flow_tb',[],{},[]),('concat-resize','concat_resize_tb',[],{},[]),('integer-resize','integer_resize_tb',[],{},[]),('continuous-intrinsics','continuous_intrinsics_tb',[],{},[]),('retained-parameters','retained_parameters_tb',[],{},[]),('intrinsic-unary','intrinsic_unary_tb',[],{},[]),('axis-fifo','axis_tb',[('axis/fifo.pigen',{})],{},[]),
              ('child-output-concat','child_concat_tb',[],{},[])]
    for seed in (1,17,4919):
        cases.append((f'axi4-seed{seed}', 'axi4_tb', [('axi4/bridge.pigen',{})], {'SEED':seed}, []))
        cases.append((f'axil-seed{seed}', 'axil_tb', [('axil/registers.pigen',{})], {'SEED':seed}, []))
    for name,cl,init,rp,rcd,ras,wr,rfc,mrd,interval in [
        ('sdram-default',3,20000,2,2,5,2,7,2,700),
        ('sdram-cas2',2,64,2,2,5,2,7,2,97),
        ('sdram-stretched',3,96,3,4,8,3,11,3,113)]:
        values=dict(INIT_CYCLES=init,CAS_LATENCY=cl,T_RP=rp,T_RCD=rcd,T_RAS=ras,T_WR=wr,T_RFC=rfc,T_MRD=mrd,REFRESH_INTERVAL=interval)
        params=dict(INIT=init,CL=cl,RP=rp,RCD=rcd,RAS=ras,WR=wr,RFC=rfc,MRD=mrd,INTERVAL=interval)
        cases.append((name,'sdram_tb',[('sdram/controller.pigen',values)],params,
                      [ROOT/'cores/sdram/pins.sv',ROOT/'tests/cores/sdram_model.sv']))
    results = []
    for name, top, sources, params, adapters in cases:
        directory=out/name;directory.mkdir(exist_ok=True)
        log=directory/'run.log';log.write_text('')
        start=time.monotonic()
        generated=[]
        source_evidence=[]
        for i,(source, values) in enumerate(sources):
            text=(ROOT/'cores'/source).read_text()
            # Pigen specializes parameter defaults at compile time. Preserve the exact
            # configuration source in the evidence directory; never patch emitted RTL.
            for key,value in values.items():
                text,count=re.subn(r'(parameter (?:integer|(?:int|uint|bit)\[\d+\]) '+key+r'\s*=\s*)\d+',
                                   lambda m:m[1]+str(value),text)
                assert count==1,(key,count)
            configured=directory/f'source{i}.pigen';configured.write_text(text)
            source_evidence += [ROOT/'cores'/source,configured]
            sv=directory/f'core{i}.sv'
            run([compiler,configured,'-o',sv],ROOT,log);generated.append(sv)
        if top=='when_value_tb':
            original=ROOT/'tests/cores/when_value.pigen';sv=directory/'core.sv'
            run([compiler,original,'-o',sv],ROOT,log);generated.append(sv);source_evidence.append(original)
            text=original.read_text()
            for badname,head,diagnostic in [
                ('not-transfer','valid(choice)','when requires a transfer event'),
                ('missing-value','selected <=','when requires a transfer event'),
                ('not-lvalue',"selected + 8'd0 <= choice",'transfer destination lvalue')]:
                changed=re.sub(r"when \(selected <= choice\[0\] \? choice \+ 8'd1 : choice \^ 8'ha5\)",lambda m:'when ('+head+')',text)
                bad=directory/(badname+'.pigen');bad.write_text(changed);output=directory/(badname+'.sv');output.write_text('preserved output\n')
                result=subprocess.run([str(compiler),str(bad),'-o',str(output)],capture_output=True,text=True,timeout=10)
                (directory/(badname+'.log')).write_text(result.stdout+result.stderr)
                assert result.returncode!=0 and diagnostic in result.stderr,(badname,result.stderr)
                assert output.read_text()=='preserved output\n'
        if top=='transfer_flow_tb':
            sv=directory/'core.sv'
            run([compiler,ROOT/'examples/transfer_flow.pigen','-o',sv],ROOT,log)
            generated.append(sv)
        if top=='concat_resize_tb':
            sv=directory/'core.sv'
            run([compiler,ROOT/'tests/cores/concat_resize.pigen','-o',sv],ROOT,log)
            generated.append(sv)
        if top=='integer_resize_tb':
            sv=directory/'core.sv'
            run([compiler,ROOT/'tests/cores/integer_resize.pigen','-o',sv],ROOT,log)
            generated.append(sv)
        if top=='continuous_intrinsics_tb':
            sv=directory/'core.sv'
            run([compiler,ROOT/'tests/cores/continuous_intrinsics.pigen','-o',sv],ROOT,log)
            generated.append(sv)
            original=(ROOT/'tests/cores/continuous_intrinsics.pigen').read_text()
            for negative_name,changed,diagnostic in [
                ('readonly',original.replace('assign source_offered = valid(source);','assign source = peek(destination);'),'writable static projection'),
                ('buffered-destination',original.replace('assign source_offered = valid(source);','assign destination = peek(source);'),'cannot produce a buffered token'),
                ('overlap',original.replace('assign raw = peek(source);','assign raw = peek(source); assign raw = peek(source);'),'overlapping writers'),
                ('buffered-read',original.replace('assign raw = peek(source);',"assign raw = valid(source) ? source : 8'h0;"),'observation requires peek'),
                ('arity',original.replace('assign raw = peek(source);','assign raw = ready(source, destination);'),'invalid handshake call argument count')]:
                bad=directory/(negative_name+'.pigen');bad.write_text(changed)
                preserved=directory/(negative_name+'.sv');preserved.write_text('preserved output\n')
                rejected=subprocess.run([str(compiler),str(bad),'-o',str(preserved)],text=True,capture_output=True,timeout=10)
                (directory/(negative_name+'.log')).write_text(rejected.stdout+rejected.stderr)
                assert rejected.returncode!=0 and diagnostic in rejected.stderr, (negative_name,rejected.stderr)
                assert preserved.read_text()=='preserved output\n'

        if top=='retained_parameters_tb':
            sv=directory/'core.sv'
            run([compiler,ROOT/'tests/cores/retained_parameters.pigen','-o',sv],ROOT,log)
            generated.append(sv)
        if top=='intrinsic_unary_tb':
            sv=directory/'core.sv'
            run([compiler,ROOT/'tests/cores/intrinsic_unary.pigen','-o',sv],ROOT,log)
            generated.append(sv)
        if top=='child_concat_tb':
            sv=directory/'core.sv'
            run([compiler,ROOT/'tests/cores/child_concat.pigen','-o',sv],ROOT,log)
            generated.append(sv)
            negative=directory/'read-only.pigen'
            negative.write_text((ROOT/'tests/cores/child_concat.pigen').read_text().replace(
                '.result({upper,lower})','.result({upper,value[3:0]})'))
            rejected=subprocess.run([str(compiler),str(negative),'-o',str(directory/'invalid.sv')],
                                    text=True,capture_output=True,timeout=10)
            (directory/'negative.log').write_text(rejected.stdout+rejected.stderr)
            assert rejected.returncode!=0 and 'writable parent signal' in rejected.stderr
            overlap=directory/'overlap.pigen'
            original=(ROOT/'tests/cores/child_concat.pigen').read_text()
            first=original.index('    child u(');last=original.index('\nendmodule', first)
            overlap.write_text(original[:last]+original[first:last].replace('child u(', 'child v(')+original[last:])
            conflict=subprocess.run([str(compiler),str(overlap),'-o',str(directory/'overlap.sv')],text=True,capture_output=True,timeout=10)
            (directory/'overlap.log').write_text(conflict.stdout+conflict.stderr)
            assert conflict.returncode!=0 and 'multiple producers' in conflict.stderr
        designs={'when_value_tb':['when_value'], 'uart_framing_tb':['pigen_uart_tx','pigen_uart_rx'],'transfer_flow_tb':['pigen_transfer_flow'],'concat_resize_tb':['concat_narrow','concat_wide','concat_signed','concat_placeholder_resize'],'integer_resize_tb':['integer_resize'],'uart_tb':['pigen_uart_tx','pigen_uart_rx'], 'spi_tb':['pigen_spi_controller'], 'spi_target_tb':['pigen_spi_target'],
                 'i2c_tb':['pigen_i2c_byte_controller'], 'i2c_target_tb':['pigen_i2c_target'], 'i2s_tb':['pigen_i2s_controller'],
                 'sdram_tb':['pigen_sdram_controller','pigen_sdram_dq_pins'], 'axi4_tb':['pigen_axi4_bridge'], 'axil_tb':['pigen_axil_registers'], 'axis_tb':['pigen_axis_fifo'], 'child_concat_tb':['concat_wrapper'],
                 'intrinsic_unary_tb':['intrinsic_unary'], 'retained_parameters_tb':['retained_parameters'], 'continuous_intrinsics_tb':['continuous_intrinsics','compound_comb','case_comb','state_casts','loop_comb','loop_control','fill_casts','compound_fills','selected_fills','called_fills']}
        # Pin models belong to simulation; lint only synthesizable sources,
        # just as the testbench itself is excluded for every other protocol.
        lint_adapters=[p for p in adapters if p.parent != ROOT/'tests/cores']
        for design in designs[top]:
            run(['verilator','--lint-only','--top-module',design]+generated+lint_adapters+[ROOT/'rtl/pigen_primitives.sv'],ROOT,log)
        tb=ROOT/'tests/cores'/f'{top}.sv'
        binary=directory/'sim'
        command=['iverilog','-g2012','-s',top]
        for key,value in params.items():command+=['-P',f'{top}.{key}={value}']
        run(command+['-o',binary]+generated+adapters+[ROOT/'rtl/pigen_primitives.sv',tb],ROOT,log)
        output=run(['vvp',binary],directory,log)
        assert 'PASS ' in output,output
        if top=='continuous_intrinsics_tb':
            check_native_loop_control(directory, generated, original, log)
            # casex and literal-X case items are legal SV with intentional
            # Verilator style warnings. Pin their fatal diagnostics, then
            # compare their four-state behavior against an independent bench.
            variant=directory/'casex.pigen'
            variant.write_text(original.replace('casez(value)', 'casex(value)').replace("case(value) '0: results[13]", "case(value) 'x: results[13]"))
            variant_sv=directory/'casex.sv'
            run([compiler,variant,'-o',variant_sv,'--no-diagram'],ROOT,log)
            lint=subprocess.run(['verilator','--lint-only','--top-module','case_comb',str(variant_sv),str(ROOT/'rtl/pigen_primitives.sv')],text=True,capture_output=True,timeout=30)
            (directory/'casex-lint.log').write_text(lint.stdout+lint.stderr)
            assert lint.returncode!=0 and 'CASEX' in lint.stderr and 'CASEWITHX' in lint.stderr,lint.stderr
            variant_tb=directory/'casex_tb.sv'
            variant_tb.write_text(tb.read_text().replace('casez(source)', 'casex(source)').replace("case(source) '0: reference_results[13]", "case(source) 'x: reference_results[13]"))
            variant_binary=directory/'casex-sim'
            run(['iverilog','-g2012','-s',top,'-o',variant_binary,variant_sv,ROOT/'rtl/pigen_primitives.sv',variant_tb],ROOT,log)
            assert 'PASS ' in run(['vvp',variant_binary],directory,log)
        if top=='axis_tb':
            snapshots=list(csv.DictReader((directory/'slots.csv').open()))
            for old,new in zip(snapshots,snapshots[1:]):
                if old['reset']!='0' or new['reset']!='0':continue
                a={k:int(v) for k,v in old.items()};b={k:int(v) for k,v in new.items()}
                assert a['push']==(a['in_valid'] and a['in_ready'])
                assert not a['pop'] or a['count']>0
                assert b['count']==a['count']+a['push']-a['pop']
                assert b['read_ptr']==(a['read_ptr']+a['pop'])%4
                assert b['write_ptr']==(a['write_ptr']+a['push'])%4
                if a['push']:assert b['slot'+str(a['write_ptr'])]==a['in_data']
                if a['pop']:assert b['out_valid']==1 and b['out_data']==a['slot'+str(a['read_ptr'])]
                elif a['out_valid'] and not a['out_ready']:assert b['out_valid']==1 and b['out_data']==a['out_data']

        if top=='transfer_flow_tb':
            module_path=ROOT/'tools/cores'
            sys.path.insert(0,str(module_path))
            import transfer_trace
            snapshots=[{k:(int(v) if v not in ('x','X','z','Z') else None) for k,v in row.items()} for row in csv.DictReader((directory/'slots.csv').open())]
            for old,new in zip(snapshots,snapshots[1:]):
                if old['reset']==0 and new['reset']==0:transfer_trace.validate(old,new)
        evidence={}
        for path in generated+source_evidence+[tb]+adapters+[ROOT/'rtl/pigen_primitives.sv']:
            evidence[str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        results.append({'name':name,'top':top,'parameters':params,'status':'pass',
                        'seconds':round(time.monotonic()-start,3),'checks':output.strip(), 'sha256':evidence})
        print(output.strip().splitlines()[-2 if '$finish' in output else -1],flush=True)
    negative_results=[]
    for i,(source,param,value,top) in enumerate([
        ('uart/tx.pigen','CLKS_PER_BIT',3,'pigen_uart_tx'),
        ('uart/rx.pigen','CLKS_PER_BIT',3,'pigen_uart_rx'),
        *[(f'uart/{side}.pigen',parameter,value,f'pigen_uart_{side}')
          for side in ('tx','rx') for parameter,value in [('DATA_BITS',4),('DATA_BITS',10),('PARITY',-1),('PARITY',3),('STOP_BITS',0),('STOP_BITS',3)]],
        ('spi/controller.pigen','HALF_PERIOD',1,'pigen_spi_controller'),
        ('spi/controller.pigen','CPHA',2,'pigen_spi_controller'),
        ('spi/target.pigen','CPOL',2,'pigen_spi_target'),
        ('spi/target.pigen','CPHA',2,'pigen_spi_target'),
        ('i2c/byte_controller.pigen','HALF_PERIOD',1,'pigen_i2c_byte_controller'),
        ('i2c/target.pigen','ADDRESS',7,'pigen_i2c_target'),
        ('i2c/target.pigen','ADDRESS',120,'pigen_i2c_target'),
        ('i2c/target.pigen','SETUP_CYCLES',1,'pigen_i2c_target'),
        ('i2s/controller.pigen','HALF_PERIOD',1,'pigen_i2s_controller'),
        ('sdram/controller.pigen','CAS_LATENCY',4,'pigen_sdram_controller'),
        ('sdram/controller.pigen','T_MRD',1,'pigen_sdram_controller'),
        ('sdram/controller.pigen','REFRESH_INTERVAL',20,'pigen_sdram_controller')]):
        directory=out/'invalid-configurations';directory.mkdir(exist_ok=True)
        text=(ROOT/'cores'/source).read_text()
        text=re.sub(r'(parameter (?:integer|(?:int|uint|bit)\[\d+\]) '+param+r'\s*=\s*)\d+',lambda m:m[1]+str(value),text)
        configured=directory/f'bad{i}.pigen';configured.write_text(text)
        sv=directory/f'bad{i}.sv';log=directory/f'bad{i}.log';log.write_text('')
        run([compiler,configured,'-o',sv],ROOT,log)
        binary=directory/f'bad{i}.sim'
        run(['iverilog','-g2012','-s',top,'-o',binary,sv,ROOT/'rtl/pigen_primitives.sv'],ROOT,log)
        result=subprocess.run(['vvp',str(binary)],text=True,capture_output=True,timeout=10,cwd=directory)
        with log.open('a') as stream:stream.write(result.stdout+result.stderr)
        assert result.returncode!=0 and 'requires' in result.stdout, (source,result.stdout)
        negative_results.append({'source':source,'parameter':param,'value':value,'rejected':True})
    model_rejections=[]
    directory=out/'sdram-model-rejections';directory.mkdir(exist_ok=True)
    for fault,diagnostic in enumerate(['REF with open bank','ACT bank/recovery','access row/RCD','PRE violated','PRE violated','refresh deadline','mode register']):
        log=directory/f'fault{fault}.log';log.write_text('');binary=directory/f'fault{fault}.sim'
        run(['iverilog','-g2012','-s','sdram_model_negative_tb','-P',f'sdram_model_negative_tb.FAULT={fault}',
             '-o',binary,ROOT/'tests/cores/sdram_model.sv',ROOT/'tests/cores/sdram_model_negative_tb.sv'],ROOT,log)
        result=subprocess.run(['vvp',str(binary)],cwd=directory,text=True,capture_output=True,timeout=10)
        with log.open('a') as stream:stream.write(result.stdout+result.stderr)
        assert result.returncode!=0 and diagnostic in result.stdout,(fault,result.stdout)
        model_rejections.append({'fault':fault,'diagnostic':diagnostic,'rejected':True})
    versions={tool:subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT).stdout.splitlines()[0]
              for tool,command in [('iverilog',['iverilog','-V']),('verilator',['verilator','--version'])]}
    manifest={'sdram_model_rejections':model_rejections,'invalid_configurations':negative_results,'cases':results,'tools':versions,'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),
              'git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
              'working_tree_may_be_dirty':True,
              'protocol_sources':{'SPI':'https://ww1.microchip.com/downloads/en/Appnotes/Atmel-2585-Setup-and-Use-of-the-SPI_ApplicationNote_AVR151.pdf','AXI4':'https://documentation-service.arm.com/static/5f915b62f86e16515cdc3b1c',
                'SDRAM':'https://resources.ampheo.com/static/datasheets/micron-technology-inc/mt48lc16m16a2-7e.pdf',
                'AXI4-Lite':'https://documentation-service.arm.com/static/5f915b62f86e16515cdc3b1c',
                'AXI-Stream':'https://documentation-service.arm.com/static/64819f1516f0f201aa6b963c',
                'I2C':'https://www.nxp.com/docs/en/user-guide/UM10204.pdf',
                'I2S':'https://www.nxp.com/docs/en/user-manual/UM11732.pdf'}}
    patch=subprocess.check_output(['git','diff','--','src','include','Makefile','tests','.gitignore'],cwd=ROOT,text=True)
    (out/'compiler.patch').write_text(patch)
    manifest['compiler_patch_sha256']=hashlib.sha256(patch.encode()).hexdigest()
    (out/'results.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'{len(results)}/{len(results)} cases passed; evidence: {out}')

if __name__=='__main__':main()
