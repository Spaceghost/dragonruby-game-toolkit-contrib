"""Check the compiled direct-batch archive, not just its source spelling.

This proves absence of the specified wrapper symbols in this artifact, not
universal absence of overhead or allocations in the SQLite engine.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import subprocess
import sys

REQUIRED = {'sqlite3_prepare_v3', 'sqlite3_bind_int64', 'sqlite3_step',
            'sqlite3_reset', 'sqlite3_clear_bindings', 'sqlite3_finalize'}


def check_symbols(all_symbols: str, undefined: str) -> list[str]:
    names = {line.split()[-1] for line in all_symbols.splitlines() if line.split()}
    bad = sorted(n for n in names if n.startswith(('drbz_sql_', 'mrb_', '__wrap_')))
    if bad:
        raise ValueError('Forbidden intermediate wrapper symbols: ' + ', '.join(bad))
    imports = {line.split()[-1] for line in undefined.splitlines() if line.split()}
    if missing := REQUIRED - imports:
        raise ValueError('Missing direct SQLite imports: ' + ', '.join(sorted(missing)))
    if 'drbz_direct_sql_series' not in names:
        raise ValueError('Missing executed batch entry point')
    return sorted(n for n in imports if n.startswith('sqlite3_'))


def main() -> None:
    archive, destination = map(Path, sys.argv[1:])
    all_symbols = subprocess.check_output(['nm', str(archive)], text=True)
    undefined = subprocess.check_output(['nm', '-u', str(archive)], text=True)
    imports = check_symbols(all_symbols, undefined)
    # Required positive/negative controls exercise the checker itself.
    for bad in ('drbz_sql_step', 'mrb_funcall', '__wrap_malloc'):
        try:
            check_symbols(all_symbols + '\nU ' + bad, undefined)
        except ValueError:
            pass
        else:
            raise RuntimeError('Symbol checker accepted a forbidden wrapper')
    try:
        check_symbols(all_symbols, '')
    except ValueError:
        pass
    else:
        raise RuntimeError('Symbol checker accepted missing SQLite operations')
    result = {'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
              'direct_sqlite_imports': imports, 'forbidden_wrapper_symbols': [],
              'checker_negative_controls': 4,
              'scope': 'Compiled direct-batch archive; not a performance or whole-program allocation proof'}
    destination.write_text(json.dumps(result, indent=2) + '\n')
    print('DIRECT_CALL_EVIDENCE ' + json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
