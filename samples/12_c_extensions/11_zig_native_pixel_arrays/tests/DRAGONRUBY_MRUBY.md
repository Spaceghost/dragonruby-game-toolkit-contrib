# Published DragonRuby mruby compatibility target

This target implements the recipe in `docs/dragonruby-mruby.patch.readme.txt`:
mruby 3.0.0 plus the complete published `docs/dragonruby-mruby.patch`.
It is a compatibility test for that published source, not a reconstruction of a
current proprietary DragonRuby SDK or renderer.

## Exact inputs

- mruby 3.0.0 commit: `0f45836b5954accf508f333f932741b925214471`.
- Published patch Git blob: `bd85545b27f4c6397da251ca05ead018cecb9818`.
- Existing upstream control: mruby 3.4.0 commit
  `a309524d0bc90eef077a24634db2495a6f68e318`.
- Zig 0.16.0, with the existing checked download checksum.

The workflow verifies the clean mruby checkout and the patch blob before
`git apply --check --index` followed by `git apply --index`. It neither edits
nor partially applies the published patch. Any changed patch, wrong base or
application failure stops that job. Changes to the patch or its recipe trigger
this workflow, even when the Zig sample is unchanged. A deliberate patch update
must review and update the pin rather than silently testing different source.

## What executes

`.github/workflows/zig-mruby.yml` runs both sources in word, NaN and unboxed
configurations on standard `ubuntu-24.04` runners. Each job builds its own VM
and executes the actual shared `bridge.c`/Zig library in Debug and ReleaseSafe.
The same Ruby smoke suite, 10,000-frame comparisons, invalid-argument state
checks, re-registration, garbage collection and three fresh VM lifetimes run
for both sources.

The patched jobs also require `-Dmruby-dragonruby=true`. The C host verifies the
patch's `sym_default` and `sym_initialize` fields against the running VM's
symbols, so stock headers cannot satisfy the build. `dragonruby_mruby.rb`
checks the expected 3.0.0 version and the patch's floating-point `5 / 2 == 2.5`
behavior inside that same host. It then exercises constructor/hash-default
behavior around native results. A successful build alone is not a pass.

As in the upstream harness, preallocated symbols are disabled through mruby's
`disable_presym` build API and integer width/boxing options are matched between
the VM, host and adapter. The patch is applied unchanged, including its
published generated symbol files; this is not a claim to reproduce the SDK's
private build configuration. The host API table and upload sink remain the
explicitly isolated test contract. No second VM is linked into the extension.

## Reproduce on Linux

Use a fresh checkout or disposable worktree; this applies a patch to it.
From this sample's directory, run the following as a shell script after
setting absolute paths:

```sh
set -eu
export MRUBY_ROOT=/absolute/path/to/fresh-mruby-checkout
export PATCH=/absolute/path/to/contrib/docs/dragonruby-mruby.patch

test "$(git -C "$MRUBY_ROOT" rev-parse HEAD)" = 0f45836b5954accf508f333f932741b925214471
test -z "$(git -C "$MRUBY_ROOT" status --porcelain --untracked-files=all)"
test "$(git hash-object "$PATCH")" = bd85545b27f4c6397da251ca05ead018cecb9818
git -C "$MRUBY_ROOT" apply --check --index "$PATCH"
git -C "$MRUBY_ROOT" apply --index "$PATCH"

export MRUBY_BOXING=word
export MRUBY_CONFIG="$PWD/tests/mruby_config.rb"
rake -C "$MRUBY_ROOT"
for mode in Debug ReleaseSafe; do
  zig build mruby-test -Dmruby-root="$MRUBY_ROOT" \
    -Dmruby-boxing="$MRUBY_BOXING" -Dmruby-dragonruby=true \
    -Doptimize="$mode" -Dcpu=baseline --summary all
done
```

Use separate clean build directories for different boxing configurations.
The production SDK command remains
`python3 tools/sdk_smoke.py --sdk /path/to/dragonruby`; this published-source
target does not replace that real-engine validation.
