#!/usr/bin/env python3
"""Build an artifact gallery from successful core simulations; no plotting dependencies."""
import argparse
import csv
import html
import json
import re
from pathlib import Path
import shutil
import zipfile
import interactive

ROOT=Path(__file__).resolve().parents[2]
INK='#272624';DIM='#706b63';TEAL='#8b3328';PURPLE='#355d74';ORANGE='#896a2f'

def esc(value):return html.escape(str(value))
def pigen_source(source):
    pattern=r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\b(?:ready|valid|accepts|when|buf|fifo|skid|port|wire|reg|logic|int|uint|bit|fsm|state|goto|always|if|else|begin|end|module|endmodule|input|output|parameter)\b'
    result=[];cursor=0
    for match in re.finditer(pattern,source):
        result.append(esc(source[cursor:match.start()]))
        word=match[0]
        kind='comment' if word.startswith(('//','/*')) else 'literal' if word.startswith('"') else 'ready' if word=='ready' else 'valid' if word=='valid' else 'fire' if word in ('when','accepts') else 'keyword'
        result.append(f'<span class="{kind}">{esc(word)}</span>');cursor=match.end()
    result.append(esc(source[cursor:]));return ''.join(result)

def rows(path):
    with path.open() as stream:return list(csv.DictReader(stream))
def number(value):
    try:return int(value)
    except (TypeError,ValueError):return None

def svg_start(w,h):return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" role="img"><rect width="100%" height="100%" rx="2" fill="#ffffff"/><g font-family="monospace" fill="{INK}">'
def text(x,y,s,size=14,color=INK):return f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}">{esc(s)}</text>'

def diagram(path,title,labels):
    width=1100;h=160;step=width/len(labels)
    s=svg_start(width,h)+text(28,32,title,19)
    for i,label in enumerate(labels):
        x=i*step+18
        s+=f'<rect x="{x}" y="62" width="{step-36}" height="65" rx="2" fill="#f2eee5" stroke="#8a847a"/>'
        s+=text(x+12,99,label,14)
        if i<len(labels)-1:s+=f'<path d="M{x+step-34},95 h32 l-7,-5 m7,5 l-7,5" fill="none" stroke="#706b63" stroke-width="2"/>'
    path.write_text(s+'</g></svg>')

