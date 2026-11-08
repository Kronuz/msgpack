# AGENTS

Working notes for this library. Read `ARCHITECTURE.md` for the data model and exception seam.

## What this is

The MsgPack keystone extracted from Xapiand: the bundled `msgpack-c` v1.3.0 fork, Xapiand's `MsgPack` copy-on-write value type, the RapidJSON and `std::string_view` adaptors, and the JSON-Patch helper.

## File map

```text
msgpack.h                     MsgPack COW value API. Header-only, mostly verbatim.
msgpack.hpp                   msgpack-c umbrella header.
msgpack/                      Bundled msgpack-c v1.3.0 headers. exception.hpp is forked.
msgpack_patcher.h/.cc         RFC 6902 patcher over MsgPack.
xchange/rapidjson.hpp         RapidJSON adaptor, with xpol attribution.
xchange/string_view.hpp       string_view adaptor.
msgpack_exception.h           Standalone exception seam over located-exception.
likely.h                      Vendored likely()/unlikely().
examples/demo.cc              Build, serialise, JSON round-trip, patch.
test/test.cc                  ctest coverage for value, COW, patch, xchange.
benchmarks/pack_unpack.cc     Small pack/unpack throughput harness.
```

## Dependencies

CMake fetches `atomic-shared-ptr`, `enum-reflection`, `strict-stox`, `located-exception`, `perfect-hash`, `hashes`, `repr`, and RapidJSON `v1.1.0`. Use `FETCHCONTENT_SOURCE_DIR_<NAME>` to point at local checkouts while working across the Kronuz family.

## Invariants

- **Behavior-preserving extraction.** Do not rewrite algorithms or simplify the wrapper. Keep the original wire format, selector behavior, growth strategy, and lazy fill behavior.
- **The exception fork is load-bearing.** `msgpack/exception.hpp` must keep inheriting `BaseException`. Xapiand relies on catching these types through its hierarchy.
- **The seam is compile-time.** Standalone uses `msgpack_exception.h`; Xapiand defines `MSGPACK_EXCEPTION_HEADER` to its `src/exception.h`.
- **RapidJSON parse flags are part of behavior.** Keep comments, trailing commas, and NaN/Inf accepted.
- **Keep file names stable.** `msgpack.h`, `msgpack.hpp`, `msgpack_patcher.h`, and `xchange/...` are intentionally not renamed.

## Extraction deltas

- Removed `#include "config.h"` from `msgpack.h`; the referenced `XAPIAND_LUA` macro was dead in this header.
- Routed `msgpack.h` and `msgpack_patcher.*` through `MSGPACK_EXCEPTION_HEADER` with a standalone default seam.
- Changed the one `THROW(msgpack::type_error)` in `xchange/string_view.hpp` to `throw msgpack::type_error();` as called out in Xapiand's extraction plan.

## Build / test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/msgpack_demo
./build/msgpack_benchmark
```

For Xapiand integration, configure Xapiand with `-DFETCHCONTENT_SOURCE_DIR_MSGPACK=/path/to/msgpack` and define `MSGPACK_EXCEPTION_HEADER` to Xapiand's `src/exception.h`.

Xapiand also defines `XAPIAN_LIB_BUILD=1` explicitly now. The old in-tree `msgpack.h` pulled in generated `config.h`, which incidentally made Xapian's internal `DB_ACTION_MASK_` visible before some Xapian headers were included. The standalone header deliberately does not include `config.h`, so the host owns that define.

## Bounded target

`msgpack/bounded.hpp` contains the flat lexical decoder, fixed scratch and the append-to-write packer adapter. `msgpack::bounded` configures before DOM dependency population. `MSGPACK_BUILD_DOM` defaults to ON; OFF must fetch none of the DOM dependencies. `test/bounded.cc`, `test/bounded_vectors.py` and `examples/bounded.cc` cover this target. String-view overloads on the adapter reuse the existing header/body encoders; keep the bundled `msgpack/pack.hpp` unchanged.

Keep schema, nesting and aggregate validation in the application. Decoder `finish()` checks partial tokens only. Preserve allocator state across token emission, fence malformed or failed decoding, and keep fixed scratch free of allocation and unused-prefix copies. The bounded target uses standard exceptions and must not pull in `exception.h`; the existing parser and DOM exception hierarchy remains unchanged.
