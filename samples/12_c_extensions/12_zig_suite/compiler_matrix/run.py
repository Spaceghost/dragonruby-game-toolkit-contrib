#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, os, pathlib, platform, random, shutil, statistics, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parent
SUITE = ROOT.parent
WORKLOADS = [
    "count/scalar/4k","count/tuned/4k","count/scalar/1m","count/tuned/1m","sum/ordered/65536",
    "regex/compile-search/4k","regex/compiled-search/4k","scanner/original",
    "stars/original/no-wrap","stars/tuned/no-wrap","stars/original/mixed","stars/tuned/mixed",
    "stars/original/all-wrap","stars/tuned/all-wrap",
]
PROFILES = {
    "o2": ["-O2"], "o3": ["-O3"], "oz": ["-Oz"],
    "o3-thinlto": ["-O3","-flto=thin","-fuse-ld=lld"],
}
COMPILERS = {"clang": ["clang"], "zigcc": ["zig","cc"]}
COMMON = ["-std=c11","-ffp-contract=off","-Wall","-Wextra"]
if os.name != "nt": COMMON += ["-D_DEFAULT_SOURCE","-D_POSIX_C_SOURCE=200809L"]

def run(cmd, **kw): return subprocess.run(cmd, text=True, check=False, **kw)
def sha256(path): return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def machine_name(): return platform.machine().lower()

def target_flags(compiler: str, cpu: str) -> list[str]:
    machine = machine_name()
    x86 = machine in ("x86_64","amd64")
    arm = machine in ("aarch64","arm64")
    if compiler == "zigcc":
        if cpu == "native": return ["-mcpu=native"]
        if x86: return ["-mcpu=x86_64"]
        if arm: return ["-mcpu=generic"]
    else:
        if cpu == "native": return ["-march=native"] if x86 else ["-mcpu=native"]
        if x86: return ["-march=x86-64"]
        if arm: return ["-march=armv8-a"]
    raise SystemExit(f"unsupported compiler/target {compiler} {machine} {cpu}")

def generate_reference(out: pathlib.Path) -> None:
    cmd=[sys.executable,str(SUITE/"tools/reference.py"),str(SUITE.parent/"04_handcrafted_extension_advanced/native/ext-bindings.c"),str(SUITE.parent/"03_native_pixel_arrays/app/ext.c"),str(out)]
    p=run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if p.returncode: raise SystemExit(p.stdout)

