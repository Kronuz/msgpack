// The documents the benchmarks read, as MessagePack bytes and as JSON text. Nothing here includes a
// msgpack library, so both benchmark programs (this repository's msgpack-c 1.3.0 and the comparators'
// msgpack-c) share it.
//
//   small   63 B: a flat record with one nested map
//   med    502 B: a typical record (15 keys, nested maps and arrays, every scalar type)
//   large  104 KB: an array of 200 med-like records
//   wide   8.9 KB: one map of 1000 keys
//
// Maps hold their keys in the order they were added, which is sorted, so every library reads the same
// order. The integers use their shortest MessagePack form and the doubles are float 64, as msgpack-c
// writes them.
#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace corpus {

struct Value {
	enum Kind { NIL, BOOL, INT, DBL, STR, ARR, MAP } kind = NIL;
	bool b = false;
	std::int64_t i = 0;
	double d = 0;
	std::string s;
	std::vector<std::string> keys;   // MAP: the key of items[n]
	std::vector<Value> items;        // ARR: the elements; MAP: the values
};

inline Value nil() { return Value(); }
inline Value boolean(bool v) { Value x; x.kind = Value::BOOL; x.b = v; return x; }
inline Value integer(std::int64_t v) { Value x; x.kind = Value::INT; x.i = v; return x; }
inline Value real(double v) { Value x; x.kind = Value::DBL; x.d = v; return x; }
inline Value text(std::string v) { Value x; x.kind = Value::STR; x.s = std::move(v); return x; }
inline Value array() { Value x; x.kind = Value::ARR; return x; }
inline Value object() { Value x; x.kind = Value::MAP; return x; }
inline Value& push(Value& arr, Value v) { arr.items.push_back(std::move(v)); return arr.items.back(); }
inline Value& put(Value& obj, std::string key, Value v) {
	obj.keys.push_back(std::move(key));
	obj.items.push_back(std::move(v));
	return obj.items.back();
}

// ---- the documents

inline const char* const kLorem[] = {
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.",
	"Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat duis aute irure.",
	"Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur excepteur sint occaecat.",
};
inline const char* const kFirst[] = { "John", "Maria", "Chen", "Aisha", "Pedro" };
inline const char* const kLast[] = { "Smith", "Garcia", "Wang", "Khan", "Silva" };

inline std::string id_of(int i) {
	char b[32];
	std::snprintf(b, sizeof b, "doc-%06d", i);
	return b;
}

inline std::string email_of(int i) {
	char b[48];
	std::snprintf(b, sizeof b, "user%d@example.com", i);
	return b;
}

inline Value full_name(int i) {
	Value n = object();
	put(n, "first", text(kFirst[i % 5]));
	put(n, "last", text(kLast[i % 5]));
	return n;
}

inline Value make_small(int i) {
	Value r = object();
	put(r, "_id", text(id_of(i)));
	put(r, "active", boolean(i % 3 != 0));
	put(r, "age", integer(18 + i % 60));
	put(r, "name", full_name(i));
	put(r, "score", integer(-17 + i % 40));
	return r;
}

inline Value make_record(int i) {
	Value history = array();
	for (int j = 0; j < 4; ++j) {
		Value e = object();
		put(e, "kind", text(j & 1 ? "logout" : "login"));
		put(e, "ok", boolean(j != 2));
		put(e, "ts", integer(1600000000 + j * 3600 + i));
		push(history, std::move(e));
	}
	Value scores = array();
	for (int j = 0; j < 16; ++j) {
		push(scores, integer((i * 7 + j * 13) % 1000));
	}
	Value geo = object();
	put(geo, "lat", real(40.7128 + i * 0.001));
	put(geo, "lon", real(-74.006 - i * 0.001));
	Value phones = array();
	push(phones, text("+1 555 0100"));
	push(phones, text("+1 555 0101"));
	Value tags = array();
	for (const char* t : { "alpha", "beta", "gamma", "delta" }) {
		push(tags, text(t));
	}
	Value r = object();
	put(r, "_id", text(id_of(i)));
	put(r, "about", text(kLorem[i % 3]));
	put(r, "active", boolean(i % 3 != 0));
	put(r, "age", integer(18 + i % 60));
	put(r, "balance", real(1000.0 + i * 1.37));
	put(r, "company", text("Acme Industries"));
	put(r, "email", text(email_of(i)));
	put(r, "geo", std::move(geo));
	put(r, "history", std::move(history));
	put(r, "name", full_name(i));
	put(r, "nothing", nil());
	put(r, "phones", std::move(phones));
	put(r, "score", integer(-17 + i % 40));
	put(r, "scores", std::move(scores));
	put(r, "tags", std::move(tags));
	return r;
}

inline Value make_large() {
	Value a = array();
	for (int i = 0; i < 200; ++i) {
		push(a, make_record(i));
	}
	return a;
}

inline Value make_wide() {
	Value m = object();
	for (int i = 0; i < 1000; ++i) {
		char k[16];
		std::snprintf(k, sizeof k, "k%04d", i);
		put(m, k, integer(i * 3));
	}
	return m;
}

// ---- MessagePack bytes

inline void be(std::string& out, std::uint64_t v, int bytes) {
	for (int n = bytes - 1; n >= 0; --n) {
		out.push_back(static_cast<char>((v >> (8 * n)) & 0xff));
	}
}

