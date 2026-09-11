#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, os, pathlib, platform, random, shutil, statistics, subprocess, sys, time
ROOT=pathlib.Path(__file__).resolve().parent; SUITE=ROOT.parent
WORKLOADS=["count/scalar/4k","count/tuned/4k","count/scalar/1m","count/tuned/1m","sum/ordered/65536","regex/compile-search/4k","regex/compiled-search/4k","scanner/original","stars/original/no-wrap","stars/tuned/no-wrap","stars/original/mixed","stars/tuned/mixed","stars/original/all-wrap","stars/tuned/all-wrap"]
EQUIV=[("count/scalar/4k","count/tuned/4k"),("count/scalar/1m","count/tuned/1m"),("stars/original/no-wrap","stars/tuned/no-wrap"),("stars/original/mixed","stars/tuned/mixed"),("stars/original/all-wrap","stars/tuned/all-wrap")]
PROFILES={"o2":["-O2"],"o3":["-O3"],"oz":["-Oz"],"o3-thinlto":["-O3","-flto=thin","-fuse-ld=lld"]}; COMPILERS={"clang":["clang"],"zigcc":["zig","cc"]}
COMMON=["-std=c11","-ffp-contract=off","-Wall","-Wextra"]+([] if os.name=="nt" else ["-D_DEFAULT_SOURCE","-D_POSIX_C_SOURCE=200809L"])
def run(cmd,**kw): return subprocess.run(cmd,text=True,check=False,**kw)
def machine(): return platform.machine().lower()
def flags(kind,cpu):
    x86=machine() in ("x86_64","amd64"); arm=machine() in ("aarch64","arm64")
    if kind=="zigcc": return ["-mcpu=native"] if cpu=="native" else (["-mcpu=x86_64"] if x86 else ["-mcpu=generic"])
    if cpu=="native": return ["-march=native"] if x86 else ["-mcpu=native"]
    return ["-march=x86-64"] if x86 else ["-march=armv8-a"] if arm else []
