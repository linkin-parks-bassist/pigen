#!/usr/bin/env python3
"""Animate actual pin traces using the visual palette of the Opus UART clip."""
import argparse
import hashlib
import json
import csv
from pathlib import Path
import subprocess
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

PROFILES={
    'axi4':('AXI4 / burst traffic through native requests',
            ['aw_valid','aw_ready','w_valid','w_ready','b_valid','b_ready','r_valid','r_ready','r_last'],350,
            'Independent burst engines preserve IDs and boundaries while backend readiness stalls.'),
    'sdram':('SDR SDRAM / read data across the CAS interval',
             ['ras_n','cas_n','we_n','dq_write','dq_read','in_ready','out_valid'],330,
             'CAS=2 pin trace. The memory drives DQ for one word; the controller buffers the response.'),
    'axil':('AXI4-Lite / address and data arrive independently',
            ['aw_valid','aw_ready','w_valid','w_ready','b_valid','b_ready','ar_valid','ar_ready','r_valid','r_ready'],150,
            'AW and W buffer separately. B and R keep their offered response while readiness is low.'),
    'spi_target':('SPI target / an external clock moves each byte',
           ['cs_n','sck','mosi','miso','miso_enable','tx_ready','rx_valid'],180,
           'Mode 0, asynchronous pins: consume TX once per byte; buffer RX after eight sample edges.'),
    'spi':('SPI / two bytes moving in opposite directions', ['cs_n','sck','mosi','miso'],170,
           'Mode 0: sample on rising SCK; change on falling SCK. CS stays low for eight bits.'),
    'i2c':('I2C / the target stretches the clock', ['scl','sda','stretch'],320,
           'Open-drain clock readback pauses the byte engine while the target holds SCL low.'),
    'i2s':('I2S / stereo samples on a continuous clock', ['bclk','ws','tx','rx'],540,
           'Word select leads the next channel by one bit. Left and right slots each carry 16 bits.'),
    'axis':('AXI-Stream / buffering becomes backpressure', ['in_valid','in_ready','out_valid','out_ready'],175,
            'Five beats fit in storage. With the receiver blocked, the sender must hold its beat.')}

def main():
    p=argparse.ArgumentParser();p.add_argument('--profile',choices=PROFILES,required=True)
    p.add_argument('--trace',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    title,keys,end,caption=PROFILES[a.profile]
    all_rows=list(csv.DictReader(a.trace.open()))
    if a.profile=='sdram':
        first_read=next(int(r['cycle']) for r in all_rows if r['dq_read']=='1')
        data=[r for r in all_rows if first_read-20<=int(r['cycle'])<=first_read+70]
    else:data=[r for r in all_rows if int(r['cycle'])<=end]
    x=np.array([int(r['cycle']) for r in data]);assert len(x)>10
    values=[np.array([(int(r[k]) if r[k] in ('0','1') else np.nan) for r in data]) for k in keys]
    a.out.mkdir(parents=True,exist_ok=True)
    plt.rcParams.update({'font.family':'DejaVu Sans Mono','font.size':13})
    fig=plt.figure(figsize=(12.8,7.2),dpi=100,facecolor='#f5f3ed')
    fig.text(.07,.92,title,color='#272624',fontsize=23,weight='bold')
    fig.text(.07,.86,'Actual emitted Pigen RTL / Icarus pin trace',color='#706b63',fontsize=14)
    ax=fig.add_axes([.13,.22,.81,.57],facecolor='#f5f3ed')
    ax.set_xlim(x[0],x[-1]);ax.set_ylim(-.4,len(keys)*1.6)
    colors=['#8b3328','#355d74','#896a2f','#56514a']
    lines=[]
    for n,(key,v) in enumerate(zip(keys,values)):
        color='#355d74' if 'ready' in key else '#896a2f' if 'valid' in key else colors[n%len(colors)]
        y=(len(keys)-n-1)*1.6+v*.85
        ax.step(x,y,where='post',color=color,alpha=.2,lw=2)
        line,=ax.step([],[],where='post',color=color,lw=2.7);lines.append((line,y))
    ax.set_yticks([(len(keys)-n-1)*1.6+.4 for n in range(len(keys))],keys)
    ax.tick_params(colors='#272624');ax.spines[['top','right','left']].set_visible(False)
    ax.spines['bottom'].set_color('#d4cec2');ax.set_xlabel('System clock cycles',color='#706b63')
    ax.grid(axis='x',color='#d4cec2',alpha=.45)
    cursor=ax.axvline(x[0],color='#896a2f',lw=2)
    fig.text(.07,.095,caption,color='#272624',fontsize=11)
    counter=fig.text(.94,.15,'',ha='right',color='#896a2f',fontsize=14)
    if any(np.isnan(v).any() for v in values):
        fig.text(.07,.045,'Trace gaps denote unknown values before reset initializes storage.',color='#706b63',fontsize=10)
    frames=144
    def update(f):
        at=x[0]+min(1,f/(frames*.85))*(x[-1]-x[0]);i=max(1,np.searchsorted(x,at,side='right'))
        for line,y in lines:line.set_data(np.append(x[:i],at),np.append(y[:i],y[i-1]))
        cursor.set_xdata([at,at]);counter.set_text(f'cycle {int(at)}')
    cmd=['ffmpeg','-y','-loglevel','error','-f','rawvideo','-pix_fmt','rgba','-s','1280x720','-r','24','-i','-',
         '-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(a.out/(a.profile+'_timing.mp4'))]
    proc=subprocess.Popen(cmd,stdin=subprocess.PIPE)
    try:
        for f in range(frames):update(f);fig.canvas.draw();proc.stdin.write(fig.canvas.buffer_rgba())
    finally:proc.stdin.close()
    if proc.wait():raise RuntimeError('ffmpeg failed')
    update(frames-1);fig.savefig(a.out/(a.profile+'_poster.png'),facecolor=fig.get_facecolor());plt.close(fig)
    (a.out/(a.profile+'_timing.json')).write_text(json.dumps({'trace_sha256':hashlib.sha256(a.trace.read_bytes()).hexdigest(),'source':str(a.trace),'profile':a.profile},indent=2))
    print(a.out/(a.profile+'_timing.mp4'))

if __name__=='__main__':main()
