# Persistent native starfield

This stage answers a narrower question than the kernel benchmark: where does the
starfield win go when state and render records stay native for the whole frame?

`starfield.zig` owns no heap. A caller allocates one persistent buffer sized by
`drbz_starfield_storage_bytes`, initializes SoA `x/y/speed` plus packed sprite
records once, and then chooses one of three measured stages:

1. `update`: tuned Zig motion only.
2. `update-pack`: motion plus packing `[x,y,w,h,path_id]` records.
3. `frame-sink`: update + pack + exactly one borrowed batch-sink call whose test
   implementation consumes every sprite record.

The sink models the native renderer boundary but **is not the proprietary
DragonRuby GPU renderer**. Public contrib source exposes per-star `draw_sprite`
from the old advanced C sample and Ruby-side array queueing, but no public
`drb_api_t` bulk-sprite C function that can honestly be called here. A matching
SDK adapter is therefore a separate integration gate rather than an invented API.

The benchmark runs 64, 1,024, 16,384 and 100,000 stars. Correctness compares all
three stages from identical seeds bit-for-bit for positions, speeds and RNG state,
then verifies packed records and exactly one sink call. Timing allocation occurs
before the measured region. CI additionally inspects the starfield archive for
allocator imports; that is scoped evidence, not a whole-process allocation claim.
