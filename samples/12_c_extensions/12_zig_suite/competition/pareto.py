#!/usr/bin/env python3
"""Build per-workload C/Zig/Odin Pareto frontiers without inventing a weighted score."""
from __future__ import annotations
import argparse, json, math, pathlib, re
TUNED=('c_tuned','zig_tuned','odin_tuned')
SOURCE={'c_tuned':'src/competitive.c','zig_tuned':'src/competitive.zig','odin_tuned':'src/competitive.odin'}
SIZE_FILE={'c_tuned':'c-sizes.stdout','zig_tuned':'zig-sizes.stdout','odin_tuned':'odin-sizes.stdout'}
ASM_FILE={'c_tuned':'c-assembly.stdout','zig_tuned':'zig-assembly.stdout','odin_tuned':'odin-assembly.stdout'}
COUNT_SYMBOL={'c_tuned':'drbc_count_dual','zig_tuned':'drbz_count_dual','odin_tuned':'drbo_count_dual'}
STAR_SYMBOL={'c_tuned':'drbc_stars_block','zig_tuned':'drbz_stars_block','odin_tuned':'drbo_stars_block'}

def logical_source(path):
    text=path.read_text(errors='replace'); lines=0; in_block=False
    for raw in text.splitlines():
        s=raw.strip()
        if in_block:
            if '*/' in s: in_block=False; s=s.split('*/',1)[1].strip()
            else: continue
        if s.startswith('/*'):
            if '*/' not in s: in_block=True
            s=s.split('/*',1)[0].strip()
        if s and not s.startswith('//') and not s.startswith('# '): lines+=1
    return {'source_lines':lines,'source_bytes':len(text.encode())}

def parse_sizes(path):
    rows=[]
    if not path.exists(): return rows
    rx=re.compile(r'^([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+\S\s+(.+)$')
    for line in path.read_text(errors='replace').splitlines():
        m=rx.match(line.strip())
        if m: rows.append((int(m.group(1),16),int(m.group(2),16),m.group(3)))
    return rows

def size_for(rows, variant, case):
    symbol=(STAR_SYMBOL if '/stars/' in case else COUNT_SYMBOL)[variant]
    exact=[(a,s,n) for a,s,n in rows if n==symbol]
    return exact[0][1] if exact else None

def extract_function(text,name):
    rx=re.compile(r'^[0-9A-Fa-f]+\s+<'+re.escape(name)+r'>:\s*$',re.M); m=rx.search(text)
    if not m: return ''
    tail=text[m.end():]; nxt=re.search(r'^\s*[0-9A-Fa-f]+\s+<[^>]+>:\s*$',tail,re.M)
    return tail[:nxt.start()] if nxt else tail

def asm_metrics(path,variant,case):
    if not path.exists(): return {}
    name=(STAR_SYMBOL if '/stars/' in case else COUNT_SYMBOL)[variant]; body=extract_function(path.read_text(errors='replace'),name); inst=[]
    for line in body.splitlines():
        if ':' not in line: continue
        parts=re.split(r'\s+',line.split(':',1)[1].strip())
        while parts and re.fullmatch(r'[0-9a-fA-F]{2,16}',parts[0]): parts.pop(0)
        if parts and re.match(r'[A-Za-z.]',parts[0]): inst.append(parts[0].lower())
    if not inst: return {}
    return {'asm_instructions':len(inst),'asm_calls':sum(x.startswith(('call','bl')) for x in inst),'asm_branches':sum(x.startswith(('j','b.','cb','tb')) and not x.startswith('jmpq') for x in inst),'asm_mnemonics':len(set(inst))}

def allocation_metrics(path,case,variant):
    if not path.exists(): return {}
    rows=json.loads(path.read_text()); matches=[r for r in rows if r.get('case')==case and r.get('variant')==variant]
    if not matches: return {}
    return {'alloc_calls':max(sum(r.get(k,0) for k in ('malloc_calls','calloc_calls','realloc_calls','aligned_calls')) for r in matches),'requested_bytes':max(r.get('requested_bytes',0) for r in matches)}

