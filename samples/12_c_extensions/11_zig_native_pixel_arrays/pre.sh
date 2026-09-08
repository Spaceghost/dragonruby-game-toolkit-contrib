#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
: "${ZIG:=zig}"
: "${DRB_ROOT:=../../..}"

if ! command -v "$ZIG" >/dev/null 2>&1; then
  echo "Install Zig 0.16.0 or set ZIG to its executable path." >&2
  exit 1
fi
if [ ! -f "$DRB_ROOT/dragonruby.h" ] && [ ! -f "$DRB_ROOT/include/dragonruby.h" ]; then
  echo "Set DRB_ROOT to the matching DragonRuby SDK root (dragonruby.h is missing)." >&2
  exit 1
fi

case "$(uname -s):$(uname -m)" in
  Linux:x86_64) target=x86_64-linux-gnu; platform=linux-amd64 ;;
  Darwin:arm64) target=aarch64-macos; platform=macos ;;
  Darwin:x86_64) target=x86_64-macos; platform=macos ;;
  *)
    if [ -z "${ZIG_TARGET:-}" ] || [ -z "${DRB_PLATFORM:-}" ]; then
      echo "Set ZIG_TARGET and DRB_PLATFORM explicitly for this host." >&2
      exit 1
    fi
    target=$ZIG_TARGET; platform=$DRB_PLATFORM ;;
esac

exec "$ZIG" build extension --prefix . -Doptimize=ReleaseSafe \
  "-Dtarget=${ZIG_TARGET:-$target}" -Dcpu=baseline \
  "-Dplatform=${DRB_PLATFORM:-$platform}" "-Ddragonruby-root=$DRB_ROOT" "$@"
