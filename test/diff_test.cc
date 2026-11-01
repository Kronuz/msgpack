// Differential test: MsgPack against the frozen copy of the earlier implementation
// (test/frozen_msgpack.h, class MsgPackOld).
//
// Both types read the same random documents and run the same random sequence of operations;
// every result and every exception must be equal: unserialise (value and offset), hash(),
// serialise(), to_string() with and without indent, copies, moves, walks, mutations after unserialise
// and lock(). The text of a document that holds NaN or Inf is the one thing that differs by design:
// the frozen implementation stops at the number, the current one writes null (see compare_logs).
// Documents hold every msgpack type in every width, big integers, NaN and -0.0, empty
// containers, ext, bin, and repeated keys at any depth.
//
// The one class where the two types are meant to differ is a document that repeats a string key
// in some map. The frozen implementation merges the repeats wrongly (it copies the wrong
// entry over the ones after it, leaves the tree and the index of the map out of step, and can
// read freed memory), and the current one keeps the last value in the position of the first
// (test/model.h, test/merge_test.cc). For those documents the frozen implementation reads the
// merged document instead (what a correct merge gives, built by model::merged), the current
// one reads the original and is filled before the operations start (what first fills a map
// decides what is merged); everything else, offsets included, is compared as it is.
// A document that repeats a string key and also has a key that is not a string is not
// compared: filling throws at the non-string key after merging what came before it, and the
// frozen merge is not defined there.
//
// The frozen implementation asserts on its merge in a build with assertions, so this
// translation unit turns them off.
#ifndef NDEBUG
#define NDEBUG
#endif

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include "msgpack.h"
#include "frozen_msgpack.h"
#include "model.h"

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			std::exit(1); \
		} \
	} while (0)

namespace {

struct Rng {
	std::uint64_t state;

	std::uint64_t next() {
		std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
		return z ^ (z >> 31);
	}

	std::uint64_t below(std::uint64_t n) {
		return n ? next() % n : 0;
	}

	bool chance(unsigned num, unsigned den) {
		return below(den) < num;
	}
};


struct Doc {
	std::string bytes;
	bool non_string_key = false;   // some map has a key that is not a string
	bool non_finite = false;       // some float is NaN or Inf
};


class Gen {
	Rng& r;
	Doc& doc;
	int key_mode;          // 0 unique keys, 1 a small pool (many repeats), 2 both, 3 unique with one repeat
	bool allow_non_string;
	unsigned counter = 0;

	std::string& out() { return doc.bytes; }

	void be(std::uint64_t v, int nbytes) {
		for (int i = nbytes - 1; i >= 0; --i) {
			out().push_back(static_cast<char>((v >> (8 * i)) & 0xff));
		}
	}

	void head(std::uint8_t b) { out().push_back(static_cast<char>(b)); }

	void put_uint(std::uint64_t v) {
		std::vector<int> widths;
		if (v <= 127) widths.push_back(0);
		if (v <= 0xff) widths.push_back(1);
		if (v <= 0xffff) widths.push_back(2);
		if (v <= 0xffffffffULL) widths.push_back(4);
		widths.push_back(8);
		int w = widths[r.chance(1, 2) ? 0 : r.below(widths.size())];
		switch (w) {
			case 0: head(static_cast<std::uint8_t>(v)); break;
			case 1: head(0xcc); be(v, 1); break;
			case 2: head(0xcd); be(v, 2); break;
			case 4: head(0xce); be(v, 4); break;
			default: head(0xcf); be(v, 8); break;
		}
	}

	void put_int(std::int64_t v) {
		// signed widths; a non-negative value in a signed width is legal too
		std::vector<int> widths;
		if (v >= -32 && v <= 127) widths.push_back(0);
		if (v >= -128 && v <= 127) widths.push_back(1);
		if (v >= -32768 && v <= 32767) widths.push_back(2);
		if (v >= INT32_MIN && v <= INT32_MAX) widths.push_back(4);
		widths.push_back(8);
		int w = widths[r.chance(1, 2) ? 0 : r.below(widths.size())];
		switch (w) {
			case 0: head(static_cast<std::uint8_t>(v)); break;
			case 1: head(0xd0); be(static_cast<std::uint64_t>(v), 1); break;
			case 2: head(0xd1); be(static_cast<std::uint64_t>(v), 2); break;
			case 4: head(0xd2); be(static_cast<std::uint64_t>(v), 4); break;
			default: head(0xd3); be(static_cast<std::uint64_t>(v), 8); break;
		}
	}

	std::uint64_t random_u64() {
		static const std::uint64_t edge[] = {
			0, 1, 31, 32, 126, 127, 128, 129, 254, 255, 256, 257, 65534, 65535, 65536, 65537,
			0xfffffffeULL, 0xffffffffULL, 0x100000000ULL, 0x7fffffffffffffffULL,
			0x8000000000000000ULL, 0xffffffffffffffffULL,
		};
		if (r.chance(1, 2)) return edge[r.below(sizeof(edge) / sizeof(edge[0]))];
		int bits = 1 + static_cast<int>(r.below(64));
		return r.next() >> (64 - bits);
	}