def includes(): return ["-I",str(SUITE/"src"),"-I",str(SUITE.parent/"02_intermediate/app")]
def reference(out):
    p=run([sys.executable,str(SUITE/"tools/reference.py"),str(SUITE.parent/"04_handcrafted_extension_advanced/native/ext-bindings.c"),str(SUITE.parent/"03_native_pixel_arrays/app/ext.c"),str(out)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if p.returncode: raise SystemExit(p.stdout)
def compile_harness(out):
    if os.name=="nt": return None
    obj=out/"bench-harness.o"; cmd=["clang",*COMMON,*flags("clang",A.cpu),*includes(),"-O2","-c",str(ROOT/"bench.c"),"-o",str(obj)]
    p=run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if p.returncode: raise SystemExit(f"host timing harness failed: {p.stdout[-4000:]}")
    return obj
def compile_one(name,kind,cc,pflags,out,generated,harness):
    exe=out/(f"bench-{name}.exe" if os.name=="nt" else f"bench-{name}"); cpu=flags(kind,A.cpu)
    algorithm=[SUITE/"src/competitive.c",SUITE/"tests/controls.c",generated,SUITE.parent/"02_intermediate/app/re.c"]
    times=[]; logs=[]; last_cmd=[]
    for rep in range(3):
        repdir=out/f"obj-{name}-{rep}"; repdir.mkdir(exist_ok=True); objects=[]; start=time.perf_counter_ns(); failed=False
        if os.name=="nt":
            # Windows host Clang is MSVC ABI while zig cc defaults to GNU ABI;
            # keep the QPC timer in the candidate translation unit rather than
            # mixing object ABIs. QPC does not exhibit the Linux clock issue.
            cmd=cc+COMMON+pflags+cpu+includes()+[str(ROOT/"bench.c"),*map(str,algorithm),"-o",str(exe)]
            p=run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT); logs.append(p.stdout); last_cmd=cmd; failed=p.returncode!=0
        else:
            for idx,src in enumerate(algorithm):
                obj=repdir/f"a{idx}.o"; cmd=cc+COMMON+pflags+cpu+includes()+["-c",str(src),"-o",str(obj)]; p=run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT); logs.append(p.stdout); last_cmd=cmd
                if p.returncode: failed=True; break
                objects.append(obj)
            if not failed:
                # Candidate compiler performs the final link so its ThinLTO
                # plugin can optimize candidate bitcode. The timer/harness is
                # already ordinary host-Clang machine code and cannot vanish.
                cmd=cc+pflags+cpu+[str(harness),*map(str,objects),"-o",str(exe)]; p=run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT); logs.append(p.stdout); last_cmd=cmd; failed=p.returncode!=0
        times.append(time.perf_counter_ns()-start)
        if failed: return None,{"name":name,"compiler":kind,"supported":False,"command":last_cmd,"log":"\n".join(logs)[-4000:]}
    (out/f"build-{name}.log").write_text("\n".join(logs)); size_tool=shutil.which("size") or shutil.which("llvm-size"); text=0
    if size_tool:
        s=run([size_tool,"-B",str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT).stdout; (out/f"size-{name}.txt").write_text(s)
        try: text=int(s.strip().splitlines()[-1].split()[0])
        except Exception: pass
    return exe,{"name":name,"compiler":kind,"supported":True,"command":last_cmd,"build_ns":times,"build_median_ns":statistics.median(times),"file_bytes":exe.stat().st_size,"text_bytes":text,"sha256":hashlib.sha256(exe.read_bytes()).hexdigest(),"timer_compiler":"candidate" if os.name=="nt" else "host-clang-object"}
def rows(text):
    r=[json.loads(x) for x in text.splitlines() if x.startswith("{")]
    if [x.get("workload") for x in r]!=WORKLOADS: raise SystemExit("incomplete benchmark output")
    return r
def main():
    out=pathlib.Path(A.output); out.mkdir(parents=True,exist_ok=True); generated=out/"original.c"; reference(generated); harness=compile_harness(out)
    meta={"cpu_mode":A.cpu,"machine":machine(),"platform":platform.platform(),"zig_version":run(["zig","version"],stdout=subprocess.PIPE).stdout.strip(),"timer_isolation":"candidate on Windows; host-Clang precompiled harness elsewhere"}; builds=[]; bins={}
    for kind,cc in COMPILERS.items():
        if not shutil.which(cc[0]): raise SystemExit(f"missing required compiler {cc[0]}")
        meta[kind+"_version"]=run(cc+["--version"],stdout=subprocess.PIPE,stderr=subprocess.STDOUT).stdout.splitlines()[:4]
        for profile,pflags in PROFILES.items():
            name=f"{kind}-{profile}"; exe,rec=compile_one(name,kind,cc,pflags,out,generated,harness); builds.append(rec)
            if exe: bins[name]=exe
    required={f"{c}-{p}" for c in COMPILERS for p in ("o2","o3","oz")}; missing=required-set(bins)
    if missing: raise SystemExit(f"required profiles failed: {sorted(missing)}")
    (out/"builds.json").write_text(json.dumps(builds,indent=2)+"\n")
    calib={}
    for name,exe in bins.items():
        p=run([str(exe),str(A.min_ns),str(0xD1CEB00C)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if p.returncode: raise SystemExit(f"calibration failed {name}: {p.stdout[-2000:]}")
        calib[name]=rows(p.stdout)
    plan=[max(r[i]["iterations"] for r in calib.values()) for i in range(len(WORKLOADS))]; index={w:i for i,w in enumerate(WORKLOADS)}
    for a,b in EQUIV:
        ia,ib=index[a],index[b]; n=max(plan[ia],plan[ib]); plan[ia]=plan[ib]=n
    meta["fixed_iterations"]={w:n for w,n in zip(WORKLOADS,plan)}; (out/"environment.json").write_text(json.dumps(meta,indent=2)+"\n")
    obs=[]; rnd=random.Random(0x5A17CC); fixed=list(map(str,plan))
    for process in range(3):
      for trial in range(11):
        order=list(bins); rnd.shuffle(order); seed=(0x9E3779B97F4A7C15^(process<<40)^(trial<<8))&((1<<64)-1); got={}
        for slot,name in enumerate(order):
            p=run([str(bins[name]),"--fixed",str(seed),*fixed],stdout=subprocess.PIPE,stderr=subprocess.STDOUT); rr=rows(p.stdout) if p.returncode==0 else []
            if p.returncode: raise SystemExit(f"runtime failed {name}: {p.stdout[-2000:]}")
            got[name]=rr
            for i,row in enumerate(rr):
                if row["iterations"]!=plan[i]: raise SystemExit("unequal work")
                if row["elapsed_ns"]<=0 or row["ns_per_op"]<=0: raise SystemExit(f"invalid timer result {name}: {row}")
                obs.append({**row,"variant":name,"process":process,"trial":trial,"order":slot,"seed":seed})
        for i,w in enumerate(WORKLOADS):
            if len({r[i]["checksum"] for r in got.values()})!=1: raise SystemExit(f"compiler checksum mismatch {w}")
        table={r["workload"]:r["checksum"] for r in next(iter(got.values()))}
        for a,b in EQUIV:
            if table[a]!=table[b]: raise SystemExit(f"equivalent workload mismatch: {a} vs {b}")
    with (out/"raw.jsonl").open("w") as f:
        for r in obs: f.write(json.dumps(r,separators=(",",":"))+"\n")
    groups={}
    for r in obs: groups.setdefault((r["variant"],r["workload"]),[]).append(r["ns_per_op"])
    br={r["name"]:r for r in builds if r.get("supported")}; summary=[]; lines=["# C compiler/profile matrix","",f"Host `{platform.system()} {machine()}`, CPU `{A.cpu}`. Equal-work paired workloads; 33 interleaved observations/profile.","","| workload | variant | median ns/op | MAD | build ms | text B | file B |","| --- | --- | ---: | ---: | ---: | ---: | ---: |"]
    for w in WORKLOADS:
      for v in sorted(bins):
        vals=groups[v,w]; med=statistics.median(vals); mad=statistics.median(abs(x-med) for x in vals); b=br[v]; lines.append(f"| {w} | {v} | {med:.3f} | {mad:.3f} | {b['build_median_ns']/1e6:.1f} | {b['text_bytes']} | {b['file_bytes']} |"); summary.append({"workload":w,"variant":v,"median_ns":med,"mad_ns":mad})
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n"); (out/"report.md").write_text("\n".join(lines)+"\n"); print("\n".join(lines))
if __name__=="__main__":
    p=argparse.ArgumentParser(); p.add_argument("--cpu",choices=["baseline","native"],required=True); p.add_argument("--output",required=True); p.add_argument("--min-ns",type=int,default=2_000_000); A=p.parse_args(); main()
