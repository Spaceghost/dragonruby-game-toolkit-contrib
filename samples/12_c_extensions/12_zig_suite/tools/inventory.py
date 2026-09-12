"""Require an explicit disposition for every tracked native source/header.

Read Git blobs, not working-tree symlink targets. Original-source changes and
new unclassified native files require a reviewed coverage-manifest update.
"""
from __future__ import annotations
import json
from pathlib import Path, PurePosixPath
import subprocess

EXTENSIONS = {'.c', '.h', '.m', '.mm', '.cc', '.cpp', '.cxx', '.hpp'}
SUITE = 'samples/12_c_extensions/12_zig_suite/'


def main() -> None:
    root = Path(subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip())
    coverage = json.loads((root / SUITE / 'COVERAGE.json').read_text())
    expected = {}
    for group in coverage['groups']:
        for implementation in group['zig']:
            if not (root / SUITE / implementation).is_file():
                raise SystemExit(f'Missing declared Zig implementation: {implementation}')
        for relative, sha in group['files'].items():
            name = group['root'] + relative
            if name in expected:
                raise SystemExit(f'Duplicate coverage entry: {name}')
            expected[name] = (sha, group['role'])
    measurement_files = json.loads((root / SUITE / 'measure/COVERAGE.json').read_text())
    for manifest in (coverage['suite_native_files'], measurement_files):
        for relative, role in manifest.items():
            name = SUITE + relative
            if name in expected:
                raise SystemExit(f'Duplicate coverage entry: {name}')
            expected[name] = (None, role)
    raw = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=root)
    rows, seen = [], set()
    for entry in raw.split(b'\0'):
        if not entry:
            continue
        meta, path_bytes = entry.split(b'\t', 1)
        mode, sha, stage = meta.decode().split()
        path = path_bytes.decode()
        if PurePosixPath(path).suffix not in EXTENSIONS:
            continue
        if stage != '0':
            raise SystemExit(f'Unmerged native source: {path}')
        if path not in expected:
            raise SystemExit(f'Unclassified native source: {path}; update the coverage manifest')
        pin, role = expected[path]
        if pin is not None and sha != pin:
            raise SystemExit(f'Original source changed: {path}: {sha} != {pin}')
        seen.add(path)
        data = subprocess.check_output(['git', 'cat-file', 'blob', sha], cwd=root)
        kind = 'symlink' if mode == '120000' else 'source'
        row = {'path': path, 'blob': sha, 'kind': kind, 'lines': len(data.splitlines()), 'role': role}
        rows.append(row)
        print('NATIVE_SOURCE ' + json.dumps(row, sort_keys=True))
    if missing := set(expected) - seen:
        raise SystemExit('Stale coverage entries: ' + ', '.join(sorted(missing)))
    print('NATIVE_TOTAL ' + json.dumps({'entries': len(rows), 'c_sources': sum(row['path'].endswith('.c') and row['kind'] == 'source' for row in rows), 'symlinks': sum(row['kind'] == 'symlink' for row in rows), 'unclassified': 0}))


if __name__ == '__main__':
    main()
