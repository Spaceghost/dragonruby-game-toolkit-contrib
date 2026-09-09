"""Inventory Git objects rather than following working-tree symlinks."""
from __future__ import annotations
import json
from pathlib import PurePosixPath
import subprocess

EXTENSIONS = {'.c', '.h', '.m', '.mm', '.cc', '.cpp', '.cxx', '.hpp'}


def main() -> None:
    raw = subprocess.check_output(['git', 'ls-files', '--stage', '-z'])
    rows = []
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
        data = subprocess.check_output(['git', 'cat-file', 'blob', sha])
        kind = 'symlink' if mode == '120000' else 'source'
        row = {'path': path, 'blob': sha, 'kind': kind, 'lines': len(data.splitlines())}
        rows.append(row)
        print('NATIVE_SOURCE ' + json.dumps(row, sort_keys=True))
    print('NATIVE_TOTAL ' + json.dumps({'entries': len(rows), 'c_sources': sum(row['path'].endswith('.c') and row['kind'] == 'source' for row in rows), 'symlinks': sum(row['kind'] == 'symlink' for row in rows)}))


if __name__ == '__main__':
    main()
