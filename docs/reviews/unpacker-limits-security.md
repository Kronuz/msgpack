# Security review: `fix/unpacker-limits` (msgpack:5ee7adc, msgpack:339afe3)

Reviewer: security. Subject: `msgpack/unpack.hpp` (`push_aggregate`, `grow_array`, `grow_map`, the claim bookkeeping), the ledger `docs/status/unpacker-limits.md` and the mechanism note `docs/mechanisms/unpacker-reservation.md`. Diff read against `1ab7a92`.

## Verdict: APPROVE WITH CONDITIONS

The fix does what it says. A hostile `array32`/`map32` header no longer reserves anything the bytes in hand cannot hold, nesting and split feeds do not add up to more than linear memory, and I found no way past the claim bookkeeping. No blockers. The conditions are about what the mechanism note promises callers: the per-byte ceiling is real but about four times what an honest streamed document cost before, and the depth-unlimited paths are still open.

Threat model (not stated by the design; assumed from the brief): the attacker is a client with network access who sends an HTTP body that Xapiand's `RESTORE` feeds to `msgpack::unpacker`, with whatever feed sizes the transport produces. The attacker's gain is memory exhaustion (a node crash or OOM kill) from a small body. A local operator feeding `--restore` from stdin is trusted, but is listed where it differs.

## Findings

### SR1 (medium): a streamed container costs up to 126 bytes of malloc per input byte, about 4x the honest one-piece cost

Attacker: network client, one body. The documented bound is 384 bytes per input byte (`8 x sizeof(object_kv)`); the real ceiling is lower but is not the 32 bytes an exact reservation costs, because a container that outgrows its reservation keeps every earlier block in the zone, and the zone rounds each block up to a power of two times 8 KB.

Probe `.scratch/sec/p1_amplify.cc` (array32 / map32 of nil, header declares 0xffffffff, items fed one byte at a time, sweep of first-feed size 1..8192 and total 1..65 K): worst 126.3 bytes malloc'd per input byte (total 32,894 bytes, 4.19 MB malloc'd), the same for the map (a nil:nil entry is 2 bytes and 48 bytes of storage). The honest one-piece figure is 32.0 (`msgpack::unpack` of an exact `array32`/`map32`).

Probe `.scratch/sec/p5_streamed_honest.cc` (an honest array whose count equals its items, fed in pieces): 62 to 88 bytes per input byte at 1 KB, 4 KB and 64 KB feeds, against 33 to 44 in one piece. So a large honest document arriving over HTTP now costs about twice what it cost before, and a hostile one up to four times.

Probe `.scratch/sec/p2_invariants.cc` (200,000 seeded adversarial streams mixing array32/map32/array16/map16 headers of 0xffffffff, fixarrays, strings, scalars, truncated, random feed sizes, depth limits on and off): worst 162.7 bytes per input byte after discounting the 8 KB zone each completed document allocates; never near 384. The libFuzzer target agrees (below).

What it means: a 1 MB body costs the node up to about 126 MB, a 100 MB body about 12 GB. Before the change a body of exact counts cost 32x. The bound is linear, so this is not a blocker, but the mechanism note's "at most twice the items read" understates the zone effect, and a body-size cap sized from 32x is now 4x too generous.

Options:
1. Keep as is and put the measured factor (126 hostile, about 66 to 88 honest streamed) in the mechanism note so callers size their body cap from it. Cheapest; no behavior change.
2. Extend the last block in place: when the container's storage is the zone's most recent allocation (the case for arrays of scalars, which is the worst case), grow it by moving `m_ptr` instead of allocating and copying. Removes the doubled retention for exactly the hostile shape, keeps the rule.
3. Grow by 1.5x instead of 2x: smaller retained total, more copies. Not recommended alone.

### SR2 (low): depth-unlimited callers; the stack costs 84 bytes per input byte

Attacker: network client, only where the caller passes no depth limit. `unpack_limit` still defaults to depth 0xffffffff, and `unpack_stack` grew from 64 to 80 bytes (capacity and claim).

