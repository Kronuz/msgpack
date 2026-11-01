#!/usr/bin/env python3
"""Joins the output of msgpack_benchmark and msgpack_compare into one table per document, and checks that the
libraries read the same document.

usage: benchmarks/compare.py OUTPUT...

Each OUTPUT is the text one of the programs printed (rows of: library, document, operation, ns, ..., chk). The
table has one line per operation and one column per library (median ns); the last columns are msgpack's time
over the fastest other library's (above 1 is slower) and, for `read`, the A/A ratio, the same measurement taken
twice, which is the noise of the run. A checksum that differs between libraries on an operation that reads the
document (read, read+walk, read+lookup3, lookup3) is an error and the exit status is 1."""
import re
import sys

ROW = re.compile(r'^(\S+)\s+(\S+)\s+(\S+)\s+([\d.]+) ns\s+min\s+([\d.]+)\s+spread\s+([\d.]+)%\s+chk ([0-9a-f]{16})')
READS = {'read', 'read+walk', 'read+lookup3', 'lookup3'}
OURS = 'msgpack'
COLUMNS = ['msgpack', 'msgpack-c1.3', 'msgpack-c9', 'nlohmann', 'mpack', 'rapidjson', 'shared_ptr']
DOCS = ['small', 'med', 'large', 'wide']


def main(paths):
    rows = {}   # (doc, op) -> {lib: (ns, spread, chk)}
    for path in paths:
        for line in open(path):
            m = ROW.match(line)
            if m:
                lib, doc, op, ns, _min, spread, chk = m.groups()
                rows.setdefault((doc, op), {})[lib] = (float(ns), float(spread), chk)
            elif line.startswith('MISMATCH'):
                print(line.strip())
                return 1
    bad = 0
    for doc in DOCS:
        ops = [op for (d, op) in rows if d == doc and not op.endswith('(A/A)')]
        if not ops:
            continue
        ops = sorted(set(ops), key=lambda o: min(i for i, k in enumerate(rows) if k == (doc, o)))
        print(f'\n{doc}: median ns per call')
        print('%-16s' % 'operation' + ''.join('%14s' % c for c in COLUMNS) + '   msgpack/best   A/A')
        for op in ops:
            libs = rows[(doc, op)]
            cells = ''.join('%14s' % ('%.0f' % libs[c][0] if c in libs else '-') for c in COLUMNS)
            others = [v[0] for k, v in libs.items() if k not in (OURS, 'shared_ptr')]
            ratio = '%.2fx' % (libs[OURS][0] / min(others)) if OURS in libs and others else '-'
            aa = rows.get((doc, op + '(A/A)'), {}).get(OURS)
            aa_text = '%.2f' % (aa[0] / libs[OURS][0]) if aa and OURS in libs else '-'
            print('%-16s%s%14s%7s' % (op, cells, ratio, aa_text))
            if op in READS:
                sums = {k: v[2] for k, v in libs.items() if k not in ('rapidjson', 'shared_ptr')}
                if len(set(sums.values())) > 1:
                    bad += 1
                    print(f'   ERROR: checksums differ on {doc} {op}: {sums}')
    if bad:
        print(f'\n{bad} operation(s) where libraries read different documents')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]) if len(sys.argv) > 1 else print(__doc__) or 2)