	std::int64_t random_negative() {
		static const std::int64_t edge[] = {
			-1, -2, -31, -32, -33, -34, -127, -128, -129, -32767, -32768, -32769,
			INT32_MIN + 1LL, INT32_MIN, INT32_MIN - 1LL, INT64_MIN + 1, INT64_MIN,
		};
		if (r.chance(1, 2)) return edge[r.below(sizeof(edge) / sizeof(edge[0]))];
		int bits = 1 + static_cast<int>(r.below(63));
		return -1 - static_cast<std::int64_t>(r.next() >> (64 - bits));
	}

	static double as_double(std::uint64_t bits) {
		double d;
		std::memcpy(&d, &bits, sizeof(d));
		return d;
	}

	void put_double() {
		static const std::uint64_t finite[] = {
			0x0000000000000000ULL,  // 0.0
			0x8000000000000000ULL,  // -0.0
			0x3ff0000000000000ULL,  // 1.0
			0xbff8000000000000ULL,  // -1.5
			0x3fb999999999999aULL,  // 0.1
			0x7e37e43c8800759cULL,  // 1e300
			0x7fefffffffffffffULL,  // DBL_MAX
			0x0010000000000000ULL,  // DBL_MIN
			0x0000000000000001ULL,  // 5e-324
			0x419d6f34540ca000ULL,  // 123456789.125
		};
		static const std::uint64_t non_finite[] = {
			0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff8000000000000ULL,
			0xfff8000000000000ULL, 0x7ff0000000000001ULL,
		};
		std::uint64_t bits;
		if (r.chance(1, 40)) {
			bits = non_finite[r.below(sizeof(non_finite) / sizeof(non_finite[0]))];
			doc.non_finite = true;
		} else if (r.chance(1, 2)) {
			bits = finite[r.below(sizeof(finite) / sizeof(finite[0]))];
		} else {
			do {
				bits = r.next();
			} while (!std::isfinite(as_double(bits)));
		}
		head(0xcb);
		be(bits, 8);
	}

	void put_float() {
		static const std::uint32_t finite[] = {
			0x00000000u, 0x80000000u, 0x3fc00000u, 0x7f7fffffu, 0x00000001u, 0xc2f70000u,
		};
		std::uint32_t bits;
		if (r.chance(1, 40)) {
			static const std::uint32_t non_finite[] = { 0x7f800000u, 0xff800000u, 0x7fc00000u };
			bits = non_finite[r.below(3)];
			doc.non_finite = true;
		} else if (r.chance(1, 2)) {
			bits = finite[r.below(sizeof(finite) / sizeof(finite[0]))];
		} else {
			do {
				bits = static_cast<std::uint32_t>(r.next());
			} while (((bits >> 23) & 0xff) == 0xff);
		}
		head(0xca);
		be(bits, 4);
	}

	std::string random_bytes(std::size_t n) {
		std::string s;
		static const char* utf8[] = { "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\xce\xbb" };
		int flavor = static_cast<int>(r.below(10));
		while (s.size() < n) {
			if (flavor < 7) {
				s.push_back(static_cast<char>('a' + r.below(26)));
			} else if (flavor == 7) {
				static const char odd[] = { 0, 1, 31, '"', '\\', '/', '\n', '\t', 127, ' ', 'z' };
				s.push_back(odd[r.below(sizeof(odd))]);
			} else if (flavor == 8) {
				s += utf8[r.below(4)];
			} else {
				s.push_back(static_cast<char>(r.below(256)));
			}
		}
		s.resize(n);
		return s;
	}

	void put_str_bytes(const std::string& s) {
		std::size_t n = s.size();
		std::vector<int> widths;
		if (n <= 31) widths.push_back(0);
		if (n <= 0xff) widths.push_back(1);
		if (n <= 0xffff) widths.push_back(2);
		widths.push_back(4);
		int w = widths[r.chance(2, 3) ? 0 : r.below(widths.size())];
		switch (w) {
			case 0: head(static_cast<std::uint8_t>(0xa0 | n)); break;
			case 1: head(0xd9); be(n, 1); break;
			case 2: head(0xda); be(n, 2); break;
			default: head(0xdb); be(n, 4); break;
		}
		out() += s;
	}

	std::size_t random_length(std::size_t big) {
		unsigned x = static_cast<unsigned>(r.below(1000));
		if (x < 600) return r.below(5);
		if (x < 850) return 5 + r.below(12);
		if (x < 970) return 17 + r.below(24);
		if (x < 995) return 41 + r.below(big);
		return 41 + r.below(big * 3);
	}

	void put_container_head(bool is_map, std::size_t n) {
		if (is_map) {
			if (n <= 15 && r.chance(3, 4)) {
				head(static_cast<std::uint8_t>(0x80 | n));
			} else if (n <= 0xffff && r.chance(1, 2)) {
				head(0xde); be(n, 2);
			} else {
				head(0xdf); be(n, 4);
			}
		} else {
			if (n <= 15 && r.chance(3, 4)) {
				head(static_cast<std::uint8_t>(0x90 | n));
			} else if (n <= 0xffff && r.chance(1, 2)) {
				head(0xdc); be(n, 2);
			} else {
				head(0xdd); be(n, 4);
			}
		}
	}

