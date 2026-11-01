#!/usr/bin/env python3
"""Mutation and revert checks for unserialise, hash(), to_string(), the merge of repeated keys, the growth of
arrays, the fill of a map that has a key that is not a string, replace() over a value that was read, lock(), the JSON
text of a key that is not a string and of a float that is not finite, and update().
Each mutant edits one thing in msgpack.h and
must make a named test fail (a nonzero exit, a crash or a timeout); a control with no edit must pass. A revert
of unserialise, hash() or to_string() puts the previous implementation of one of the three back and must fail
the allocation test; revert_merge, revert_growth, revert_failed_fill, revert_replace and revert_lock undo the fixes.

usage: tools/mutants.py REPO DEPS_DIR OUT_DIR [NAME...]
  DEPS_DIR is the `_deps` directory of a configured CMake build (the dependencies' sources).
  CXX picks the compiler, SAN adds sanitizer flags (the dead-zone mutant needs SAN=-fsanitize=address),
  JOBS sets the parallel builds."""
import os, re, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
W, D, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
EXC = f'{D}/located_exception-src/exception.cc'
INC = [f'-I{D}/{n}' for n in ['atomic_shared_ptr-src', 'enum_reflection-src', 'located_exception-src', 'strict_stox-src', 'constexpr_phf-src',
                               'hashes-src', 'repr-src', 'char_classify-src', 'static_string-src', 'rapidjson-src/include']]
DEF = '-DRAPIDJSON_PARSE_DEFAULT_FLAGS=(kParseCommentsFlag|kParseTrailingCommasFlag|kParseNanAndInfFlag)'
src = open(f'{W}/msgpack.h').read()

def rep(old, new, count=1):
    def f(s):
        assert s.count(old) >= 1, old
        return s.replace(old, new, count)
    return f

def rep_nth(old, new, n):
    """Replaces the n-th occurrence (1-based) and no other."""
    def f(s):
        assert s.count(old) >= n, old
        i = -1
        for _ in range(n):
            i = s.index(old, i + 1)
        return s[:i] + new + s[i + len(old):]
    return f

def previous(path):
    return subprocess.run(['git', '-C', W, 'show', f'667dc3d:{path}'], capture_output=True, text=True, check=True).stdout

