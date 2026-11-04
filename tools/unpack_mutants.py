#!/usr/bin/env python3
"""Mutation checks for the unpacker's reservation (msgpack/unpack.hpp). Each mutant edits one thing and must make
test/unpacker_limits_test.cc fail (a nonzero exit, a crash or a timeout); the control, with no edit, must pass.

usage: tools/unpack_mutants.py REPO DEPS_DIR OUT_DIR [NAME...]
  DEPS_DIR is the `_deps` directory of a configured CMake build (the dependencies' sources).
  CXX picks the compiler, SAN adds sanitizer flags (no_grow_array wants SAN=-fsanitize=address)."""
import os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
W, D, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
EXC = f'{D}/located_exception-src/exception.cc'
INC = [f'-I{D}/{n}' for n in ['atomic_shared_ptr-src', 'enum_reflection-src', 'located_exception-src', 'strict_stox-src',
                               'constexpr_phf-src', 'hashes-src', 'repr-src', 'rapidjson-src/include']]
DEF = '-DRAPIDJSON_PARSE_DEFAULT_FLAGS=(kParseCommentsFlag|kParseTrailingCommasFlag|kParseNanAndInfFlag)'
src = open(f'{W}/msgpack/unpack.hpp').read()

def rep(old, new):
    def f(s):
        assert s.count(old) == 1, old
        return s.replace(old, new)
    return f

mutants = {
 'control': lambda s: s,
 # the old behaviour: the declared size is reserved
 'reserve_declared': rep('std::min<std::size_t>(tmp, spare / f.bytes_per_item())', 'tmp'),
 # a header counts on bytes another header already counts on: quadratic in the input
 'claims_ignored': rep('const std::size_t spare = after > m_claimed ? after - m_claimed : 0;', 'const std::size_t spare = after;'),
 # the claim of an item that started is never given back: valid documents regrow
 'claims_never_released': rep('if (parent.claim()) { parent.decr_claim(); --m_claimed; }', ''),
 # a map entry counted as one byte
 'map_entry_one_byte': rep('static std::size_t bytes_per_item() { return 2; }', 'static std::size_t bytes_per_item() { return 1; }'),
 # the grown storage loses its items
 'grow_drops_items': rep('if (size) std::memcpy(block->ptr, ptr, size*sizeof(Item));', ''),
 # the storage is not grown: items are written past it
 'no_grow_array': rep('if (sp.obj().via.array.size == sp.capacity()) grow_array(sp);', ''),
 'no_grow_map': rep('if (sp.obj().via.map.size == sp.capacity()) grow_map(sp);', ''),
 # the grown storage does not grow
 'grow_by_zero': rep('sp.capacity() ? sp.capacity() + sp.capacity() / 2 + 1 : 1);', 'sp.capacity());'),
 # the reservation is not recorded: the growth check sees capacity 0 and regrows every item
 'capacity_not_recorded': rep('m_stack.back().set_capacity(reserve);', ''),
 # every growth starts a block of its own and leaves the last one in the zone
 'grown_block_not_reused': rep('grown_block* block = sp.block();', 'grown_block* block = nullptr;'),
 'grown_block_not_recorded': rep('sp.set_block(block);', ''),
 # a container reads the block of the container that was in its stack entry before
 'stale_block': rep('m_stack.back().set_block(nullptr);', ''),
 # the resized storage is dropped
 'realloc_result_dropped': rep('block->ptr = grown;', ''),
}
only = sys.argv[4:]
os.makedirs(OUT, exist_ok=True)

def run(name):
    d = f'{OUT}/{name}'
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for item in ['msgpack', 'xchange', 'likely.h', 'msgpack.h', 'msgpack.hpp', 'msgpack_exception.h', 'test']:
        p = f'{W}/{item}'
        (shutil.copytree if os.path.isdir(p) else shutil.copy)(p, f'{d}/{item}')
    open(f'{d}/msgpack/unpack.hpp', 'w').write(mutants[name](src))
    exe = f'{d}/t'
    cmd = ['nice', '-n', '10', os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-g', '-Wno-deprecated-declarations', f'-I{d}', f'-I{d}/test'] \
        + INC + [DEF, f'{d}/test/unpacker_limits_test.cc', EXC, '-o', exe] + os.environ.get('SAN', '').split()
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    if r.returncode != 0:
        return name, 'COMPILE ERROR', r.stderr[-400:]
    try:
        r = subprocess.run([exe], capture_output=True, text=True, errors='replace', timeout=120)
        out = (r.stderr + r.stdout).strip().splitlines()
        return name, 'passed' if r.returncode == 0 else f'FAILED (exit {r.returncode})', ' | '.join(out[:3])[:300]
    except subprocess.TimeoutExpired:
        return name, 'FAILED (timeout)', ''

names = only or list(mutants)
with ThreadPoolExecutor(int(os.environ.get('JOBS', '2'))) as pool:
    results = list(pool.map(run, names))
bad = 0
for name, status, detail in results:
    expect_pass = name == 'control'
    ok = status == 'passed' if expect_pass else status.startswith('FAILED')
    bad += not ok
    print(f'{"ok  " if ok else "BAD "}{name:26} {status:22} {detail}')
sys.exit(1 if bad else 0)
