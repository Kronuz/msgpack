// Reference models for the tests, written from the definitions and not from the implementation.
//
// A map holds each key once. When a document repeats a key, the last value wins and the entry
// keeps the position of the key's first occurrence. `merged` builds that document from the bytes
// a reader gives (std::map for the keys, a vector for the order), and `Doc` is a small JSON-like
// value the tests change with the same operations they apply to a MsgPack.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "msgpack.hpp"

namespace model {

// Bytes of msgpack values written by hand, for documents a MsgPack would not build itself
// (repeated keys, keys that are not strings).
inline std::string str_bytes(const std::string& s) {
	std::string r;
	if (s.size() < 32) {
		r.push_back(static_cast<char>(0xa0 | s.size()));
	} else {
		r.push_back(static_cast<char>(0xd9));
		r.push_back(static_cast<char>(s.size()));
	}
	return r + s;
}

inline std::string uint_bytes(std::uint64_t v) {
	std::string r;
	if (v < 128) {
		r.push_back(static_cast<char>(v));
	} else {
		r.push_back(static_cast<char>(0xcd));
		r.push_back(static_cast<char>(v >> 8));
		r.push_back(static_cast<char>(v));
	}
	return r;
}

inline std::string map_head(std::size_t n) {
	std::string r;
	if (n < 16) {
		r.push_back(static_cast<char>(0x80 | n));
	} else {
		r.push_back(static_cast<char>(0xde));
		r.push_back(static_cast<char>(n >> 8));
		r.push_back(static_cast<char>(n));
	}
	return r;
}

inline std::string array_head(std::size_t n) {
	std::string r;
	if (n < 16) {
		r.push_back(static_cast<char>(0x90 | n));
	} else {
		r.push_back(static_cast<char>(0xdc));
		r.push_back(static_cast<char>(n >> 8));
		r.push_back(static_cast<char>(n));
	}
	return r;
}

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

// JSON text for the bytes of a document, written without RapidJSON and without MsgPack: the oracle
// for `to_string()` and `as_document()`. The text is what RapidJSON's Writer (`indent` < 0) or
// PrettyWriter (`indent` spaces) writes. JSON has string keys only: a str or bin key is written as
// it is and any other key as its own compact JSON text (a float that is not finite as `NaN`,
// `Infinity` or `-Infinity`, an ext as `null`); that is the rule the README states.
inline std::string json_string(const char* p, std::size_t n) {
	static const char hexdigits[] = "0123456789ABCDEF";
	std::string r = "\"";
	for (std::size_t i = 0; i < n; ++i) {
		const unsigned char c = static_cast<unsigned char>(p[i]);
		switch (c) {
			case '"': r += "\\\""; break;
			case '\\': r += "\\\\"; break;
			case '\b': r += "\\b"; break;
			case '\f': r += "\\f"; break;
			case '\n': r += "\\n"; break;
			case '\r': r += "\\r"; break;
			case '\t': r += "\\t"; break;
			default:
				if (c < 0x20) {
					r += "\\u00";
					r += hexdigits[c >> 4];
					r += hexdigits[c & 15];
				} else {
					r += static_cast<char>(c);
				}
		}
	}
	return r + "\"";
}

// The doubles the tests use, with the shortest text that reads back as the same double.
inline std::string json_double(double d) {
	if (d != d) return "NaN";
	if (d == 1.0 / 0.0) return "Infinity";
	if (d == -1.0 / 0.0) return "-Infinity";
	static const std::pair<double, const char*> known[] = {
		{0.5, "0.5"}, {-2.25, "-2.25"}, {1.0, "1.0"}, {100.0, "100.0"}, {1.5, "1.5"}, {0.001, "0.001"},
		{123456.789, "123456.789"}, {1e300, "1e300"}, {-1.5e-7, "-1.5e-7"}, {3.0, "3.0"},
	};
	if (d == 0.0) return std::signbit(d) ? "-0.0" : "0.0";
	for (const auto& k : known) {
		if (k.first == d) return k.second;
	}
	std::abort();   // a double the tests do not know the text of
}

inline void json_write(const msgpack::object& o, int indent, int level, std::string& out);

inline std::string json_key_text(const msgpack::object& k) {
	if (k.type == msgpack::type::STR) return std::string(k.via.str.ptr, k.via.str.size);
	if (k.type == msgpack::type::BIN) return std::string(k.via.bin.ptr, k.via.bin.size);
	std::string r;
	json_write(k, -1, 0, r);
	return r;
}

inline void json_write(const msgpack::object& o, int indent, int level, std::string& out) {
	auto newline = [&](int lv) {
		if (indent >= 0) {
			out += '\n';
			out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(lv), ' ');
		}
	};
	switch (o.type) {
		case msgpack::type::BOOLEAN: out += o.via.boolean ? "true" : "false"; break;
		case msgpack::type::POSITIVE_INTEGER: out += std::to_string(o.via.u64); break;
		case msgpack::type::NEGATIVE_INTEGER: out += std::to_string(o.via.i64); break;
		case msgpack::type::FLOAT: out += json_double(o.via.f64); break;
		case msgpack::type::STR: out += json_string(o.via.str.ptr, o.via.str.size); break;
		case msgpack::type::BIN: out += json_string(o.via.bin.ptr, o.via.bin.size); break;
		case msgpack::type::ARRAY:
			out += '[';
			for (std::uint32_t i = 0; i < o.via.array.size; ++i) {
				if (i) out += ',';
				newline(level + 1);
				json_write(o.via.array.ptr[i], indent, level + 1, out);
			}
			if (o.via.array.size) newline(level);
			out += ']';
			break;
		case msgpack::type::MAP:
			out += '{';
			for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
				if (i) out += ',';
				newline(level + 1);
				const std::string key = json_key_text(o.via.map.ptr[i].key);
				out += json_string(key.data(), key.size());
				out += indent >= 0 ? ": " : ":";
				json_write(o.via.map.ptr[i].val, indent, level + 1, out);
			}
			if (o.via.map.size) newline(level);
			out += '}';
			break;
		default: out += "null"; break;   // nil and ext
	}
}

