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
  grow: capacity = min(declared, 2 * capacity, at least 1); copy; the old block stays in the zone
```

`m_claimed` is the sum of the claims of the open containers. A header counts on `after - m_claimed`, not on `after`: bytes a container already counts on do not justify another container's storage, so nesting `array32` headers is linear in the input and not quadratic. A claim is given back when an item starts (its first byte is read), because that byte is no longer in `after`.

A valid document has at least one unread byte for every item still to come, so `reserve == n` and the storage is exact, as before; the only addition on that path is the comparison and the claim bookkeeping. A header that declares more than the buffer holds (a document fed in pieces, or a hostile one) gets a smaller block and grows by doubling as the items come, so the storage follows the bytes that arrived: at most twice the items read, and the blocks it outgrew stay in the zone until the object is released.

## Bounds

Storage for a container never exceeds about 24 bytes per input byte (an array item is a 24-byte `msgpack::object`; a map entry is a 48-byte `object_kv` for two bytes). With the zone rounding a large block up to a power of two and the outgrown blocks kept, the worst case is under 8 x `sizeof(object_kv)` = 384 bytes per input byte; `test/unpacker_limits_test.cc` and `fuzz/unpacker_fuzz.cc` assert that bound on every input. `unpack_limit` is unchanged and still checked first (`array_size_overflow` and the others). The depth limit defaults to 0xffffffff; the stack of open containers (about 80 bytes per level) is not covered by the rule, so a caller reading untrusted input passes a depth limit, as Xapiand does (`MAX_JSON_DEPTH`).

## Tests

- `msgpack_unpacker_limits`: hostile `array32`, `map32`, `array16`, `map16`, `str32`, `bin32`, `ext32` headers with no payload (one feed, `unpack()`, a few bytes behind, nested a thousand deep, split across feeds a byte at a time, after valid items); a megabyte behind a header (an array item is a byte, a map entry two); a valid document is stored without regrowth; random documents read the same in one piece, in every chunk size and at random split points; a 200,000-item array streamed in 1 KB feeds.
- `tools/unpack_mutants.py`: eleven edits of the mechanism; each must fail that test.
- `fuzz/unpacker_fuzz.cc` (`-DMSGPACK_BUILD_FUZZ=ON`, clang): one piece against chunks must end the same way and neither may ask `malloc` for more than the bound.
