# Changelog

All notable changes to `msgpack`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

## 2026-11-01

### Added

- Benchmarks against other libraries, off by default (`-DMSGPACK_BUILD_BENCH=ON`).
  `msgpack_benchmark` (`benchmarks/pack_unpack.cc`) times `MsgPack` and the msgpack-c it
  bundles; `msgpack_compare` (`benchmarks/compare.cc`) times msgpack-c 9.0.0, nlohmann::json
  3.12.0, mpack 1.1.1 and RapidJSON, fetched at those tags by the benchmark targets only.
  Both read the four documents of `benchmarks/corpus.h` (63 B, 502 B, 104 KB, 8.9 KB; MessagePack
  bytes and JSON text, written without a msgpack library) and print rows with the same operation
  names and a checksum of the document; `benchmarks/compare.py` joins the outputs and fails
  when libraries that read the same bytes disagree. The numbers on arm64 (Apple M1 Pro) and
  x86-64 (Intel Core i9-9980HK), where the time goes and what the fast paths keep identical
  are in `docs/perf/survey-2026-10.md`. The small throughput loop in `benchmarks/pack_unpack.cc`
  is replaced by `msgpack_benchmark`, which is no longer built by default.
- The cost of the failed-fill check, measured with the benchmarks on the two machines: the tree
  of the commit before it against the tree with it, run alternately three times. On arm64 the
  rows that fill maps (`read+walk`, `read+lookup3`, `update`) change by 0.9% at most on the med,
  large and wide documents (the small document is 1 to 6% faster), and no row moves by more than
  6%. The `write` rows read 2.4 to 3.8% slower in a default build although the change does not
  touch that path; with every function aligned to 64 bytes (`-falign-functions=64`) in both trees
  they read 0.98 to 1.00, so the difference is where the compiler puts the code. On x86-64 (a
  shared machine, the repeated `read` row varies 3% on its own) every row is within 6.4%, and the
  fill rows within 2.2%, except `update` of the wide document at 4.4% faster.

### Fixed

- `replace()` and `put()` over a key (or an index) whose old value was a map or an array
  that had been read keep the new value's index right. The new object replaced the old one
  but the value's `Body` kept the index and the capacity of the old value, so reading or
  changing the new value went by the entries of the old one: an assertion in a build with
  assertions, an out-of-bounds read otherwise (`{"k": {"x":1,"y":2,"w":3}}`, `doc["k"]["x"]`,
  then `doc.put("k", {"a":1})`, then `doc["k"]["zz"] = 5`). The new object is built first (that
  can throw and changes nothing), then the old value's index is dropped, as `operator=` does.
  Replacing a value that was locked on its own throws `const_error`, as assigning to it does.
  `test/replace_lock_test.cc` (`msgpack_replace_lock`) replaces every kind of value with every
  kind, in a map and in an array, after reading the old one not at all, by reference and
  completely (900 cases), then reads and changes the new one against the model; and 3,200
  random replaces at any depth. Four mutants, all killed (`revert_replace`,
  `replace_map_keeps_index`, `replace_array_keeps_index`, `replace_capacity_kept`).
- A `lock()` that throws leaves nothing locked. Filling a map with a key that is not a
  string throws, and `lock()` locked each map before filling the ones below it, so the maps above
  the bad one stayed locked (and `locked()` said so). `lock()` now fills everything and then
  locks. The same test checks it on random documents with and without a bad map (61 and 339 of
  400); mutant `revert_lock`.
- `erase()` returns 1 for the last element of a map or an array, as for any other. It
  returned 0 although it had erased it: the count shared its slot with "is there an element
  after it". The data was always right. The frozen implementation has the old behavior, so
  `test/diff_test.cc` compares the change in size across an erase and not the returned count.
  `test_erase_counts` in `test/hunt_test.cc` covers every position and key of small documents;
  mutant `erase_last_returns_zero`.
- References to the children of an array element stay valid when the array grows. Growing
  re-pointed the elements and also dropped each element's index of its own children, so a
  `MsgPack&` taken from `a.at(0)["k"]` dangled (AddressSanitizer: heap-use-after-free) after enough
  `push_back`s. An element keeps its index when the array only moves to a larger buffer, and drops
  it only when an `erase` moved other objects into its position. `test_references_survive_growth` in
  `test/growth_test.cc`.
