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

Requires a C++20 compiler and CMake 3.16 or newer. The default DOM build fetches RapidJSON and seven Kronuz libraries. Bounded-only configuration fetches none. CI builds the library on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan, and TSan builds.

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

## Bounded lexical decoding

`msgpack::bounded` is a header-only target using bundled headers and the standard library. Configure with `-DMSGPACK_BUILD_DOM=OFF` to omit the COW value, patcher and all fetched dependencies. The existing DOM target and dependencies remain enabled by default.

```cpp
#include <msgpack/bounded.hpp>

msgpack::bounded::InlineBytes<64> output;
msgpack::bounded::AppendPacker pack(output);
pack.pack_array(2).pack_uint64(42);
pack.pack_str(std::string_view("hello"));
msgpack::bounded::BasicDecoder<msgpack::bounded::InlineBytes<32>> decoder;
auto input = std::span<const char>(output.data(), output.size());
while (!input.empty()) {
    std::size_t budget = 1;
    if (auto token = decoder.next(input, budget)) {
        // Validate the application's tuple shape and values here.
    }
}
decoder.finish();
```

The decoder emits flat array headers, integers, strings, binary fields, booleans and nil. It enforces supplied lexical limits before growing payload storage, emits at most one owning token per call and consumes at most the supplied input-byte budget. Maps, floats and extensions remain outside this narrow decoder. `finish()` rejects partial tokens; it does not validate a complete recursive MessagePack object. Empty input and a bare array header can therefore finish successfully. The application validates nesting, tuple arity and aggregate counts.

`InlineBytes<N>` supplies allocation-free storage with populated-prefix copies. `BasicDecoder<std::string>` is the dynamic default; allocator-bearing storage can be supplied explicitly as the second constructor argument. Emission and token reset preserve that allocator. Allocator resources must outlive their allocations unless the supplied allocator itself retains ownership. Byte accounting does not bound arbitrary allocator work or application schema processing.

`AppendPacker` adapts an output's synchronous `append(string_view)` to the existing packer's `write` seam. It is noncopyable because the packer borrows its stream. Its `pack_str(string_view)` and `pack_bin(string_view)` overloads check the 32-bit wire length and call the existing header/body encoders. Existing integer-size overloads keep their behavior. Output errors may leave partial caller-owned scratch; publish only a completed encoding. The bounded decoder uses standard exceptions, independently of the existing DOM exception seam.

See [examples/bounded.cc](examples/bounded.cc) and [test/bounded.cc](test/bounded.cc). New bounded code is MIT licensed; the bundled packer retains its existing Boost license.
