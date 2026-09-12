#!/usr/bin/env python3
"""Validate the ABI manifest and, optionally, a portable runtime proof."""
from __future__ import annotations
import argparse, json, pathlib, re
HERE = pathlib.Path(__file__).resolve().parent

def load_manifest(path: pathlib.Path):
    data=json.loads(path.read_text())
    if data.get('schema') != 1 or data.get('abi_version') != 1: raise SystemExit('unsupported ABI manifest schema/version')
    backends=set(data['backends'])
    if backends != {'c','zig','odin'}: raise SystemExit(f'unexpected backends: {sorted(backends)}')
    for name,op in data['operations'].items():
        parity=op.get('parity')
        if parity=='required':
            missing=backends-set(op.get('symbols',{}))
            if missing: raise SystemExit(f'{name}: required backend symbols missing: {sorted(missing)}')
        elif parity in {'intentional_shared_dependency','host_boundary','platform_boundary'}:
            if not op.get('reason'): raise SystemExit(f'{name}: intentional gap/boundary lacks reason')
        else: raise SystemExit(f'{name}: unknown parity class {parity!r}')
    return data

def proof(path,manifest):
    lines=path.read_text().splitlines(); line=next((x for x in lines if x.startswith('PORTABLE_ABI_PROOF ')),None)
    if not line: raise SystemExit('portable proof missing')
    record=json.loads(line.split(' ',1)[1])
    if record.get('backends')!=3 or record.get('pointer_bytes')!=8 or not record.get('dynlib'): raise SystemExit(f'invalid portable proof: {record}')
    for field,typ in {'star':'drbz_star','scanner':'drbz_scanner','view':'drbz_view','sprite':'drbz_packed_sprite','starfield':'drbz_starfield'}.items():
        if record.get(field)!=manifest['types'][typ]['size_64']: raise SystemExit(f'{typ}: size mismatch')
    rows=[json.loads(x.split(' ',1)[1]) for x in lines if x.startswith('PORTABLE_CONFORMANCE ')]
    if {r.get('backend') for r in rows}!={'c','zig','odin'}: raise SystemExit('conformance backend set mismatch')
    if any(r.get('abi_version')!=manifest['abi_version'] or not r.get('shutdown_clean') for r in rows): raise SystemExit('bad conformance record')

def symbols(path,manifest,backend):
    text=path.read_text(errors='replace'); missing=[]
    for op in manifest['operations'].values():
        if op.get('parity')!='required': continue
        value=op['symbols'][backend]
        for name in value if isinstance(value,list) else [value]:
            if '*' in name or name=='host_adapter': continue
            if not re.search(r'(?<![A-Za-z0-9_])'+re.escape(name)+r'(?![A-Za-z0-9_])',text): missing.append(name)
    if missing: raise SystemExit(f'{backend}: symbols absent from audit: {missing}')

def main():
    p=argparse.ArgumentParser(); p.add_argument('--manifest',type=pathlib.Path,default=HERE/'manifest.json'); p.add_argument('--proof',type=pathlib.Path); p.add_argument('--symbols',action='append',default=[])
    a=p.parse_args(); m=load_manifest(a.manifest)
    if a.proof: proof(a.proof,m)
    for spec in a.symbols: backend,file=spec.split(':',1); symbols(pathlib.Path(file),m,backend)
    print(json.dumps({'abi_manifest':'ok','version':m['abi_version'],'operations':len(m['operations']),'backends':sorted(m['backends'])},sort_keys=True))
if __name__=='__main__': main()