- An allocation that fails leaves the document consistent. After a `std::bad_alloc` in `put`,
  `replace`, `emplace`, `add`, `push_back`, `append`, `update`, `operator[]` or a fill, the tree held
  an entry or element that the index did not (key not found, `std::out_of_range` from the array), or
  the children pointed into the buffer the tree had just left. `_put` takes back the entry it added,
  `_init_map` and `_init_array` take back what they indexed, and `_reserve_map` and `_reserve_array`
  re-point the children before the step that allocates. An operation that fails may or may not have
  taken effect, but the `MsgPack` agrees with its own bytes and keeps working.
- A map that repeats a key is merged correctly. The last value wins and the entry
  keeps the position of the key's first occurrence, so
  `{"a":1,"a":2,"b":3,"c":4}` is `{"a":2,"b":3,"c":4}`; it used to be
  `{"a":2,"b":3,"b":3}`. `_init_map` copied the entry after the repeat over every
  later entry (`*q = *(p + 1)` where `*(q + 1)` was meant) and skipped the entry
  it moved, so `c` was lost, `b` repeated, the index of the map disagreed with the
  tree (`assert(via.map.size == map.size())` failed in a build with assertions) and
  `erase(key)` then dereferenced the end iterator. The merge runs when a map is
  first filled (a lookup, a walk, `hash()`, `erase`); `serialise()` and `size()` of a
  map nothing has filled still give the document as it came. The reference is a
  `std::map` model (`test/model.h`): `test/merge_test.cc` (`msgpack_merge`) checks
  lookups, `erase`, writes, `hash()`, copies and round trips against it on
  hand-written documents, on every document of up to 6 entries over 3 keys (1092),
  and on 3000 random nested ones (2348 with a repeat).
  What a consumer sees: for a document that repeats a key, `hash()` is now the hash
  of the merged map (it was the hash of the corrupted one), and so are `serialise()`
  and a copy after the first fill. Documents without a repeated key are unchanged,
  and `hash()` of one still allocates nothing.
- Pushing onto an array of maps after touching some of its elements no longer reads
  freed memory. The cause was `MsgPack(MsgPack&&)` not being `noexcept`: when
  `std::vector<MsgPack>` reallocated (`_init_array`, `reserve`), it deep-copied
  every element, and each copy had a zone of its own. `_update_array` then
  pointed the copy at the parent's tree while its `_zone` stayed the copy's, so a
  string written through the element's children (`array[i]["name"] = ...`) was
  allocated in the copy's zone and stored in the parent's tree, and the vector
  freed that zone with the old elements. The move constructor is `noexcept` (its
  body only moves `shared_ptr`s), so the vector moves its elements. `_update_array`
  also makes an element rebuild the index of its children when the element now
  stands for another object (after the elements moved, and after `erase(index)`),
  which a copy used to do by accident. `test/growth_test.cc` (`msgpack_growth`):
  400 records, then `push_back` of a record and a write to `array[i]["name"]`
  800 times, on a built and on an unserialised array, and 400 random sequences of
  push, set, erase and read on arrays of maps, against the model; under
  AddressSanitizer it reads no freed memory.
- Proof that each test fails without its fix (`tools/mutants.py`, 7 new
  mutants, all killed, x86_64 Linux, clang 12 with AddressSanitizer and
  UBSan): `revert_merge` (the old shift, no re-visit of the moved entry; the index
  assertion), `merge_shifts_from_p`, `merge_skips_moved_entry`,
  `merge_first_value_wins`, `merge_size_not_reduced`, `revert_growth` (no
  `noexcept`; AddressSanitizer, and without it a mismatch with the model) and
  `update_array_keeps_index`. On the unfixed code `msgpack_merge` aborts on the index
  assertion and `msgpack_growth` fails.
