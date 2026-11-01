// A fill that throws leaves the map as it was.
//
// A map is indexed by key the first time something reads or changes it through the API (find,
// count, at, [], dereferencing an iterator, erase, hash, ...). A map that has a key that is not
// a string cannot be indexed: filling it throws "MAP keys must be of type STR". The throw changes
// nothing: the document is the bytes it came as (the repeated keys before the bad key are not
// merged either),
// size() counts the entries as they came, and the next operation that fills the map throws the
// same way again. The oracle is the input: every call, in any order and as often as it is made,
// either throws that exception or leaves serialise() equal to the wire bytes.
//
// The test checks it on hand-written documents (every kind of key that is not a string, in
// every position, with repeats around it), on every document of up to 4 entries over the keys
// "a", "b" and an integer with every sequence of up to 3 operations, and on random nested
// documents (test/model.h holds the model of the part around the bad maps) that keep being
// changed with set, erase, push_back and erase(index) while the bad maps sit in them.
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <typeinfo>
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
			std::fprintf(stderr, "%s:%d: %s: results differ\n  got      %s\n  expected %s\n", __FILE__, __LINE__, (context).c_str(), hex(a).c_str(), hex(b).c_str()); \
			std::exit(1); \
		} \
	} while (0)

namespace {

using model::array_head;
using model::map_head;
using model::str_bytes;
using model::uint_bytes;

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

// One entry of a map as bytes: the packed key and the packed value.
struct Entry {
	std::string key;
	std::string val;
};

std::string map_bytes(const std::vector<Entry>& entries) {
	std::string r = map_head(entries.size());
	for (const auto& e : entries) {
		r += e.key + e.val;
	}
	return r;
}

// One packed key of every msgpack type that is not a string.
const std::vector<std::string>& bad_keys() {
	static const std::vector<std::string> keys = {
		uint_bytes(5),                          // positive integer
		std::string("\xff", 1),                 // negative integer
		std::string("\xc0", 1),                 // nil
		std::string("\xc3", 1),                 // true
		std::string("\xc2", 1),                 // false
		std::string("\xcb\x3f\xf0\0\0\0\0\0\0", 9),  // float 1.0
		std::string("\xc4\x01x", 3),            // bin
		std::string("\x90", 1),                 // empty array
		std::string("\x80", 1),                 // empty map
		std::string("\xd4\x01\x00", 3),         // fixext
	};
	return keys;
}

std::size_t entries_of(const std::string& map_wire) {
	return static_cast<unsigned char>(map_wire[0]) & 0x0f;   // these maps have fewer than 16 entries
}

// ---- what an operation did

const std::string kMessage = "MAP keys must be of type STR";
std::string g_failure;   // the outcome of the first fill that failed; every later one is the same

template <typename F>
std::string outcome(F&& f) {
	try {
		f();
		return "ok";
	} catch (const BaseException& e) {
		return std::string("threw ") + typeid(e).name() + ": " + e.get_message();
	} catch (const std::exception& e) {
		return std::string("threw ") + typeid(e).name() + ": " + e.what();
	}
}

enum Op { FIND, COUNT, AT_KEY, INDEX, AT_POS, BEGIN, ERASE, SET, HASH, SIZE, SERIALISE, COPY, N_OPS };
const char* const op_names[N_OPS] = { "find", "count", "at(key)", "[]", "at(pos)", "*begin()", "erase", "set", "hash", "size", "serialise", "copy" };

bool fills(Op op) {
	return op <= HASH;
}

// Applies `op` to the map `m`, which came as `wire` and has not been changed, and checks what it
// did: the operations that fill throw the one exception, and after any of them the map is as it
// came.
void apply(MsgPack& m, Op op, const std::string& wire, const std::string& context) {
	const std::string where = context + ", " + op_names[op];
	std::string r = "ok";
	switch (op) {
		case FIND: r = outcome([&] { (void)m.find("a"); }); break;
		case COUNT: r = outcome([&] { (void)m.count("a"); }); break;
		case AT_KEY: r = outcome([&] { (void)m.at("a"); }); break;
		case INDEX: r = outcome([&] { (void)m["a"]; }); break;
		case AT_POS: r = outcome([&] { (void)m.at(static_cast<std::size_t>(0)); }); break;
		case BEGIN: r = outcome([&] { auto it = m.begin(); (void)*it; }); break;
		case ERASE: r = outcome([&] { (void)m.erase("a"); }); break;
		case SET: r = outcome([&] { m["zz"] = 1; }); break;
		case HASH: r = outcome([&] { (void)m.hash(); }); break;
		case SIZE: break;
		case SERIALISE: break;
		case COPY: {
			MsgPack c(m);
			CHECK_EQ(c.serialise(), wire, where);
			CHECK(outcome([&] { (void)c.find("a"); }) == g_failure || g_failure.empty());
			break;
		}
		default: break;
	}
	if (fills(op)) {
		if (g_failure.empty()) {
			CHECK(r.find(kMessage) != std::string::npos);
			g_failure = r;
		}
		if (r != g_failure) {
			std::fprintf(stderr, "%s: %s\n  expected %s\n", where.c_str(), r.c_str(), g_failure.c_str());
			std::exit(1);
		}
	} else {
		CHECK(r == "ok");
	}
	CHECK_EQ(m.serialise(), wire, where);
	if (m.size() != entries_of(wire)) {
		std::fprintf(stderr, "%s: size() is %zu, the map has %zu entries\n", where.c_str(), m.size(), entries_of(wire));
		std::exit(1);
	}
}

std::string describe(const std::vector<Op>& ops) {
	std::string r;
	for (Op op : ops) {
		r += std::string(r.empty() ? "" : ", ") + op_names[op];
	}
	return r;
}

void run_sequence(const std::string& wire, const std::vector<Op>& ops, const std::string& name) {
	MsgPack m = MsgPack::unserialise(wire);
	for (std::size_t i = 0; i < ops.size(); ++i) {
		apply(m, ops[i], wire, name + " after [" + describe(std::vector<Op>(ops.begin(), ops.begin() + i)) + "]");
	}
}

// ---- hand-written documents

void test_cases() {
	const std::string one = uint_bytes(1), two = uint_bytes(2), three = uint_bytes(3);
	const std::string a = str_bytes("a"), b = str_bytes("b");
	const std::string five = uint_bytes(5), nil("\xc0", 1);

	struct Case {
		const char* name;
		std::vector<Entry> entries;
	};
	const std::vector<Case> cases = {
		{ "bad key last", { { a, one }, { five, two } } },
		{ "bad key first", { { five, one }, { a, two } } },
		{ "bad key between", { { a, one }, { five, two }, { b, three } } },
		{ "only a bad key", { { five, one } } },
		{ "two bad keys", { { five, one }, { nil, two } } },
		{ "repeat before the bad key", { { a, one }, { a, two }, { five, three } } },
		{ "repeat after the bad key", { { five, one }, { a, two }, { a, three } } },
		{ "repeat around the bad key", { { a, one }, { five, two }, { a, three } } },
		{ "empty key", { { str_bytes(""), one }, { five, two } } },
	};
	const std::vector<std::vector<Op>> sequences = {
		{ ERASE, ERASE }, { HASH, ERASE }, { COUNT, ERASE }, { FIND, SET }, { BEGIN, AT_POS, FIND },
		{ ERASE, HASH, COUNT, ERASE }, { AT_POS, AT_POS }, { INDEX, SERIALISE, INDEX }, { SET, SET, ERASE },
		{ COPY, ERASE, COPY },
	};
	for (const auto& c : cases) {
		const std::string wire = map_bytes(c.entries);
		for (const auto& ops : sequences) {
			run_sequence(wire, ops, c.name);
		}
		// every operation, three times in a row
		for (int op = 0; op < N_OPS; ++op) {
			run_sequence(wire, { Op(op), Op(op), Op(op) }, c.name);
		}
	}

	// every kind of key that is not a string, first, last and between repeats
	std::size_t kind = 0;
	for (const std::string& bad : bad_keys()) {
		const std::string name = "bad key kind " + std::to_string(kind++);
		const std::vector<std::vector<Entry>> docs = {
			{ { a, one }, { bad, two } },
			{ { bad, one }, { a, two } },
			{ { a, one }, { a, two }, { bad, three } },
			{ { a, one }, { bad, two }, { a, three } },
		};
		for (const auto& entries : docs) {
			const std::string wire = map_bytes(entries);
			for (int x = 0; x < N_OPS; ++x) {
				for (int y = 0; y < N_OPS; ++y) {
					run_sequence(wire, { Op(x), Op(y) }, name);
				}
			}
		}
	}
}

// ---- every small document, every short sequence

std::size_t test_exhaustive() {
	const std::vector<std::string> keys = { str_bytes("a"), str_bytes("b"), uint_bytes(5) };
	std::size_t docs = 0, runs = 0;
	for (std::size_t n = 1; n <= 4; ++n) {
		std::size_t total = 1;
		for (std::size_t i = 0; i < n; ++i) total *= keys.size();
		for (std::size_t code = 0; code < total; ++code) {
			std::vector<Entry> entries;
			bool has_bad = false;
			std::size_t c = code;
			for (std::size_t i = 0; i < n; ++i, c /= keys.size()) {
				entries.push_back({ keys[c % keys.size()], uint_bytes(10 + i) });
				has_bad = has_bad || c % keys.size() == 2;
			}
			if (!has_bad) continue;
			++docs;
			const std::string wire = map_bytes(entries);
			const std::string name = "doc " + hex(wire);
			std::vector<Op> ops;
			for (int x = 0; x < N_OPS; ++x) {
				ops = { Op(x) };
				run_sequence(wire, ops, name);
				++runs;
				for (int y = 0; y < N_OPS; ++y) {
					ops = { Op(x), Op(y) };
					run_sequence(wire, ops, name);
					++runs;
					for (int z = 0; z < N_OPS; ++z) {
						ops = { Op(x), Op(y), Op(z) };
						run_sequence(wire, ops, name);
						++runs;
					}
				}
			}
		}
	}
	std::printf("exhaustive: %zu documents, %zu sequences\n", docs, runs);
	return docs;
}

// ---- random nested documents with bad maps inside

// A map that has at least one key that is not a string, among string keys that may repeat. The
// values are fixints and the keys minimal, because serialise() packs the tree and so gives every
// integer its shortest form, filled or not.
std::string random_bad_map(Rng& r) {
	const std::size_t n = 1 + r.below(4);
	const std::size_t bad_at = r.below(n);
	std::vector<Entry> entries;
	for (std::size_t i = 0; i < n; ++i) {
		const bool bad = i == bad_at || r.below(5) == 0;
		entries.push_back({ bad ? bad_keys()[r.below(bad_keys().size())] : str_bytes(std::string(1, static_cast<char>('a' + r.below(3)))), uint_bytes(r.below(128)) });
	}
	return map_bytes(entries);
}

model::Doc random_doc(Rng& r, int depth, bool root) {
	const std::uint64_t kind = root ? 3 + r.below(2) : r.below(depth >= 3 ? 3 : 6);
	switch (kind) {
		case 0: return model::Doc::num(r.below(1000));
		case 1: return model::Doc::str(std::string(r.below(6), 'x'));
		case 2: return model::Doc::nil();
		case 3: {
			model::Doc d = model::Doc::map();
			const std::size_t n = r.below(5);
			for (std::size_t i = 0; i < n; ++i) {
				d.set("k" + std::to_string(r.below(6)), random_doc(r, depth + 1, false));
			}
			return d;
		}
		case 4: {
			model::Doc d = model::Doc::arr();
			const std::size_t n = r.below(5);
			for (std::size_t i = 0; i < n; ++i) {
				d.a.push_back(random_doc(r, depth + 1, false));
			}
			return d;
		}
		default: return model::Doc::raw(random_bad_map(r));
	}
}

// Every bad map in the document throws the one exception and is as it came, reached through the
// maps and arrays around it (which are filled on the way, and which the model describes).
void check_bad_maps(MsgPack& m, const model::Doc& d, Rng& r, const std::string& context) {
	switch (d.kind) {
		case model::Doc::RAW:
			apply(m, Op(r.below(HASH + 1)), d.s, context);
			apply(m, Op(r.below(N_OPS)), d.s, context);
			break;
		case model::Doc::MAP:
			for (const auto& kv : d.m) {
				check_bad_maps(m[kv.first], kv.second, r, context);
			}
			break;
		case model::Doc::ARR:
			for (std::size_t i = 0; i < d.a.size(); ++i) {
				check_bad_maps(m.at(i), d.a[i], r, context);
			}
			break;
		default:
			break;
	}
}

// A random node reached from the root through the maps and arrays that are not bad.
void pick(MsgPack*& m, model::Doc*& d, Rng& r) {
	while (r.below(3) != 0) {
		if (d->kind == model::Doc::MAP && !d->m.empty()) {
			auto& kv = d->m[r.below(d->m.size())];
			m = &(*m)[kv.first];
			d = &kv.second;
		} else if (d->kind == model::Doc::ARR && !d->a.empty()) {
			const std::size_t i = r.below(d->a.size());
			m = &m->at(i);
			d = &d->a[i];
		} else {
			break;
		}
	}
}

// With MSGPACK_FAILED_FILL_TRACE set, each random step is printed before it runs, so that the
// last line names what a crash happened in.
void trace(const std::string& line) {
	static const bool on = std::getenv("MSGPACK_FAILED_FILL_TRACE") != nullptr;
	if (on) {
		std::fprintf(stderr, "%s\n", line.c_str());
	}
}

void test_random(std::size_t documents) {
	std::size_t steps = 0, bad = 0;
	for (std::size_t n = 0; n < documents; ++n) {
		Rng r{ 1000 + n * 7919 };
		model::Doc doc = random_doc(r, 0, true);
		MsgPack root = MsgPack::unserialise(doc.bytes());
		const std::string name = "random document " + std::to_string(n);
		CHECK_EQ(root.serialise(), doc.bytes(), name);
		for (int step = 0; step < 14; ++step, ++steps) {
			MsgPack* m = &root;
			model::Doc* d = &doc;
			pick(m, d, r);
			const std::string context = name + " step " + std::to_string(step);
			switch (d->kind) {
				case model::Doc::RAW:
					apply(*m, Op(r.below(N_OPS)), d->s, context);
					++bad;
					break;
				case model::Doc::MAP: {
					const std::string key = "k" + std::to_string(r.below(8));
					const std::uint64_t choice = r.below(4);
					trace(context + ": map " + hex(d->bytes()) + " choice " + std::to_string(choice) + " key " + key);
					switch (choice) {
						case 0: {
							const std::uint64_t v = r.below(500);
							(*m)[key] = v;
							d->set(key, model::Doc::num(v));
							break;
						}
						case 1:
							m->erase(std::string_view(key));
							d->erase(key);
							break;
						case 2: {
							// Only a key the map does not have: putting over a filled map or array leaves the
							// index of the old value on the new one (see "Known problems" in CHANGELOG).
							if (d->find(key) != nullptr) break;
							const std::string raw = random_bad_map(r);
							m->put(std::string_view(key), MsgPack::unserialise(raw));
							d->set(key, model::Doc::raw(raw));
							break;
						}
						default:
							CHECK((m->find(key) != m->end()) == (d->find(key) != nullptr));
							break;
					}
					break;
				}
				case model::Doc::ARR: {
					const std::uint64_t choice = r.below(4);
					trace(context + ": array " + hex(d->bytes()) + " choice " + std::to_string(choice));
					switch (choice) {
						case 0: {
							const std::uint64_t v = r.below(500);
							m->push_back(v);
							d->a.push_back(model::Doc::num(v));
							break;
						}
						case 1: {
							const std::string raw = random_bad_map(r);
							m->push_back(MsgPack::unserialise(raw));
							d->a.push_back(model::Doc::raw(raw));
							break;
						}
						case 2:
							if (!d->a.empty()) {
								m->erase(static_cast<std::size_t>(0));
								d->a.erase(d->a.begin());
							}
							break;
						default:
							CHECK(m->size() == d->a.size());
							break;
					}
					break;
				}
				default:
					break;
			}
			CHECK_EQ(root.serialise(), doc.bytes(), context);
			if (step % 4 == 3) check_bad_maps(root, doc, r, context);
		}
		check_bad_maps(root, doc, r, name + " at the end");
		CHECK_EQ(root.serialise(), doc.bytes(), name + " at the end");
		CHECK_EQ(MsgPack(root).serialise(), doc.bytes(), name + " copy");
	}
	std::printf("random: %zu documents, %zu steps, %zu on a bad map\n", documents, steps, bad);
}

}  // namespace

int main(int argc, char** argv) {
	std::size_t documents = 600;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--stress") && i + 1 < argc) documents = std::strtoull(argv[++i], nullptr, 10);
	}
	test_cases();
	CHECK(test_exhaustive() > 50);
	test_random(documents);
	std::printf("msgpack failed fill OK\n");
	return 0;
}
