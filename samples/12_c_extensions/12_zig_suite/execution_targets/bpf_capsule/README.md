# BPF Capsule execution target

This is an execution target for the existing C/Zig/Odin competition, not a fourth language participant.

The question is deliberately harsher than native throughput: **can the same implementation preserve its semantics after being lowered into verifier-loadable eBPF and executed under an unmodified Linux kernel?** A frontend, transform, verifier, kernel-profile, or runtime failure is retained as evidence rather than replaced with target-specific code until it passes.

## First slice

The first executable slice reuses `src/competitive.c` unchanged and calls its tuned `drbc_count_dual` implementation from a BPF Capsule `syscall` program. The host compares the result with an independent scalar LF oracle and reports the number of Capsule continuations plus kernel-accounted execution time.

The fixed `.data` transport intentionally limits this first slice to inputs of at most 64 KiB. It covers the small, 4 KiB, 8 KiB, 16 KiB, and 64 KiB LF workloads. The 1 MiB warm/rotating cases need a host-visible Capsule-memory transport; they must not be made to "pass" by embedding megabytes into the BPF data section.

The star kernels come after LF parity. Their callback/RNG ordering is already part of the native semantic contract, so the target must preserve that exact order rather than substituting a BPF-friendly approximation.

## Build

With BPF Capsule installed together with `bpftool` and libbpf development files:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr
cmake --build build --parallel
```

This produces `rival.bpf.o`, a libbpf skeleton, and `rival_capsule_host`.

Running the host requires a kernel/configuration that permits BPF program loading and `BPF_PROG_TEST_RUN`:

```sh
./build/rival_capsule_host input.bin
```

Successful output is one JSON object containing bytes, LF count, continuation count, and kernel-accounted nanoseconds. The host exits nonzero on load failure, Capsule failure, excessive continuation count, or semantic mismatch.

## Evidence contract

`TARGET.json` is the machine-readable target contract. Results should retain every phase separately:

1. language frontend accepted the unchanged implementation;
2. Capsule transformation/link succeeded;
3. BPF object was produced;
4. the kernel accepted it;
5. execution completed, including any continuations;
6. result matched the existing semantic oracle.

Native timing remains the comparison baseline. Capsule results add dimensions to the existing Pareto evidence; they do not replace the native competition or produce a fake aggregate winner.

C is implemented first. Zig must enter through genuine LLVM-compatible output from the Zig implementation. Odin currently has a textual LLVM-IR route; whether that IR can be assembled by the LLVM 23 toolchain required by BPF Capsule is itself part of the experiment.
