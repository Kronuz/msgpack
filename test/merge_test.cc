// A map holds each key once. A document that repeats a key is merged when its map is first
// filled: the last value wins, in the position of the key's first occurrence. This test checks
// that against a reference model (test/model.h: std::map for the keys, a vector for the order)
// on hand-written documents, on every document of up to 6 entries over 3 keys, and on random
// nested documents with repeats at any depth, and then keeps changing the merged document and
// the model together with erase, set and add.
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "msgpack.h"
#include "model.h"

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			std::exit(1); \
		} \
	} while (0)

#define CHECK_EQ(a, b, context) \
	do { \
		if (!((a) == (b))) { \
			std::fprintf(stderr, "%s:%d: %s: results differ\n  got      %s\n  expected %s\n", __FILE__, __LINE__, context, hex(a).c_str(), hex(b).c_str()); \
			std::exit(1); \
		} \
	} while (0)

namespace {

std::string hex(const std::string& s) {
	static const char* d = "0123456789abcdef";
	std::string r;
	for (unsigned char c : s) {
		r.push_back(d[c >> 4]);
		r.push_back(d[c & 15]);
	}
	return r.size() > 400 ? r.substr(0, 400) + "..." : r;
}

struct Rng {
	std::uint64_t state;
	std::uint64_t next() {
		std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
		return z ^ (z >> 31);
	}
	std::uint64_t below(std::uint64_t n) { return n ? next() % n : 0; }
};

// ---- documents as bytes, with repeats allowed

std::string str_bytes(const std::string& s) {
	std::string r;
	if (s.size() < 32) {
		r.push_back(static_cast<char>(0xa0 | s.size()));
	} else {
		r.push_back(static_cast<char>(0xd9));
		r.push_back(static_cast<char>(s.size()));
	}
	return r + s;
}

std::string uint_bytes(std::uint64_t v) {
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

std::string map_head(std::size_t n) {
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

std::string array_head(std::size_t n) {
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

// {k0: v0, k1: v1, ...} where each pair is (key, bytes of the value)
std::string map_of(const std::vector<std::pair<std::string, std::string>>& entries) {
	std::string r = map_head(entries.size());
	for (const auto& e : entries) {
		r += str_bytes(e.first) + e.second;
	}
	return r;
}

// ---- the operations the tests apply to a MsgPack and to the model

// Touches every node through the API, which fills every map and array on the way.
void fill_all(MsgPack& m) {
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

// The model document for bytes that have been merged: no map repeats a key.
model::Doc doc_of(const msgpack::object& o) {
	switch (o.type) {
		case msgpack::type::POSITIVE_INTEGER:
			return model::Doc::num(o.via.u64);
		case msgpack::type::STR:
			return model::Doc::str(std::string(o.via.str.ptr, o.via.str.size));
		case msgpack::type::ARRAY: {
			model::Doc d = model::Doc::arr();
			for (std::uint32_t i = 0; i < o.via.array.size; ++i) {
				d.a.push_back(doc_of(o.via.array.ptr[i]));
			}
			return d;
		}
		case msgpack::type::MAP: {
			model::Doc d = model::Doc::map();
			for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
				d.m.emplace_back(std::string(o.via.map.ptr[i].key.via.str.ptr, o.via.map.ptr[i].key.via.str.size), doc_of(o.via.map.ptr[i].val));
			}
			return d;
		}
		default:
			return model::Doc::nil();
	}
}

model::Doc doc_of_bytes(const std::string& bytes) {
	msgpack::object_handle h = msgpack::unpack(bytes.data(), bytes.size());
	return doc_of(h.get());
}

void set_leaf(MsgPack& m, const std::string& key, const model::Doc& v) {
	switch (v.kind) {
		case model::Doc::UINT: m[key] = v.u; break;
		case model::Doc::STR: m[key] = v.s; break;
		default: m[key] = nullptr; break;
	}
}

// ---- hand-written cases

struct Case {
	const char* name;
	std::string wire;
	const char* expected_json;
};

void test_cases() {
	const std::string one = "\x01", two = "\x02", three = "\x03", four = "\x04", five = "\x05";
	std::vector<Case> cases = {
		{ "repeat then two", map_of({ { "a", one }, { "a", two }, { "b", three }, { "c", four } }), "{\"a\":2,\"b\":3,\"c\":4}" },
		{ "repeat at the end", map_of({ { "a", one }, { "b", two }, { "a", three } }), "{\"a\":3,\"b\":2}" },
		{ "repeat only", map_of({ { "a", one }, { "a", two } }), "{\"a\":2}" },
		{ "three equal", map_of({ { "a", one }, { "a", two }, { "a", three } }), "{\"a\":3}" },
		{ "interleaved", map_of({ { "a", one }, { "b", two }, { "a", three }, { "b", four }, { "c", five } }), "{\"a\":3,\"b\":4,\"c\":5}" },
		{ "repeat in the middle", map_of({ { "a", one }, { "b", two }, { "b", three }, { "c", four }, { "d", five } }), "{\"a\":1,\"b\":3,\"c\":4,\"d\":5}" },
		{ "empty key", map_of({ { "", one }, { "a", two }, { "", three } }), "{\"\":3,\"a\":2}" },
		{ "prefixes are different keys", map_of({ { "a", one }, { "ab", two }, { "a", three }, { "abc", four } }), "{\"a\":3,\"ab\":2,\"abc\":4}" },
		{ "nested repeat", map_of({ { "x", map_of({ { "k", one }, { "k", two }, { "y", three }, { "z", four } }) }, { "a", one } }), "{\"x\":{\"k\":2,\"y\":3,\"z\":4},\"a\":1}" },
		{ "the last value is a map", map_of({ { "a", one }, { "b", two }, { "a", map_of({ { "q", three }, { "q", four } }) } }), "{\"a\":{\"q\":4},\"b\":2}" },
		{ "repeat under an array", array_head(2) + map_of({ { "k", one }, { "k", two }, { "m", three } }) + map_of({ { "n", one }, { "n", two }, { "o", three } }), "[{\"k\":2,\"m\":3},{\"n\":2,\"o\":3}]" },
	};
	for (const auto& c : cases) {
		MsgPack m = MsgPack::unserialise(c.wire);
		fill_all(m);
		CHECK_EQ(m.to_string(), std::string(c.expected_json), c.name);
		CHECK_EQ(m.serialise(), model::merged(c.wire), c.name);
		// the copy, the round trip and the hash see the merged document
		CHECK_EQ(MsgPack(m).serialise(), model::merged(c.wire), c.name);
		CHECK_EQ(MsgPack::unserialise(m.serialise()).to_string(), std::string(c.expected_json), c.name);
	}

	// Looking up a key after the merge finds the last value, and every key is found.
	{
		MsgPack m = MsgPack::unserialise(map_of({ { "a", one }, { "a", two }, { "b", three }, { "c", four } }));
		CHECK(m["a"].as_u64() == 2);
		CHECK(m.size() == 3);
		CHECK(m["b"].as_u64() == 3);
		CHECK(m["c"].as_u64() == 4);
		CHECK(m.size() == 3);
		CHECK(m.count("c") == 1);
		CHECK(m.find("zz") == m.end());
	}
	// ... and so does the first thing done to a map being a hash, a walk or an erase
	{
		const std::string wire = map_of({ { "a", one }, { "a", two }, { "b", three }, { "c", four } });
		{
			MsgPack m = MsgPack::unserialise(wire);
			m.erase("a");
			CHECK_EQ(m.serialise(), map_of({ { "b", three }, { "c", four } }), "erase first");
		}
		{
			MsgPack m = MsgPack::unserialise(wire);
			m.erase("c");
			CHECK_EQ(m.serialise(), map_of({ { "a", two }, { "b", three } }), "erase last");
			m.erase("c");
			CHECK_EQ(m.serialise(), map_of({ { "a", two }, { "b", three } }), "erase a missing key");
		}
		{
			MsgPack m = MsgPack::unserialise(wire);
			(void)m.hash();
			m.erase("b");
			CHECK_EQ(m.serialise(), map_of({ { "a", two }, { "c", four } }), "hash, then erase");
			m["d"] = 9;
			CHECK_EQ(m.serialise(), map_of({ { "a", two }, { "c", four }, { "d", "\x09" } }), "then add");
		}
		{
			MsgPack m = MsgPack::unserialise(wire);
			m["b"] = 7;
			CHECK_EQ(m.serialise(), map_of({ { "a", two }, { "b", "\x07" }, { "c", four } }), "set after the merge");
		}
		{
			MsgPack m = MsgPack::unserialise(wire);
			m.erase("a");
			m.erase("b");
			m.erase("c");
			CHECK_EQ(m.serialise(), std::string("\x80"), "erase all");
		}
	}
}

// ---- every document of up to 6 entries over 3 keys

void test_exhaustive() {
	static const char* keys[] = { "a", "b", "c" };
	std::size_t documents = 0;
	for (std::size_t n = 1; n <= 6; ++n) {
		std::size_t total = 1;
		for (std::size_t i = 0; i < n; ++i) total *= 3;
		for (std::size_t code = 0; code < total; ++code) {
			std::vector<std::pair<std::string, std::string>> entries;
			std::size_t c = code;
			for (std::size_t i = 0; i < n; ++i) {
				entries.emplace_back(keys[c % 3], uint_bytes(10 + i));
				c /= 3;
			}
			const std::string wire = map_of(entries);
			const std::string expected = model::merged(wire);
			const model::Doc expected_doc = doc_of_bytes(expected);
			++documents;

			// after the first fill, by a walk
			{
				MsgPack m = MsgPack::unserialise(wire);
				fill_all(m);
				CHECK_EQ(m.serialise(), expected, "walk");
				for (const auto& kv : expected_doc.m) {
					CHECK(m[kv.first].as_u64() == kv.second.u);
				}
				CHECK(m.size() == expected_doc.m.size());
			}
			// after the first fill, by a hash
			{
				MsgPack m = MsgPack::unserialise(wire);
				(void)m.hash();
				CHECK_EQ(m.serialise(), expected, "hash");
			}
			// after the first fill, by a lookup of each key; then erase every key in turn
			for (const auto& victim : expected_doc.m) {
				MsgPack m = MsgPack::unserialise(wire);
				model::Doc d = expected_doc;
				CHECK(m[victim.first].as_u64() == victim.second.u);
				m.erase(victim.first);
				d.erase(victim.first);
				CHECK_EQ(m.serialise(), d.bytes(), "erase");
				for (const auto& kv : d.m) {
					CHECK(m[kv.first].as_u64() == kv.second.u);
				}
				m["z"] = 99;
				d.set("z", model::Doc::num(99));
				CHECK_EQ(m.serialise(), d.bytes(), "add after erase");
			}
			// set every key in turn
			for (const auto& target : expected_doc.m) {
				MsgPack m = MsgPack::unserialise(wire);
				model::Doc d = expected_doc;
				m[target.first] = std::string("replaced");
				d.set(target.first, model::Doc::str("replaced"));
				CHECK_EQ(m.serialise(), d.bytes(), "set");
			}
			// the hash is the one of the merged map
			{
				MsgPack m = MsgPack::unserialise(wire);
				msgpack::object_handle h = msgpack::unpack(wire.data(), wire.size());
				CHECK(m.hash() == model::hash_merged_maps(h.get()));
			}
		}
	}
	std::printf("exhaustive: %zu documents\n", documents);
}

// ---- random nested documents

std::string random_value(Rng& r, int depth);

std::string random_map(Rng& r, int depth) {
	static const char* keys[] = { "a", "b", "c", "d", "" };
	std::size_t n = r.below(9);
	std::vector<std::pair<std::string, std::string>> entries;
	for (std::size_t i = 0; i < n; ++i) {
		entries.emplace_back(keys[r.below(5)], random_value(r, depth + 1));
	}
	return map_of(entries);
}

std::string random_value(Rng& r, int depth) {
	switch (depth >= 3 ? r.below(3) : r.below(6)) {
		case 0: return uint_bytes(r.below(300));
		case 1: return str_bytes(std::string(r.below(40), static_cast<char>('a' + r.below(26))));
		case 2: return "\xc0";
		case 3: case 4: return random_map(r, depth);
		default: {
			std::size_t n = r.below(5);
			std::string s = array_head(n);
			for (std::size_t i = 0; i < n; ++i) s += random_value(r, depth + 1);
			return s;
		}
	}
}

// A random map of the model and the MsgPack it matches.
void pick_map(Rng& r, model::Doc& d, MsgPack& m, model::Doc*& dd, MsgPack*& mm) {
	dd = &d;
	mm = &m;
	for (int steps = 0; steps < 4; ++steps) {
		if (r.below(3) == 0) break;
		if (dd->kind == model::Doc::MAP && !dd->m.empty()) {
			std::size_t i = r.below(dd->m.size());
			model::Doc* next = &dd->m[i].second;
			if (next->kind != model::Doc::MAP && next->kind != model::Doc::ARR) break;
			mm = &(*mm)[dd->m[i].first];
			dd = next;
		} else if (dd->kind == model::Doc::ARR && !dd->a.empty()) {
			std::size_t i = r.below(dd->a.size());
			model::Doc* next = &dd->a[i];
			if (next->kind != model::Doc::MAP && next->kind != model::Doc::ARR) break;
			mm = &mm->at(i);
			dd = next;
		} else {
			break;
		}
	}
}

void test_random(std::size_t iterations) {
	static const char* keys[] = { "a", "b", "c", "d", "e", "" };
	std::size_t with_repeats = 0;
	for (std::size_t it = 0; it < iterations; ++it) {
		const std::uint64_t seed = 1000 + it * 7919;
		Rng r{ seed };
		const std::string wire = random_map(r, 0);
		const std::string expected = model::merged(wire);
		if (expected != wire) ++with_repeats;

		// a hash first: the maps reached through maps are merged and hashed, an array is hashed as it is
		{
			MsgPack m = MsgPack::unserialise(wire);
			msgpack::object_handle h = msgpack::unpack(wire.data(), wire.size());
			if (m.hash() != model::hash_merged_maps(h.get())) {
				std::fprintf(stderr, "seed %" PRIu64 ": hash differs for %s\n", seed, hex(wire).c_str());
				std::exit(1);
			}
		}

		MsgPack m = MsgPack::unserialise(wire);
		fill_all(m);
		if (m.serialise() != expected) {
			std::fprintf(stderr, "seed %" PRIu64 ": merged document differs\n  wire     %s\n  got      %s\n  expected %s\n", seed, hex(wire).c_str(), hex(m.serialise()).c_str(), hex(expected).c_str());
			std::exit(1);
		}
		model::Doc d = doc_of_bytes(expected);
		for (int step = 0; step < 12; ++step) {
			model::Doc* dd;
			MsgPack* mm;
			pick_map(r, d, m, dd, mm);
			if (dd->kind != model::Doc::MAP) continue;
			const std::string key = keys[r.below(6)];
			switch (r.below(4)) {
				case 0:
					mm->erase(key);
					dd->erase(key);
					break;
				case 1: {
					model::Doc v = r.below(2) ? model::Doc::num(r.below(1000)) : model::Doc::str(std::string(r.below(30), 'v'));
					set_leaf(*mm, key, v);
					dd->set(key, v);
					break;
				}
				case 2:
					if (!dd->m.empty()) {
						const std::string existing = dd->m[r.below(dd->m.size())].first;
						mm->erase(existing);
						dd->erase(existing);
					}
					break;
				default:
					(void)mm->hash();
					break;
			}
			if (m.serialise() != d.bytes()) {
				std::fprintf(stderr, "seed %" PRIu64 " step %d: differs after the change of %s\n  got      %s\n  expected %s\n", seed, step, key.c_str(), hex(m.serialise()).c_str(), hex(d.bytes()).c_str());
				std::exit(1);
			}
		}
		// a copy and a round trip of the changed document
		CHECK_EQ(MsgPack(m).serialise(), d.bytes(), "copy");
		CHECK_EQ(MsgPack::unserialise(m.serialise()).serialise(), d.bytes(), "round trip");
	}
	std::printf("random: %zu documents, %zu with repeated keys\n", iterations, with_repeats);
	CHECK(with_repeats > iterations / 4);
}

}  // namespace

int main(int argc, char** argv) {
	std::size_t iterations = 3000;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--stress") && i + 1 < argc) iterations = std::strtoull(argv[++i], nullptr, 10);
	}
	test_cases();
	test_exhaustive();
	test_random(iterations);
	std::printf("msgpack merge OK\n");
	return 0;
}
