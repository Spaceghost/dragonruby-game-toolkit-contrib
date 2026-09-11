# Host Clang versus `zig cc`

This matrix asks a compiler question separately from the C-versus-Zig language
competition. The **same C sources** are compiled by the runner's host Clang and
checksum-pinned Zig 0.16.0's `zig cc`, on the same physical hosted runner.

Each compiler receives matched `-O2`, `-O3`, and `-Oz` profiles plus an
experimental `-O3 -flto=thin` profile when the local linker supports it. CPU
selection is also matched: x86 baseline uses `-march=x86-64`; native jobs use
`-march=native`. No fast math is enabled and FP contraction is disabled.

The runtime corpus includes scalar/tuned newline count, ordered sum, original
`tiny-regex-c` compile+search and compiled search, the extracted original scanner,
and extracted-original versus tuned C star motion under no-wrap/mixed/all-wrap
workloads. Original stateful kernels are generated from source-pinned upstream
sample files by `tools/reference.py` rather than handwritten again.

For every supported compiler/profile the runner records three full build wall
clock measurements, executable/text/data/BSS sizes, symbol sizes and binary hash.
Runtime is then interleaved across compiler/profile executables for 3 processes x
11 trials. Checksums must agree across all compiler profiles; original/tuned star
and scalar/tuned newline controls also have to agree before any timing is kept.

This is **not** a proprietary DragonRuby SDK build matrix. Public contrib source
contains the original compile recipes but not a public complete `drb_api_t`
renderer SDK. The matrix therefore measures the native C work we can reproduce
exactly in public CI; matching-SDK shared-library compile/load timing remains a
separate gate when that SDK is available.