- `test/diff_test.cc` reads a document that repeats a string key as the merged document on
  the frozen side (`model::merged`), the current implementation reads it as it is and
  fills every map first, and `hash()` of the original is held to the model's. Nothing
  else about the comparison changed. Two sets of steps are left out because the frozen
  implementation is wrong by itself there: changing an array with a map in it (the
  growth bug), and anything after the first exception on a document with a key that
  is not a string (the frozen implementation misreads a retry; see the fix for a
  failed fill above). 33 random documents that repeat a key and
  also have a key that is not a string are not compared; the current implementation
  still reads them under the sanitizers. `MSGPACK_DIFF_TRACE=1` names each document
  as it is checked.
- A fill that throws leaves the map as it was. A map that has a key that is not a string
  cannot be indexed, and filling it (a lookup, `at`, `[]`, dereferencing an iterator,
  `erase`, `hash()`) throws "MAP keys must be of type STR". `_init_map` indexed the
  entries before the bad key and then threw, so the next fill took the keys already
  indexed for repeats and removed them from the tree: on `{"a":1, 5:2}` the second
  `erase("a")` gave `serialise()` = `810502` (the frozen code gave `80`, after skipping
  the non-string key and reading it as a string). `_init_map` now checks the type of
  every key it is about to index before it changes anything, so the throw leaves the
  tree, the index and `size()` as they were (repeated keys before the bad key are not
  merged either) and every later fill throws the same way. `serialise()`, `size()` and
  copies do not fill, so they work on such a map. `at(size_t)`, which read
  `via.str` of the key at that position, is only reached after the fill, so it no longer
  reads a key that is not a string. The oracle is the input bytes:
  `test/failed_fill_test.cc` (`msgpack_failed_fill`) applies every operation that fills,
  in any order and as often as it likes, to every kind of key that is not a string in every
  position with repeats around it, to every document of up to 4 entries over the keys
  `"a"`, `"b"` and an integer with every sequence of up to 3 of 12 operations (90
  documents, 169560 sequences), and to 600 random nested documents (8400 steps, 1697 on a
  bad map) that keep being changed with set, erase, push_back and `erase(index)`
  against the model (`test/model.h`, whose `Doc` gains a RAW kind). Each call either throws
  the one exception or leaves `serialise()` equal to the bytes it came as. On the unfixed
  code it fails on the second `erase`. Three mutants, all killed (`tools/mutants.py`):
  `revert_failed_fill` (the check back in the loop), `failed_fill_first_key_only` and
  `failed_fill_last_key_skipped`.

### Hunt

`test/hunt_test.cc` (`msgpack_hunt`, and `msgpack_hunt_alloc` where `operator new` can be replaced) is a
bounded bug hunt on `msgpack.h`: random sequences of every mutating operation (set by `[]`, `replace`,
`put`, `emplace`, `add`, `update`, `push_back`, `append`, `erase` by key, position, iterator and `MsgPack`,
`clear`, `reserve`, assignment of a whole value, 40 keys or elements at once, copy and move) at any depth of
a document of maps, arrays, scalars and bad maps, against the model in `test/model.h`, comparing
`serialise()` after every step and reading the whole document back through the API every few. Properties
checked on documents without a bad map: `hash()`, `to_string()`, `as_document()` and a parse of the text
agree with a fresh read of the same bytes; `lock()` of a copy throws exactly when a bad map is in it.
Failures injected: bad maps (a fill that throws) and, in `msgpack_hunt_alloc`, one allocation made to fail
(`std::bad_alloc`) around one operation in three, after which the model is read back from the bytes and
the `MsgPack` must agree with them. Runs of the final tree: 300 runs in CTest; stress runs of 12,000 runs
under AddressSanitizer and UBSan on arm64 macOS, 6,000 plain and 6,000 with failures (7,559 injected) under
UBSan on x86-64 Linux, and the failing allocations under AddressSanitizer on both (1,870 and 399
injected; LeakSanitizer on Linux), no report. It found the `erase()` count, the failure-safety above, and
the reference problem. Mutants for it: `erase_last_returns_zero`, `init_map_keeps_partial`,
`put_map_keeps_entry`, `put_array_keeps_size`, `reserve_map_repoints_late`,
`reserve_array_repoints_late` (run `hunt_alloc` mutants with `SAN=-fsanitize=undefined` or none).
`MSGPACK_HUNT_SKIP=op,op` skips operations and `MSGPACK_HUNT_TRACE=1` prints every step.

