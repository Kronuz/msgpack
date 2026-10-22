# msgpack

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/msgpack/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/msgpack/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

A JSON-like value for C++20 that serializes to MessagePack: build it with
initializer lists, index it like a map or array, copy it cheaply, and patch it with
RFC 6902 JSON Patch. It is Xapiand's universal value type, extracted.

## ✨ Highlights

- 🧱 **`MsgPack`**: maps, arrays, strings, numbers, and booleans, with typed accessors
  and selectors.
- 🐄 **Copy-on-write**: copies share storage until one of them changes.
- 📦 **MessagePack on the wire**: `serialise()` and `MsgPack::unserialise()`.
- 🔁 **JSON both ways** through RapidJSON, accepting comments, trailing commas, and
  `NaN`/`Inf`.
- 🩹 **JSON Patch**: `add`, `remove`, `replace`, `move`, `copy`, `test`, plus `incr`
  and `decr`.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer. RapidJSON and seven Kronuz
libraries are fetched at configure time. CI builds it on Linux (x86_64, arm64), macOS
(arm64, x86_64), and FreeBSD, plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(msgpack
  GIT_REPOSITORY https://github.com/Kronuz/msgpack.git
  GIT_TAG        main)
FetchContent_MakeAvailable(msgpack)

target_link_libraries(my_app PRIVATE msgpack::msgpack)
```

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

	std::cout << copy.to_string() << '\n';   // the patched document as JSON
}
```

## 🎯 When to use it

- ✅ Schemaless documents that move between JSON and MessagePack, or Xapiand-compatible
  code.
- ❌ A modern MessagePack library for new code: this bundles msgpack-c 1.3.0, as
  Xapiand did. Prefer current msgpack-c or a dedicated serializer.

## 📚 Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): the value type, the bundled msgpack-c fork, the
  adaptors, and the dependencies.
- [AGENTS.md](AGENTS.md): repository map and invariants.

## 🛠️ Building and testing

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build
./build/msgpack_demo && ./build/msgpack_benchmark
```

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) first. CI must
pass on every platform.

## 📄 License

MIT, Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE). The bundled
msgpack-c 1.3.0 headers are under the Boost Software License 1.0; see
[LICENSE_1_0.txt](LICENSE_1_0.txt). Extracted from [Xapiand](https://github.com/Kronuz/Xapiand).