	std::string key_string() {
		static const std::string pool[] = { "", "a", "b", "ab", std::string("a\0", 2), "key with space", "a\\b" };
		switch (key_mode) {
			case 1:
				return pool[r.below(sizeof(pool) / sizeof(pool[0]))];
			case 2:
				if (r.chance(1, 3)) return pool[r.below(sizeof(pool) / sizeof(pool[0]))];
				[[fallthrough]];
			default:
				return "k" + std::to_string(counter++) + random_bytes(r.below(4));
		}
	}

public:
	Gen(Rng& rng, Doc& d) : r(rng), doc(d), key_mode(static_cast<int>(rng.below(4))), allow_non_string(rng.chance(1, 20)) { }

	void map(int depth, std::size_t n) {
		std::vector<std::string> keys;
		for (std::size_t i = 0; i < n; ++i) {
			keys.push_back(key_string());
		}
		if (key_mode == 3 && n >= 2 && r.chance(1, 2)) {
			keys[r.below(n)] = keys[r.below(n)];   // one repeat among many
		}
		put_container_head(true, n);
		for (std::size_t i = 0; i < n; ++i) {
			if (allow_non_string && r.chance(1, 6)) {
				doc.non_string_key = true;
				switch (r.below(4)) {
					case 0: head(0xc0); break;
					case 1: put_uint(r.below(300)); break;
					case 2: head(0xc4); head(2); out() += "xy"; break;
					default: head(0xc3); break;
				}
			} else {
				put_str_bytes(keys[i]);
			}
			value(depth + 1);
		}
	}

	void array(int depth, std::size_t n) {
		put_container_head(false, n);
		for (std::size_t i = 0; i < n; ++i) {
			value(depth + 1);
		}
	}

	void value(int depth) {
		unsigned x = static_cast<unsigned>(r.below(100));
		bool container = depth < 5 && x < (depth == 0 ? 90u : 28u);
		if (container) {
			std::size_t n = random_length(depth == 0 ? 40 : 20);
			if (r.chance(1, 2)) map(depth, n); else array(depth, n);
			return;
		}
		switch (r.below(14)) {
			case 0: head(0xc0); break;
			case 1: head(0xc2); break;
			case 2: head(0xc3); break;
			case 3: case 4: put_uint(random_u64()); break;
			case 5: case 6: put_int(random_negative()); break;
			case 7: put_double(); break;
			case 8: put_float(); break;
			case 9: case 10: put_str_bytes(random_bytes(random_length(300))); break;
			case 11: {
				std::string b = random_bytes(random_length(300));
				std::size_t n = b.size();
				if (n <= 0xff && r.chance(2, 3)) { head(0xc4); be(n, 1); }
				else if (n <= 0xffff && r.chance(1, 2)) { head(0xc5); be(n, 2); }
				else { head(0xc6); be(n, 4); }
				out() += b;
				break;
			}
			case 12: {
				static const std::size_t fixed[] = { 1, 2, 4, 8, 16 };
				std::int8_t type = static_cast<std::int8_t>(r.chance(1, 4) ? r.below(256) : r.below(4));
				if (r.chance(1, 2)) {
					std::size_t n = fixed[r.below(5)];
					static const std::uint8_t fix[] = { 0xd4, 0xd5, 0xd6, 0xd7, 0xd8 };
					head(fix[n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : n == 8 ? 3 : 4]);
					head(static_cast<std::uint8_t>(type));
					out() += random_bytes(n);
				} else {
					std::size_t n = random_length(40);
					if (n <= 0xff) { head(0xc7); be(n, 1); }
					else if (n <= 0xffff) { head(0xc8); be(n, 2); }
					else { head(0xc9); be(n, 4); }
					head(static_cast<std::uint8_t>(type));
					out() += random_bytes(n);
				}
				break;
			}
			default: {
				std::size_t n = random_length(10);
				if (r.chance(1, 2)) map(depth, n); else array(depth, n);
				break;
			}
		}
	}
};


Doc random_doc(Rng& r) {
	Doc doc;
	Gen g(r, doc);
	g.value(0);
	return doc;
}


std::string current_exception_text() {
	try {
		throw;
	} catch (const BaseException& e) {
		return std::string(typeid(e).name()) + ": " + e.get_message();
	} catch (const std::exception& e) {
		return std::string(typeid(e).name()) + ": " + e.what();
	} catch (...) {
		return "unknown exception";
	}
}


std::string hex(const std::string& s) {
	static const char* digits = "0123456789abcdef";
	std::string r;
	for (unsigned char c : s) {
		r.push_back(digits[c >> 4]);
		r.push_back(digits[c & 15]);
	}
	return r;
}


enum Op {
	HASH, SERIALISE, JSON, JSON_INDENT, COPY, COPY_MUTATE, WALK, MUTATE, ERASE, MOVE, ROUNDTRIP, SIZE, N_OPS
};

struct Step {
	Op op;
	int arg;
};


// Appends the structure of `m` read through the public API (which fills every map and array on
// the way); a map is walked by position and by key.
template <typename M>
void walk(M& m, std::string& out, int depth) {
	if (depth > 12) {
		out += "...";
		return;
	}
	if (m.is_map()) {
		out += '{';
		for (auto it = m.begin(); it != m.end(); ++it) {
			out += hex(it->serialise());
			out += ':';
			walk(it.value(), out, depth + 1);
			out += ',';
		}
		out += '}';
	} else if (m.is_array()) {
		out += '[';
		for (std::size_t i = 0; i < m.size(); ++i) {
			walk(m.at(i), out, depth + 1);
			out += ',';
		}
		out += ']';
	} else {
		out += hex(m.serialise());
	}
}


template <typename M>
void mutate(M& m, int arg) {
	if (m.is_map()) {
		m["zz_new"] = arg;
		m["zz_text"] = "a string long enough to leave the small buffers behind";
		m["zz_text"] = "replaced";
	} else if (m.is_array()) {
		m.push_back(arg);
		m.push_back("tail");
	} else {
		m = arg;
	}
}


// What the maps of a document do with their keys.
struct KeyShape {
	bool repeats = false;        // some map holds a string key twice
	bool non_string = false;     // some map has a key that is not a string
	bool array_of_maps = false;  // the document is an array with a map somewhere inside
};

bool contains_map(const msgpack::object& o) {
	if (o.type == msgpack::type::MAP) return true;
	if (o.type == msgpack::type::ARRAY) {
		for (std::uint32_t i = 0; i < o.via.array.size; ++i) {
			if (contains_map(o.via.array.ptr[i])) return true;
		}
	}
	return false;
}

void key_shape(const msgpack::object& o, KeyShape& shape) {
	if (o.type == msgpack::type::MAP) {
		for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
			const auto& a = o.via.map.ptr[i].key;
			if (a.type != msgpack::type::STR) {
				shape.non_string = true;
			} else {
				for (std::uint32_t j = 0; j < i; ++j) {
					const auto& b = o.via.map.ptr[j].key;
					if (b.type == msgpack::type::STR && a.via.str.size == b.via.str.size &&
						std::memcmp(a.via.str.ptr, b.via.str.ptr, a.via.str.size) == 0) {
						shape.repeats = true;
					}
				}
			}
			key_shape(o.via.map.ptr[i].val, shape);
		}
	} else if (o.type == msgpack::type::ARRAY) {
		for (std::uint32_t i = 0; i < o.via.array.size; ++i) {
			key_shape(o.via.array.ptr[i], shape);
		}
	}
}


