# AGENTS

Working notes for this library. Read `ARCHITECTURE.md` for the data model and exception seam.

## What this is

The MsgPack keystone extracted from Xapiand: the bundled `msgpack-c` v1.3.0 fork, Xapiand's `MsgPack` value type (a copy is a deep copy), the RapidJSON and `std::string_view` adaptors, and the JSON-Patch helper.

## File map

```text
msgpack.h                     MsgPack value API. Header-only, mostly verbatim.
msgpack.hpp                   msgpack-c umbrella header.
msgpack/                      Bundled msgpack-c v1.3.0 headers. exception.hpp is forked.
msgpack_patcher.h/.cc         RFC 6902 patcher over MsgPack.
xchange/rapidjson.hpp         RapidJSON adaptor, with xpol attribution.
xchange/string_view.hpp       string_view adaptor.
msgpack_exception.h           Standalone exception seam over located-exception.
likely.h                      Vendored likely()/unlikely().
examples/demo.cc              Build, serialise, JSON round-trip, patch.
test/test.cc                  ctest coverage for value, copies, patch, xchange.
test/model.h                  Reference models: the merged document, the hash of it, and a JSON-like Doc the tests edit like a MsgPack.
test/diff_test.cc             ctest msgpack_diff: random documents and operations against the frozen test/frozen_msgpack.h (a document that repeats a key is read as its merged form on the frozen side).
test/merge_test.cc            ctest msgpack_merge: repeated keys (last value wins, position of the first) against the model: hand-written, exhaustive and random documents.
test/growth_test.cc           ctest msgpack_growth: pushing onto and changing arrays of maps against the model; reads no freed memory under AddressSanitizer.
test/failed_fill_test.cc      ctest msgpack_failed_fill: a map with a key that is not a string throws the same way on every fill and stays as it came; hand-written, exhaustive and random documents.
test/replace_lock_test.cc     ctest msgpack_replace_lock: replace and put over a map or array that was already read, and lock() on a document with a bad map, against the model.
test/json_keys_test.cc        ctest msgpack_json_keys: to_string() and as_document() of a map with a key that is not a string, against the JSON writer of test/model.h.
test/update_test.cc           ctest msgpack_update: update() against a model of the merge: the outcome, and nothing changed when it throws.
test/hunt_test.cc             ctest msgpack_hunt and msgpack_hunt_alloc: random sequences of every mutating operation against the model, with fills that throw and (hunt_alloc) failing allocations.
test/frozen_msgpack.h         The earlier msgpack.h with MsgPack renamed MsgPackOld. The oracle: never edit it.
test/lifetime_test.cc         ctest msgpack_lifetime: values outlive the input, the other values and threads.
test/alloc_test.cc            ctest msgpack_alloc: allocation counts of unserialise, hash() and to_string() against the frozen copy.
tools/mutants.py              Edits the implementation 51 ways (the previous unserialise, hash() and to_string() put back; the merge, growth, failed-fill, replace, lock, erase-count, failure-safety, JSON-key and update() fixes undone and broken more ways); a test must fail on each.
benchmarks/pack_unpack.cc     msgpack_benchmark: MsgPack against the msgpack-c it bundles, on the corpus (read, walk, lookups, write, copy, update, JSON, hash).
benchmarks/compare.cc         msgpack_compare: msgpack-c (current), nlohmann::json, mpack and RapidJSON on the same corpus and operations.
benchmarks/corpus.h           The four documents (small, med, large, wide) as MessagePack bytes and JSON text, written without a msgpack library.
benchmarks/bench_util.h       Timer, output row and checksum helpers shared by the two programs.
benchmarks/compare.py         Joins the two outputs into one table per document and checks that the libraries read the same document.
benchmarks/mpack-config.h     The mpack build configuration for msgpack_compare.
docs/perf/survey-2026-10.md   What the benchmarks measure on arm64 and x86-64, where the time goes, what the fast paths must keep identical, what is open.
CHANGELOG.md                  Dated changes, numbers and known problems.
```

## Dependencies

CMake fetches `atomic-shared-ptr`, `enum-reflection`, `strict-stox`, `located-exception`, `perfect-hash`, `hashes`, `repr`, and RapidJSON `v1.1.0`. Use `FETCHCONTENT_SOURCE_DIR_<NAME>` to point at local checkouts while working across the Kronuz family.

## Invariants

