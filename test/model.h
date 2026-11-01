// Reference models for the tests, written from the definitions and not from the implementation.
//
// A map holds each key once. When a document repeats a key, the last value wins and the entry
// keeps the position of the key's first occurrence. `merged` builds that document from the bytes
// a reader gives (std::map for the keys, a vector for the order), and `Doc` is a small JSON-like
// value the tests change with the same operations they apply to a MsgPack.
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "msgpack.hpp"

namespace model {

// The bytes `MsgPack::serialise()` gives for `o` once every map in it has been filled: every
// map reduced to unique keys, in the order of their first occurrence, with the last value.
inline void pack_merged(msgpack::packer<msgpack::sbuffer>& pk, const msgpack::object& o) {
	switch (o.type) {
		case msgpack::type::MAP: {
			std::vector<std::string> order;
			std::map<std::string, const msgpack::object*> last;
			for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
				const auto& kv = o.via.map.ptr[i];
				std::string key(kv.key.via.str.ptr, kv.key.via.str.size);
				auto it = last.find(key);
				if (it == last.end()) {
					order.push_back(key);
					last.emplace(key, &kv.val);
				} else {
					it->second = &kv.val;
				}
			}
			pk.pack_map(static_cast<std::uint32_t>(order.size()));
			for (const auto& key : order) {
				pk.pack_str(static_cast<std::uint32_t>(key.size()));
				pk.pack_str_body(key.data(), static_cast<std::uint32_t>(key.size()));
				pack_merged(pk, *last[key]);
			}
			break;
		}
		case msgpack::type::ARRAY:
			pk.pack_array(o.via.array.size);
			for (std::uint32_t i = 0; i < o.via.array.size; ++i) {
				pack_merged(pk, o.via.array.ptr[i]);
			}
			break;
		default:
			pk.pack(o);
			break;
	}
}

// The merged document for the bytes of a document whose map keys are all strings.
inline std::string merged(std::string_view bytes) {
	msgpack::object_handle h = msgpack::unpack(bytes.data(), bytes.size());
	msgpack::sbuffer buf;
	msgpack::packer<msgpack::sbuffer> pk(&buf);
	pack_merged(pk, h.get());
	return std::string(buf.data(), buf.size());
}

// The hash `MsgPack::hash()` gives: the XOR over each map reached through maps of
// hash(key) ^ hash(value), and the hash of the packed bytes for everything else. It is taken
// on the merged document, because hashing fills the maps it reaches through maps.
inline std::size_t hash_merged_maps(const msgpack::object& o) {
	if (o.type == msgpack::type::MAP) {
		std::vector<std::string> order;
		std::map<std::string, const msgpack::object*> last;
		for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
			const auto& kv = o.via.map.ptr[i];
			std::string key(kv.key.via.str.ptr, kv.key.via.str.size);
			auto it = last.find(key);
			if (it == last.end()) {
				order.push_back(key);
				last.emplace(key, &kv.val);
			} else {
				it->second = &kv.val;
			}
		}
		std::size_t h = 0;
		for (const auto& key : order) {
			h ^= std::hash<std::string>()(key);
			h ^= hash_merged_maps(*last[key]);
		}
		return h;
	}
	msgpack::sbuffer buf;
	msgpack::pack(&buf, o);
	return std::hash<std::string>()(std::string(buf.data(), buf.size()));
}

// A small JSON-like value: null, unsigned integer, string, array, or map with unique keys in
// insertion order. The operations are the ones the tests apply to a MsgPack.
struct Doc {
	enum Kind { NIL, UINT, STR, ARR, MAP } kind = NIL;
	std::uint64_t u = 0;
	std::string s;
	std::vector<Doc> a;
	std::vector<std::pair<std::string, Doc>> m;

	// Defaulted after the class, where Doc is complete: libstdc++ 14 with clang needs that for
	// a std::pair that holds a Doc, and the implicit ones are defined inside the class.
	Doc();
	Doc(const Doc&);
	Doc(Doc&&);
	Doc& operator=(const Doc&);
	Doc& operator=(Doc&&);
	~Doc();

	static Doc nil() { return Doc(); }
	static Doc num(std::uint64_t v) { Doc d; d.kind = UINT; d.u = v; return d; }
	static Doc str(std::string v) { Doc d; d.kind = STR; d.s = std::move(v); return d; }
	static Doc arr() { Doc d; d.kind = ARR; return d; }
	static Doc map() { Doc d; d.kind = MAP; return d; }

	Doc* find(const std::string& key) {
		for (auto& kv : m) {
			if (kv.first == key) {
				return &kv.second;
			}
		}
		return nullptr;
	}

	void set(const std::string& key, Doc v) {
		if (Doc* d = find(key)) {
			*d = std::move(v);
		} else {
			m.emplace_back(key, std::move(v));
		}
	}

	bool erase(const std::string& key) {
		for (auto it = m.begin(); it != m.end(); ++it) {
			if (it->first == key) {
				m.erase(it);
				return true;
			}
		}
		return false;
	}

	void pack(msgpack::packer<msgpack::sbuffer>& pk) const {
		switch (kind) {
			case NIL: pk.pack_nil(); break;
			case UINT: pk.pack_uint64(u); break;
			case STR:
				pk.pack_str(static_cast<std::uint32_t>(s.size()));
				pk.pack_str_body(s.data(), static_cast<std::uint32_t>(s.size()));
				break;
			case ARR:
				pk.pack_array(static_cast<std::uint32_t>(a.size()));
				for (const auto& e : a) e.pack(pk);
				break;
			case MAP:
				pk.pack_map(static_cast<std::uint32_t>(m.size()));
				for (const auto& kv : m) {
					pk.pack_str(static_cast<std::uint32_t>(kv.first.size()));
					pk.pack_str_body(kv.first.data(), static_cast<std::uint32_t>(kv.first.size()));
					kv.second.pack(pk);
				}
				break;
		}
	}

	std::string bytes() const {
		msgpack::sbuffer buf;
		msgpack::packer<msgpack::sbuffer> pk(&buf);
		pack(pk);
		return std::string(buf.data(), buf.size());
	}
};

inline Doc::Doc() = default;
inline Doc::Doc(const Doc&) = default;
inline Doc::Doc(Doc&&) = default;
inline Doc& Doc::operator=(const Doc&) = default;
inline Doc& Doc::operator=(Doc&&) = default;
inline Doc::~Doc() = default;

}  // namespace model
