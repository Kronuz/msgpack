# Architecture

`msgpack` has four layers. The extraction keeps them together because the boundaries are real but the use is one family.

```text
msgpack-c v1.3.0 headers
        |
        v
MsgPack COW value type
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

`msgpack.h` wraps `msgpack::object` with a copy-on-write value API. Maps and arrays are lazily materialized into C++ containers when accessed, then written back into the underlying msgpack object storage. The original growth factors, reserve paths, undefined extension type, selector syntax, conversions, and serialise/unserialise format are preserved.

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
- **COW semantics are preserved.** Copy, lazy fill, reserve, map key storage, and parent/key update behavior are lifted from Xapiand.
- **The msgpack-c fork stays forked.** Its exceptions inherit `BaseException` by design.
- **RapidJSON stays lenient.** The parse flags for comments, trailing commas, and NaN/Inf are part of Xapiand behavior.
- **Names and layout stay stable.** Consumers include the same file names they did inside Xapiand.

## What's included

- **`msgpack/` + `msgpack.hpp`**: upstream `msgpack-c` v1.3.0 headers, with Xapiand's forked `msgpack/exception.hpp` preserved. The fork makes msgpack exceptions inherit `BaseException`.
- **`msgpack.h`**: Xapiand's `MsgPack` value type, with JSON-like map/array access, serialise/unserialise helpers, typed accessors, selectors, and copy-on-write storage.
- **`xchange/rapidjson.hpp` and `xchange/string_view.hpp`**: msgpack adaptors for RapidJSON and `std::string_view`.
- **`msgpack_patcher.{h,cc}`**: RFC 6902 JSON-Patch operations (`add`, `remove`, `replace`, `move`, `copy`, `test`, plus Xapiand's `incr`/`decr`).

## Dependencies

CMake fetches the Kronuz support libraries at `main`: `atomic-shared-ptr`, `enum-reflection`, `strict-stox`, `located-exception`, `perfect-hash`, `hashes`, and `repr`. RapidJSON is fetched at `v1.1.0` and kept header-only. The library also carries Xapiand's `likely.h` locally.

The build defines RapidJSON's default parse flags as Xapiand did, accepting comments, trailing commas, and `NaN`/`Inf`.

Use local dependency checkouts with the normal FetchContent overrides, for example
`-DFETCHCONTENT_SOURCE_DIR_LOCATED_EXCEPTION=/path/to/located-exception`.
