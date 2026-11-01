# Changelog

All notable changes to `msgpack`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

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
  it reaches through maps takes the filling path as before, so those hash
  identically and leave the tree as before (see Known problems).
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

These are in the code the frozen copy keeps, and the differential test holds the
current code to the same behavior. Each needs an owner decision.

- Merging a repeated key is wrong. `_init_map` copies the entry after the repeat
  over every later entry (`*q = *(p + 1)` where `*(q + 1)` is meant) and then
  skips the entry it moved, so `{"a":1,"a":2,"b":3,"c":4}` becomes
  `{"a":2,"b":3,"b":3}`: `c` is lost, `b` repeats, and the map index disagrees
  with the tree (`assert(via.map.size == map.size())` fails in a build with
  assertions). It runs the first time a map with a repeated key is filled: by
  `hash()`, a lookup, a walk. `erase(key)` on such a map then dereferences the end
  iterator (`_update_map`), a crash.
- Pushing elements onto an array of maps after touching some of them reads freed
  memory (`_init_array` copies the elements of a `std::vector<MsgPack>` whose
  nested bodies hold their own zone): 400 records, then 800 times `push_back`
  of a record and a write to `array[i]["name"]`. AddressSanitizer reports it on
  the frozen copy too.
- `to_string()` reads `via.str` of a map key that is not a string: undefined
  behavior, as `as_document()` did.
- A `std::vector<MsgPack>` growth copies every element, because `MsgPack(MsgPack&&)`
  is not `noexcept`: each copy is a deep copy of that element.

### Not done

- A per-size freelist for the `Body` blocks (1.06 to 1.24x on arm64 for a read
  followed by touching every node, 1.4 to 1.7x on x86_64 in the survey): it needs a
  decision on cross-thread frees (a bounded per-thread cache, or a shared pool)
  and on the retained memory per thread.
