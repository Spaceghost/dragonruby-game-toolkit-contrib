"""Extract the original arithmetic, not a second handwritten oracle.

SDK/Ruby glue is intentionally outside these kernel measurements. The square
and tiny-regex sources are compiled directly by build.zig. For the two stateful
rendering examples, preserve the original motion/fill statements while passing
state and output storage explicitly. Source pins make that adaptation auditable.
"""
from __future__ import annotations
import hashlib
from pathlib import Path
import sys


def blob(data: bytes) -> str:
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def read_pinned(path: str, expected: str) -> str:
    data = Path(path).read_bytes()
    actual = blob(data)
    if actual != expected:
        raise SystemExit(f'Unreviewed original source: {path}: {actual} != {expected}')
    return data.decode()


def between(source: str, start: str, end: str) -> str:
    if source.count(start) != 1 or source.count(end) != 1:
        raise SystemExit('Original source extraction markers are not unique')
    first = source.index(start)
    last = source.index(end, first)
    return source[first:last]


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit('usage: reference.py original-stars.c original-scanner.c output.c')
    stars = read_pinned(sys.argv[1], '5ec7bdd410407632343ac8024ee7f23cba4d2222')
    scanner = read_pinned(sys.argv[2], 'e8f5458c41034dfdf38f75163a277ef015fae125')
    motion = between(stars, '  star->x += star->s;', '  mrb_value x =')
    frame = between(scanner, '    // Set up our', '    // Send it to the renderer')
    declaration = '    uint32_t pixels[dimension * dimension];\n'
    if frame.count(declaration) != 1:
        raise SystemExit('Original scanner buffer declaration changed')
    frame = frame.replace(declaration, '')
    output = '''/* Generated from pinned repository sources; never edit this file. */
#include "native.h"
#define dimension 10
void original_c_scanner(int state[2], uint32_t pixels[100]) {
    int pos = state[0], posinc = state[1];
''' + frame + '''
    state[0] = pos; state[1] = posinc;
}
#undef dimension
/* Inject the same RNG stream into both implementations. */
#define random_float() random(context)
void original_c_stars(drbz_star *stars, size_t len, drbz_random random, void *context) {
    for (size_t i = 0; i < len; ++i) {
        drbz_star *star = &stars[i];
''' + motion + '''
    }
}
#undef random_float
'''
    Path(sys.argv[3]).write_text(output)


if __name__ == '__main__':
    main()