- **Behavior-preserving extraction.** Do not rewrite algorithms or simplify the wrapper. Keep the original wire format, selector behavior, growth strategy, and lazy fill behavior.
- **`unserialise`, `hash()` and `to_string()` work on the msgpack tree and must give what `test/frozen_msgpack.h` gives**: values, offsets, exceptions, and the changes to the tree. The exceptions are the three things the frozen copy gets wrong, all fixed: a map that repeats a key (the last value wins, in the position of the first; the frozen copy copies the wrong entry), growing an array of maps (the frozen copy copies its elements), and a fill that throws on a key that is not a string (the frozen copy leaves the keys before it indexed and drops them on the next fill). The frozen copy also returns 0 from `erase` for the last element, and `msgpack_diff` compares the change in size instead of the return value. `msgpack_diff` reads a document that repeats a key as the merged document on the frozen side, so a change to the merge belongs in `test/merge_test.cc`, against the `std::map` model. `hash()` takes the tree path only for a map whose keys, and those of the maps nested in it, are strings and do not repeat; a repeated key sends it to the filling code, which merges the repeats in the tree. Do not make the tree path handle repeats: the result of `hash()`, `serialise()` and copies afterwards depends on the merge. Run `msgpack_diff` after any change to those three, and the lifetime and growth tests under AddressSanitizer after one to `Body` or to the arrays.
- **Keep `_init_map` checking every key before it changes anything.** A fill that throws on a key that is not a string must leave the tree and the index as they were: indexing the keys before it and then throwing makes the next fill take them for repeats and drop them (`msgpack_failed_fill`).
- **Replacing a value resets the old value's index.** `_put` over a map or array that was filled builds the new object first, calls `_deinitialize()` on the old child, assigns, and sets `_capacity = size()`; a child that is locked throws `const_error`. Keeping the index makes the next read or write use entries of a value that is gone (`msgpack_replace_lock`).
- **`lock()` fills everything, then locks.** `_fill(true, false)` and then `_fill(true, true)`: a fill that throws deep in the document leaves nothing locked.
- **`_update_array(pos, false)` keeps the index; only an erase resets it.** Growing an array must not drop the children's indexes: a `MsgPack&` held to `a.at(0)["k"]` points into them (`test_references_survive_growth`).
- **Registering entries rolls back on `std::bad_alloc`.** `_init_map`, `_init_array`, `_put` and `_insert` leave the tree and the index agreeing when an allocation fails part way; `_reserve_map` and `_reserve_array` re-point the tree before they build the index. `erase` returns 1 for the last element too (`msgpack_hunt_alloc`).
- **A map key that is not a string is written as its JSON text, in one place.** `msgpack_wrapper::accept_key` and `json_key` (in `xchange/rapidjson.hpp`, with `accept`) serve `to_string()` and the `as_document()` adaptor; neither reads `via.str` of a key without checking that it is a `str` (a `bin` is read through `via.bin`). The rule is in the README and is held by `msgpack_json_keys` against the JSON writer of `test/model.h`.
- **`update()` checks, then applies.** `_update_check()` makes, in the same order, every call of `_update_apply()` that can throw for a reason other than memory, and changes nothing; `update()` runs the check and then the apply, so a throw leaves the value as it was. A change to one of the two needs the same change in the other (`msgpack_update` against a model of the merge). An allocation that fails in the apply leaves the entries before it applied; the README says so.
- **Keep `MsgPack(MsgPack&&)` `noexcept`.** A `std::vector<MsgPack>` copies its elements when it reallocates otherwise, and each copy has a zone of its own that its children write into (`msgpack_growth` fails under AddressSanitizer).
- **The exception fork is load-bearing.** `msgpack/exception.hpp` must keep inheriting `BaseException`. Xapiand relies on catching these types through its hierarchy.
- **The seam is compile-time.** Standalone uses `msgpack_exception.h`; Xapiand defines `MSGPACK_EXCEPTION_HEADER` to its `src/exception.h`.
- **RapidJSON parse flags are part of behavior.** Keep comments, trailing commas, and NaN/Inf accepted.
- **Keep file names stable.** `msgpack.h`, `msgpack.hpp`, `msgpack_patcher.h`, and `xchange/...` are intentionally not renamed.

## Extraction deltas

- Removed `#include "config.h"` from `msgpack.h`; the referenced `XAPIAND_LUA` macro was dead in this header.
- Routed `msgpack.h` and `msgpack_patcher.*` through `MSGPACK_EXCEPTION_HEADER` with a standalone default seam.
- `msgpack/unpack.hpp` and `msgpack/object.hpp` skip `memcpy` for an empty string or bin (a null source with size zero is undefined; UBSan reports it).
- Changed the one `THROW(msgpack::type_error)` in `xchange/string_view.hpp` to `throw msgpack::type_error();` as called out in Xapiand's extraction plan.

## Build / test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/msgpack_demo
```

Benchmarks (off by default; `MSGPACK_BUILD_BENCH` fetches msgpack-c, nlohmann::json and mpack at pinned tags for
`msgpack_compare` only, and RapidJSON is already a dependency). Run them on a quiet machine:

```sh
cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DMSGPACK_BUILD_BENCH=ON
cmake --build build-bench --target msgpack_benchmark msgpack_compare
./build-bench/msgpack_benchmark > ours.txt
./build-bench/msgpack_compare > compare.txt
python3 benchmarks/compare.py ours.txt compare.txt
```

`BENCH_QUICK=1` shortens every row for a run that only checks that the programs work; both programs take an
optional document name and an operation substring (`msgpack_benchmark med read`).

For Xapiand integration, configure Xapiand with `-DFETCHCONTENT_SOURCE_DIR_MSGPACK=/path/to/msgpack` and define `MSGPACK_EXCEPTION_HEADER` to Xapiand's `src/exception.h`.

Xapiand also defines `XAPIAN_LIB_BUILD=1` explicitly now. The old in-tree `msgpack.h` pulled in generated `config.h`, which incidentally made Xapian's internal `DB_ACTION_MASK_` visible before some Xapian headers were included. The standalone header deliberately does not include `config.h`, so the host owns that define.
