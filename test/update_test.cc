// update(): merging a map into a value, against a model, and what a throw leaves behind.
//
// `a.update(b)`, when b is a map: each entry of b is applied to a in order. An entry whose value is
// undefined erases the key from a. An entry whose key a does not have is added. An entry whose key a
// has, where a's value and b's value are both maps, merges recursively; in any other case b's value
// replaces a's. When b is not a map, b replaces a. An undefined a first becomes an empty map. A map that
// has a key that is not a string cannot be read by key: when the merge has to read one (as a, or as
// b), `update()` throws "MAP keys must be of type STR"; a value that is not a map or an array holder
// and has to be read by key throws "is not iterable". The README states the guarantee: an update()
// that throws for either reason has changed nothing, whichever entries came before the one that
// throws (they used to stay applied). The model (`merge`) returns the first error in entry order
// and the caller discards the changes.
//
// Not here: a locked value (a build with assertions aborts in `assert` before it throws, so a test
// cannot catch it), and an allocation that fails part way (test/hunt_test.cc, in `msgpack_hunt_alloc`:
// the document stays consistent, and earlier entries stay applied).
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "msgpack.h"
#include "model.h"

using model::Doc;

namespace {

[[noreturn]] void fail(const std::string& context, const std::string& got, const std::string& expected) {
	std::fprintf(stderr, "FAIL: %s\n  got      %s\n  expected %s\n", context.c_str(), got.c_str(), expected.c_str());
	std::exit(1);
}

std::string hex(const std::string& s) {
	static const char d[] = "0123456789abcdef";
	std::string r;
	for (unsigned char c : s) {
		r += d[c >> 4];
		r += d[c & 15];
	}
	return r;
}

const char* const kBadMessage = "MAP keys must be of type STR";
const char* const kIterableMessage = "is not iterable";

enum Outcome { OK, BAD_KEY, NOT_ITERABLE };
const char* const outcome_names[] = {"no throw", "bad key", "not iterable"};

// The bytes of an undefined value: what a MsgPack writes for it.
const std::string& undef_bytes() {
	static const std::string b = MsgPack::undefined().serialise();
	return b;
}

Doc undef() { return Doc::raw(undef_bytes()); }
bool is_undef(const Doc& d) { return d.kind == Doc::RAW && d.s == undef_bytes(); }

// A map with a key that is not a string (an integer, a nil, an array), with a few string keys around it.
Doc bad(int variant = 0) {
	switch (variant) {
		case 0: return Doc::raw(model::map_head(1) + model::uint_bytes(5) + model::uint_bytes(1));
		case 1: return Doc::raw(model::map_head(2) + model::str_bytes("a") + model::uint_bytes(1) + std::string("\xc0", 1) + model::uint_bytes(2));
		default: return Doc::raw(model::map_head(2) + std::string("\x90", 1) + model::uint_bytes(3) + model::str_bytes("b") + model::uint_bytes(4));
	}
}
bool is_bad_map(const Doc& d) { return d.kind == Doc::RAW && !d.s.empty() && (static_cast<unsigned char>(d.s[0]) & 0xf0) == 0x80; }

Doc num(std::uint64_t v) { return Doc::num(v); }
Doc str(const char* s) { return Doc::str(s); }
Doc arr_of(std::initializer_list<Doc> l) { Doc d = Doc::arr(); d.a.assign(l.begin(), l.end()); return d; }
Doc map_of(std::initializer_list<std::pair<const char*, Doc>> l) {
	Doc d = Doc::map();
	for (const auto& kv : l) d.set(kv.first, kv.second);
	return d;
}

// The model of update(): the merge of s into t, which stops at the first error in entry order.
Outcome merge(Doc& t, const Doc& s) {
	if (s.kind != Doc::MAP && !is_bad_map(s)) {
		t = s;                       // not a map: replaces (an undefined s too)
		return OK;
	}
	if (is_bad_map(s)) return BAD_KEY;   // going through s fills it
	if (s.m.empty()) {
		if (is_undef(t)) t = Doc::map();
		return OK;                   // nothing of t is read
	}
	if (is_undef(t)) {
		t = Doc::map();
	} else if (is_bad_map(t)) {
		return BAD_KEY;              // the first entry reads t by key
	} else if (t.kind != Doc::MAP) {
		return NOT_ITERABLE;
	}
	for (const auto& kv : s.m) {
		if (is_undef(kv.second)) {
			t.erase(kv.first);
			continue;
		}
		Doc* item = t.find(kv.first);
		if (!item) {
			t.set(kv.first, kv.second);
		} else if (item->kind == Doc::MAP || is_bad_map(*item)) {
			const Outcome o = merge(*item, kv.second);
			if (o != OK) return o;
		} else {
			*item = kv.second;
		}
	}
	return OK;
}

Outcome classify(const BaseException& e, const std::string& where) {
	const std::string msg = e.get_message();
	if (msg.find(kBadMessage) != std::string::npos) return BAD_KEY;
	if (msg.find(kIterableMessage) != std::string::npos) return NOT_ITERABLE;
	fail("update() threw something else: " + where, msg, "the bad key or the not iterable message");
}

// Reads every value of the MsgPack through the API and compares it with the model. A map that has a key
// that is not a string must throw when it is read by key.
void read_all(MsgPack& m, const Doc& d, const std::string& where) {
	switch (d.kind) {
		case Doc::NIL:
			if (!m.is_null()) fail("a nil at " + where, "not nil", "nil");
			return;
		case Doc::UINT:
			if (!m.is_number() || m.as_u64() != d.u) fail("a number at " + where, "differs", std::to_string(d.u));
			return;
		case Doc::STR:
			if (!m.is_string() || m.as_str() != d.s) fail("a string at " + where, "differs", d.s);
			return;
		case Doc::ARR:
			if (!m.is_array() || m.size() != d.a.size()) fail("an array at " + where, "differs", "size " + std::to_string(d.a.size()));
			for (std::size_t i = 0; i < d.a.size(); ++i) read_all(m.at(i), d.a[i], where + "[" + std::to_string(i) + "]");
			return;
		case Doc::MAP:
			if (!m.is_map() || m.size() != d.m.size()) fail("a map at " + where, "differs", "size " + std::to_string(d.m.size()));
			for (const auto& kv : d.m) read_all(m.at(kv.first), kv.second, where + "." + kv.first);
			return;
		case Doc::RAW:
			if (is_undef(d)) {
				if (!m.is_undefined()) fail("an undefined at " + where, "defined", "undefined");
			} else if (is_bad_map(d)) {
				bool threw = false;
				try {
					(void)m.count("a");
				} catch (const BaseException& e) {
					threw = classify(e, where) == BAD_KEY;
				}
				if (!threw) fail("a read of a bad map at " + where, "no throw", kBadMessage);
			}
			return;
	}
}

std::size_t g_cases = 0, g_threw[3] = {};

// One update: T is changed by S, in the four ways of reading them first, and compared with the model.
void check(const Doc& t, const Doc& s, const std::string& label) {
	Doc expected = t;
	const Outcome want = merge(expected, s);
	for (int variant = 0; variant < 4; ++variant) {
		const std::string where = label + " variant " + std::to_string(variant) + " T=" + hex(t.bytes()) + " S=" + hex(s.bytes());
		MsgPack mt = MsgPack::unserialise(t.bytes());
		MsgPack ms = MsgPack::unserialise(s.bytes());
		if (variant & 1) read_all(mt, t, where + " (reading T)");
		if (variant & 2) read_all(ms, s, where + " (reading S)");
		Outcome got = OK;
		try {
			if (variant == 3) {
				const MsgPack& cs = ms;
				mt.update(cs);       // a const source
			} else {
				mt.update(ms);
			}
		} catch (const BaseException& e) {
			got = classify(e, where);
		}
		if (got != want) fail("update() outcome: " + where, outcome_names[got], outcome_names[want]);
		const Doc& result = want == OK ? expected : t;
		if (mt.serialise() != result.bytes()) fail("T after update() (" + std::string(outcome_names[want]) + "): " + where, hex(mt.serialise()), hex(result.bytes()));
		if (ms.serialise() != s.bytes()) fail("S after update(): " + where, hex(ms.serialise()), hex(s.bytes()));
		// and what is left reads as the model says, and takes another update
		read_all(mt, result, where + " (after)");
		if (result.kind == Doc::MAP) {
			const Doc zz = map_of({{"zz", num(1)}});
			Doc again = result;
			const Outcome o2 = merge(again, zz);
			Outcome g2 = OK;
			try {
				mt.update(MsgPack::unserialise(zz.bytes()));
			} catch (const BaseException& e) {
				g2 = classify(e, where);
			}
			if (g2 != o2) fail("a second update(): " + where, outcome_names[g2], outcome_names[o2]);
			if (mt.serialise() != (o2 == OK ? again : result).bytes()) fail("T after a second update(): " + where, hex(mt.serialise()), hex((o2 == OK ? again : result).bytes()));
		}
	}
	++g_cases;
	++g_threw[want];
}

// Every shape of T and of S over a small alphabet of keys and values.
std::vector<Doc> targets() {
	return {
		undef(), Doc::nil(), num(7), str("s"), arr_of({}), arr_of({num(1)}), Doc::map(), map_of({{"a", num(1)}}),
		map_of({{"a", num(1)}, {"b", num(2)}}), map_of({{"a", map_of({{"b", num(1)}, {"c", num(2)}})}}),
		map_of({{"a", map_of({{"b", num(1)}})}, {"c", num(3)}}), map_of({{"a", arr_of({num(1)})}}), map_of({{"a", undef()}}),
		bad(0), bad(1), map_of({{"a", bad(0)}}), map_of({{"a", map_of({{"b", bad(1)}})}}), map_of({{"a", num(1)}, {"b", bad(0)}}),
		map_of({{"b", bad(2)}, {"a", num(1)}}), map_of({{"a", map_of({{"b", num(1)}})}, {"b", map_of({{"c", bad(0)}})}}),
		map_of({{"a", map_of({{"b", bad(0)}})}, {"b", num(2)}}), map_of({{"a", num(1)}, {"b", num(2)}, {"z", num(3)}}),
		map_of({{"z", map_of({{"a", map_of({{"b", num(1)}})}})}}),
	};
}

std::vector<Doc> values() {
	return {
		num(9), str("x"), Doc::map(), map_of({{"b", num(2)}}), map_of({{"b", undef()}}), map_of({{"c", undef()}}), map_of({{"z", num(1)}}),
		map_of({{"b", bad(0)}}), bad(1), undef(), arr_of({num(1)}), Doc::nil(), map_of({{"b", map_of({{"c", num(5)}})}}),
		map_of({{"b", num(5)}, {"c", undef()}}), map_of({{"c", bad(2)}}),
	};
}

std::vector<Doc> sources() {
	std::vector<Doc> v = {Doc::nil(), num(9), str("x"), arr_of({num(1)}), undef(), bad(0), Doc::map()};
	const auto vals = values();
	const char* const keys[] = {"a", "b", "z"};
	for (const char* k : keys) {
		for (const auto& x : vals) v.push_back(map_of({{k, x}}));
	}
	const std::pair<const char*, const char*> pairs[] = {{"a", "b"}, {"b", "a"}, {"a", "z"}, {"z", "a"}, {"b", "z"}};
	for (const auto& p : pairs) {
		for (const auto& x : vals) {
			for (const auto& y : vals) v.push_back(map_of({{p.first, x}, {p.second, y}}));
		}
	}
	return v;
}

// Random documents: keys over a few names, values of every kind, bad maps, and (as S) undefined values.
struct Rng {
	std::uint64_t s;
	explicit Rng(std::uint64_t seed) : s(seed * 0x9e3779b97f4a7c15ULL + 0x7654321) {}
	std::uint64_t below(std::uint64_t n) {
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s % n;
	}
};

Doc rand_doc(Rng& r, int depth, bool in_s) {
	const std::uint64_t k = r.below(depth > 0 ? 12 : 7);
	switch (k) {
		case 0: return Doc::nil();
		case 1: case 2: return num(r.below(100));
		case 3: return str(r.below(2) ? "x" : "yy");
		case 4: return in_s ? undef() : num(r.below(100));
		case 5: return bad(static_cast<int>(r.below(3)));
		case 6: return arr_of({num(r.below(10))});
		case 7: return arr_of({rand_doc(r, depth - 1, in_s), num(1)});
		default: {
			Doc d = Doc::map();
			static const char* const keys[] = {"a", "b", "c", "z"};
			for (std::uint64_t i = 0, n = r.below(4); i < n; ++i) d.set(keys[r.below(4)], rand_doc(r, depth - 1, in_s));
			return d;
		}
	}
}

}  // namespace

