# Source layout

`msgpack.h` is the include users name. It includes the pieces below in this order. Each piece is a self-contained header (it includes the standard headers and the sibling pieces it uses; `msgpack_types.h` holds the external includes), though users include `msgpack.h`.

| File | Holds |
|---|---|
| `msgpack.h` | The include users name: includes the pieces below |
| `msgpack_types.h` | The external includes, constants, the `undefined` marker and the `MsgPackType` enum |
| `msgpack_class.h` | The `MsgPack` class, its nested `Iterator` and `Body`, and its msgpack adaptors (convert, pack, object) |
| `msgpack_ctor.h` | Iterator entry points, constructors, initializers and assignment |
| `msgpack_init.h` | Initializing, deinitializing and reserving a value of each type |
| `msgpack_modify.h` | Finding, erasing, putting and filling elements: the internal operations and the public erase, replace, emplace, insert, update, put, add and append |
| `msgpack_access.h` | Element access: get, at, `operator[]`, path, select, find and count |
| `msgpack_hash.h` | The packed-bytes buffer, the map hashing helpers and `MsgPack::hash()` |
| `msgpack_convert.h` | Conversions, size, scalar accessors, `as_*` and the type predicates |
| `msgpack_arithmetic.h` | Comparison and the arithmetic operators |
| `msgpack_serialize.h` | `to_string`, `serialize`, stream output and `std::hash` |
| `msgpack_exception.h` | The standalone exception seam |
| `msgpack_patcher.h`, `msgpack_patcher.cc` | The RFC 6902 patcher |

The adaptors sit in `msgpack_class.h`, before the constructors, because the constructors instantiate `msgpack::object` for `MsgPack`.

`msgpack/` is the vendored msgpack-c tree and `test/frozen_msgpack.h` the frozen reference; neither is edited by hand.

## Kept whole

`test/diff_test.cc` (1,026 lines) stays one file: it is one differential test (target `msgpack_diff_test`), whose random document generator, operation runner and comparison share one anonymous namespace in one translation unit. Cutting it would take either fragments included inside that namespace or a linkage change, for no piece a maintainer would open alone.