mutants = {
 'control': ('diff', lambda s: s),
 'revert_unserialise': ('alloc', rep('std::make_shared<Body>(Body::unpack_tag{}, data, len, off)', 'std::make_shared<Body>(msgpack::unpack(data, len, off).get())')),
 'revert_hash': ('alloc', rep('if (msgpack_wrapper::hash_map(*_body->_obj, tree_hash)) {', 'if (false) {')),
 'revert_to_string': ('alloc', rep('msgpack_wrapper::accept(*_const_body->_obj, writer);', 'as_document().Accept(writer);', 2)),
 'revert_all': ('alloc', lambda s: previous('msgpack.h')),
 'dups_ignored': ('diff', rep('if (has_duplicate_keys(kv, n, hashes)) {', 'if (false) {')),
 'table_dups_missed': ('diff', rep('if (hashes[j] == hashes[i] && same_key(kv, i, j)) {\n\t\t\t\t\treturn true;\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t\treturn false;\n\t}\n\n\t// The value of', 'if (false) {\n\t\t\t\t\treturn true;\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t\treturn false;\n\t}\n\n\t// The value of')),
 'quadratic_dups_missed': ('diff', rep('if (hashes[i] == hashes[j] && same_key(kv, i, j)) {', 'if (false) {')),
 'non_string_key_taken': ('diff', rep('if (kv[i].key.type != msgpack::type::STR) {\n\t\t\t\treturn false;', 'if (false) {\n\t\t\t\treturn false;')),
 'nested_maps_packed': ('diff', rep('if (val.type == msgpack::type::MAP) {\n\t\t\t\tstd::size_t val_hash;', 'if (false) {\n\t\t\t\tstd::size_t val_hash;')),
 'empty_keys_distinct': ('diff', rep('return a.size == b.size && (a.size == 0 || std::memcmp(a.ptr, b.ptr, a.size) == 0);', 'return a.size == b.size && a.size != 0 && std::memcmp(a.ptr, b.ptr, a.size) == 0;')),
 'pack_buffer_lost_stack': ('diff', rep('\t\t\tif (_data == _stack) {\n\t\t\t\tstd::memcpy(data, _stack, _size);\n\t\t\t}\n', '')),
 'pack_buffer_overrun': ('diff', rep('if (unlikely(_capacity - _size < len)) {', 'if (unlikely(_capacity - _size + 1 < len)) {')),
 'json_bin_as_null': ('diff', rep('case msgpack::type::BIN: // fall through\n\t\t\tcase msgpack::type::STR:\n\t\t\t\treturn handler.String', 'case msgpack::type::STR:\n\t\t\t\treturn handler.String')),
 'json_no_end_object': ('diff', rep('return handler.EndObject(o.via.map.size);', 'return true;')),
 'json_continues_after_false': ('diff', rep('if (!accept(*p, handler)) {\n\t\t\t\t\t\treturn false;\n\t\t\t\t\t}', 'accept(*p, handler);')),
 'json_int_as_uint': ('diff', rep('return handler.Int64(o.via.i64);', 'return handler.Uint64(static_cast<uint64_t>(o.via.i64));')),
 'unserialise_references_input': ('lifetime', rep('msgpack::unpack(*_zone, data, len, off)', 'msgpack::unpack(*_zone, data, len, off, [](msgpack::type::object_type, std::size_t, void*) { return true; })')),
 'unserialise_off_not_advanced': ('lifetime', rep('msgpack::unpack(*_zone, data, len, off)', 'msgpack::unpack(*_zone, data, len, *std::make_shared<std::size_t>(off))')),
 'unserialise_dead_zone': ('lifetime', rep('_base(std::make_shared<msgpack::object>(msgpack::unpack(*_zone, data, len, off)))', '_base(std::make_shared<msgpack::object>(msgpack::unpack(*std::make_unique<msgpack::zone>(), data, len, off)))')),
 # the merge of a repeated key (last value wins, in the position of the first)
 'revert_merge': ('merge', lambda s: rep('--p;  // the entry that moved into this position is next', '')(rep('*q = *(q + 1);', '*q = *(p + 1);')(s))),
 'merge_shifts_from_p': ('merge', rep('*q = *(q + 1);', '*q = *(p + 1);')),
 'merge_skips_moved_entry': ('merge', rep('--p;  // the entry that moved into this position is next', '')),
 'merge_first_value_wins': ('merge', rep('*value_body._obj = p->val;', '')),
 'merge_size_not_reduced': ('merge', rep('--_body->_obj->via.map.size;', '')),
 # growing an array of maps (the zone-sharing failure needs SAN=-fsanitize=address; the others do not)
 'revert_growth': ('growth', rep('MsgPack&& other) noexcept', 'MsgPack&& other)', 2)),
 'update_array_keeps_index': ('growth', rep('mobj._body->_initialized = false;', '')),
 # a fill that throws leaves the map as it was
 'revert_failed_fill': ('failed_fill', lambda s: rep('assert(p->key.type == msgpack::type::STR);', 'if (p->key.type != msgpack::type::STR) { THROW(msgpack::type_error, "MAP keys must be of type STR"); }')(rep('if (k->key.type != msgpack::type::STR) {', 'if (false) {')(s))),
 'failed_fill_first_key_only': ('failed_fill', rep('k < kend; ++k) {', 'k < kend; k = kend) {')),
 'failed_fill_last_key_skipped': ('failed_fill', rep('k < kend; ++k) {', 'k + 1 < kend; ++k) {')),
 # replace() over a value that was read, and lock()
 'revert_replace': ('replace_lock', rep('old_value._deinitialize();', '', 2)),
 'replace_map_keeps_index': ('replace_lock', rep('old_value._deinitialize();', '', 1)),
 'replace_array_keeps_index': ('replace_lock', rep_nth('old_value._deinitialize();', '', 2)),
 'replace_capacity_kept': ('replace_lock', rep('old_value._body->_capacity = old_value._body->size();', '', 2)),
 'revert_lock': ('replace_lock', rep('_fill(true, false);\n\t_fill(true, true);', '_fill(true, true);')),
 # the hunt: erase() counts, and an allocation that fails (hunt_alloc is built with a replaced operator new, so
 # run it with SAN unset or -fsanitize=undefined only)
 'erase_last_returns_zero': ('hunt', rep('return std::make_pair(1, end());   // the last', 'return std::make_pair(0, end());   // the last', 2)),
 'init_map_keeps_partial': ('hunt_alloc', rep('_body->map.erase(std::string_view(q->key.via.str.ptr, q->key.via.str.size));', '(void)q;')),
 'put_map_keeps_entry': ('hunt_alloc', rep('--_body->_obj->via.map.size;   // an allocation failed: the entry is not added', '')),
 'put_array_keeps_size': ('hunt_alloc', rep('_body->_obj->via.array.size = _body->array.size();   // an allocation failed: nothing is added', '', 2)),
 'reserve_map_repoints_late': ('hunt_alloc', rep('_update_map(0);   // the children point into the new buffer before anything can fail\n\t\t\t_init_map(_body->map.size());', '_init_map(_body->map.size());\n\t\t\t_update_map(0);')),
 'reserve_array_repoints_late': ('hunt_alloc', rep('_update_array(0);   // the children point into the new buffer before anything can fail\n\t\t\t_init_array(_body->array.size());', '_init_array(_body->array.size());\n\t\t\t_update_array(0);')),
 # update(): the arguments are checked before anything changes
 'update_no_check': ('update', rep('_update_check(o);\n\t_update_apply(o);', '_update_apply(o);')),
 'update_check_after_apply': ('update', rep('_update_check(o);\n\t_update_apply(o);', '_update_apply(o);\n\t_update_check(o);')),
 'update_check_skips_nested': ('update', rep('item._update_check(val);', '(void)val;')),
 'update_check_skips_erase_read': ('update', rep('auto it = find(key);', 'auto it = val.is_undefined() ? end() : find(key);')),
 'update_check_skips_source': ('update', rep('if (o._body->get_type() != Type::MAP) {\n\t\treturn;', 'if (o._body->get_type() != Type::MAP || o.size() > 1) {\n\t\treturn;')),
 'update_check_skips_const': ('update', rep('\tif (_body->_const) {\n\t\tTHROW(msgpack::const_error, "Constant object");\n\t}\n\tif (_body->_lock) {\n\t\tassert(!_body->_lock);\n\t\tTHROW(msgpack::const_error, "Locked object");\n\t}\n\tif (o._body->get_type() != Type::MAP) {', '\tif (o._body->get_type() != Type::MAP) {')),
}
# Mutants of xchange/rapidjson.hpp: (kind, a no-op edit of msgpack.h, the edit of the adaptor). The key of a map
# entry that is not a string is written as its JSON text.
xmutants = {
 'key_read_as_str': ('json_keys', lambda s: s, rep('const std::string text = json_key(key);\n\t\t\t\treturn handler.Key(text.data(), static_cast<rapidjson::SizeType>(text.size()));', 'return handler.Key(key.via.str.ptr, key.via.str.size);')),
 'convert_key_read_as_str': ('json_keys', lambda s: s, rep('const std::string text = msgpack_wrapper::json_key(ptr->key);\n\t\t\t\t\t\t\tkey.SetString(text.data(), static_cast<rapidjson::SizeType>(text.size()), v.GetAllocator());', 'key.SetString(ptr->key.via.str.ptr, ptr->key.via.str.size, v.GetAllocator());')),
 'key_nan_not_written': ('json_keys', lambda s: s, rep('rapidjson::CrtAllocator, rapidjson::kWriteNanAndInfFlag>', 'rapidjson::CrtAllocator, rapidjson::kWriteDefaultFlags>')),
 'bin_key_as_json': ('json_keys', lambda s: s, rep('case msgpack::type::BIN:\n\t\t\t\treturn handler.Key(key.via.bin.ptr, key.via.bin.size);\n', '')),
 'ext_written_as_false': ('json_keys', lambda s: s, rep('case msgpack::type::NIL: // fall through\n\t\t\tdefault:\n\t\t\t\treturn handler.Null();', 'case msgpack::type::NIL: // fall through\n\t\t\tdefault:\n\t\t\t\treturn handler.Bool(false);')),
 # a float that is not finite is null in the text (to_string()) and the name of the number as a key
 'nonfinite_value_stops': ('json_keys', lambda s: s, rep('if (!std::isfinite(o.via.f64)) {\n\t\t\t\t\treturn handler.Null();\n\t\t\t\t}\n', '')),
 'nonfinite_only_nan': ('json_keys', lambda s: s, rep('if (!std::isfinite(o.via.f64)) {\n\t\t\t\t\treturn handler.Null();', 'if (std::isnan(o.via.f64)) {\n\t\t\t\t\treturn handler.Null();')),
 'nonfinite_as_zero': ('json_keys', lambda s: s, rep('if (!std::isfinite(o.via.f64)) {\n\t\t\t\t\treturn handler.Null();', 'if (!std::isfinite(o.via.f64)) {\n\t\t\t\t\treturn handler.Uint(0);')),
 'nonfinite_key_null': ('json_keys', lambda s: s, rep('return std::isnan(key.via.f64) ? "NaN" : key.via.f64 > 0 ? "Infinity" : "-Infinity";', 'return "null";')),
 'nonfinite_key_sign_lost': ('json_keys', lambda s: s, rep('key.via.f64 > 0 ? "Infinity" : "-Infinity"', '"Infinity"')),
 'nonfinite_key_unchecked': ('json_keys', lambda s: s, rep('if (key.type == msgpack::type::FLOAT && !std::isfinite(key.via.f64)) {', 'if (false) {')),
}
mutants.update({k: v[:2] for k, v in xmutants.items()})
only = sys.argv[4:]
os.makedirs(OUT, exist_ok=True)