int main(int argc, char** argv) {
	std::size_t random_cases = 3000;
	if (argc > 1) random_cases = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
	if (!MsgPack::unserialise(undef_bytes()).is_undefined()) fail("the bytes of an undefined value", "defined", "undefined");

	// the constant undefined value cannot be changed, whatever the source is (the error is the constant's)
	for (const Doc& source : {map_of({{"a", num(1)}}), bad(0), num(3), Doc::map()}) {
		bool threw = false;
		try {
			MsgPack::undefined().update(MsgPack::unserialise(source.bytes()));
		} catch (const BaseException& e) {
			threw = std::string(e.get_message()).find("Constant object") != std::string::npos;
		}
		if (!threw) fail("update() of the constant undefined value, source " + hex(source.bytes()), "no throw, or another error", "Constant object");
	}

	const auto ts = targets();
	const auto ss = sources();
	for (std::size_t i = 0; i < ts.size(); ++i) {
		for (std::size_t j = 0; j < ss.size(); ++j) check(ts[i], ss[j], "exhaustive T" + std::to_string(i) + " S" + std::to_string(j));
	}
	std::printf("exhaustive: %zu targets, %zu sources\n", ts.size(), ss.size());

	for (std::size_t i = 0; i < random_cases; ++i) {
		Rng r(i + 1);
		Doc t = rand_doc(r, 3, false);
		for (int step = 0; step < 3; ++step) {
			Doc s = rand_doc(r, 3, true);
			if (s.kind != Doc::MAP && r.below(3)) s = map_of({{"a", s}});
			check(t, s, "random " + std::to_string(i) + " step " + std::to_string(step));
			Doc next = t;
			if (merge(next, s) == OK) t = next;
		}
	}
	std::printf("random: %zu documents, three updates each\n", random_cases);
	std::printf("cases %zu: %zu merged, %zu threw on a bad key, %zu on a value that is not a container\n", g_cases, g_threw[OK], g_threw[BAD_KEY], g_threw[NOT_ITERABLE]);
	std::puts("msgpack update OK");
	return 0;
}