### Known problems found and left as they are

Each needs an owner decision.

- `update()` is not atomic. It merges entry by entry, so when it meets a bad map it has to fill
  (or an allocation fails) the entries before that point stay applied.
- `to_string()` and `as_document()` read `via.str` of a map key that is not a string: undefined
  behavior, as the earlier `as_document()` did (the hunt only prints documents without a bad map).
- A `MsgPack&` to a child of a value that is replaced, erased or cleared is gone with the value, as
  with any container; a reference to an element itself is invalidated when an earlier element of its
  array is erased (the elements move up).
- Not covered by the hunt: the JSON Patch helper (`msgpack_patcher`), threads, repeated keys together
  with edits (`msgpack_merge` covers repeats), changing a locked value (a build with assertions aborts
  instead of throwing), and a failing `msgpack::zone` allocation (it uses `malloc`, which the hunt
  cannot fail).

## 2026-10-31

### Changed

- `MsgPack::unserialise` reads into the value's own zone. It used to unpack into
  a zone of its own and deep-copy the tree into a second one (`MsgPack(object)`);
  the unpacked tree is now the value's tree. The input is still never referenced:
  strings, binaries and ext bodies are copied into the zone, so a value outlives
  its buffer. Values, offsets, exceptions, `serialise()` bytes and the growth of a
  value after a mutation are unchanged. A read costs 5 allocations instead of 7
  and one 8 KB zone chunk instead of two (`test/alloc_test.cc`). Time per read on
  an M4 Pro (arm64) and an i9-9980HK (x86_64), small 63 B / med 0.5 KB /
  large 104 KB / wide 8.9 KB: arm64 1.52x / 1.49x / 1.53x / 1.55x, x86_64
  1.53x / 1.55x / 1.64x / 2.01x.
- `MsgPack::hash()` hashes the msgpack tree directly. It built a `Body` for every
  child of every map and packed every leaf into its own 8 KB `sbuffer` (20 to 55
  allocations for a map of 15 keys); it now makes none for maps of at most 16 keys
  whose values pack to at most 256 bytes each (91 fewer allocations for the 0.5 KB
  record in `test/alloc_test.cc`), and packs a leaf into a 256-byte stack buffer
  (the heap past that). A map of more than 16 keys allocates two arrays, one per
  such map, for its key hashes and the repeated-key check. The value is the same: the
  XOR over each map's entries of `hash(key) ^ hash(value)`, with the hash of the
  packed bytes for every other value. arm64: small 7.93x, med 3.94x, large (an
  array root, which packs the whole 104 KB) 1.15x; x86_64 12.88x, 6.64x, 1.13x.
  A map with a repeated key, or a key that is not a string, anywhere in the maps
  it reaches through maps takes the filling path as before, and so merges the
  repeats in the tree (see 2026-11-01, Fixed).
  `hash()` no longer fills the maps it reads, so it is safe to call from several
  threads on one value when no key repeats.
- `MsgPack::to_string` makes the calls on the RapidJSON `Writer` (or
  `PrettyWriter`) that `Document::Accept` made for the document `as_document()`
  built, in the same order and stopping at the first `false`; the Document is not
  built. The text is identical, including the partial text before a `NaN` or
  `Inf` and the indented form. arm64: 2.2x to 2.5x / 1.5x / 1.3x to 1.5x / 1.5x to 1.65x
  (two runs, noisy rows); x86_64 1.75x / 1.58x / 1.68x / 1.83x. `as_document()` is unchanged.

Measured with a benchmark harness that was not yet part of this repository (the rows of
`msgpack_benchmark` are the same operations; see `docs/perf/survey-2026-10.md`):
`unserialise`, `hash()`, `to_string()` and the rows around them over four documents of 63 B,
0.5 KB, 104 KB and 8.9 KB. The frozen and the new header are built side by side with `-O3`
and run interleaved (old, new, old, new).

- arm64: M4 Pro, Apple clang 17, the machine to itself, 3 passes; the A/A rows differ by
  at most 3%.
