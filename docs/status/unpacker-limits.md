# Unpacker limits (branch fix/unpacker-limits)

## State

Fix written and proven on this branch; pushed for CI; not merged. The security review (`docs/reviews/unpacker-limits-security.md`) is APPROVE WITH CONDITIONS; SR1 and SR3 are folded in below, SR2 is Xapiand's `fix/restore-depth-limit`.

The streaming `msgpack::unpacker` (used by Xapiand's `RESTORE` and the HTTP body parser) accepted `array32` and `map32` headers of 0xffffffff and reserved `n * sizeof(object)` up front in `unpack_array` and `unpack_map`, so five bytes asked the zone for 96 GB (a map: 206 GB). Xapiand's body-size bound covers `msgpack_load`, which passes limits equal to the body size; the unpacker has no body size to compare with because the document may arrive in pieces. The fix is in the library, so every caller gets it: the parser reserves only what the bytes in the buffer can hold and grows the storage as items arrive (`docs/mechanisms/unpacker-reservation.md`). `str32`, `bin32` and `ext32` were already safe: the payload is awaited before it is allocated (shown below).

## Done

The fix and its tests are msgpack:5ee7adc on `fix/unpacker-limits`; this note, the mechanism note, the changelog and AGENTS.md follow it.

| Claim | Evidence |
| --- | --- |
| Tests fail with the fix reverted | `unpack.hpp` put back to `origin/main`: `test_hostile_headers_one_chunk`, `_one_shot`, `_with_few_bytes_behind`, `test_nested_hostile_headers`, `_split_across_feeds`, `test_hostile_after_valid_items` fail (`std::bad_alloc` from the refused 96 GB and 206 GB requests for `array32` and `map32`; `array16`/`map16` fail the 64 KB bound at 1.5 MB). `str32`, `bin32` and `ext32` pass on both: they were bounded already. |
| Mutants die | `tools/unpack_mutants.py`: 13 edits (reserve the declared size, claims ignored, claims never released, map entry as one byte, grown block drops its items, no growth for array and map, growth by zero, capacity not recorded, no block of its own reused or recorded, a stale block across documents, the resized block dropped) all fail `msgpack_unpacker_limits`; the control passes. |
| Sanitizers | `-fsanitize=address,undefined -fno-sanitize-recover=undefined` with Homebrew LLVM 23.1.2 (`/usr/local/opt/llvm/bin/clang++`): 11 of 11 ctest targets pass (after the SR1 change too) (`msgpack_alloc` and `msgpack_hunt_alloc` replace `operator new` and are not built under ASan, as before). Release (Apple clang): 13 of 13. |
| Fuzz | `fuzz/unpacker_fuzz.cc`, libFuzzer with ASan and UBSan, 300 s, 3,224,200 runs on the first version and 999,795 on the SR1 version (the host was busy), no finding after the two harness fixes below. |
| Xapiand | Built with `-DFETCHCONTENT_SOURCE_DIR_MSGPACK=<this worktree>` (Xapiand `d372d44d4`, Release, Apple clang): `harness/restore_stream_check.sh` all six rows ok; `harness/run_all.sh e2e` VERDICT GREEN; `RESTORE` of 5-byte `array32`, `map32` and 2000 nested `array32` headers returns 200 with 0 items and the node stays at 9 MB RSS. |

## Review round (security review SR1 to SR5)

- **SR1** (streamed containers cost up to 126 bytes of malloc per input byte against 32 in one piece): fixed in the mechanism, not documented away. Extending the last zone block in place did nothing by itself (the chunk is full when the block outgrows it: the hostile worst stayed 126.0). What works is moving an array or map that outgrows its reservation into a malloc'd block of its own, freed by a zone finalizer and resized with realloc afterwards, so the blocks it outgrew are not left in the zone and strings or nested storage allocated meanwhile do not pin it. Growth by half (1.5x) against doubling: hostile worst 34.6 against 46.6, honest streamed 23.0 to 25.1 for both; half steps kept. Streamed honest arrays went from 62 to 88 to 23 to 25 bytes per input byte (one piece: 32 to 44); hostile header with nil items from 126 to 35; strings or nested storage behind the block from 80 to 104 to 20 to 47; the 200,000 random adversarial streams from 162 to 32. Revert check: with the previous `unpack.hpp` the new tests `test_large_streamed_array_grows_in_steps`, `test_streamed_array_with_strings_is_not_retained` and `test_streamed_hostile_items_stay_under_the_bound` fail.
- **SR3**: the chunked bound in the test and the fuzz target is `slack + 4 x sizeof(object) x bytes`, the same as one piece (it was `2 x 8 x sizeof(object_kv)`), and `test_streamed_hostile_items_stay_under_the_bound` reaches the shape that would break it (a regression to the earlier 126 fails it).
- **SR4**: stated in the mechanism note.
- **SR5**: `grow` throws `std::bad_alloc` when the byte count would wrap `size_t`.
- **SR2**: Xapiand `restore_documents` passes `msgpack_limits()` (branch `fix/restore-depth-limit`, below).

The fuzz target found two faults in itself, not the library: a float32 NaN compares unequal to itself (the objects are now compared as repacked bytes), and the byte bound was 98 per input byte, under what the zone's power-of-two rounding plus outgrown blocks can reach in the worst case (213,192 bytes for 1,455 input bytes); it is 8 x `sizeof(object_kv)` = 384 now, derived in the mechanism note. Both inputs are in `fuzz/corpus/unpacker/`.

## Design vs code

The lead's brief offered two shapes: never reserve more than the bytes justify, or explicit limits like `msgpack_load`. Explicit limits cannot work for a stream (a header may be followed by its items in later feeds, so the bytes in hand are not the document's size), and they leave every other caller exposed with the default 0xffffffff. The implemented shape is the first, with `unpack_limit` unchanged.

- `unpack_array` and `unpack_map` take a `reserve` argument (an internal `detail` interface; no other caller in the repo or in Xapiand).
- The per-byte cost is not 1:1: an item costs 24 bytes (48 for a map entry of two bytes) of storage per byte read, which is inherent to the tree. Measured per input byte: streamed 23 to 47, in one piece 32 to 70 (the zone's power-of-two chunks); the asserted bound is 96 (`4 x sizeof(object)`) plus 64 KB for both. See the table in the mechanism note.
- Not changed: the depth limit still defaults to 0xffffffff (the stack of open containers is about 80 bytes per level of input); callers parsing untrusted bytes pass a depth limit, as Xapiand does.

## Next

1. Security review of `msgpack/unpack.hpp` (`push_aggregate`, `grow_array`, `grow_map`, the claim bookkeeping).
2. The lead measures the unpacker on the corpus (`msgpack_benchmark`) before landing; no performance claim here.
3. After landing: bump the msgpack pin in Xapiand, update item 3 of `docs/status/phase7c-linux-gate-fixes.md`, and mention it in the cpp-libraries upgrade note.

## Blocked on

Nothing.