// The shape of the document at the start of `bytes`; a document that does not parse has none.
KeyShape document_key_shape(const std::string& bytes) {
	KeyShape shape;
	try {
		std::size_t off = 0;
		msgpack::object_handle h = msgpack::unpack(bytes.data(), bytes.size(), off);
		key_shape(h.get(), shape);
		shape.array_of_maps = h.get().type == msgpack::type::ARRAY && contains_map(h.get());
	} catch (const std::exception&) {
		shape = KeyShape();
	}
	return shape;
}


// The document at the start of `bytes` with every map merged as test/model.h says (no byte after
// it): what the frozen implementation reads in place of a document that repeats a key.
std::string merged_document(const std::string& bytes) {
	std::size_t off = 0;
	msgpack::object_handle h = msgpack::unpack(bytes.data(), bytes.size(), off);
	msgpack::sbuffer buf;
	msgpack::packer<msgpack::sbuffer> pk(&buf);
	model::pack_merged(pk, h.get());
	return std::string(buf.data(), buf.size());
}


// Touches every node through the API, which fills (and merges) every map and array.
template <typename M>
void fill_all(M& m) {
	if (m.is_map()) {
		for (auto it = m.begin(); it != m.end(); ++it) {
			fill_all(it.value());
		}
	} else if (m.is_array()) {
		for (std::size_t i = 0; i < m.size(); ++i) {
			fill_all(m.at(i));
		}
	}
}


// What `run` does with a document.
struct Mode {
	bool merged_input = false;   // the document repeats keys and the current implementation reads it as it is:
	                             // the offset is not logged (the frozen side reads the merged bytes, which are
	                             // shorter) and every map is filled first, so the merge has been done before the
	                             // first operation, as in the frozen side's input
	bool mutate = true;          // run the steps that change the value (mutate, erase)
	bool copy_mutate = true;     // run the step that changes a copy
	bool stop_at_throw = false;  // end the sequence at the first step that throws: filling a map that has a key
	                             // that is not a string throws, and the next fill of that map gives something else
	                             // in each implementation (see "Known problems" in CHANGELOG)
};


