#!/usr/bin/env python3
"""Measure direct per-source compile cost for the three tuned rival kernels."""
from __future__ import annotations
import argparse, json, pathlib, shutil, statistics, subprocess, tempfile, time
ROOT=pathlib.Path(__file__).resolve().parent; SRC=ROOT.parent/'src'
def timed(command,cwd):
    t=time.perf_counter_ns(); subprocess.run(command,cwd=cwd,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); return time.perf_counter_ns()-t
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--cpu',choices=('baseline','native'),default='baseline'); ap.add_argument('--output',type=pathlib.Path,required=True); args=ap.parse_args()
    rows={}
    with tempfile.TemporaryDirectory(prefix='rival-build-metrics-') as tmp_s:
        tmp=pathlib.Path(tmp_s); odin_src=tmp/'odin'; odin_src.mkdir(); shutil.copy2(SRC/'competitive.odin',odin_src/'competitive.odin')
        for variant in ('c_tuned','zig_tuned','odin_tuned'):
            samples=[]; sizes=[]
            for run in range(6):
                out=tmp/f'{variant}-{run}.o'
                if variant=='c_tuned':
                    cmd=['zig','cc','-O3','-std=c11','-fPIC','-ffp-contract=off','-I',str(SRC),'-c',str(SRC/'competitive.c'),'-o',str(out)]
                    if args.cpu=='native': cmd.insert(2,'-march=native')
                    cwd=ROOT
                elif variant=='zig_tuned':
                    cmd=['zig','build-obj',str(SRC/'competitive.zig'),'-O','ReleaseFast','-fPIC','-lc',f'-femit-bin={out}']
                    if args.cpu=='native': cmd += ['-mcpu','native']
                    cwd=ROOT
                else:
                    cmd=['odin','build',str(odin_src),'-build-mode:obj','-no-entry-point','-reloc-mode:pic','-o:speed','-no-bounds-check',f'-out:{out}']
                    if args.cpu=='native': cmd.append('-microarch:native')
                    cwd=ROOT
                ns=timed(cmd,cwd)
                if run: samples.append(ns)
                sizes.append(out.stat().st_size)
            rows[variant]={'median_compile_ns':int(statistics.median(samples)),'min_compile_ns':min(samples),'max_compile_ns':max(samples),'object_bytes':int(statistics.median(sizes[1:])), 'samples':len(samples)}
    payload={'schema':1,'cpu':args.cpu,'method':'five warm direct single-source object compiles; compiler startup included','variants':rows}
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text(json.dumps(payload,indent=2,sort_keys=True)+'\n'); print('BUILD_METRICS '+json.dumps(payload,sort_keys=True))
if __name__=='__main__': main()
