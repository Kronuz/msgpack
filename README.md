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
  RapidJSON, and reading accepts comments, trailing commas, and `NaN`/`Inf`. JSON
  has no NaN or Infinity, so `to_string()` writes a float that is NaN or infinite as
  `null`, as `JSON.stringify` does, and the text is always JSON; reading the text
  back gives a null, not the number. `as_document()` keeps the number.
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
  as its text (`[1,"a"]`, `{"5":1}`, quoted; a number that is not finite inside it
  is `null`). A `bin` key is written as its bytes,
  as a `str` is. Two keys can give the same text (`5` and `"5"`), and then the
  JSON repeats the key.
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

`msgpack_benchmark` and `msgpack_compare` run the same operations on the same documents; the tables are the
502-byte record `med` of [`benchmarks/corpus.h`](benchmarks/corpus.h) (15 keys, nested maps and arrays, every
scalar type), in ns per call, as the median of nine repetitions, the smaller of two runs. A row is an operation:
`unserialise` is bytes to a value (`MsgPack::unserialise`, `msgpack::unpack`, `json::from_msgpack`,
`mpack_tree_parse`), `serialise` a value to bytes, `to_string` a value to JSON text (`Writer` over a DOM,
`json::dump`), "read, set one field, write" is those three steps in a row, and "`unserialise`, then visit every
node" reads and then walks the whole document. A `-` means the library has no such call in the benchmark. The
comparators are fetched by the benchmark targets only, at these versions:
[msgpack-c](https://github.com/msgpack/msgpack-c) 1.3.0 (the copy `MsgPack` is built on) and its current release
`cpp-9.0.0`, [nlohmann::json](https://github.com/nlohmann/json) 3.12.0,
[mpack](https://github.com/ludocode/mpack) 1.1.1, and [RapidJSON](https://github.com/Tencent/rapidjson) at the
commit the library pins (`24b5e7a`). `benchmarks/compare.py` checks that every library that reads the same bytes
produces the same document (a checksum over every node) and fails when two disagree. mpack reads into a tree that
points into the input and copies no string, so it is the floor for the first and last rows.

Apple M4 Pro (arm64, macOS 26), Apple clang 17.0.0, CMake Release (`-O3 -DNDEBUG`):

| Operation | MsgPack | msgpack-c 1.3.0 | msgpack-c 9.0.0 | nlohmann::json | mpack | RapidJSON |
|---|---:|---:|---:|---:|---:|---:|
| `unserialise` | 707 | 670 | 635 | 5,272 | 283 | - |
| `serialise` | 504 | 480 | 581 | 1,191 | - | - |
| `to_string` | 1,266 | - | - | 1,937 | - | 1,277 |
| read, set one field, write | 2,294 | - | - | 6,549 | - | - |
| `unserialise`, then visit every node | 4,903 | 1,035 | 995 | 5,796 | 829 | - |

Intel Core i9-9980HK (x86-64, macOS 15), Apple clang 17.0.0, CMake Release (`-O3 -DNDEBUG`):

| Operation | MsgPack | msgpack-c 1.3.0 | msgpack-c 9.0.0 | nlohmann::json | mpack | RapidJSON |
|---|---:|---:|---:|---:|---:|---:|
| `unserialise` | 1,255 | 1,137 | 1,198 | 13,392 | 596 | - |
| `serialise` | 765 | 648 | 1,268 | 3,502 | - | - |
| `to_string` | 2,009 | - | - | 4,000 | - | 1,785 |
| read, set one field, write | 6,621 | - | - | 17,063 | - | - |
| `unserialise`, then visit every node | 19,825 | 1,684 | 1,744 | 14,587 | 1,563 | - |

```sh
cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DMSGPACK_BUILD_BENCH=ON
cmake --build build-bench --target msgpack_benchmark msgpack_compare
./build-bench/msgpack_benchmark > ours.txt
./build-bench/msgpack_compare > compare.txt
python3 benchmarks/compare.py ours.txt compare.txt
```

`MsgPack` takes 2.1 to 2.5 times mpack's time to read the record and 1.1 times msgpack-c's; a read followed
by a walk of every node takes 5.9 times (M4 Pro) and 12.7 times (i9) mpack's, because the first visit of a map
or array fills its index. Writing and JSON text stay within 1.2 times the fastest comparator, and read, set one
field, write takes 0.35 to 0.39 of nlohmann's time.
[docs/perf/survey-2026-10.md](docs/perf/survey-2026-10.md) has all four documents on both
machines, every operation, and where the time goes.

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
