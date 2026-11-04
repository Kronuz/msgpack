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