// Runs `steps` on the document in `bytes`.
template <typename M>
std::vector<std::string> run(const std::string& bytes, const Doc& doc, const std::vector<Step>& steps, bool lock_at_end, const Mode& mode) {
	const bool merged_input = mode.merged_input;
	std::vector<std::string> log;
	auto step = [&](const char* name, auto&& f) {
		try {
			log.push_back(std::string(name) + ": " + f());
		} catch (...) {
			log.push_back(std::string(name) + " threw " + current_exception_text());
		}
	};

	std::size_t off = 0;
	std::unique_ptr<M> root;   // the value as unserialise returns it, not assigned into another
	try {
		root.reset(new M(M::unserialise(bytes.data(), bytes.size(), off)));
		log.push_back(merged_input ? std::string("unserialise") : "unserialise off " + std::to_string(off));
	} catch (...) {
		log.push_back("unserialise threw " + current_exception_text() + (merged_input ? std::string() : " off " + std::to_string(off)));
		return log;
	}
	M& cur = *root;
	if (merged_input) fill_all(cur);

	for (const Step& s : steps) {
		switch (s.op) {
			case HASH:
				step("hash", [&] { return std::to_string(cur.hash()); });
				break;
			case SERIALISE:
				step("serialise", [&] { return hex(cur.serialise()); });
				break;
			case JSON:
				if (!doc.non_string_key) step("json", [&] { return cur.to_string(); });
				break;
			case JSON_INDENT:
				if (!doc.non_string_key) step("json indent", [&] { return cur.to_string(s.arg % 5); });
				break;
			case COPY:
				step("copy", [&] {
					M c(cur);
					std::string r = hex(c.serialise());
					r += " " + std::to_string(c.hash());
					return r;
				});
				break;
			case COPY_MUTATE:
				if (mode.copy_mutate) step("copy mutate", [&] {
					M c(cur);
					mutate(c, s.arg);
					return hex(c.serialise()) + " | " + hex(cur.serialise());
				});
				break;
			case WALK:
				step("walk", [&] { std::string r; walk(cur, r, 0); return r; });
				break;
			case MUTATE:
				if (mode.mutate) step("mutate", [&] { mutate(cur, s.arg); return hex(cur.serialise()); });
				break;
			case ERASE:
				// What erase() returns is not compared: the frozen implementation returns 0 for the last
				// element of a map or an array, which it did erase (see the CHANGELOG). The change in size
				// says how many were erased.
				if (mode.mutate) step("erase", [&] {
					if (cur.is_map()) {
						const std::size_t before = cur.size();
						cur.erase(std::string_view("a"));
						return std::to_string(before - cur.size()) + " " + hex(cur.serialise());
					}
					if (cur.is_array()) {
						const std::size_t before = cur.size();
						cur.erase(static_cast<std::size_t>(0));
						return std::to_string(before - cur.size()) + " " + hex(cur.serialise());
					}
					return std::string("scalar");
				});
				break;
			case MOVE:
				step("move", [&] {
					M t(std::move(cur));
					cur = std::move(t);
					return hex(cur.serialise());
				});
				break;
			case ROUNDTRIP:
				step("roundtrip", [&] {
					std::string wire = cur.serialise();
					std::string padded = wire + "\xc1junk";
					std::size_t o = 0;
					M u = M::unserialise(padded.data(), padded.size(), o);
					std::string r = std::to_string(o) + " " + hex(u.serialise()) + " " + std::to_string(u.hash());
					std::size_t o2 = 0;
					M v = M::unserialise(std::string_view(wire), o2);
					r += " " + std::to_string(o2) + " " + hex(v.serialise());
					return r;
				});
				break;
			case SIZE:
				step("size", [&] { return std::to_string(cur.size()); });
				break;
			default:
				break;
		}
		if (mode.stop_at_throw && log.back().find(" threw ") != std::string::npos) return log;
	}
	if (lock_at_end) {
		step("lock", [&] { cur.lock(); return std::to_string(cur.locked()); });
		step("hash locked", [&] { return std::to_string(cur.hash()); });
		step("serialise locked", [&] { return hex(cur.serialise()); });
		if (!doc.non_string_key) step("json locked", [&] { return cur.to_string(); });
	}
	return log;
}


// The value `hash()` must have for a document without repeated keys: the XOR over every map of
// hash(key) ^ hash(value), and the hash of the packed bytes of everything else. Written from
// the definition, on the msgpack tree, with a std::string and an sbuffer.
std::size_t oracle_hash(const msgpack::object& o) {
	if (o.type == msgpack::type::MAP) {
		std::size_t h = 0;
		for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
			h ^= std::hash<std::string>()(std::string(o.via.map.ptr[i].key.via.str.ptr, o.via.map.ptr[i].key.via.str.size));
			h ^= oracle_hash(o.via.map.ptr[i].val);
		}
		return h;
	}
	msgpack::sbuffer buf;
	msgpack::pack(&buf, o);
	return std::hash<std::string>()(std::string(buf.data(), buf.size()));
}


struct Stats {
	std::size_t docs = 0, parsed = 0, threw = 0, with_repeats = 0, not_compared = 0, non_finite = 0, steps = 0;
};

Stats stats;
std::size_t failures = 0;


std::size_t g_nonfinite_text = 0;   // JSON texts of documents with a NaN or Inf where the two differ as they should


void compare_logs(const char* what, std::uint64_t seed, const Doc& doc, const std::vector<std::string>& a, const std::vector<std::string>& b) {
	std::size_t n = std::min(a.size(), b.size());
	for (std::size_t i = 0; i < n; ++i) {
		// The one text that differs by design: the frozen implementation stops at a NaN or Inf (the
		// Writer refuses it) and leaves the text before it, the current one writes null there and goes
		// on. So the old text must be the beginning of the new one (the same calls, in the same order,
		// up to the number), and equal to it when the number was erased before the text was written.
		if (doc.non_finite && a[i] != b[i] && a[i].rfind("json", 0) == 0 && b[i].compare(0, a[i].size(), a[i]) == 0) {
			++g_nonfinite_text;
			continue;
		}
		if (a[i] != b[i]) {
			std::fprintf(stderr, "%s: seed %" PRIu64 " step %zu differs\n  doc %s\n  old %s\n  new %s\n", what, seed, i,
				hex(doc.bytes).c_str(), a[i].substr(0, 600).c_str(), b[i].substr(0, 600).c_str());
			++failures;
			return;
		}
	}
	if (a.size() != b.size()) {
		std::fprintf(stderr, "%s: seed %" PRIu64 " logs have %zu and %zu entries\n", what, seed, a.size(), b.size());
		++failures;
	}
}