def waveform(path,data,signals,title,limit=None):
    data=[r for r in data if number(r.get('cycle')) is not None]
    if limit:data=[r for r in data if int(r['cycle'])<=limit]
    lo=min(int(r['cycle']) for r in data);hi=max(int(r['cycle']) for r in data)
    w=1100;left=155;right=1060;h=95+len(signals)*54
    x=lambda t:left+(t-lo)*(right-left)/max(1,hi-lo)
    s=svg_start(w,h)+text(25,32,title,19)
    for t in range(lo,hi+1,max(1,(hi-lo)//8)):
        xx=x(t);s+=f'<path d="M{xx},52 V{h-35}" stroke="#d4cec2"/>'+text(xx,h-13,t,11,DIM)
    for n,key in enumerate(signals):
        y=80+n*54; s+=text(20,y,key,13)
        commands=[];previous=None
        for r in data:
            value=number(r.get(key));xx=x(int(r['cycle']))
            if value is None:
                previous=None
                s+=text(xx,y,'x',11,DIM)
                continue
            yy=y-17 if value else y+8
            if previous is None:commands.append(f'M{xx:.2f},{yy}')
            else:commands.append(f'L{xx:.2f},{previous} L{xx:.2f},{yy}')
            previous=yy
        color=PURPLE if 'ready' in key else ORANGE if 'valid' in key else INK
        s+=f'<path d="'+ ' '.join(commands)+f'" stroke="{color}" stroke-width="2" fill="none"/>'
    path.write_text(s+'</g></svg>')

def comparisons(path,data,title):
    pairs=[]
    for row in data:
        for key in row:
            if key=='expected':observed='observed'
            elif key.endswith('_expected'):observed=key[:-9]+'_observed'
            else:continue
            a=number(row[key]);b=number(row.get(observed))
            if a is not None and b is not None:pairs.append((a,b))
    if not pairs:return 0
    if any(a!=b for a,b in pairs):raise ValueError(f'comparison mismatch: {title}')
    maxval=max(1,max(max(a,b) for a,b in pairs))
    s=svg_start(650,410)+text(24,32,title,18)+text(24,58,f'{len(pairs)} checked values | zero mismatches',14,TEAL)
    s+='<path d="M85,90 V340 H610 M85,340 L605,90" fill="none" stroke="#8a847a"/>'
    for a,b in pairs:
        x=85+520*a/maxval;y=340-250*b/maxval
        s+=f'<circle cx="{x:.2f}" cy="{y:.2f}" r="3" fill="{TEAL}" opacity=".5"/>'
    s+=text(260,383,'Expected value',14)+text(12,82,'Observed',13)+text(86,360,0,11,DIM)+text(490,360,maxval,11,DIM)
    path.write_text(s+'</g></svg>');return len(pairs)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,default=ROOT/'build/cores');args=parser.parse_args()
    out=args.out.resolve();interactive.main(out);manifest=json.loads((out/'results.json').read_text());media=out/'media';media.mkdir(exist_ok=True)
    sources=out/'sources'
    for base in ['cores','tests/cores','tools/cores','rtl']:
        for p in (ROOT/base).rglob('*'):
            if p.is_file() and p.suffix in ('.pigen','.sv','.py','.js','.css'):
                target=sources/p.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
    configs=[
        ('UART','uart-div16',['tx','data_valid','data_ready'],200,['Pigen byte token','8N1 transmitter','Serial pin','8N1 receiver','Buffered byte'],
         'LSB-first UART with 5–9 data bits, no/even/odd parity and one/two stop bits. All 30 formats exhaust their payloads at divisors 4 and 7; five default 8N1 divisor witnesses remain. Independent pin decoding checks exact stop duration, truly back-to-back RX, parity and every stop-bit error, false starts, held-low break recovery, output stalls, overrun and reset. Bad frames are rejected; a full RX buffer retains the old word. The views show actual frame/shift registers, timing counters and parity/stop state.'),
        ('SPI','spi-mode0-half4',['cs_n','sck','mosi','miso'],180,['Command token','Mode + clock FSM','SCK / MOSI / MISO','Response buffer'],
         'All four CPOL/CPHA modes; half periods 2, 4, 7. Independent pin-level target checks 64 full-duplex bytes per configuration, edge spacing, stalls and reset abort. One selected target, eight bits, MSB first.'),
        ('SPI target','spi-target-mode0-half73',['cs_n','sck','mosi','miso','miso_enable','tx_ready','rx_valid'],180,['Buffered TX token','Synchronized SCK / CS','MSB-first byte engine','Buffered RX token'],
         'Oversampled eight-bit full-duplex target in all four CPOL/CPHA modes. Independent asynchronous controller tests exhaust all 256 byte values both ways at jittered half periods 61, 73 and 107 ns against a 10 ns system clock. Continuous chip select, late transmit tokens, zero-fill underrun, drop-newest receive overrun, aborts after one through seven bits, and reset recovery are checked. Two-stage synchronizers require each SCK half-period and CS setup/high interval to last at least six system clocks. TX must be queued before the first leading edge and meet CPHA=0 first-bit setup. A consumed partial TX byte is discarded on deselection; queued future bytes remain. MISO output-enable releases immediately on CS high or reset; connect it to a board I/O tri-state. Physical metastability and I/O timing need board qualification.'),
        ('I2C','i2c-half4',['scl','sda','stretch'],350,['Byte commands','START / bit / ACK FSM','Open-drain pins','Clock-stretch feedback','Response token'],
         'Single-controller byte engine; half periods 2, 4, 9. Reads/writes, ACK/NACK, repeated START, held-bus chaining, STOP, clock stretching and reset. Readback pins use two-stage synchronizers. Command flags chain transactions. No multi-controller arbitration or stuck-bus timeout; timing must be chosen for the board.'),
        ('I2C target','i2c-target-addr82-half107',['scl_i','sda_i','active','reading','scl_low'],1800,['Address / direction','Receive / ACK FSM','Buffered RX / TX','Read / NACK FSM'],
         'Seven-bit addressed target with native buffered bytes and open-drain pins. Three addresses and three asynchronous timing/setup profiles test all 128 addresses and all 256 byte values in both directions. RX backpressure stretches before ACK; empty TX stretches before data. Tests cover ACK/NACK, repeated START, STOP, one-through-seven-bit partial address/write/read aborts, queued following TX preservation, reset and recovery. Each bus high/low and START/STOP interval must last at least eight system clocks. SETUP_CYCLES and physical I/O timing require qualification; general call, ten-bit addressing, spike filtering and SMBus timeout are outside this profile.'),
        ('I2S','i2s-half4',['bclk','ws','tx','rx'],550,['Stereo frame token','Continuous bit clock','16-bit L + 16-bit R','Receive buffer'],
         'Continuous-clock full-duplex controller; half periods 2, 4, 7. Independent stimulus/decoder checks 40 stereo frames per configuration, MSB order and one-bit word-select lead. Transmit underrun inserts silence; receive overrun drops the new frame.'),
        ('AXI-Stream','axis-fifo',['in_valid','in_ready','out_valid','out_ready'],180,['TDATA + TKEEP + TLAST','One 37-bit token','FIFO: four entries','Output: one entry'],
         '32-bit data, four byte enables and TLAST. Randomized stalls and payloads, in-order scoreboard, stable stalled outputs, five-beat capacity and reset epochs. Includes a thin SV pin-name adapter. This is streaming transport; memory-mapped access is provided separately below.'),
        ('AXI4-Lite','axil-seed1',['aw_valid','aw_ready','w_valid','w_ready','b_valid','b_ready','ar_valid','ar_ready','r_valid','r_ready'],150,['AW + W buffers','Four 32-bit RW words','B response buffer','AR lookup','R response buffer'],
         '32-bit AXI4-Lite register endpoint: words at byte addresses 0, 4, 8, 12. AW and W arrive independently; all 16 write-strobe masks, response stalls, concurrent disjoint reads/writes, invalid addresses and reset are checked at three deterministic seeds. Misaligned/unmapped addresses return DECERR; zero strobes are successful no-ops. Reads snapshot at lookup; a write on that same edge supplies the old value. Protection attributes are ignored. A thin SV adapter exposes standard pins.'),
        ('SDRAM','sdram-cas2',['ras_n','cas_n','we_n','dq_write','dq_read','in_ready','out_valid'],330,['Native request','ACT / RW / PRE','4-bank x16 memory','Buffered response'],
         'Single-word, closed-page SDR SDRAM controller for four banks, 8192 rows and 512 x16 columns. Tests cover CAS 2/3, default and stretched timings, all 24 address bits, all byte masks, initialization, refresh during stalls, DQ contention and three reset scenarios. An independent full-address memory model checks pin commands and timing; seven malformed sequences demonstrate checker rejection. Reset discards traffic and leaves memory contents unspecified. Cycle parameters and FPGA clock/I/O timing require qualification for the selected physical part. No burst or open-page optimization is claimed.'),
        ('AXI4','axi4-seed1',['aw_valid','aw_ready','w_valid','w_ready','b_valid','b_ready','r_valid','r_ready','r_last'],350,['AW / W burst FSM','Native write requests','AR read burst FSM','Native read requests'],
         '32-bit addresses/data and four-bit IDs. Independent read/write burst engines support FIXED, INCR and WRAP, byte/halfword/word sizes, unaligned FIXED/INCR starts, and up to 256 INCR beats. Native backends receive aligned word requests with byte strobes and return one ordered response per request. Tests independently check backend address/data/strobes, IDs, RLAST, error aggregation, response stability, reset and random delays at three seeds. Malformed descriptors return DECERR without backend access; invalid strobes or WLAST return SLVERR, draining the AWLEN beat count. Cache/protection/QoS/region and AxLOCK are ignored; there is no exclusive monitor. Both directions and the backend share reset. Backend replies use OKAY/SLVERR/DECERR. One active burst per direction, with elastic boundary buffering; no out-of-order completion or optional USER sidebands.')]
    cards=[];total=0
    for title,case,signals,limit,labels,scope in configs:
        diagram(media/f'{case}-diagram.svg',title+' / data flow',labels)
        waveform(media/f'{case}-waveform.svg',rows(out/case/'trace.csv'),signals,title+' / actual simulated pins',limit)
        n=comparisons(media/f'{case}-comparison.svg',rows(out/case/'checks.csv'),title+' / expected vs observed');total+=n
        movie={'SPI':'spi','SPI target':'spi_target','I2C':'i2c','I2S':'i2s','AXI-Stream':'axis','AXI4-Lite':'axil','SDRAM':'sdram','AXI4':'axi4'}.get(title)
        clip=(f'<details><summary>Watch the actual timing</summary><video controls preload="metadata" poster="media/{movie}_poster.png" src="media/{movie}_timing.mp4"></video></details>' if movie and (media/(movie+'_timing.mp4')).exists() else '')
        clip=f'<p><a href="interactive/{case}.html">Open interactive simulation trace</a></p><iframe loading="lazy" title="{title} interactive simulation trace" src="interactive/{case}.html"></iframe>'+clip
        core_files={'UART':['uart/tx.pigen','uart/rx.pigen'], 'SPI':['spi/controller.pigen'], 'SPI target':['spi/target.pigen'],
                    'I2C':['i2c/byte_controller.pigen'], 'I2C target':['i2c/target.pigen'], 'I2S':['i2s/controller.pigen'],
                    'AXI-Stream':['axis/fifo.pigen'], 'AXI4-Lite':['axil/registers.pigen'], 'SDRAM':['sdram/controller.pigen'], 'AXI4':['axi4/bridge.pigen']}[title]
        source_panels=''.join(f'<details class="source" open><summary>{esc(name)}</summary><p><a href="sources/cores/{esc(name)}">Open / download source</a></p><pre><code>{pigen_source((ROOT/"cores"/name).read_text())}</code></pre></details>' for name in core_files)
        extra=''
        if title=='UART':
            variant='uart-9o2-div7'
            n2=comparisons(media/f'{variant}-comparison.svg',rows(out/variant/'checks.csv'),'UART 9O2 / expected vs observed');total+=n2
            waveform(media/f'{variant}-waveform.svg',rows(out/variant/'trace.csv'),['tx','rx','tx_valid','tx_ready','rx_valid','rx_ready','parity_error','framing_error'],'UART 9O2 / actual pins and word handshakes',400)
            extra=f'<h3>Nine data bits, odd parity, two stops</h3><p><a href="interactive/{variant}.html">9O2 internal-state example</a> · <a href="{variant}/checks.csv">Checked values</a> · <a href="{variant}/wave.vcd">Full VCD</a></p><iframe loading="lazy" title="UART 9O2 internal simulation trace" src="interactive/{variant}.html"></iframe><img alt="UART 9O2 actual pin waveforms" src="media/{variant}-waveform.svg"><details><summary>{n2} independently checked 9O2 values</summary><img class="compare" alt="UART 9O2 expected and observed values" src="media/{variant}-comparison.svg"></details>'
        if title=='AXI4':
            diagram(media/f'{case}-write.svg','AXI4 / write path',['AW descriptor + W beats','Native write requests','Ordered backend ACK','B: original ID + status'])
            diagram(media/f'{case}-diagram.svg','AXI4 / independent read path',['AR descriptor','Native read requests','Ordered backend data','R: original ID + LAST'])
            extra=f'<img alt="AXI4 independent write path" src="media/{case}-write.svg"><p><a href="{case}/backend.csv">Verified backend requests</a></p>'

        if title=='SDRAM':
            pins=rows(out/case/'trace.csv')
            first_read=next(int(r['cycle']) for r in pins if r['dq_read']=='1')
            window=[r for r in pins if first_read-20<=int(r['cycle'])<=first_read+70]
            waveform(media/f'{case}-read.svg',window,['ras_n','cas_n','we_n','dq_read','out_valid','out_ready'],
                     'SDRAM / read data and response backpressure')
            extra=f'<img alt="SDRAM read window from actual pin trace" src="media/{case}-read.svg"><p><a href="{case}/commands.csv">Decoded command trace</a></p>'
        cards.append(f'''<section id="{case}"><p class="eyebrow">Pigen core / {title}</p><h2>{title}</h2><p>{scope}</p>{source_panels}
          <img alt="{title} architecture" src="media/{case}-diagram.svg"><img alt="{title} waveform from simulation" src="media/{case}-waveform.svg">
          <details><summary>Expected versus observed · {n} comparisons in this representative run</summary><img class="compare" src="media/{case}-comparison.svg"></details>
          {extra}{clip}<p class="links"><a href="{case}/checks.csv">Checked values CSV</a><a href="{case}/trace.csv">Pin trace CSV</a><a href="{case}/wave.vcd">Full VCD</a><a href="{case}/run.log">Commands & log</a></p></section>''')
    tests=''
    for c in manifest['cases']:
        checks=out/c['name']/'checks.csv'
        tests+=f'<tr><td><a href="{c["name"]}/run.log">{esc(c["name"])}</a></td><td class="pass">PASS</td><td>{esc(c["parameters"])}</td><td>{len(rows(checks)) if checks.exists() else "regression"}</td></tr>'
    conventions='''<section id="conventions"><p class="eyebrow">Transfer display conventions</p><h2>Wire values and emitted logic.</h2><p>Connections stay visible. <span class="wire idle">Dark: neither</span> · <span class="wire ready">Blue: endpoint ready</span> · <span class="wire valid">Yellow: startpoint valid</span> · <span class="wire both">Green: both</span>. Packets travel on green connections at accepted clock edges. The fork/join view reads literal emitted valid/ready nets from the simulator. Its adder input ready signals include both input valids; no conventional join-readiness formula is substituted. Arithmetic preserves ready/valid through the transform. The internal views also expose the actual registers, bit shifts, counters and FSM states; register paths remain green because registers are always valid and ready; input green indicates the transfer.</p><table><tr><th>Transfer type</th><th>Storage display</th></tr><tr><td>wire</td><td>Connection with a value label</td></tr><tr><td>reg / logic</td><td>Static value card; always valid and ready</td></tr><tr><td>buf</td><td>One fixed storage slot</td></tr><tr><td>fifo[N]</td><td>Intrinsic queue block with occupancy; internal paths remain opaque</td></tr><tr><td>skid</td><td>Two storage slots, occupancy-based backpressure</td></tr><tr><td>port</td><td>One-cycle validity pulse</td></tr></table><p class="small">Invalid buffers still show their stored payload and V=0. Values hold until the clock edge. Connections keep the same stroke width. The fork/join player includes the exact emitted assignments and current net values. Every displayed clock has equal duration, including idle clocks.</p></section>'''
    example=ROOT/'examples/transfer_flow.pigen'
    target=sources/'examples/transfer_flow.pigen';target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(example,target)
    slots=''
    if (media/'transfer_slots.mp4').exists():
        slots=f'''<section><p class="eyebrow">Explicit native transfers</p><h2>Fork, compute, join.</h2><p>A token N enters two explicit buf paths. One doubles it; the other adds one. Their values join as 3N + 1. Paused work and a blocked receiver make backpressure visible. Every connection maps to a transfer in this source; the animation exposes no FIFO internals.</p><details class="source" open><summary>examples/transfer_flow.pigen</summary><p><a href="sources/examples/transfer_flow.pigen">Open / download source</a></p><pre><code>{pigen_source(example.read_text())}</code></pre></details><p><a href="interactive/transfer-flow.html">Open interactive fork/join</a></p><iframe loading="lazy" title="Interactive fork compute join" src="interactive/transfer-flow.html"></iframe><details><summary>Video export</summary><video controls preload="metadata" poster="media/transfer_slots_poster.png" src="media/transfer_slots.mp4"></video></details><p><a href="transfer-flow/slots.csv">Actual slot values and handshakes</a> · <a href="transfer-flow/checks.csv">Checked results</a> · <a href="transfer-flow/wave.vcd">Full VCD</a></p></section>'''
    video=''
    if (media/'uart_8n1_timing.mp4').exists():
        video='<section><p class="eyebrow">UART timing / animation design by Claude Opus</p><h2>UART transmission, one bit at a time.</h2><p>The moving cursor follows the actual DIV=16 simulator trace. Mid-bit samples decode the accepted 0xA5 byte; start and stop slots stay visible.</p><video controls preload="metadata" poster="media/uart_8n1_timing_poster.png" src="media/uart_8n1_timing.mp4"></video></section>'
    css='''*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:#f5f3ed;color:#272624;font:16px/1.65 system-ui,sans-serif}main{max-width:1160px;margin:auto;padding:60px 30px}h1{font-family:Georgia,serif;font-weight:normal;font-size:clamp(36px,5vw,56px);line-height:1.15;letter-spacing:-1px;margin:14px 0 24px}h2{font-family:Georgia,serif;font-weight:normal;font-size:30px;margin:6px 0 14px}p{max-width:90ch;color:#514d47}.eyebrow{font:12px monospace;letter-spacing:2px;color:#8b3328}section{padding:45px 0;border-top:1px solid #d4cec2}a{color:#355d74}.wire{white-space:nowrap;font-weight:600}.wire::before{content:"";display:inline-block;width:24px;height:3px;margin-right:6px;background:currentColor;vertical-align:middle}.idle{color:#20252a}.ready{color:#429de8}.valid{color:#e8bd45}.both{color:#23a766}.fire{color:#23a766}nav,.links{display:flex;gap:20px;flex-wrap:wrap}nav{margin:35px 0}iframe{width:100%;height:760px;border:1px solid #d4cec2;border-radius:4px;margin:20px 0}img,video{max-width:100%;display:block;margin:20px 0;border-radius:2px}.compare{max-width:650px}summary{cursor:pointer;color:#355d74;padding:12px 0}.source{margin:24px 0;border:1px solid #d4cec2;padding:0 18px}.source code{color:#272624}.source .comment{color:#706b63;font-style:italic}.source .literal{color:#6b703d}.source .keyword{font-weight:600}.source pre{font:13px/1.55 ui-monospace,monospace;max-height:560px;overflow:auto}.source summary{font-family:monospace}pre{padding:25px;background:#ffffff;overflow:auto;border-radius:2px}table{width:100%;border-collapse:collapse;font-size:14px}td,th{text-align:left;padding:12px;border-bottom:1px solid #d4cec2}.pass{color:#8b3328}.stats{display:flex;gap:16px;margin:30px 0}.stat{flex:1;background:#ebe7dd;border-radius:2px;padding:24px}.stat b{display:block;font-size:36px;color:#8b3328}.small{font-size:13px;color:#706b63}code{color:#8b3328}'''
    body=f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pigen / verified cores lab</title><style>{css}</style><main>
    <header><p class="eyebrow">Pigen standard cores / test report</p><h1>Standard cores<br>Verification report</h1><p>Internal verification evidence for native cores and compiler regressions. Simulated values come from emitted SystemVerilog in Icarus; synthesizable configurations are linted with warnings fatal.</p>
    <div class="stats"><div class="stat"><b>{len(manifest['cases'])}</b>passing configurations & regressions</div><div class="stat"><b>{len(configs)}</b>protocol sections</div><div class="stat"><b>{total:,}</b>checked values shown in representative runs</div></div>
    <nav>{''.join(f'<a href="#{c[1]}">{c[0]}</a>' for c in configs)}<a href="#matrix">Full matrix</a><a href="pigen-cores-evidence.zip">Evidence bundle</a></nav></header>{conventions}{slots}{video}{''.join(cards)}
    <section id="matrix"><p class="eyebrow">CONFIGURATION MATRIX</p><h2>Verification matrix.</h2><table><tr><th>Test</th><th>Status</th><th>Configuration</th><th>CSV rows</th></tr>{tests}</table><p class="small">Simulation and lint establish the tested profiles. Hardware timing, metastability MTBF, electrical characteristics and formal protocol compliance have separate qualification work.</p></section>
    <section><h2>Reproduction commands.</h2><p>{len(manifest["invalid_configurations"])} invalid parameter configurations are rejected at simulation start. {len(manifest.get("sdram_model_rejections",[]))} malformed SDRAM command sequences are independently rejected by the memory checker.</p><pre>make cores-report
# Add the trace-driven Opus video (requires numpy, matplotlib, ffmpeg):
make cores-video CORE_VIDEO_PYTHON=/path/to/python</pre>
    <details><summary>SDRAM memory-checker rejection witnesses</summary><ul>{''.join(f'<li><a href="sdram-model-rejections/fault{c["fault"]}.log">{esc(c["diagnostic"])}</a></li>' for c in manifest.get('sdram_model_rejections',[]))}</ul></details>
    <p><a href="results.json">Machine-readable results, tool versions and SHA-256 evidence</a></p><p>Compiler regressions cover concatenated child outputs, rejection of read-only/overlapping destinations, unary operators around handshake intrinsics, parameter names/types used in retained SystemVerilog, signed/unsigned integer resize widths through concatenations and nested arithmetic, whole-RHS concatenation conversion with inferred placeholders, empty transfer-event bodies, and ternary/logical/relational transfer values with exact same-fire body counts. Malformed transfer events are rejected without replacing existing output files.</p>
    <details><summary>Native core sources and test infrastructure</summary><ul>{''.join(f'<li><a href="{esc(p.relative_to(out))}">{esc(p.relative_to(sources))}</a></li>' for p in sorted(sources.rglob('*')) if p.is_file())}</ul></details>
    <p>Protocol references: <a href="{manifest['protocol_sources']['AXI4']}">Arm AXI4</a>, <a href="{manifest['protocol_sources']['SDRAM']}">Micron SDR SDRAM datasheet</a>, <a href="{manifest['protocol_sources']['AXI4-Lite']}">Arm AXI4-Lite</a>, <a href="{manifest['protocol_sources']['AXI-Stream']}">Arm AXI-Stream</a>, <a href="{manifest['protocol_sources']['I2C']}">NXP I²C</a>, <a href="{manifest['protocol_sources']['I2S']}">NXP I²S</a>, <a href="{manifest['protocol_sources']['SPI']}">Microchip SPI timing</a>.</p>
    <p class="small">Tested profiles: UART 5–9 data bits, none/even/odd parity and one/two stops; eight-bit SPI controller and target; I²C controller byte engine and addressed target; 16-bit stereo I²S; 32-bit AXI-Stream transport; four-word AXI4-Lite register endpoint; x16 single-word SDR SDRAM; AXI4 burst-to-native-memory bridge. This report records simulated behavior and its source evidence.</p></section></main><script>addEventListener("message",e=>{{if(!e.data||!Number.isFinite(e.data.pigenHeight))return;for(const f of document.querySelectorAll("iframe"))if(f.contentWindow===e.source)f.style.height=Math.min(20000,e.data.pigenHeight+8)+"px";}});</script></html>'''
    (out/'index.html').write_text(body)
    with zipfile.ZipFile(out/'pigen-cores-evidence.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(out.rglob('*')):
            if p.is_file() and p.name not in ('pigen-cores-evidence.zip','sim') and p.suffix != '.sim':
                archive.write(p,p.relative_to(out))
    print(f'Artifact gallery: {out/"index.html"}')

if __name__=='__main__':main()
