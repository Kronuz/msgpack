# AGENTS

Working notes for this library. Read `ARCHITECTURE.md` for the data model and exception seam.

## What this is

The MsgPack keystone extracted from Xapiand: the bundled `msgpack-c` v1.3.0 fork, Xapiand's `MsgPack` value type (a copy is a deep copy), the RapidJSON and `std::string_view` adaptors, and the JSON-Patch helper.

## File map

```text
msgpack.h                     MsgPack value API. Header-only, mostly verbatim.
msgpack.hpp                   msgpack-c umbrella header.
msgpack/                      Bundled msgpack-c v1.3.0 headers. exception.hpp is forked.
msgpack_patcher.h/.cc         RFC 6902 patcher over MsgPack.
xchange/rapidjson.hpp         RapidJSON adaptor, with xpol attribution.
xchange/string_view.hpp       string_view adaptor.
msgpack_exception.h           Standalone exception seam over located-exception.
likely.h                      Vendored likely()/unlikely().
examples/demo.cc              Build, serialise, JSON round-trip, patch.
test/test.cc                  ctest coverage for value, copies, patch, xchange.
test/diff_test.cc             ctest msgpack_diff: random documents and operations against the frozen test/frozen_msgpack.h.
test/frozen_msgpack.h         The earlier msgpack.h with MsgPack renamed MsgPackOld. The oracle: never edit it.
test/lifetime_test.cc         ctest msgpack_lifetime: values outlive the input, the other values and threads.
test/alloc_test.cc            ctest msgpack_alloc: allocation counts of unserialise, hash() and to_string() against the frozen copy.
tools/mutants.py              Edits the implementation 15 ways and puts the previous unserialise, hash() and to_string() back; a test must fail on each.
benchmarks/pack_unpack.cc     Small pack/unpack throughput harness.
CHANGELOG.md                  Dated changes, numbers and known problems.
```

## Dependencies

CMake fetches `atomic-shared-ptr`, `enum-reflection`, `strict-stox`, `located-exception`, `perfect-hash`, `hashes`, `repr`, and RapidJSON `v1.1.0`. Use `FETCHCONTENT_SOURCE_DIR_<NAME>` to point at local checkouts while working across the Kronuz family.

## Invariants

- **Behavior-preserving extraction.** Do not rewrite algorithms or simplify the wrapper. Keep the original wire format, selector behavior, growth strategy, and lazy fill behavior.
- **`unserialise`, `hash()` and `to_string()` work on the msgpack tree and must give what `test/frozen_msgpack.h` gives**: values, offsets, exceptions, and the changes to the tree. `hash()` takes the tree path only for a map whose keys, and those of the maps nested in it, are strings and do not repeat; a repeated key sends it to the filling code, which merges the repeats in the tree (and which is not correct, see CHANGELOG). Do not make the tree path handle repeats: the result of `hash()`, `serialise()` and copies afterwards depends on the merge. Run `msgpack_diff` after any change to those three, and the lifetime test under AddressSanitizer after one to `Body`.
- **The exception fork is load-bearing.** `msgpack/exception.hpp` must keep inheriting `BaseException`. Xapiand relies on catching these types through its hierarchy.
- **The seam is compile-time.** Standalone uses `msgpack_exception.h`; Xapiand defines `MSGPACK_EXCEPTION_HEADER` to its `src/exception.h`.
- **RapidJSON parse flags are part of behavior.** Keep comments, trailing commas, and NaN/Inf accepted.
- **Keep file names stable.** `msgpack.h`, `msgpack.hpp`, `msgpack_patcher.h`, and `xchange/...` are intentionally not renamed.

## Extraction deltas

- Removed `#include "config.h"` from `msgpack.h`; the referenced `XAPIAND_LUA` macro was dead in this header.
- Routed `msgpack.h` and `msgpack_patcher.*` through `MSGPACK_EXCEPTION_HEADER` with a standalone default seam.
- `msgpack/unpack.hpp` and `msgpack/object.hpp` skip `memcpy` for an empty string or bin (a null source with size zero is undefined; UBSan reports it).
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