inline void pack_int(std::string& out, std::int64_t v) {
	if (v >= 0) {
		const std::uint64_t u = static_cast<std::uint64_t>(v);
		if (u < 128) { out.push_back(static_cast<char>(u)); }
		else if (u < 256) { out.push_back(static_cast<char>(0xcc)); be(out, u, 1); }
		else if (u < 65536) { out.push_back(static_cast<char>(0xcd)); be(out, u, 2); }
		else if (u < 4294967296ULL) { out.push_back(static_cast<char>(0xce)); be(out, u, 4); }
		else { out.push_back(static_cast<char>(0xcf)); be(out, u, 8); }
	} else if (v >= -32) {
		out.push_back(static_cast<char>(v));
	} else if (v >= -128) {
		out.push_back(static_cast<char>(0xd0)); be(out, static_cast<std::uint64_t>(v), 1);
	} else if (v >= -32768) {
		out.push_back(static_cast<char>(0xd1)); be(out, static_cast<std::uint64_t>(v), 2);
	} else if (v >= -2147483648LL) {
		out.push_back(static_cast<char>(0xd2)); be(out, static_cast<std::uint64_t>(v), 4);
	} else {
		out.push_back(static_cast<char>(0xd3)); be(out, static_cast<std::uint64_t>(v), 8);
	}
}

inline void pack_str(std::string& out, const std::string& s) {
	const std::size_t n = s.size();
	if (n < 32) { out.push_back(static_cast<char>(0xa0 | n)); }
	else if (n < 256) { out.push_back(static_cast<char>(0xd9)); be(out, n, 1); }
	else if (n < 65536) { out.push_back(static_cast<char>(0xda)); be(out, n, 2); }
	else { out.push_back(static_cast<char>(0xdb)); be(out, n, 4); }
	out += s;
}

inline void pack_head(std::string& out, std::size_t n, unsigned fix, unsigned wide16) {
	if (n < 16) { out.push_back(static_cast<char>(fix | n)); }
	else { out.push_back(static_cast<char>(wide16)); be(out, n, 2); }
}

inline void pack(std::string& out, const Value& v) {
	switch (v.kind) {
		case Value::NIL: out.push_back(static_cast<char>(0xc0)); break;
		case Value::BOOL: out.push_back(static_cast<char>(v.b ? 0xc3 : 0xc2)); break;
		case Value::INT: pack_int(out, v.i); break;
		case Value::DBL: {
			std::uint64_t bits;
			std::memcpy(&bits, &v.d, sizeof bits);
			out.push_back(static_cast<char>(0xcb));
			be(out, bits, 8);
			break;
		}
		case Value::STR: pack_str(out, v.s); break;
		case Value::ARR:
			pack_head(out, v.items.size(), 0x90, 0xdc);
			for (const auto& e : v.items) pack(out, e);
			break;
		case Value::MAP:
			pack_head(out, v.items.size(), 0x80, 0xde);
			for (std::size_t n = 0; n < v.items.size(); ++n) {
				pack_str(out, v.keys[n]);
				pack(out, v.items[n]);
			}
			break;
	}
}

// ---- JSON text

inline void json_str(std::string& out, const std::string& s) {
	out.push_back('"');
	for (unsigned char c : s) {
		if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(static_cast<char>(c)); }
		else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
		else out.push_back(static_cast<char>(c));
	}
	out.push_back('"');
}

inline void json(std::string& out, const Value& v) {
	switch (v.kind) {
		case Value::NIL: out += "null"; break;
		case Value::BOOL: out += v.b ? "true" : "false"; break;
		case Value::INT: out += std::to_string(v.i); break;
		case Value::DBL: {
			char b[40];
			const auto r = std::to_chars(b, b + sizeof b, v.d);   // the shortest text that reads back the same
			std::string t(b, r.ptr);
			if (t.find_first_of(".eE") == std::string::npos) t += ".0";   // a double stays a double
			out += t;
			break;
		}
		case Value::STR: json_str(out, v.s); break;
		case Value::ARR:
			out.push_back('[');
			for (std::size_t n = 0; n < v.items.size(); ++n) {
				if (n) out.push_back(',');
				json(out, v.items[n]);
			}
			out.push_back(']');
			break;
		case Value::MAP:
			out.push_back('{');
			for (std::size_t n = 0; n < v.items.size(); ++n) {
				if (n) out.push_back(',');
				json_str(out, v.keys[n]);
				out.push_back(':');
				json(out, v.items[n]);
			}
			out.push_back('}');
			break;
	}
}

// ---- the corpus

struct Document {
	std::string name;
	std::string mp;     // MessagePack bytes
	std::string json;   // the same document as JSON text
};

inline std::vector<Document> make_corpus() {
	std::vector<Document> docs;
	auto add = [&](const char* name, const Value& v) {
		Document d;
		d.name = name;
		pack(d.mp, v);
		corpus::json(d.json, v);
		docs.push_back(std::move(d));
	};
	add("small", make_small(7));
	add("med", make_record(7));
	add("large", make_large());
	add("wide", make_wide());
	return docs;
}

// ---- the keys looked up in each document (three paths: a nested key, a number, a key at the end)

struct Step {
	const char* key;   // nullptr: an index
	std::size_t index;
};
using Path = std::vector<Step>;

inline std::vector<Path> lookup_paths(const std::string& doc) {
	if (doc == "small") return { { { "name", 0 }, { "first", 0 } }, { { "age", 0 } }, { { "_id", 0 } } };
	if (doc == "med") return { { { "name", 0 }, { "first", 0 } }, { { "geo", 0 }, { "lat", 0 } }, { { "history", 0 }, { nullptr, 2 }, { "kind", 0 } } };
	if (doc == "large") return { { { nullptr, 100 }, { "name", 0 }, { "first", 0 } }, { { nullptr, 150 }, { "geo", 0 }, { "lat", 0 } }, { { nullptr, 199 }, { "_id", 0 } } };
	return { { { "k0500", 0 } }, { { "k0999", 0 } }, { { "k0001", 0 } } };
}

}  // namespace corpus
