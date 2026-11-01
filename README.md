# msgpack

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/msgpack/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/msgpack/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

JSON-like value type for C++20: MessagePack and JSON round trips, map and array indexing, and RFC 6902 JSON Patch.
`MsgPack` is built from initializer lists, indexed like a map or an array, and
kept as MessagePack objects from a bundled msgpack-c 1.3.0, with RapidJSON on the
JSON side. It fits schemaless documents that move between JSON and MessagePack,
and code that shares Xapiand's value type.

## ✨ Highlights

- **One value type.** `MsgPack` holds maps, arrays, strings, numbers, booleans and
  null, with typed accessors (`as_str()`, `is_map()`, ...), paths and selectors.
- **Deep copies.** A copy duplicates the whole tree into its own zone, so a copy
  and its source share nothing and a change to one never shows in the other; a
  move hands the storage over, and a reference (`MsgPack&`, `doc["key"]`) aliases
  the same value.
- **MessagePack on the wire.** `serialise()` writes the bytes and
  `MsgPack::unserialise()` reads them.
- **JSON both ways.** `to_string()` and `as_document()` write JSON through
  RapidJSON, and reading accepts comments, trailing commas, and `NaN`/`Inf`.
- **`update()` merges a map into a value.** Each entry is applied in order: an
  undefined value erases the key, a key the value lacks is added, two maps merge,
  anything else replaces, and a value that is not a map replaces the whole target.
- **All or nothing for bad arguments.** An `update()` that throws for a reason of
  its arguments (a map with a key that is not a string in the way of the merge, a
  target that cannot be read by key, a constant or locked value) has changed
  nothing, whichever entries came before the one that throws.
- **JSON Patch.** `apply_patch` runs RFC 6902 `add`, `remove`, `replace`, `move`,
  `copy` and `test`, plus `incr` and `decr`.

## 🚀 Quick start

```cpp
#include "msgpack.h"
#include "msgpack_patcher.h"

#include <iostream>

int main() {
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

	std::cout << copy.to_string() << '\n';  // {"name":"Trinity","tags":["chosen","operator"]}
}
```

[`examples/demo.cc`](examples/demo.cc) is a runnable example.

## 📦 Installation

Requires a C++20 compiler and CMake 3.16 or newer; it fetches RapidJSON and seven
sibling libraries (atomic-shared-ptr, enum-reflection, strict-stox,
located-exception, constexpr-phf, hashes, repr) and compiles one source file. CI
builds it on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus
ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(msgpack
  GIT_REPOSITORY https://github.com/Kronuz/msgpack.git
  GIT_TAG        ea15380e068f545b29a34768f08e5534c6ae88ca)  # or main for the tip
FetchContent_MakeAvailable(msgpack)

target_link_libraries(my_app PRIVATE msgpack::msgpack)
```

```cpp
#include "msgpack.h"
#include "msgpack_patcher.h"  // apply_patch
```

## 🎯 When to use it

Use it for schemaless documents that move between JSON and MessagePack, and for
code that shares Xapiand's value type.

Not for:

- **Documents that keep repeated keys.** A map holds each key once: when a
  document repeats a key, the last value wins and the entry keeps the position of
  the first. The merge happens when the map is first read or changed through the
  API (a lookup, a walk, `hash()`, `erase`); `serialise()` and `size()` of a map
  nothing has touched give the document as it came.
- **Maps keyed by anything but strings.** Reading or changing such a map by key
  throws `MAP keys must be of type STR` and changes nothing, so the next call
  throws the same way.
- **JSON with keys that are not strings.** JSON has no form for them, so
  `to_string()` and `as_document()` write each as its own JSON text: `5` as `"5"`,
  `-1.5` as `"-1.5"`, `true` as `"true"`, `null` and an ext as `"null"`, a float
  that is not finite as `"NaN"`, `"Infinity"` or `"-Infinity"`, an array or a map
  as its text (`[1,"a"]`, `{"5":1}`, quoted). A `bin` key is written as its bytes,
  as a `str` is. Two keys can give the same text (`5` and `"5"`), and then the
  JSON repeats the key.
- **JSON of values that are not finite.** A float value that is NaN or infinite
  stops the text of `to_string()` at that value, so the text is not JSON.
- **Updates that are atomic for memory.** An allocation that fails part way
  through `update()` leaves the entries before it applied; the value stays
  consistent and usable.
- **Fast visits of a freshly read value.** The first read or change of a map or
  array through the API builds its index, a `Body` per node, which makes visiting
  a freshly read value 5 to 10 times slower than in mpack or msgpack-c.
- **A current MessagePack library.** It bundles msgpack-c 1.3.0, as Xapiand does;
  new code that only serializes is better served by current msgpack-c or a
  dedicated serializer.

## ⚡ Performance

ns per call for the 502-byte record of `benchmarks/corpus.h`, median of nine
repetitions. Apple M1 Pro with Apple clang 21, CMake Release (`-O3 -DNDEBUG`).

| Operation | MsgPack | Compared with |
| --- | --- | --- |
| `unserialise` | 891 | msgpack-c 1.3.0 `unpack` 839, nlohmann::json 3.12.0 `from_msgpack` 5,597, mpack 1.1.1 383 |
| `serialise` | 602 | msgpack-c 1.3.0 `pack` 550, nlohmann::json 3.12.0 `to_msgpack` 1,507 |
| `to_string` | 1,515 | RapidJSON `Writer` over a DOM 1,516, nlohmann::json 3.12.0 `dump` 2,380 |
| read, set one field, write | 2,731 | nlohmann::json 3.12.0 7,076 |
| `unserialise`, then visit every node | 5,877 | msgpack-c 1.3.0 1,362, mpack 1.1.1 1,090 |

```sh
cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DMSGPACK_BUILD_BENCH=ON
cmake --build build-bench --target msgpack_benchmark msgpack_compare
./build-bench/msgpack_benchmark > ours.txt
./build-bench/msgpack_compare > compare.txt
python3 benchmarks/compare.py ours.txt compare.txt
```

[docs/perf/survey-2026-10.md](docs/perf/survey-2026-10.md) has all four documents
on the M1 Pro and an Intel Core i9-9980HK, every operation, and where the time
goes.

## 📚 Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): the value type, the bundled msgpack-c fork,
  the adaptors, and the dependencies.
- [docs/perf/](docs/perf/survey-2026-10.md): the benchmark method and the full
  tables.
- [CHANGELOG.md](CHANGELOG.md): what changed, with the tests behind each change.
- [AGENTS.md](AGENTS.md): repository map and invariants.
- [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries): the catalog.

## 🛠️ Building and testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

The tests check the value against a model of maps, merges, updates and JSON
keys, and against a frozen copy of the Xapiand implementation. Add `-DMSGPACK_BUILD_BENCH=ON` to
build `msgpack_benchmark` and `msgpack_compare`, which fetch msgpack-c,
nlohmann::json and mpack at pinned tags.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) and the
catalog's [rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo and Copyright (c) 2015-2020 Dubalu
LLC. See [LICENSE](LICENSE). The bundled msgpack-c 1.3.0 headers are under the
Boost Software License 1.0; see [LICENSE_1_0.txt](LICENSE_1_0.txt). Extracted
from [Xapiand](https://github.com/Kronuz/Xapiand).