def dominates(a,b,axes):
    common=[x for x in axes if a.get(x) is not None and b.get(x) is not None]
    return bool(common) and all(a[x] <= b[x] for x in common) and any(a[x] < b[x] for x in common)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('input',type=pathlib.Path); ap.add_argument('output',type=pathlib.Path); ap.add_argument('--suite-root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parent.parent); args=ap.parse_args()
    artifacts=[]
    for summary in args.input.rglob('summary.json'):
        d=summary.parent; env=json.loads((d/'environment.json').read_text()) if (d/'environment.json').exists() else {}; target=f"{env.get('machine','unknown')}|{env.get('cpu_target','unknown')}|{d.name}"; artifacts.append((target,d,json.loads(summary.read_text())))
    if not artifacts: raise SystemExit('no rivals summary.json artifacts found')
    source={v:logical_source(args.suite_root/SOURCE[v]) for v in TUNED}; cases=sorted({r['case'] for _,_,rows in artifacts for r in rows if r['variant'] in TUNED})
    output={'schema':1,'axes_note':'No weighted score. Lower is better on every numeric axis. Compile time comes from matched direct single-source object builds with compiler startup included.','cases':{}}
    md=['# C / Zig / Odin Pareto frontier','','No weighted score is computed. A candidate is dominated only when another candidate is no worse on every available axis and strictly better on at least one.','']
    for case in cases:
        metrics={v:{**source[v]} for v in TUNED}; normalized={v:[] for v in TUNED}
        for target,d,rows in artifacts:
            bm=json.loads((d/'build-metrics.json').read_text()) if (d/'build-metrics.json').exists() else {}
            for v in TUNED:
                bmv=bm.get('variants',{}).get(v)
                if bmv:
                    metrics[v]['compile_ns']=max(metrics[v].get('compile_ns',0),bmv['median_compile_ns']); metrics[v]['object_bytes']=max(metrics[v].get('object_bytes',0),bmv['object_bytes'])
            case_rows={r['variant']:r for r in rows if r['case']==case and r['variant'] in TUNED}
            if not case_rows: continue
            fastest=min(r['median_ns'] for r in case_rows.values())
            for v,r in case_rows.items():
                metrics[v].setdefault('targets',{})[target]=r['median_ns']; normalized[v].append(r['median_ns']/fastest)
                for k,val in allocation_metrics(d/'allocations.json',case,v).items(): metrics[v][k]=max(metrics[v].get(k,0),val)
                sz=size_for(parse_sizes(d/SIZE_FILE[v]),v,case)
                if sz is not None: metrics[v]['code_symbol_bytes']=max(metrics[v].get('code_symbol_bytes',0),sz)
                for k,val in asm_metrics(d/ASM_FILE[v],v,case).items(): metrics[v][k]=max(metrics[v].get(k,0),val)
        for v in TUNED:
            if normalized[v]:
                logs=[math.log(x) for x in normalized[v]]; metrics[v]['runtime_geomean_vs_fastest']=math.exp(sum(logs)/len(logs)); metrics[v]['architecture_spread']=max(normalized[v])-min(normalized[v]) if len(normalized[v])>1 else 0.0
        axes=['runtime_geomean_vs_fastest','architecture_spread','compile_ns','alloc_calls','requested_bytes','code_symbol_bytes','source_lines','source_bytes','object_bytes','asm_instructions','asm_calls','asm_branches']
        frontier=[v for v in TUNED if not any(dominates(metrics[o],metrics[v],axes) for o in TUNED if o!=v)]; output['cases'][case]={'frontier':frontier,'metrics':metrics,'axes':axes}
        md += [f'## `{case}`','',f"**Frontier:** {', '.join(frontier)}",'','| variant | run ×fastest | arch spread | compile ms | symbol B | allocs | src lines | asm inst |','|---|---:|---:|---:|---:|---:|---:|---:|']
        for v in TUNED:
            m=metrics[v]; md.append(f"| {v} | {m.get('runtime_geomean_vs_fastest',float('nan')):.3f} | {m.get('architecture_spread',float('nan')):.3f} | {m.get('compile_ns',0)/1e6:.2f} | {m.get('code_symbol_bytes','-')} | {m.get('alloc_calls','-')} | {m.get('source_lines','-')} | {m.get('asm_instructions','-')} |")
        md.append('')
    args.output.mkdir(parents=True,exist_ok=True); (args.output/'pareto.json').write_text(json.dumps(output,indent=2,sort_keys=True)+'\n'); (args.output/'pareto.md').write_text('\n'.join(md)+'\n'); print('\n'.join(md))
if __name__=='__main__': main()
