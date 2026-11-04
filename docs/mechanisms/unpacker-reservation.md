# How the unpacker reserves storage for arrays and maps

`msgpack/unpack.hpp`: `detail::context` (the parser behind `msgpack::unpack` and the streaming `msgpack::unpacker`).

## The rule

An array or map header declares a count (`array32` and `map32` go to 4,294,967,295). The parser reserves storage for **at most what the bytes it has can hold**: an array item takes at least one byte, a map entry at least two. The rest of the storage is allocated as items arrive, so a few hostile header bytes never ask for gigabytes. `str`, `bin` and `ext` need no such rule: their payload is awaited whole (`pe - m_current < m_trail`) before the zone allocates it.

## How

```text
header read (m_current at its last byte)
  after   = bytes in the buffer past the header
  spare   = after - m_claimed          (0 when negative)
  reserve = min(n, spare / bytes_per_item)     1 for an array, 2 for a map
  storage = allocate(reserve)          the entry on the stack keeps capacity = reserve,
                                       claim = reserve * bytes_per_item; m_claimed += claim
item starts (a header byte read, in the HEADER state)
  the container's claim and m_claimed give back one byte
item pushed, size == capacity
  grow: capacity = min(declared, capacity * 1.5 + 1, at least 1). The first time the storage moves to a
  malloc'd block of its own (a finalizer of the zone frees it); after that it is realloc'd. The reserved
  block in the zone's chunk stays where it is (at most the bytes the header could justify).
```

`m_claimed` is the sum of the claims of the open containers. A header counts on `after - m_claimed`, not on `after`: bytes a container already counts on do not justify another container's storage, so nesting `array32` headers is linear in the input and not quadratic. A claim is given back when an item starts (its first byte is read), because that byte is no longer in `after`.

A valid document has at least one unread byte for every item still to come, so `reserve == n` and the storage is exact, as before; the only addition on that path is the comparison and the claim bookkeeping. A header that declares more than the buffer holds (a document fed in pieces, or a hostile one) gets a smaller block and grows in half steps as the items come, so the storage follows the bytes that arrived.

The grown storage is a block of its own, not zone chunk space, because the items an array holds put strings and nested storage in the zone after it: a block in a chunk could not be resized, and every block it outgrew would stay until the zone is released (with doubling, and the zone rounding each chunk up to a power of two, that was up to 126 bytes of malloc per input byte against 32 for the same document in one piece).

## Bounds

Measured as bytes asked of malloc per input byte (`test/malloc_count.h`; a realloc counts what it adds), release or ASan build, the probes of the security review:

| Shape | One piece | Streamed (earlier version) | Streamed |
| --- | --- | --- | --- |
| honest array of nil, count = items, 1 KB to 64 KB feeds | 32 to 44 | 62 to 88 | 23 to 25 |
| hostile `array32`/`map32` header, nil items, one byte per feed (worst over sizes) | 32 | 126 | 35 |
| hostile header, items that put a string or nested storage in the zone, 1 or 100 bytes per feed | 20 to 70 | 80 to 104 | 20 to 47 |
| 200,000 random adversarial streams (mixed headers, truncated, random feeds, depth limits on and off) | | 162 | 32 |

A streamed document costs about what it costs in one piece, because the storage ends at the items it holds and no block it outgrew stays behind. Size a body cap from the one-piece cost: 24 bytes an item (an array item is a 24-byte `msgpack::object`; a map entry is a 48-byte `object_kv` for at least two bytes), up to twice that for the zone's power-of-two chunks and the growth step. The test and the fuzz target assert `64 KB + 4 x sizeof(object)` = 96 bytes per input byte on every input, one piece or chunked. `unpack_limit` is unchanged and still checked first (`array_size_overflow` and the others).

The depth limit defaults to 0xffffffff; the stack of open containers (about 88 bytes per level, 84 measured per input byte for 1 MB of `0x91`) is not covered by the rule, so a caller reading untrusted input passes a depth limit, as Xapiand does (`MAX_JSON_DEPTH`; its `--restore` path did not until `fix/restore-depth-limit`). After an exception (a limit, a bad header) the unpacker keeps the claims of the containers it was in; a caller that keeps feeding it gets the same error again, and later reservations shrink to zero and grow instead, which is slower, not larger.

## Tests

- `msgpack_unpacker_limits`: hostile `array32`, `map32`, `array16`, `map16`, `str32`, `bin32`, `ext32` headers with no payload (one feed, `unpack()`, a few bytes behind, nested a thousand deep, split across feeds a byte at a time, after valid items); a megabyte behind a header (an array item is a byte, a map entry two); a valid document is stored without regrowth; random documents read the same in one piece, in every chunk size and at random split points; a 200,000-item array and a 100,000-string array streamed in 1 KB feeds; hostile headers followed by cheap items one byte per feed; two documents in a row through one unpacker.
- `tools/unpack_mutants.py`: thirteen edits of the mechanism (the reservation, the claims, the growth, the block of its own, a stale block across documents); each must fail that test.
- `fuzz/unpacker_fuzz.cc` (`-DMSGPACK_BUILD_FUZZ=ON`, clang): one piece against chunks must end the same way and neither may ask `malloc` for more than the bound.
