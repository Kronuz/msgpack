// Two things that went wrong with a value that had already been read through the API.
//
// replace(): putting a new value over a key (or an index) whose old value was a map or an array that
// had been read left the index of the old value on the new one, so reading or changing the new value
// went by the entries of the old one. The new value is read, changed and compared with a model
// (test/model.h) after every combination of old kind, new kind and way the old value was read.
//
// lock(): locking a document locks every map and array in it. When filling one of them throws (a map
// with a key that is not a string) the maps above it stayed locked. A lock() that throws leaves
// nothing locked, and the next lock() throws the same way.
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
			std::fprintf(stderr, "%s:%d: %s: results differ\n  got      %s\n  expected %s\n", __FILE__, __LINE__, std::string(context).c_str(), hex(a).c_str(), hex(b).c_str()); \
			std::exit(1); \
		} \
	} while (0)

namespace {

using model::Doc;

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

// ---- documents

enum Kind { K_UINT, K_STR, K_NIL, K_ARRAY, K_MAP, N_KINDS };

// A value of the kind, with a few entries (maps and arrays hold scalars and one nested container).
Doc make_value(Kind kind, Rng& r, int depth) {
	switch (kind) {
		case K_UINT: return Doc::num(r.below(1000));
		case K_STR: return Doc::str(std::string(r.below(5), 'x'));
		case K_NIL: return Doc::nil();
		case K_ARRAY: {
			Doc d = Doc::arr();
			for (std::uint64_t i = 0, n = r.below(4); i < n; ++i) {
				d.a.push_back(depth < 2 && r.below(3) == 0 ? make_value(r.below(2) ? K_MAP : K_ARRAY, r, depth + 1) : Doc::num(r.below(100)));
			}
			return d;
		}
		default: {
			Doc d = Doc::map();
			for (std::uint64_t i = 0, n = r.below(4); i < n; ++i) {
				d.set("m" + std::to_string(r.below(5)), depth < 2 && r.below(3) == 0 ? make_value(r.below(2) ? K_MAP : K_ARRAY, r, depth + 1) : Doc::num(r.below(100)));
			}
			return d;
		}
	}
}

// Reads every node through the API and compares it with the model.
void check_tree(const MsgPack& m, const Doc& d, const std::string& where) {
	switch (d.kind) {
		case Doc::NIL: CHECK(m.is_null()); break;
		case Doc::UINT: CHECK(m.is_number()); CHECK(m.as_u64() == d.u); break;
		case Doc::STR: CHECK(m.is_string()); CHECK(m.str_view() == d.s); break;
		case Doc::MAP:
			CHECK(m.is_map());
			CHECK(m.size() == d.m.size());
			for (const auto& kv : d.m) {
				CHECK(m.count(kv.first) == 1);
				check_tree(m.at(kv.first), kv.second, where + "." + kv.first);
			}
			break;
		case Doc::ARR:
			CHECK(m.is_array());
			CHECK(m.size() == d.a.size());
			for (std::size_t i = 0; i < d.a.size(); ++i) {
				check_tree(m.at(i), d.a[i], where + "[" + std::to_string(i) + "]");
			}
			break;
		default: break;
	}
}

// Changes the value in `m` and in the model the same way: a new key, a new element, or an erase.
void change(MsgPack& m, Doc& d, Rng& r) {
	if (d.kind == Doc::MAP) {
		if (r.below(2) == 0 || d.m.empty()) {
			const std::string key = "n" + std::to_string(r.below(4));
			const std::uint64_t v = r.below(100);
			m[key] = v;
			d.set(key, Doc::num(v));
		} else {
			const std::string key = d.m[r.below(d.m.size())].first;
			m.erase(std::string_view(key));
			d.erase(key);
		}
	} else if (d.kind == Doc::ARR) {
		if (r.below(2) == 0 || d.a.empty()) {
			const std::uint64_t v = r.below(100);
			m.push_back(v);
			d.a.push_back(Doc::num(v));
		} else {
			m.erase(static_cast<std::size_t>(0));
			d.a.erase(d.a.begin());
		}
	}
}

// ---- replace()

// How the old value is read before it is replaced: not at all, a reference to it, or all of it.
enum Read { R_NONE, R_REFERENCE, R_ALL, N_READS };

void replace_case(bool root_is_map, Kind old_kind, Kind new_kind, Read read, std::uint64_t seed) {
	Rng r{ seed };
	const std::string name = std::string(root_is_map ? "map" : "array") + " root, old " + std::to_string(old_kind) + ", new " + std::to_string(new_kind) +
		", read " + std::to_string(read) + ", seed " + std::to_string(seed);
	Doc doc = root_is_map ? Doc::map() : Doc::arr();
	for (int i = 0; i < 3; ++i) {
		Doc v = make_value(i == 1 ? old_kind : Kind(r.below(N_KINDS)), r, 0);
		if (root_is_map) doc.set("k" + std::to_string(i), std::move(v));
		else doc.a.push_back(std::move(v));
	}
	MsgPack root = MsgPack::unserialise(doc.bytes());
	const std::string key = "k1";

	Doc& old_doc = root_is_map ? doc.m[1].second : doc.a[1];
	MsgPack* old_ref = nullptr;
	if (read != R_NONE) {
		old_ref = root_is_map ? &root.at(std::string_view(key)) : &root.at(static_cast<std::size_t>(1));
	}
	if (read == R_ALL) {
		check_tree(*old_ref, old_doc, name + " (old)");
	}

	Doc fresh = make_value(new_kind, r, 0);
	MsgPack value = MsgPack::unserialise(fresh.bytes());
	if (root_is_map) {
		root.replace(std::string_view(key), std::move(value));
		doc.set(key, fresh);
	} else {
		root.replace(static_cast<std::size_t>(1), std::move(value));
		doc.a[1] = fresh;
	}
	CHECK_EQ(root.serialise(), doc.bytes(), name + " after the replace");

	// the new value, read and changed through the API, and its neighbours
	MsgPack& now = root_is_map ? root.at(std::string_view(key)) : root.at(static_cast<std::size_t>(1));
	Doc& now_doc = root_is_map ? doc.m[1].second : doc.a[1];
	check_tree(now, now_doc, name + " (new)");
	check_tree(root, doc, name + " (all)");
	for (int step = 0; step < 4; ++step) {
		change(now, now_doc, r);
		CHECK_EQ(root.serialise(), doc.bytes(), name + " after a change");
		check_tree(now, now_doc, name + " (new, changed)");
	}
	CHECK_EQ(MsgPack(root).serialise(), doc.bytes(), name + " copy");
}

std::size_t test_replace_exhaustive() {
	std::size_t n = 0;
	for (int root_is_map = 0; root_is_map < 2; ++root_is_map) {
		for (int o = 0; o < N_KINDS; ++o) {
			for (int w = 0; w < N_KINDS; ++w) {
				for (int rd = 0; rd < N_READS; ++rd) {
					for (std::uint64_t seed = 1; seed <= 6; ++seed, ++n) {
						replace_case(root_is_map, Kind(o), Kind(w), Read(rd), seed * 7919 + n);
					}
				}
			}
		}
	}
	return n;
}

// Random documents: a replace at a random depth, then changes and reads.
void test_replace_random(std::size_t documents) {
	std::size_t replaces = 0;
	for (std::size_t n = 0; n < documents; ++n) {
		Rng r{ 5000 + n * 104729 };
		Doc doc = Doc::map();
		for (int i = 0; i < 4; ++i) {
			doc.set("k" + std::to_string(i), make_value(Kind(r.below(N_KINDS)), r, 0));
		}
		MsgPack root = MsgPack::unserialise(doc.bytes());
		const std::string name = "random document " + std::to_string(n);
		for (int step = 0; step < 8; ++step, ++replaces) {
			// descend to a container, reading what is on the way
			MsgPack* m = &root;
			Doc* d = &doc;
			while (r.below(3) != 0 && (d->kind == Doc::MAP ? !d->m.empty() : d->kind == Doc::ARR && !d->a.empty())) {
				if (d->kind == Doc::MAP) {
					auto& kv = d->m[r.below(d->m.size())];
					m = &(*m)[kv.first];
					d = &kv.second;
				} else {
					const std::size_t i = r.below(d->a.size());
					m = &m->at(i);
					d = &d->a[i];
				}
			}
			if (r.below(2)) check_tree(*m, *d, name);
			Doc fresh = make_value(Kind(r.below(N_KINDS)), r, 0);
			MsgPack value = MsgPack::unserialise(fresh.bytes());
			if (d->kind == Doc::MAP && !d->m.empty()) {
				const std::size_t i = r.below(d->m.size());
				m->replace(std::string_view(d->m[i].first), std::move(value));
				d->m[i].second = fresh;
			} else if (d->kind == Doc::ARR && !d->a.empty()) {
				const std::size_t i = r.below(d->a.size());
				m->replace(i, std::move(value));
				d->a[i] = fresh;
			} else {
				continue;
			}
			CHECK_EQ(root.serialise(), doc.bytes(), name + " after a replace");
			check_tree(root, doc, name + " after a replace");
			change(*m, *d, r);
			CHECK_EQ(root.serialise(), doc.bytes(), name + " after a change");
		}
	}
	std::printf("replace, random: %zu documents, %zu replaces\n", documents, replaces);
}

// ---- lock()

std::string bad_map(Rng& r) {
	std::string s = model::map_head(2);
	s += model::str_bytes("a") + model::uint_bytes(r.below(100));
	s += r.below(2) ? model::uint_bytes(5) : std::string("\xc0", 1);
	s += model::uint_bytes(r.below(100));
	return s;
}

Doc lock_doc(Rng& r, int depth, bool with_bad, bool& has_bad) {
	const std::uint64_t kind = depth == 0 ? 3 + r.below(2) : r.below(depth >= 3 ? 3 : 6);
	switch (kind) {
		case 0: return Doc::num(r.below(100));
		case 1: return Doc::str("s");
		case 2: return Doc::nil();
		case 3: {
			Doc d = Doc::map();
			for (std::uint64_t i = 0, n = r.below(4); i < n; ++i) d.set("k" + std::to_string(i), lock_doc(r, depth + 1, with_bad, has_bad));
			return d;
		}
		case 4: {
			Doc d = Doc::arr();
			for (std::uint64_t i = 0, n = r.below(4); i < n; ++i) d.a.push_back(lock_doc(r, depth + 1, with_bad, has_bad));
			return d;
		}
		default:
			if (with_bad) {
				has_bad = true;
				return Doc::raw(bad_map(r));
			}
			return Doc::num(7);
	}
}

// Whether every map and array reached through the API is locked (or none is).
void check_locked(const MsgPack& m, const Doc& d, bool expected, const std::string& where) {
	if (d.kind == Doc::RAW) {
		CHECK(!m.locked());
		return;
	}
	if (d.kind == Doc::MAP) {
		if (m.locked() != expected) {
			std::fprintf(stderr, "%s: a map is %slocked\n", where.c_str(), m.locked() ? "" : "not ");
			std::exit(1);
		}
		for (const auto& kv : d.m) check_locked(m.at(kv.first), kv.second, expected, where);
	} else if (d.kind == Doc::ARR) {
		if (m.locked() != expected) {
			std::fprintf(stderr, "%s: an array is %slocked\n", where.c_str(), m.locked() ? "" : "not ");
			std::exit(1);
		}
		for (std::size_t i = 0; i < d.a.size(); ++i) check_locked(m.at(i), d.a[i], expected, where);
	}
}

void test_lock(std::size_t documents) {
	std::size_t failed = 0, locked = 0;
	for (std::size_t n = 0; n < documents; ++n) {
		Rng r{ 900 + n * 7919 };
		bool has_bad = false;
		Doc doc = lock_doc(r, 0, r.below(2) == 0, has_bad);
		const std::string name = "lock, document " + std::to_string(n);
		MsgPack root = MsgPack::unserialise(doc.bytes());
		if (has_bad) {
			// a bad map is somewhere below: lock() throws, twice, and locks nothing
			++failed;
			bool threw = false;
			try { root.lock(); } catch (const std::exception&) { threw = true; }
			CHECK(threw);
			CHECK(!root.locked());
			check_locked(root, doc, false, name + " after the first lock()");
			threw = false;
			try { root.lock(); } catch (const std::exception&) { threw = true; }
			CHECK(threw);
			check_locked(root, doc, false, name + " after the second lock()");
			// and the document is as it was, and can still be changed
			CHECK_EQ(root.serialise(), doc.bytes(), name);
			if (doc.kind == Doc::MAP) {
				root["added"] = 1;
				doc.set("added", Doc::num(1));
			} else {
				root.push_back(1);
				doc.a.push_back(Doc::num(1));
			}
			CHECK_EQ(root.serialise(), doc.bytes(), name + " after a change");
		} else {
			++locked;
			root.lock();
			CHECK(root.locked());
			check_locked(root, doc, true, name + " after lock()");
			root.lock();   // locking twice is fine
			CHECK_EQ(root.serialise(), doc.bytes(), name);
		}
	}
	CHECK(failed > documents / 10 && locked > documents / 10);
	std::printf("lock: %zu documents, %zu with a bad map, %zu locked\n", documents, failed, locked);
}

}  // namespace

int main(int argc, char** argv) {
	std::size_t documents = 400;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--stress") && i + 1 < argc) documents = std::strtoull(argv[++i], nullptr, 10);
	}
	std::printf("replace, exhaustive: %zu cases\n", test_replace_exhaustive());
	test_replace_random(documents);
	test_lock(documents);
	std::printf("msgpack replace and lock OK\n");
	return 0;
}
