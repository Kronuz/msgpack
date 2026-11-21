/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2019 Dubalu LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// The packed-bytes buffer, the map hashing helpers and MsgPack::hash().
#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include "msgpack_body.h"
#include "msgpack_iterator.h"

namespace msgpack_wrapper {
	// Collects packed bytes: values up to 256 bytes stay on the stack, longer ones grow on the heap.
	// `write` is the part the packer calls for every field, so it stays small enough to inline.
	class pack_buffer {
		char _stack[256];
		char* _data = _stack;
		std::size_t _size = 0;
		std::size_t _capacity = sizeof(_stack);

#if defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		void _grow(std::size_t len) {
			std::size_t capacity = _data == _stack ? 8192 : _capacity * 2;
			while (capacity - _size < len) {
				capacity *= 2;
			}
			char* data = static_cast<char*>(_data == _stack ? std::malloc(capacity) : std::realloc(_data, capacity));
			if (!data) {
				throw std::bad_alloc();
			}
			if (_data == _stack) {
				std::memcpy(data, _stack, _size);
			}
			_data = data;
			_capacity = capacity;
		}

	public:
		pack_buffer() = default;
		pack_buffer(const pack_buffer&) = delete;
		pack_buffer& operator=(const pack_buffer&) = delete;

		~pack_buffer() {
			if (_data != _stack) {
				std::free(_data);
			}
		}

		void write(const char* buf, std::size_t len) {
			if (unlikely(_capacity - _size < len)) {
				_grow(len);
			}
			std::memcpy(_data + _size, buf, len);
			_size += len;
		}

		std::string_view view() const noexcept {
			return std::string_view(_data, _size);
		}
	};

	// The hash of the bytes `serialize()` gives for `o`.
	inline std::size_t hash_packed(const msgpack::object& o) {
		pack_buffer buf;
		msgpack::pack(&buf, o);
		return std::hash<std::string_view>()(buf.view());
	}

	inline bool same_key(const msgpack::object_kv* kv, std::size_t i, std::size_t j) noexcept {
		const auto& a = kv[i].key.via.str;
		const auto& b = kv[j].key.via.str;
		return a.size == b.size && (a.size == 0 || std::memcmp(a.ptr, b.ptr, a.size) == 0);
	}

	// Whether two of the `n` string keys are equal; `hashes` holds the hash of each key.
	inline bool has_duplicate_keys(const msgpack::object_kv* kv, std::size_t n, const std::size_t* hashes) {
		if (n <= 16) {
			for (std::size_t i = 1; i < n; ++i) {
				for (std::size_t j = 0; j < i; ++j) {
					if (hashes[i] == hashes[j] && same_key(kv, i, j)) {
						return true;
					}
				}
			}
			return false;
		}
		// An open-addressing table of key indexes, at most half full.
		std::size_t slots = 64;
		while (slots < n * 2) {
			slots *= 2;
		}
		const std::size_t mask = slots - 1;
		constexpr std::uint32_t empty = static_cast<std::uint32_t>(-1);
		std::unique_ptr<std::uint32_t[]> table(new std::uint32_t[slots]);
		std::memset(table.get(), 0xff, slots * sizeof(std::uint32_t));
		for (std::size_t i = 0; i < n; ++i) {
			for (std::size_t slot = hashes[i] & mask;; slot = (slot + 1) & mask) {
				const std::uint32_t j = table[slot];
				if (j == empty) {
					table[slot] = static_cast<std::uint32_t>(i);
					break;
				}
				if (hashes[j] == hashes[i] && same_key(kv, i, j)) {
					return true;
				}
			}
		}
		return false;
	}

	// The value of `MsgPack::hash()` for a map object, computed on the tree: the XOR over the
	// entries of hash(key) ^ hash(value), where a value that is a map hashes the same way and
	// any other value hashes its packed bytes.
	// Returns false, with no effect, when a key of this map or of a map nested directly in maps
	// is not a string or is repeated: the filled map handles those two cases (it throws, or
	// merges the repeats in the tree), and the caller takes that path.
	inline bool hash_map(const msgpack::object& o, std::size_t& result) {
		const msgpack::object_kv* const kv = o.via.map.ptr;
		const std::size_t n = o.via.map.size;
		std::size_t inline_hashes[16];
		std::unique_ptr<std::size_t[]> heap_hashes;
		std::size_t* hashes = inline_hashes;
		if (n > 16) {
			heap_hashes.reset(new std::size_t[n]);
			hashes = heap_hashes.get();
		}
		const std::hash<std::string_view> hasher;
		for (std::size_t i = 0; i < n; ++i) {
			if (kv[i].key.type != msgpack::type::STR) {
				return false;
			}
			hashes[i] = hasher(std::string_view(kv[i].key.via.str.ptr, kv[i].key.via.str.size));
		}
		if (has_duplicate_keys(kv, n, hashes)) {
			return false;
		}
		std::size_t hash = 0;
		for (std::size_t i = 0; i < n; ++i) {
			hash ^= hashes[i];
			const msgpack::object& val = kv[i].val;
			if (val.type == msgpack::type::MAP) {
				std::size_t val_hash;
				if (!hash_map(val, val_hash)) {
					return false;
				}
				hash ^= val_hash;
			} else {
				hash ^= hash_packed(val);
			}
		}
		result = hash;
		return true;
	}
}


inline std::size_t MsgPack::hash() const {
	switch (_body->get_type()) {
		case Type::MAP: {
			std::size_t tree_hash;
			if (msgpack_wrapper::hash_map(*_body->_obj, tree_hash)) {
				return tree_hash;
			}
			// A key that is not a string, or a repeated key: fill the map, which throws or
			// merges the repeats in the tree, and hash what is left.
			_fill(false, false);
			size_t pos = 0;
			std::size_t hash = 0;
			const auto pend = &_body->_obj->via.map.ptr[_body->_obj->via.map.size];
			for (auto p = &_body->_obj->via.map.ptr[pos]; p < pend; ++p, ++pos) {
				if (p->key.type != msgpack::type::STR) {
					THROW(msgpack::type_error, "MAP keys must be of type STR");
				}
				auto val = MsgPack(std::make_shared<Body>(_body->_zone, _body->_base, _body, true, 0, nullptr, &p->val));
				static const std::hash<std::string_view> hasher;
				hash ^= hasher(std::string_view(p->key.via.str.ptr, p->key.via.str.size));
				hash ^= val.hash();
			}
			return hash;
		}
		default:
			return msgpack_wrapper::hash_packed(*_body->_obj);
	}
}