void check_document(std::uint64_t seed, const Doc& doc, Rng& r) {
	++stats.docs;
	if (std::getenv("MSGPACK_DIFF_TRACE")) {   // names the document a crash happened in
		std::fprintf(stderr, "document seed %" PRIu64 " %s\n", seed, hex(doc.bytes.substr(0, 300)).c_str());
	}
	const KeyShape shape = document_key_shape(doc.bytes);
	std::vector<Step> steps;
	std::size_t n_steps = 2 + r.below(10);
	for (std::size_t i = 0; i < n_steps; ++i) {
		steps.push_back(Step{ static_cast<Op>(r.below(N_OPS)), static_cast<int>(r.below(1000)) });
	}
	// Plain runs of the operations that are most sensitive to order: the first thing done to a
	// document decides whether hash() or a walk fills (and merges) the maps first.
	static const Op orders[][3] = {
		{ HASH, SERIALISE, COPY }, { SERIALISE, HASH, JSON }, { WALK, HASH, SERIALISE }, { JSON, COPY, HASH },
	};
	const bool lock_at_end = r.chance(1, 8);
	if (shape.repeats && shape.non_string) {
		// Not compared (see the top of the file), but the current implementation still reads it, so
		// a sanitizer sees what it does with a map that throws half way through its merge.
		++stats.not_compared;
		Mode reads_only;
		reads_only.mutate = reads_only.copy_mutate = false;
		for (const auto& order : orders) {
			std::vector<Step> fixed;
			for (Op op : order) fixed.push_back(Step{ op, 0 });
			run<MsgPack>(doc.bytes, doc, fixed, false, reads_only);
		}
		run<MsgPack>(doc.bytes, doc, steps, lock_at_end, reads_only);
		return;
	}
	// A document that repeats a string key is read by the frozen implementation as the merged
	// document, by the current one as it is.
	const bool merge = shape.repeats;
	const std::string old_bytes = merge ? merged_document(doc.bytes) : doc.bytes;
	// A step that changes the value is left out where the frozen implementation is wrong by itself:
	// an array with a map in it, grown after the maps were filled, gave elements that point into
	// freed memory (test/growth_test.cc covers that against the model). A sequence on a document
	// with a key that is not a string ends at its first exception (see Mode::stop_at_throw).
	Mode mode;
	mode.merged_input = merge;
	mode.mutate = !shape.array_of_maps;
	mode.stop_at_throw = shape.non_string;
	for (const auto& order : orders) {
		std::vector<Step> fixed;
		for (Op op : order) fixed.push_back(Step{ op, 0 });
		auto a = run<MsgPackOld>(old_bytes, doc, fixed, false, mode);
		auto b = run<MsgPack>(doc.bytes, doc, fixed, false, mode);
		compare_logs("fixed order", seed, doc, a, b);
	}
	auto a = run<MsgPackOld>(old_bytes, doc, steps, lock_at_end, mode);
	auto b = run<MsgPack>(doc.bytes, doc, steps, lock_at_end, mode);
	stats.steps += steps.size();
	compare_logs("random ops", seed, doc, a, b);
	if (a[0].find("threw") != std::string::npos) ++stats.threw; else ++stats.parsed;
	if (doc.non_finite) ++stats.non_finite;

	// hash() against the definition: the first thing done to a document that repeats no key, and
	// to one that does, whose maps are merged by it (those reached through maps; an array is
	// packed as it is).
	std::size_t off = 0;
	try {
		msgpack::object_handle h = msgpack::unpack(doc.bytes.data(), doc.bytes.size(), off);
		if (shape.non_string) return;
		if (merge) ++stats.with_repeats;
		MsgPack m = MsgPack::unserialise(doc.bytes);
		std::size_t expected = merge ? model::hash_merged_maps(h.get()) : oracle_hash(h.get());
		if (m.hash() != expected) {
			std::fprintf(stderr, "hash oracle: seed %" PRIu64 " doc %s: got %zu, expected %zu\n", seed, hex(doc.bytes).c_str(), m.hash(), expected);
			++failures;
		}
	} catch (const std::exception&) {
	}
}