def build_and_run(name):
    kind, f = mutants[name]
    d = f'{OUT}/{name}'
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for item in ['msgpack', 'xchange', 'likely.h', 'msgpack.hpp', 'msgpack_exception.h', 'test']:
        p = f'{W}/{item}'
        (shutil.copytree if os.path.isdir(p) else shutil.copy)(p, f'{d}/{item}')
    open(f'{d}/msgpack.h', 'w').write(f(src))
    if name in xmutants:
        with open(f'{d}/xchange/rapidjson.hpp') as fh:
            adaptor = fh.read()
        with open(f'{d}/xchange/rapidjson.hpp', 'w') as fh:
            fh.write(xmutants[name][2](adaptor))
    if name == 'revert_all':   # the previous header does not have the unpacker fix either
        for item in ['msgpack/unpack.hpp', 'msgpack/object.hpp']:
            shutil.copy(f'{W}/{item}', f'{d}/{item}')
    test = {'diff': 'diff_test.cc', 'lifetime': 'lifetime_test.cc', 'alloc': 'alloc_test.cc', 'merge': 'merge_test.cc', 'growth': 'growth_test.cc', 'failed_fill': 'failed_fill_test.cc', 'replace_lock': 'replace_lock_test.cc', 'hunt': 'hunt_test.cc', 'hunt_alloc': 'hunt_test.cc', 'json_keys': 'json_keys_test.cc', 'update': 'update_test.cc'}[kind]
    exe = f'{d}/t'
    cmd = ['nice', '-n', '10', os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-g', '-Wno-deprecated-declarations', f'-I{d}', f'-I{d}/test'] + INC + [DEF, f'{d}/test/{test}', EXC, '-o', exe] + os.environ.get('SAN', '').split() + (['-pthread'] if kind == 'lifetime' else []) + (['-DHUNT_INJECT_ALLOC'] if kind == 'hunt_alloc' else [])
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    if r.returncode != 0:
        return name, kind, 'COMPILE ERROR', r.stderr[-400:]
    try:
        r = subprocess.run([exe], capture_output=True, text=True, errors='replace', timeout=600)
        tail = (r.stdout + r.stderr).strip().splitlines()[-1:] or ['']
        return name, kind, 'passed' if r.returncode == 0 else f'FAILED rc={r.returncode}', tail[0][:200]
    except subprocess.TimeoutExpired:
        return name, kind, 'TIMEOUT', ''

names = [n for n in mutants if not only or n in only]
with ThreadPoolExecutor(max_workers=int(os.environ.get('JOBS', '6'))) as ex:
    for name, kind, status, tail in ex.map(build_and_run, names):
        verdict = ''
        if name == 'control':
            verdict = 'OK' if status == 'passed' else 'BAD (control must pass)'
        else:
            verdict = 'KILLED' if status.startswith('FAILED') or status == 'TIMEOUT' else 'SURVIVED' if status == 'passed' else '?'
        print(f'{name:32} {kind:9} {status:16} {verdict:9} {tail}')
