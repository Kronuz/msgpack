# Architecture

`msgpack` has four layers. The extraction keeps them together because the boundaries are real but the use is one family.

```text
msgpack-c v1.3.0 headers
        |
        v
MsgPack value type
        |
        +--> xchange adaptors (RapidJSON, string_view)
        |
        v
RFC 6902 patcher over MsgPack
```

## Layer 1: msgpack-c core

`msgpack/` and `msgpack.hpp` are the vendored upstream `msgpack-c` v1.3.0 header set. Xapiand forked `msgpack/exception.hpp`, and this library preserves that fork: `msgpack::type_error`, `unpack_error`, `parse_error`, and the size-overflow errors all inherit `BaseException` from `exception.h`.

That choice is deliberate. Xapiand catches these exceptions through its own hierarchy, so swapping to stock `std::exception` types would change behavior.

## Layer 2: `MsgPack`

`msgpack.h` wraps `msgpack::object` with a JSON-like value API. Maps and arrays are lazily materialized into C++ containers when accessed, then written back into the underlying msgpack object storage. The original growth factors, reserve paths, undefined extension type, selector syntax, conversions, and serialise/unserialise format are preserved.

A value owns a `Body`: a `msgpack::zone` (the arena that holds the tree), the root `msgpack::object`, and the lazily built index of its maps and arrays. Every child of a map or array shares its parent's zone and root through `shared_ptr`, so a part taken out of a value keeps the whole tree alive.

**A map holds each key once.** A document may repeat a key (the wire format allows it). When a map is first filled, the last value of a repeated key wins and takes the position of the key's first occurrence: `{"a":1,"a":2,"b":3,"c":4}` becomes `{"a":2,"b":3,"c":4}`. The merge edits the tree, so `serialise()`, copies and `hash()` after it see the merged map, and a map that nothing has filled still serialises as it came (and `size()` of it counts the entries as they came). `test/merge_test.cc` holds this against a reference built on `std::map` (`test/model.h`). `hash()` of a map reached through maps has the same value as the merged map; a map inside an array is hashed as its packed bytes, merged or not.

**Growing an array moves its elements.** The element bodies of an array live in a `std::vector<MsgPack>`, and `MsgPack(MsgPack&&)` is `noexcept` so that the vector moves them when it reallocates. A copy would give each element a zone of its own, which the element's children then write strings into and which the vector frees with the old elements (`test/growth_test.cc`, under AddressSanitizer).

**Copies are deep.** `MsgPack(const MsgPack&)` and `operator=(const MsgPack&)` allocate a new zone and copy every node into it (`object_with_zone`). The copy and its source share nothing. A copy costs time and memory in proportion to the size of the value; a move transfers the `shared_ptr<Body>` and costs one pointer swap.

**The tree is the source of truth.** The index of `Body` objects is built on first touch of a map or array and always points into the tree, so an operation that only reads the data can read the tree directly:

- `unserialise` reads into the new body's own zone (`Body(unpack_tag, ...)`). The unpacked tree is the value's tree, with no second copy. Strings and binaries are copied into the zone, so a value never points into the input buffer.
- `hash()` computes the XOR over the entries of a map, `hash(key) ^ hash(value)`, with values that are maps hashed the same way and every other value hashed as its packed bytes (a 256-byte stack buffer, the heap past that). It does this on the tree, with no `Body` per node, when the maps it reaches through maps have only string keys and no repeated key. Otherwise it fills the map first, as it always did: filling throws for a key that is not a string and merges repeated keys in the tree, so the hash, and what `serialise()` and a copy give afterwards, depend on it.
- `to_string()` makes the calls on a RapidJSON `Writer` (or `PrettyWriter`) that `rapidjson::Document::Accept` would make for the document `as_document()` builds, in the same order and stopping at the first `false`, so a `NaN` or `Inf` leaves the same partial text.

The public type remains in the global namespace and the header name stays `msgpack.h` so existing consumers only need this repo on the include path.

