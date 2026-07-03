# msgpack

A batteries-included MessagePack value library extracted from [Xapiand](https://github.com/Kronuz/Xapiand). It bundles the Xapiand fork of `msgpack-c` v1.3.0, keeps the `MsgPack` copy-on-write value type, ships the RapidJSON and `std::string_view` adaptors, and includes the RFC 6902 JSON-Patch helper over a `MsgPack` DOM.

The file layout is intentionally the same as Xapiand's original tree: include `msgpack.h`, `msgpack.hpp`, `msgpack/...`, `msgpack_patcher.h`, or `xchange/...` exactly as before.

## Usage

```cpp
#include "msgpack.h"
#include "msgpack_patcher.h"

MsgPack doc = {
	{ "name", "Neo" },
	{ "tags", { "chosen" } },
};

doc["tags"].push_back("operator");
std::string wire = doc.serialise();
MsgPack copy = MsgPack::unserialise(wire);

MsgPack patch = MsgPack::ARRAY();
patch.push_back(MsgPack({ { "op", "replace" }, { "path", "/name" }, { "value", "Trinity" } }));
apply_patch(patch, copy);
```

## What's included

- **`msgpack/` + `msgpack.hpp`**: upstream `msgpack-c` v1.3.0 headers, with Xapiand's forked `msgpack/exception.hpp` preserved. The fork makes msgpack exceptions inherit `BaseException`.
- **`msgpack.h`**: Xapiand's `MsgPack` value type, with JSON-like map/array access, serialise/unserialise helpers, typed accessors, selectors, and copy-on-write storage.
- **`xchange/rapidjson.hpp` and `xchange/string_view.hpp`**: msgpack adaptors for RapidJSON and `std::string_view`.
- **`msgpack_patcher.{h,cc}`**: RFC 6902 JSON-Patch operations (`add`, `remove`, `replace`, `move`, `copy`, `test`, plus Xapiand's `incr`/`decr`).

## Dependencies

CMake fetches the Kronuz support libraries at `main`: `atomic-shared-ptr`, `enum-reflection`, `strict-stox`, `located-exception`, `perfect-hash`, `hashes`, and `repr`. RapidJSON is fetched at `v1.1.0` and kept header-only. The library also carries Xapiand's `likely.h` locally.

The build defines RapidJSON's default parse flags as Xapiand did, accepting comments, trailing commas, and `NaN`/`Inf`.

## Build / test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/msgpack_demo
./build/msgpack_benchmark
```

Use local dependency checkouts with the normal FetchContent overrides, for example `-DFETCHCONTENT_SOURCE_DIR_LOCATED_EXCEPTION=/path/to/located-exception`.

## License

The Xapiand wrapper, patcher, adaptors, and seams are MIT licensed. The bundled `msgpack-c` v1.3.0 headers are Boost Software License 1.0; see `LICENSE_1_0.txt` and the copyright headers in `msgpack/` and `msgpack.hpp`.
