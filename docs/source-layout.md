# Source layout

`msgpack.h` is the include users name. It includes the pieces below in this order. Each piece is a self-contained header (it includes the standard headers and the sibling pieces it uses; `msgpack_types.h` holds the external includes), though users include `msgpack.h`.

| File | Holds |
|---|---|
| `msgpack.h` | The include users name: includes the pieces below |
| `msgpack_types.h` | The external includes, constants, the `undefined` marker and the `MsgPackType` enum |
| `msgpack_class.h` | The `MsgPack` class declaration |
| `msgpack_iterator.h` | `MsgPack::Iterator`, the iterator over arrays and maps |
| `msgpack_body.h` | `MsgPack::Body`, the shared state behind a value |
| `msgpack_adaptor.h` | The msgpack adaptors of `MsgPack` (convert, pack, object), which the constructors need |
| `msgpack_ctor.h` | Iterator entry points, constructors, initializers and assignment |
| `msgpack_init.h` | Initializing, deinitializing and reserving a value of each type |
| `msgpack_find_erase.h` | The internal find, erase and clear |
| `msgpack_put.h` | The internal append, put, emplace and insert |
| `msgpack_modify.h` | Fill, lock, and the public erase, replace, emplace, insert, update, put, add and append |
| `msgpack_access.h` | Element access: get, at, `operator[]`, path, select, find and count |
| `msgpack_hash.h` | The packed-bytes buffer, the map hashing helpers and `MsgPack::hash()` |
| `msgpack_convert.h` | Conversions, size, scalar accessors, `as_*` and the type predicates |
| `msgpack_arithmetic.h` | Comparison and the arithmetic operators |
| `msgpack_serialize.h` | `to_string`, `serialize`, stream output and `std::hash` |
| `msgpack_exception.h` | The standalone exception seam |
| `msgpack_patcher.h`, `msgpack_patcher.cc` | The RFC 6902 patcher |

`msgpack/` is the vendored msgpack-c tree and `test/frozen_msgpack.h` the frozen reference; neither is edited by hand.

## Tests

`test/diff_test.cc` is the differential test (target `msgpack_diff_test`): the file holds the setup and `main()`, and includes three pieces inside its anonymous namespace.

| File | Holds |
|---|---|
| `test/diff_test_gen.h` | Random documents: the generator, its random source and the hex helpers |
| `test/diff_test_run.h` | The operations run on both types and the walks that read a value through the API |
| `test/diff_test_check.h` | Comparing the two types on a document, every prefix and the hand-written documents |