// Every prefix of a document: both types either throw the same exception or read the same value.
void check_prefixes(std::uint64_t seed, const Doc& doc) {
	std::size_t n = doc.bytes.size();
	std::size_t stride = n > 400 ? n / 200 : 1;
	for (std::size_t len = 0; len < n; len += stride) {
		const char* data = doc.bytes.data();
		std::string a, b;
		std::size_t oa = 0, ob = 0;
		try {
			MsgPackOld m = MsgPackOld::unserialise(data, len, oa);
			a = "ok " + std::to_string(oa) + " " + hex(m.serialise());
		} catch (...) {
			a = "threw " + current_exception_text() + " " + std::to_string(oa);
		}
		try {
			MsgPack m = MsgPack::unserialise(data, len, ob);
			b = "ok " + std::to_string(ob) + " " + hex(m.serialise());
		} catch (...) {
			b = "threw " + current_exception_text() + " " + std::to_string(ob);
		}
		if (a != b) {
			std::fprintf(stderr, "prefix %zu: seed %" PRIu64 "\n  old %s\n  new %s\n", len, seed, a.substr(0, 300).c_str(), b.substr(0, 300).c_str());
			++failures;
			return;
		}
	}
}


std::string str_header(std::size_t n) {
	std::string h;
	if (n <= 31) {
		h.push_back(static_cast<char>(0xa0 | n));
	} else if (n <= 0xff) {
		h.push_back(static_cast<char>(0xd9)); h.push_back(static_cast<char>(n));
	} else if (n <= 0xffff) {
		h.push_back(static_cast<char>(0xda)); h.push_back(static_cast<char>(n >> 8)); h.push_back(static_cast<char>(n));
	} else {
		h.push_back(static_cast<char>(0xdb));
		for (int i = 3; i >= 0; --i) h.push_back(static_cast<char>(n >> (8 * i)));
	}
	return h;
}

std::string str_obj(const std::string& s) {
	return str_header(s.size()) + s;
}

std::string map_head(std::size_t n) {
	std::string h;
	if (n <= 15) {
		h.push_back(static_cast<char>(0x80 | n));
	} else {
		h.push_back(static_cast<char>(0xdf));
		for (int i = 3; i >= 0; --i) h.push_back(static_cast<char>(n >> (8 * i)));
	}
	return h;
}

std::string array_head(std::size_t n) {
	std::string h;
	if (n <= 15) {
		h.push_back(static_cast<char>(0x90 | n));
	} else {
		h.push_back(static_cast<char>(0xdd));
		for (int i = 3; i >= 0; --i) h.push_back(static_cast<char>(n >> (8 * i)));
	}
	return h;
}


// Documents chosen by hand: the shapes the random generator reaches only by luck.
std::vector<std::pair<std::string, Doc>> special_documents() {
	std::vector<std::pair<std::string, Doc>> docs;
	auto add = [&](std::string name, std::string bytes, bool non_string = false, bool non_finite = false) {
		Doc d;
		d.bytes = std::move(bytes);
		d.non_string_key = non_string;
		d.non_finite = non_finite;
		docs.emplace_back(std::move(name), std::move(d));
	};
	const std::string one = "\x01";
	// {"a": 1, "a": 2, "b": 3, "c": 4}: the repeat is merged, and the entries after it must follow
	add("repeat then two", map_head(4) + str_obj("a") + one + str_obj("a") + "\x02" + str_obj("b") + "\x03" + str_obj("c") + "\x04");
	add("repeat last", map_head(3) + str_obj("a") + one + str_obj("b") + "\x02" + str_obj("a") + "\x03");
	add("repeat first two", map_head(2) + str_obj("a") + one + str_obj("a") + "\x02");
	add("three equal", map_head(3) + str_obj("a") + one + str_obj("a") + "\x02" + str_obj("a") + "\x03");
	add("nested repeat", map_head(2) + str_obj("x") + map_head(2) + str_obj("k") + one + str_obj("k") + "\x02" + str_obj("y") + one);
	add("repeat under array", map_head(1) + str_obj("x") + array_head(1) + map_head(2) + str_obj("k") + one + str_obj("k") + "\x02");
	add("repeat under array under map", array_head(1) + map_head(2) + str_obj("k") + one + str_obj("k") + "\x02");
	add("empty key twice", map_head(2) + str_obj("") + one + str_obj("") + "\x02");
	add("prefix keys", map_head(3) + str_obj("a") + one + str_obj(std::string("a\0", 2)) + "\x02" + str_obj("ab") + "\x03");
	add("empty map", "\x80");
	add("empty array", "\x90");
	add("empty str", "\xa0");
	add("empty map in map", map_head(1) + str_obj("m") + "\x80");
	add("non string key", map_head(2) + str_obj("a") + one + "\x05" + "\x02", true);
	add("non string key nested", map_head(1) + str_obj("a") + map_head(1) + "\xc0" + one, true);
	add("nan", "\xcb\x7f\xf8\x00\x00\x00\x00\x00\x00", false, true);
	add("nan in array", array_head(3) + one + "\xcb\x7f\xf8\x00\x00\x00\x00\x00\x00" + one, false, true);
	add("inf in map", map_head(2) + str_obj("a") + one + str_obj("b") + "\xca\x7f\x80\x00\x00", false, true);
	add("minus zero", "\xcb\x80\x00\x00\x00\x00\x00\x00\x00");
	add("denormal", "\xcb\x00\x00\x00\x00\x00\x00\x00\x01");
	add("uint64 max", "\xcf\xff\xff\xff\xff\xff\xff\xff\xff");
	add("int64 min", "\xd3\x80\x00\x00\x00\x00\x00\x00\x00");
	add("uint64 in int64 form", "\xd3\x00\x00\x00\x00\x00\x00\x00\x07");
	add("ext 5", "\xd4\x05\x07");
	add("ext undefined type", "\xd4\x00\x00");
	add("ext negative type", "\xd4\xff\x01");
	add("ext in map", map_head(1) + str_obj("e") + "\xc7\x03\x7f" + "abc");
	add("bin in map", map_head(1) + str_obj("b") + "\xc4\x03" + "abc");
	add("nul and control", map_head(1) + str_obj(std::string("k\0\x01\x1f", 4)) + str_obj(std::string("v\0\"\\\n\t\x7f", 7)));
	add("invalid utf8", str_obj("\xff\xfe\xc3"));

	// 200 levels of maps and of arrays
	{
		std::string m, a;
		for (int i = 0; i < 200; ++i) { m += map_head(1) + str_obj("k"); a += array_head(1); }
		add("deep maps", m + "\x07");
		add("deep arrays", a + "\x07");
		std::string mixed;
		for (int i = 0; i < 200; ++i) mixed += (i % 2 ? array_head(1) : map_head(1) + str_obj("k"));
		add("deep mixed", mixed + "\x07");
	}
	// a map of 40, 300 and 3000 distinct keys; the same with the last key repeating the first
	for (std::size_t n : { 17u, 40u, 300u, 3000u }) {
		std::string distinct = map_head(n), repeated = map_head(n);
		for (std::size_t i = 0; i < n; ++i) {
			distinct += str_obj("key" + std::to_string(i)) + one;
			repeated += str_obj(i + 1 == n ? "key0" : "key" + std::to_string(i)) + one;
		}
		std::string tag = std::to_string(n);
		add("map of " + tag, distinct);
		add("map of " + tag + " repeating", repeated);
	}
	// long strings, as leaves, keys and bin; and an array root holding a long packed value
	{
		std::string big(70000, 'x'), mid(300, 'y'), edge(250, 'z');
		add("70 KB string", str_obj(big));
		add("string in map", map_head(2) + str_obj("big") + str_obj(big) + str_obj(mid) + str_obj(edge));
		add("long key", map_head(1) + str_obj(big) + one);
		add("array root of long values", array_head(3) + str_obj(big) + str_obj(mid) + str_obj(edge));
		add("array root of 20000 ints", array_head(20000) + std::string(20000, '\x05'));
		std::string packed_edge;   // packed values of exactly 255, 256 and 257 bytes
		for (std::size_t n : { 252u, 253u, 254u, 255u }) packed_edge += str_obj(std::string(n, 'e'));
		add("packed size around 256", array_head(4) + packed_edge);
		add("map of values around 256", map_head(4) + str_obj("a") + str_obj(std::string(250, 'q')) + str_obj("b") + str_obj(std::string(251, 'q')) +
			str_obj("c") + str_obj(std::string(252, 'q')) + str_obj("d") + str_obj(std::string(253, 'q')));
	}
	return docs;
}

}  // namespace