inline std::string reference_json(std::string_view bytes, int indent = -1) {
	msgpack::object_handle h = msgpack::unpack(bytes.data(), bytes.size());
	std::string out;
	json_write(h.get(), indent, 0, out);
	return out;
}

// A small JSON-like value: null, unsigned integer, string, array, or map with unique keys in
// insertion order, and RAW: packed bytes the model does not interpret and writes as they are (a
// map with a key that is not a string, which a MsgPack keeps as it came). The operations are the
// ones the tests apply to a MsgPack.
struct Doc {
	enum Kind { NIL, UINT, STR, ARR, MAP, RAW } kind = NIL;
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
	static Doc raw(std::string packed) { Doc d; d.kind = RAW; d.s = std::move(packed); return d; }

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

	void pack(msgpack::sbuffer& buf) const {
		msgpack::packer<msgpack::sbuffer> pk(&buf);
		switch (kind) {
			case NIL: pk.pack_nil(); break;
			case UINT: pk.pack_uint64(u); break;
			case STR:
				pk.pack_str(static_cast<std::uint32_t>(s.size()));
				pk.pack_str_body(s.data(), static_cast<std::uint32_t>(s.size()));
				break;
			case RAW: buf.write(s.data(), s.size()); break;
			case ARR:
				pk.pack_array(static_cast<std::uint32_t>(a.size()));
				for (const auto& e : a) e.pack(buf);
				break;
			case MAP:
				pk.pack_map(static_cast<std::uint32_t>(m.size()));
				for (const auto& kv : m) {
					pk.pack_str(static_cast<std::uint32_t>(kv.first.size()));
					pk.pack_str_body(kv.first.data(), static_cast<std::uint32_t>(kv.first.size()));
					kv.second.pack(buf);
				}
				break;
		}
	}

	std::string bytes() const {
		msgpack::sbuffer buf;
		pack(buf);
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