- x86_64: i9-9980HK, Apple clang 17, the machine to itself, 2 passes. `hash()` comes from a second
  run with the final header, 5 rounds.
- A GitHub runner (AMD EPYC 7763, 4 vCPUs, 3 interleaved passes, noisier): 1.54x to 1.61x
  for `unserialise`, 5.50x / 2.72x / 1.13x for `hash()`, 1.42x to 1.65x for `to_string()`.
- Every checksum of the 240 rows (every wire type, the repeated-key document included) is
  equal between the two headers on all three.
- Rows this change does not touch (`copy`, `serialise`, `walk(filled)`, `lookup3(filled)`,
  `unpack`, `as_document`) stay within 3% of the old header on arm64, except `lookup3(filled)`
  and `walk(filled)` on the 0.5 KB and 104 KB documents (0.96x to 0.98x, spreads of 4% to
  10%). The code on those paths is unchanged and a second build with a different function
  alignment measures 1.00x; the cause was not found, and the tree they read now has the
  unpacker's layout instead of a copy's. On the i9 `lookup3(filled)` of the 63 B document is
  0.92x (spread 8%) and the other untouched rows are within 3%.
- The first version of the array-root `hash()` had the growth of its buffer inline in
  `write` and measured 0.57x to 0.69x on x86_64 (glibc and the i9). `write` is now a short
  inline function and only the growth is out of line.

### Fixed

- The documentation said a `MsgPack` copy is copy-on-write and shares storage with
  its source. It never did: `MsgPack(const MsgPack&)` and `operator=(const MsgPack&)`
  deep-copy the tree into a new zone (0.37 us for a 0.5 KB record on the M4 Pro,
  against 3.6 ns for a `shared_ptr` copy), and a copy and its source share nothing.
  README, ARCHITECTURE, AGENTS and the test comments say so. Behavior is
  unchanged.
- The bundled unpacker and `object_with_zone<object>` no longer call `memcpy` with
  a null source and a size of zero for an empty string or bin (undefined
  behavior, reported by UndefinedBehaviorSanitizer on any document with an empty
  string). The bytes copied are the same.

### Added

- `test/diff_test.cc` (`msgpack_diff`): random documents (every msgpack type in
  every width, 64-bit extremes, NaN, Inf and -0.0, empty containers, ext, bin,
  strings with NUL and invalid UTF-8, repeated keys at any depth, keys that are
  not strings) and 48 hand-built ones, read and operated on in random order by
  `MsgPack` and by the frozen `MsgPackOld` (`test/frozen_msgpack.h`, the previous
  `msgpack.h` with the names changed). Every result and every exception must be
  equal: `unserialise` (value and offset, for every prefix of a document too),
  `hash()` (also against a definition written from the specification, for
  documents without repeated keys), `serialise()`, `to_string()` with and without
  indent, copies, moves, walks, mutations, `lock()`.
- `test/lifetime_test.cc` (`msgpack_lifetime`): a value outlives its input buffer
  (overwritten and freed), the values made from it, the unpacker, and the thread
  that made it; copies and moves; a part taken out of a value; growth past the
  unpacker's chunk; failed reads leave the offset alone. Run under
  AddressSanitizer in CI.
- `test/alloc_test.cc` (`msgpack_alloc`): allocation counts of `unserialise`,
  `hash()` and `to_string()` against the frozen implementation (not built under a
  sanitizer, because it replaces `operator new`).

### Known problems found and left as they are

These were in the code the frozen copy keeps, and the differential test held the
current code to the same behavior. The merge of a repeated key, the read of freed
memory when an array of maps grows, and the copy of every element when a
`std::vector<MsgPack>` grows were fixed on 2026-11-01 (above); `to_string()` on a key
that is not a string is still open and is listed there.

### Not done

- A per-size freelist for the `Body` blocks (1.06 to 1.24x on arm64 for a read
  followed by touching every node, 1.4 to 1.7x on x86_64 in the survey): it needs a
  decision on cross-thread frees (a bounded per-thread cache, or a shared pool)
  and on the retained memory per thread.
