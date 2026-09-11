#!/usr/bin/env python3
import json, os, pathlib, platform, shutil, subprocess, sys, tempfile, time

root=pathlib.Path(__file__).resolve().parents[1]
out=pathlib.Path(os.environ.get('MATRIX_OUT', root/'matrix-results.json'))
target=os.environ['ZIG_TARGET']
profiles={
 'O0':['-O0'],
 'O2':['-O2'],
 'O3':['-O3'],
 'Os':['-Os'],
 'O3-native':['-O3','-march=native'],
}
if os.name=='nt': exe_suffix='.exe'
else: exe_suffix=''
rows=[]
for name,cflags in profiles.items():
    work=pathlib.Path(tempfile.mkdtemp(prefix='zigcc-'+name+'-'))
    try:
        cache=work/'cache'; cache.mkdir()
        env=os.environ.copy(); env['ZIG_GLOBAL_CACHE_DIR']=str(cache); env['ZIG_LOCAL_CACHE_DIR']=str(work/'local-cache')
        zig_obj=work/'competitive-zig.o'
        cpu='native' if name=='O3-native' else 'baseline'
        zcmd=['zig','build-obj',str(root/'src/competitive.zig'),'-O','ReleaseFast','-target',target,'-mcpu='+cpu,'-femit-bin='+str(zig_obj)]
        t=time.perf_counter(); subprocess.run(zcmd,check=True,env=env,cwd=root); zig_compile=time.perf_counter()-t
        exe=work/('matrix-bench'+exe_suffix)
        ccmd=['zig','cc','-std=c11','-target',target,*cflags,'-I'+str(root/'src'),str(root/'matrix/matrix_bench.c'),str(root/'src/competitive.c'),str(zig_obj),'-o',str(exe)]
        t=time.perf_counter(); subprocess.run(ccmd,check=True,env=env,cwd=root); c_compile=time.perf_counter()-t
        runs=[]
        for _ in range(5):
            cp=subprocess.run([str(exe)],check=True,text=True,capture_output=True,cwd=root)
            values={}
            for line in cp.stdout.splitlines():
                if line.startswith('RESULT '):
                    _,k,v=line.split(maxsplit=2); values[k]=float(v) if k!='checksum' else int(v)
            runs.append(values)
        keys=[k for k in runs[0] if k!='checksum']
        med={k:sorted(r[k] for r in runs)[len(runs)//2] for k in keys}
        if len({r['checksum'] for r in runs})!=1: raise SystemExit('checksum drift')
        rows.append({'profile':name,'target':target,'cflags':cflags,'zig_cpu':cpu,'zig_compile_s':zig_compile,'c_link_compile_s':c_compile,'binary_bytes':exe.stat().st_size,'median':med,'checksum':runs[0]['checksum']})
    finally:
        shutil.rmtree(work,ignore_errors=True)
payload={'host':{'system':platform.system(),'machine':platform.machine(),'processor':platform.processor()},'target':target,'zig_version':subprocess.check_output(['zig','version'],text=True).strip(),'rows':rows}
out.write_text(json.dumps(payload,indent=2,sort_keys=True)+'\n')
print(json.dumps(payload,indent=2,sort_keys=True))