def tool_output(candidates, binary):
    tool=next((shutil.which(x) for x in candidates if shutil.which(x)),None)
    if not tool: return "",0,0,0
    text=run([tool,"-B",str(binary)] if pathlib.Path(tool).name.startswith("size") else [tool,str(binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT).stdout
    vals=(0,0,0)
    for line in reversed(text.strip().splitlines()):
        fields=line.split()
        if len(fields)>=3:
            try: vals=tuple(map(int,fields[:3])); break
            except ValueError: pass
    return text,*vals

def compile_variant(name, compiler, cc, flags, outdir, generated):
    binary=outdir/(f"bench-{name}.exe" if os.name=="nt" else f"bench-{name}")
    sources=[ROOT/"bench.c",SUITE/"src/competitive.c",SUITE/"tests/controls.c",generated,SUITE.parent/"02_intermediate/app/re.c"]
    cpu_flags=target_flags(compiler, ARGS.cpu)
    cmd=cc+COMMON+flags+cpu_flags+["-I",str(SUITE/"src"),"-I",str(SUITE.parent/"02_intermediate/app")]+[str(x) for x in sources]+["-o",str(binary)]
    times=[]; output=""
    for rep in range(3):
        start=time.perf_counter_ns(); p=run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT); elapsed=time.perf_counter_ns()-start
        output=p.stdout; times.append(elapsed)
        if p.returncode:
            (outdir/f"build-{name}.log").write_text(output)
            return None,{"name":name,"compiler":compiler,"supported":False,"command":cmd,"cpu_flags":cpu_flags,"build_ns":times,"log":output[-4000:]}
    (outdir/f"build-{name}.log").write_text(output)
    size,text_bytes,data_bytes,bss_bytes=tool_output(["size","llvm-size"],binary)
    (outdir/f"size-{name}.txt").write_text(size)
    nmtool=next((shutil.which(x) for x in ("nm","llvm-nm") if shutil.which(x)),None)
    nm=run([nmtool,"-S","--size-sort",str(binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT).stdout if nmtool else ""
    (outdir/f"symbols-{name}.txt").write_text(nm)
    return binary,{"name":name,"compiler":compiler,"supported":True,"command":cmd,"cpu_flags":cpu_flags,"build_ns":times,"build_median_ns":statistics.median(times),"file_bytes":binary.stat().st_size,"text_bytes":text_bytes,"data_bytes":data_bytes,"bss_bytes":bss_bytes,"sha256":sha256(binary)}

def compiler_version(cc):
    p=run(cc+["--version"],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    return p.stdout.strip().splitlines()[:4]

def parse_rows(text):
    rows=[]
    for line in text.splitlines():
        if line.startswith("{"):
            row=json.loads(line)
            if row.get("workload") in WORKLOADS: rows.append(row)
    if [r["workload"] for r in rows] != WORKLOADS: raise RuntimeError(f"incomplete runtime output: {text[-2000:]}")
    return rows

def main():
    out=pathlib.Path(ARGS.output); out.mkdir(parents=True,exist_ok=True)
    generated=out/"original.c"; generate_reference(generated)
    meta={"cpu_mode":ARGS.cpu,"machine":machine_name(),"platform":platform.platform(),"compiler_versions":{k:compiler_version(v) for k,v in COMPILERS.items()},"target_flags":{k:target_flags(k,ARGS.cpu) for k in COMPILERS},"zig_version":run(["zig","version"],stdout=subprocess.PIPE).stdout.strip()}
    (out/"environment.json").write_text(json.dumps(meta,indent=2)+"\n")
    builds=[]; binaries={}
    for compiler,cc in COMPILERS.items():
        if not shutil.which(cc[0]):
            builds.append({"name":compiler,"compiler":compiler,"supported":False,"log":"compiler not installed"}); continue
        for profile,pflags in PROFILES.items():
            name=f"{compiler}-{profile}"
            binary,record=compile_variant(name,compiler,cc,pflags,out,generated); builds.append(record)
            if binary: binaries[name]=binary
    (out/"builds.json").write_text(json.dumps(builds,indent=2)+"\n")
    required={f"{c}-{p}" for c in COMPILERS for p in ("o2","o3","oz")}
    missing=required-set(binaries)
    if missing: raise SystemExit(f"required compiler profiles failed: {sorted(missing)}")

    calibration_seed=0xD1CEB00C; calibration={}
    for name,binary in binaries.items():
        p=run([str(binary),str(ARGS.min_ns),str(calibration_seed)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if p.returncode: raise SystemExit(f"calibration failed {name}: {p.stdout[-4000:]}")
        calibration[name]=parse_rows(p.stdout)
    plan=[]
    for wi,workload in enumerate(WORKLOADS):
        ns={rows[wi]["iterations"] for rows in calibration.values()}; chosen=max(ns)
        if chosen <= 0: raise SystemExit(f"invalid calibration {workload}: {ns}")
        plan.append(chosen)
    meta["fixed_iterations"]={w:n for w,n in zip(WORKLOADS,plan)}
    (out/"environment.json").write_text(json.dumps(meta,indent=2)+"\n")

    observations=[]; rng=random.Random(0x5A17CC); fixed=[str(n) for n in plan]
    for process in range(3):
        for trial in range(11):
            names=list(binaries); rng.shuffle(names); seed=(0x9E3779B97F4A7C15 ^ (process<<40) ^ (trial<<8)) & ((1<<64)-1); trial_rows={}
            for order,name in enumerate(names):
                p=run([str(binaries[name]),"--fixed",str(seed),*fixed],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                if p.returncode: raise SystemExit(f"runtime failed {name}: {p.stdout[-4000:]}")
                rows=parse_rows(p.stdout); trial_rows[name]=rows
                for wi,row in enumerate(rows):
                    if row["iterations"] != plan[wi]: raise SystemExit(f"unequal work {name} {row}")
                    observations.append({**row,"variant":name,"process":process,"trial":trial,"order":order,"seed":seed})
            for wi,workload in enumerate(WORKLOADS):
                checks={rows[wi]["checksum"] for rows in trial_rows.values()}
                if len(checks)!=1: raise SystemExit(f"checksum mismatch {workload} p{process} t{trial}: {checks}")
            table={r["workload"]:r["checksum"] for r in next(iter(trial_rows.values()))}
            for suffix in ("no-wrap","mixed","all-wrap"):
                if table[f"stars/original/{suffix}"] != table[f"stars/tuned/{suffix}"]: raise SystemExit(f"original/tuned star checksum mismatch: {suffix}")
            for size in ("4k","1m"):
                if table[f"count/scalar/{size}"] != table[f"count/tuned/{size}"]: raise SystemExit(f"count mismatch {size}")
    with (out/"raw.jsonl").open("w") as f:
        for row in observations: f.write(json.dumps(row,separators=(",",":"))+"\n")

    groups={}
    for row in observations: groups.setdefault((row["variant"],row["workload"]),[]).append(row["ns_per_op"])
    build_by={r["name"]:r for r in builds if r.get("supported")}
    lines=["# C compiler/profile matrix","",f"Host `{platform.system()} {machine_name()}`, CPU mode `{ARGS.cpu}`. Identical C sources and equal-work batches; 33 interleaved observations per supported profile.","","| workload | variant | median ns/op | MAD | build median ms | text bytes | file bytes |","| --- | --- | ---: | ---: | ---: | ---: | ---: |"]
    summary=[]
    for workload in WORKLOADS:
        for variant in sorted(binaries):
            vals=groups[(variant,workload)]; med=statistics.median(vals); mad=statistics.median(abs(x-med) for x in vals); b=build_by[variant]
            lines.append(f"| {workload} | {variant} | {med:.3f} | {mad:.3f} | {b['build_median_ns']/1e6:.1f} | {b['text_bytes']} | {b['file_bytes']} |")
            summary.append({"workload":workload,"variant":variant,"median_ns":med,"mad_ns":mad})
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n"); (out/"report.md").write_text("\n".join(lines)+"\n"); print("\n".join(lines))

if __name__=="__main__":
    ap=argparse.ArgumentParser(); ap.add_argument("--cpu",choices=["baseline","native"],required=True); ap.add_argument("--output",required=True); ap.add_argument("--min-ns",type=int,default=2_000_000); ARGS=ap.parse_args(); main()