Probe `.scratch/sec/p3_nesting.cc`: 1,000,000 bytes of `0x91` into a default `msgpack::unpacker`: 1,000,001 stack levels, vector capacity 1,048,576 x 80 = 83.9 bytes per input byte, plus 24 bytes of zone per byte, maxrss 211 MB (the vector's doubling briefly holds old and new). 1,000,000 `array32`/`map32` max headers (5 MB): 16.8 bytes per input byte, 196 MB. The claim bookkeeping does not add to this beyond the 16 bytes per level.

Xapiand's HTTP stream (`make_msgpack_unpacker`, `MAX_JSON_DEPTH` 200) and `msgpack_load` pass a depth limit, so the network path is covered. Two paths do not:
1. `DatabaseHandler::restore_documents` (`src/database/handler.cc:988`, Xapiand `d372d44d4`) builds `msgpack::unpacker unpacker;` with default limits and reads stdin (`--restore`). The attacker is whoever controls the file the operator restores. It then copies each object into a `MsgPack` (recursive).
2. `MsgPack::unserialise` (`msgpack.h:675`) calls `msgpack::unpack(*_zone, data, len, off)` with default limits. Used on stored data, so trusted; the bound on the reservation holds there too.

Fix on the Xapiand side (pass `msgpack_limits()` in `restore_documents`). A library-side default depth would narrow behavior and is the owner's call; I do not ask for it.

### SR3 (low): test and fuzz bounds are looser than the documented one

`test/unpacker_limits_test.cc` and `fuzz/unpacker_fuzz.cc` assert `slack + per_byte * len` for one piece and `per_byte * len * 2` for chunks, with `per_byte` = 384. The note says 384 per byte. The chunked check therefore allows 768, twice the documented bound, and a regression from 126 to 600 would pass. The measured worst is 126 to 163. Tighten the chunked bound to the documented figure (or to a measured ceiling with a margin, 192 is the derivable one: 24 x 4 growth x 2 rounding).

### SR4 (informational): state after a thrown limit is stale

When `unpack_array` throws, or the depth limit throws after `m_claimed += ...` in `push_aggregate`, `m_claimed` keeps the claim. Xapiand's STREAM_MSGPACK handler catches the exception and keeps feeding the same unpacker, which re-parses from the same offset and adds the claim again. The drift only grows `m_claimed`, so later reservations shrink to 0 (they grow by doubling instead); I could not make it underflow because `--m_claimed` is guarded by the container's own non-zero claim, which `m_claimed` includes. Safe direction, pre-existing wedge (the unpacker repeats the same error). No action beyond a line in the note.

### SR5 (informational, pre-existing): 32-bit size_t

`reserve * sizeof(object_kv)` and `cap * sizeof(object_kv)` wrap on a 32-bit `size_t` once the count passes about 89 million entries. The old code wrapped from five bytes (`n * 48` with `n` up to 4 G); the new code needs a 4 GB document first, so the change is a strict improvement, but `grow_*` has no overflow check. Not built or run (no 32-bit target here).

## What I attacked and what held

| Suspicion | Probe | Result |
| --- | --- | --- |
| Header reserve overflows or wraps | read: `after = (m_end - m_current) - 1` is at least 0 (the pointer is on the last header byte, the length check ran before); `spare` is clamped at 0; `reserve <= tmp <= 2^32`, `reserve * 48 < 2^38` on 64-bit | holds on 64-bit; see SR5 for 32-bit |
| Claims drift or underflow (`m_claimed` vs the sum of stack claims) | `p2_invariants.cc`: after every feed `m_claimed == sum(claim)` and `m_claimed == 0` when a document completes, 200,000 seeds, ASan+UBSan | no violation |
| Nested headers reserve against the same bytes | read + `p3_nesting.cc` (`array32` max x 1 M) + `p2` | the child's `spare` is `after - m_claimed` and the parent's claim is still outstanding, so the child reserves 0; zone stays 1.2 bytes per input byte |
| Split-feed headers sum to more than linear | `p1_amplify.cc`, `p2` | linear, at most 126 to 163 per byte (SR1) |
| Over-claim starves valid documents | read | claim is one byte per item, never more than the unread bytes of a valid document, so valid one-piece documents still reserve exactly |
| Claim released without the byte consumed | read: release is at HEADER entry, after the `m_current == pe` early return; every item start consumes at least its first byte | none found |
| Stale stack entry reused with an old claim or capacity | read: `push_aggregate` overwrites container type, count, capacity and claim; a completed container's claim is 0 (`reserve <= n` items start) | holds; `m_stack[0]` stale after an exception is overwritten before any child starts |
| `grow_*` arithmetic | read: `cap = min(size + count, max(1, 2 * capacity))`; `count >= 1` so `cap >= size + 1`; never exceeds the declared `n`; memcpy of `size` items from the old block | holds |
| Zone rounding | read `allocate_expand` (chunk size doubles from 8 KB until it fits; the previous chunk's remainder is abandoned) | up to 2x per block; included in the 126 figure |
| `str32`/`bin32`/`ext32` | `p4_payload.cc`: 2 GB and 4 GB-2 headers with 1 MB of payload fed in 4 KB pieces | zero zone allocation until the payload is complete; `ext32` 0xffffffff on 64-bit awaits 4 GB and allocates nothing; per-type limits fire before any reservation |
| `unpack()`, `MsgPack::unserialise`, `msgpack_load` bypass | read: all call `detail::unpack_imp` -> `context::execute`; `object.hpp` and the adaptors copy already-parsed objects whose sizes are real; `rapidjson.hpp` allocates from JSON sizes already parsed | no bypass of the bound |
| `unpack_limit` order | `p4_payload.cc`: limit 100 vs header 1000 | `array_size_overflow` thrown, no allocation |
| Fuzz | branch target `msgpack_unpacker_fuzz` (ASan+UBSan, libFuzzer, seeded with `fuzz/corpus/unpacker`), 150 s | 1,330,081 runs, no finding, 667 coverage points |
| Branch tests | `msgpack_unpacker_limits_test` under ASan+UBSan | ok |

## Conditions

1. Update `docs/mechanisms/unpacker-reservation.md` (and the ledger's "per-byte bound" paragraph) with the measured figures: hostile up to about 126 to 163 bytes per input byte, honest streamed arrays 62 to 88 against 33 to 44 in one piece, and say that a caller's body cap must be sized from them (SR1 option 1). Or take option 2 and re-measure. Either way, tighten the chunked bound in the test and fuzz target to what the note states (SR3).
2. Xapiand: pass `msgpack_limits()` in `restore_documents` (SR2). Not a gate for this library branch; track it in the Xapiand pin bump (item 3 of `docs/status/phase7c-linux-gate-fixes.md`).

## Blocking items

None.

## Probes (kept in `.scratch/sec/`, ignored by git)

`build.sh` (ASan+UBSan build against `.scratch/build-asan`), `p1_amplify.cc`, `p2_invariants.cc` (uses `#define private public` to read `m_claimed` and the stack), `p3_nesting.cc`, `p4_payload.cc`, `p5_streamed_honest.cc`, `fuzz.log`, `fcorp/` (fuzz corpus copy). Build tree: `.scratch/build-asan` (clang 23.1.2, `-fsanitize=address,undefined -fno-sanitize-recover=undefined -O1`; probes at `-O2`). Re-run: `.scratch/sec/build.sh .scratch/sec/p1_amplify.cc .scratch/sec/p1_amplify -O2 && .scratch/sec/p1_amplify`.

## Not run

32-bit build (SR5 is by reading); TSan (the unpacker is single-threaded per instance); Linux, FreeBSD and arm64; the branch's other ten ctest targets under sanitizers (the ledger reports 11 of 11); the branch's mutant script; Xapiand end to end and a live `RESTORE` over HTTP; long fuzz (150 s only); performance of the reservation path.

## Re-check

Re-checker: sonnet, at msgpack:8ced372 (fixes msgpack:4fa54a7, msgpack:8ced372) and Xapiand `fix/restore-depth-limit` (Xapiand:65971dc6a). Fresh worktree, Homebrew LLVM 23.1.2, `-fsanitize=address,undefined -fno-sanitize-recover=undefined`. The reviewer's probes `p1` to `p6` were rebuilt against the fix; new probes `p7` to `p11` and `mc_inject.h` are kept in `.scratch/sec/` of the `limits` worktree.

### Verdicts

| Finding | Verdict | Evidence |
| --- | --- | --- |
| SR1 | CLOSED | `msgpack/unpack.hpp:469` (`grow`), `:267` (`free_grown_block`), `:451` (`set_block(nullptr)`). `p1_amplify`: hostile `array32`/`map32` nil items 35.0 bytes per input byte (was 126.3; one piece 32.0). `p5_streamed_honest`: 23 to 25 at 1 KB and 4 KB feeds (was 62 to 88), 41 to 44 in one piece. `p2_invariants`, 200,000 seeds: worst 32.3 (was 162.7), invariants hold. Live heap, not asked bytes (`p9_live`, ASan's count, 1 MB one byte per feed): 36.0 worst for array and map of nil, 22.0 for 1-char strings. `p6_strings`: 20 to 70 (the 4 KB feed of 1-item arrays is the 70). Revert: the branch test with `msgpack/unpack.hpp` from msgpack:698a518 fails 4 checks (`test_large_streamed_array_grows_in_steps` :344, `test_streamed_hostile_items_stay_under_the_bound` :399 twice, `test_streamed_array_with_strings_is_not_retained` :368). |
| SR2 | CLOSED | `src/database/handler.cc:988` on `fix/restore-depth-limit` builds the unpacker with `make_msgpack_unpacker()` (`src/database/utils.cc:248`: depth `MAX_JSON_DEPTH` = 200, the limits of the HTTP stream). `p11_restore` replays the `restore_documents` loop (reserve 1024, feed, `next`, copy into `MsgPack`) on `0x91` x 200000 + `0x01`: the default unpacker overflows the stack (ASan stack-overflow, the fixed crash), the limited one throws `depth size overflow` at the first feed. Xapiand itself was not built (see Not run). |
| SR3 | CLOSED | `test/unpacker_limits_test.cc:19` and `fuzz/unpacker_fuzz.cc:18`: `per_byte = 4 * sizeof(object)` = 96, applied once to chunked and one-piece alike (`slack + per_byte * len`); the `* 2` is gone from both. Measured worst 35 (hostile), 32 (random), 70 (container-heavy) all under it; the revert check above shows it fails at the old 126 shape. The documented figure in the note (96) is the asserted one. |
| SR4 | CLOSED | stated in `docs/mechanisms/unpacker-reservation.md` (a caller that keeps feeding gets the same error; reservations shrink to zero, slower not larger). |
| SR5 | CLOSED | `unpack.hpp:472` throws `std::bad_alloc` when `cap * sizeof(Item)` would wrap. By reading; no 32-bit build. |

### The new mechanism

1. **Finalizer order and double free.** The block is registered once, in the zone that is current at the first growth (`:480`), and `free_grown_block` frees `ptr` then the block (`:267`). Finalizers run in reverse registration order and the blocks hold only POD objects, so order does not matter. A stack entry's pointer is reset at every container header (`:451`, mutant `stale_block` dies with a use-after-free), a completed container's entry is popped, and the root entry `m_stack[0]` is reset before reuse (`test_a_second_document_does_not_reuse_the_storage_of_the_first`). No double free found.
2. **Aliasing under `realloc`.** Items are copied by value into the storage; nothing keeps a pointer to an element (`push_item` copies from `execute`'s local `obj` or from `sp.obj()` in the stack vector, and the parent's `ptr` is the only reference, rewritten at `:499` and `:504` in the same statement as the growth). ASan's `realloc` always moves the block, so a stale pointer is caught. `p7_alias`: 2,500 random nested documents (87.8 MB; array32/array16/fix headers, maps, strings, bin; random feed sizes 1 to 400; handles moved between vectors and read after the unpacker is gone) equal the one-piece parse, no report. With the realloc result dropped (`block->ptr = grown` removed) the same probe dies at once with a heap-buffer-overflow, so it has teeth. The 13 mutants of `tools/unpack_mutants.py` all fail under ASan+UBSan, the control passes.
3. **Exception safety.** `p8_inject` fails the k-th `malloc`/`realloc` for every k of a nested 13.8 KB document fed in 1, 3, 50 and 13,845 byte pieces (11,388 runs, with the unpacker destroyed after the throw, and with it fed the rest), LeakSanitizer on: no leak, no double free, no use after free. A mutant that skips `::free(block->ptr)` in the `push_finalizer` failure path is caught by the same sweep (46 leaks), so the sweep reaches that path. A failed `realloc` leaves `block->ptr` and the capacity as they were (`:490`).
4. **Moving the zone.** `release_zone()` runs only when a document completes, when every container is done; the finalizer array moves with the zone (`finalizer_array` move constructor). `msgpack_lifetime` and `msgpack_growth` pass.
5. **Branch tests, fuzz.** All 11 ctest targets pass under ASan+UBSan; `msgpack_unpacker_fuzz` 150 s on the branch corpus (552,438 runs, host busy), no finding.

### New findings

None blocking. Two informational:

1. **N1 (informational): the stack entry grew to 88 bytes.** `unpack_stack` gained `m_block` (80 to 88); `p3_nesting` on 1 MB of `0x91` shows 92.3 bytes per input byte for the stack (the note says 84). It only matters for depth-unlimited callers (SR2). The note's "84 measured" is for the 80-byte entry.
2. **N2 (informational): the counted bytes leave out malloc's per-block overhead for tiny grown containers.** Each container that outgrows its reservation costs an 8-byte `grown_block` (a 32-byte chunk in glibc), a block, and a 16-byte finalizer. Measured with the allocator's own in-use count and `maxrss` (`p10_small`, 3 million containers, header the last byte of a feed): `[0x91 c0]` 52.0 against 47.8 on msgpack:698a518, `[0x81 c0 c0]` 40.0 against 39.0, `[0x92 c0 c0]` 40.0 against 47.5 bytes per input byte. Not worse than before and under 96; macOS only, glibc could count more.

### Not run

Xapiand build and `harness/cli_roundtrip_check.sh` (SR2 is read plus the `p11` replay of the loop with the library); Linux, FreeBSD, arm64 and a libstdc++ build of msgpack:8ced372's `test/malloc_count.h`; a 32-bit build (SR5); TSan; performance of the grow path (no benchmark); fuzz longer than 150 s.