int main(int argc, char** argv) {
	std::size_t iterations = 1500;
	std::uint64_t base_seed = 1;
	bool single = false;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--stress") && i + 1 < argc) {
			iterations = std::strtoull(argv[++i], nullptr, 10);
		} else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) {
			base_seed = std::strtoull(argv[++i], nullptr, 10);
			single = true;
		}
	}

	std::size_t special = 0;
	for (auto& [name, doc] : special_documents()) {
		Rng r{ 12345 + special };
		check_document(1000000 + special, doc, r);
		check_prefixes(1000000 + special, doc);
		// every special document gets every order of the sensitive operations, and a lot of random ones
		for (int k = 0; k < 6; ++k) {
			Rng q{ 777 + special * 100 + k };
			check_document(2000000 + special * 100 + k, doc, q);
		}
		++special;
	}
	std::printf("special documents: %zu, failures so far %zu\n", special, failures);
	CHECK(failures == 0);

	if (single) {
		Rng r{ base_seed };
		Doc doc = random_doc(r);
		check_document(base_seed, doc, r);
		check_prefixes(base_seed, doc);
		CHECK(failures == 0);
		std::printf("seed %" PRIu64 " ok\n", base_seed);
		return 0;
	}

	for (std::size_t i = 0; i < iterations; ++i) {
		std::uint64_t seed = base_seed + i * 7919;
		Rng r{ seed };
		Doc doc = random_doc(r);
		check_document(seed, doc, r);
		if (i % 4 == 0) check_prefixes(seed, doc);
		if (failures > 5) break;
	}
	std::printf("documents %zu (read %zu, rejected %zu; %zu with repeated keys compared against the merged document, %zu with repeated and non-string keys not compared, %zu with NaN or Inf, %zu of their texts go on past the number), operations %zu, failures %zu\n",
		stats.docs, stats.parsed, stats.threw, stats.with_repeats, stats.not_compared, stats.non_finite, g_nonfinite_text, stats.steps, failures);
	CHECK(failures == 0);
	CHECK(g_nonfinite_text > 0);
	CHECK(stats.parsed > stats.docs / 2);
	CHECK(stats.with_repeats > 20);
	CHECK(stats.not_compared < stats.docs / 4);
	std::printf("msgpack differential OK\n");
	return 0;
}