## Layer 3: xchange adaptors

`xchange/rapidjson.hpp` converts between msgpack objects and RapidJSON values/documents. It keeps the xpol attribution from the original file. `xchange/string_view.hpp` adds zero-copy `std::string_view` conversion and packing. Its one extraction delta is the documented `THROW(msgpack::type_error)` to `throw msgpack::type_error()` change, because the adaptor has no app exception macro of its own.

## Layer 4: patcher

`msgpack_patcher.{h,cc}` applies JSON-Patch operations over `MsgPack`. It uses RapidJSON's JSON Pointer tokenizer for RFC 6901 path splitting, `strict-stox` for array indices, `perfect-hash`/`hashes` for operation dispatch, and `repr` for error text.

## Exception seam

`msgpack.h` and `msgpack_patcher.*` include exceptions through `MSGPACK_EXCEPTION_HEADER` when it is defined. Standalone builds use `msgpack_exception.h`, which includes `located-exception`'s `exception.h`, adds `ClientError`, and exposes lowercase aliases for the wrapper's historical `THROW(out_of_range, ...)` / `THROW(invalid_argument, ...)` use.

The vendored `msgpack/exception.hpp` still includes `exception.h` directly. In standalone builds that resolves to `located-exception`. In Xapiand, `src/exception.h` stays earlier on the include path, so msgpack exceptions keep Xapiand's real `BaseException` hierarchy.

## Invariants

- **Wire format is byte-identical.** Do not change the `UNDEFINED` extension type, msgpack-c pack/unpack behavior, or the serialised bytes emitted by `MsgPack::serialise()`.
- **Copy semantics are preserved.** A copy is deep. Lazy fill, reserve, map key storage, and parent/key update behavior are lifted from Xapiand.
- **`unserialise`, `hash()` and `to_string()` give what the earlier implementation gave**, for every input except the two that implementation got wrong (a repeated key in a map, and growing an array of maps; see the CHANGELOG): the same values, offsets, exceptions, and the same changes to the tree. `test/diff_test.cc` checks them against the frozen `test/frozen_msgpack.h`, reading a document that repeats a key as the merged document on the frozen side. `test/merge_test.cc` and `test/growth_test.cc` hold the two fixes to a model.
- **The msgpack-c fork stays forked.** Its exceptions inherit `BaseException` by design.
- **RapidJSON stays lenient.** The parse flags for comments, trailing commas, and NaN/Inf are part of Xapiand behavior.
- **Names and layout stay stable.** Consumers include the same file names they did inside Xapiand.

## What's included

- **`msgpack/` + `msgpack.hpp`**: upstream `msgpack-c` v1.3.0 headers, with Xapiand's forked `msgpack/exception.hpp` preserved. The fork makes msgpack exceptions inherit `BaseException`.
- **`msgpack.h`**: Xapiand's `MsgPack` value type, with JSON-like map/array access, serialise/unserialise helpers, typed accessors, selectors, and deep-copy storage.
- **`xchange/rapidjson.hpp` and `xchange/string_view.hpp`**: msgpack adaptors for RapidJSON and `std::string_view`.
- **`msgpack_patcher.{h,cc}`**: RFC 6902 JSON-Patch operations (`add`, `remove`, `replace`, `move`, `copy`, `test`, plus Xapiand's `incr`/`decr`).

## Dependencies

CMake fetches the Kronuz support libraries at `main`: `atomic-shared-ptr`, `enum-reflection`, `strict-stox`, `located-exception`, `perfect-hash`, `hashes`, and `repr`. RapidJSON is fetched at `v1.1.0` and kept header-only. The library also carries Xapiand's `likely.h` locally.

The build defines RapidJSON's default parse flags as Xapiand did, accepting comments, trailing commas, and `NaN`/`Inf`.

Use local dependency checkouts with the normal FetchContent overrides, for example
`-DFETCHCONTENT_SOURCE_DIR_LOCATED_EXCEPTION=/path/to/located-exception`.
