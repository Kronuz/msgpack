# Changelog

All notable changes to `msgpack`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

## 2026-11-01

### Fixed

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
  is not a string (see Known problems). 33 random documents that repeat a key and
  also have a key that is not a string are not compared; the current implementation
  still reads them under the sanitizers. `MSGPACK_DIFF_TRACE=1` names each document
  as it is checked.

### Known problems found and left as they are

Each needs an owner decision.

- A fill that throws leaves the map half indexed, and the next fill takes the keys
  already indexed for repeats. `{"a":1, 5:2}` (a key that is not a string): the first
  `erase("a")` throws and leaves the document as it was; the second throws again, but
  has merged `"a"` into itself and dropped it from the tree (`serialise()` gives
  `810502`, a map of one entry). The frozen code, whose retry then skipped the non-string
  key and read it as a string, returned 0 and gave `80`. A fix would validate the keys
  before the first change, or empty the index when `_init_map` throws; it is not made
  here because it changes what a failed fill leaves behind.
- `at(size_t)` on a map reads `via.str` of the key at that position without checking
  its type (undefined behavior for a key that is not a string, reached after the
  failure above).
- `to_string()` reads `via.str` of a map key that is not a string: undefined
  behavior, as `as_document()` did.

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
  (two holds, noisy rows); x86_64 1.75x / 1.58x / 1.68x / 1.83x. `as_document()` is unchanged.

Measured with `bench_ours` from the campaign's cluster J harness (outside this repository):
`unserialise`, `hash()`, `to_string()` and the rows around them over four documents of 63 B,
0.5 KB, 104 KB and 8.9 KB. The frozen and the new header are built side by side with `-O3`
and run interleaved (old, new, old, new).

- arm64: M4 Pro, Apple clang 17, one `quiet` strict hold, 3 passes; the A/A rows differ by
  at most 3%.
- x86_64: i9-9980HK, Apple clang 17, one strict hold, 2 passes. `hash()` comes from a second
  hold with the final header, 5 rounds.
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
