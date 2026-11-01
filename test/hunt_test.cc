// A bounded bug hunt on msgpack.h: random sequences of every mutating operation against the model
// in test/model.h, with failures injected.
//
// A document is a map or an array of nested maps, arrays, scalars and "bad" maps (a map with a key
// that is not a string, which throws when it is filled). Each step picks a node, applies one
// operation to the MsgPack and to the model (set, replace, put, emplace, add, update, push_back,
// append, erase by key, position and iterator, clear, reserve, assignment of a whole value, copy,
// move), and compares serialise() with the model; every few steps the whole document is read back
// through the API (every key looked up, every position read) and compared.
//
// Failures injected:
//  - a bad map anywhere: every operation that fills it throws the one exception and changes nothing;
//  - an allocation that fails (HUNT_INJECT_ALLOC, a replaced operator new that throws std::bad_alloc
//    after a countdown, armed around one operation): the operation may or may not have taken effect,
//    so the model is read back from what serialise() gives, and the MsgPack must agree with its own
//    bytes (every key found, every position read) and keep working.
//
// usage: msgpack_hunt_test [--stress N] [--seed S] [--steps N]; MSGPACK_HUNT_SKIP=op,op skips named
// operations (to look past a known problem); MSGPACK_HUNT_TRACE=1 prints every step.
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include "msgpack.h"
#include "model.h"

#ifdef HUNT_INJECT_ALLOC
namespace {
long g_countdown = -1;   // allocations left before one fails; negative: off
long g_injected = 0;
}
void* operator new(std::size_t n) {
	if (g_countdown >= 0 && g_countdown-- == 0) {
		g_countdown = -1;
		++g_injected;
		throw std::bad_alloc();
	}
	if (void* p = std::malloc(n ? n : 1)) return p;
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#endif

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
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
	return r.size() > 300 ? r.substr(0, 300) + "..." : r;
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

// ---- where a run is, for the report of a failure

std::string g_where;
std::uint64_t g_seed = 0;

[[noreturn]] void fail(const std::string& what, const std::string& got, const std::string& expected) {
	std::fprintf(stderr, "FAILED seed %" PRIu64 ", %s\n  %s\n  got      %s\n  expected %s\n", g_seed, g_where.c_str(), what.c_str(), got.c_str(), expected.c_str());
	std::exit(1);
}

#define CHECK_SAME(what, got, expected) \
	do { \
		if (!((got) == (expected))) fail(what, hex(got), hex(expected)); \
	} while (0)

bool tracing() {
	static const bool on = std::getenv("MSGPACK_HUNT_TRACE") != nullptr;
	return on;
}

std::set<std::string>& skipped() {
	static std::set<std::string> s = [] {
		std::set<std::string> r;
		if (const char* e = std::getenv("MSGPACK_HUNT_SKIP")) {
			std::string t = e;
			for (std::size_t i = 0; i <= t.size();) {
				std::size_t j = t.find(',', i);
				if (j == std::string::npos) j = t.size();
				r.insert(t.substr(i, j - i));
				i = j + 1;
			}
		}
		return r;
	}();
	return s;
}

// ---- documents

const std::vector<std::string>& bad_keys() {
	static const std::vector<std::string> k = {
		model::uint_bytes(5), std::string("\xff", 1), std::string("\xc0", 1), std::string("\xc3", 1),
		std::string("\xc4\x01x", 3), std::string("\x90", 1),
	};
	return k;
}

std::string bad_map_bytes(Rng& r) {
	const std::size_t n = 1 + r.below(3);
	const std::size_t bad_at = r.below(n);
	std::string s = model::map_head(n);
	for (std::size_t i = 0; i < n; ++i) {
		s += i == bad_at || r.below(5) == 0 ? bad_keys()[r.below(bad_keys().size())] : model::str_bytes(std::string(1, static_cast<char>('a' + r.below(3))));
		s += model::uint_bytes(r.below(100));
	}
	return s;
}

bool is_bad_map(const Doc& d) {
	if (d.kind != Doc::RAW || d.s.empty()) return false;
	const unsigned char c = d.s[0];
	return (c & 0xf0) == 0x80;
}

// Whether a bad map is anywhere in the document (this node included).
bool has_bad(const Doc& d) {
	if (is_bad_map(d)) return true;
	for (const auto& e : d.a) if (has_bad(e)) return true;
	for (const auto& kv : d.m) if (has_bad(kv.second)) return true;
	return false;
}

std::string key_of(Rng& r) { return "k" + std::to_string(r.below(6)); }

Doc rand_doc(Rng& r, int depth, bool allow_bad) {
	const std::uint64_t kind = r.below(depth >= 3 ? 3 : (allow_bad ? 7 : 6));
	switch (kind) {
		case 0: return Doc::num(r.below(1000));
		case 1: return Doc::str(std::string(r.below(5), 'x'));
		case 2: return Doc::nil();
		case 3:
		case 4: {
			Doc d = Doc::arr();
			for (std::uint64_t i = 0, n = r.below(4); i < n; ++i) d.a.push_back(rand_doc(r, depth + 1, allow_bad));
			return d;
		}
		case 5: {
			Doc d = Doc::map();
			for (std::uint64_t i = 0, n = r.below(4); i < n; ++i) d.set(key_of(r), rand_doc(r, depth + 1, allow_bad));
			return d;
		}
		default: return Doc::raw(bad_map_bytes(r));
	}
}

// The document the bytes of a MsgPack say it is (for the model to continue from after a failure).
Doc from_object(const msgpack::object& o) {
	switch (o.type) {
		case msgpack::type::NIL: return Doc::nil();
		case msgpack::type::POSITIVE_INTEGER: return Doc::num(o.via.u64);
		case msgpack::type::STR: return Doc::str(std::string(o.via.str.ptr, o.via.str.size));
		case msgpack::type::ARRAY: {
			Doc d = Doc::arr();
			for (std::uint32_t i = 0; i < o.via.array.size; ++i) d.a.push_back(from_object(o.via.array.ptr[i]));
			return d;
		}
		case msgpack::type::MAP: {
			bool plain = true;
			for (std::uint32_t i = 0; i < o.via.map.size; ++i) plain = plain && o.via.map.ptr[i].key.type == msgpack::type::STR;
			if (plain) {
				Doc d = Doc::map();
				for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
					d.m.emplace_back(std::string(o.via.map.ptr[i].key.via.str.ptr, o.via.map.ptr[i].key.via.str.size), from_object(o.via.map.ptr[i].val));
				}
				return d;
			}
			[[fallthrough]];
		}
		default: {
			msgpack::sbuffer sb;
			msgpack::pack(sb, o);
			return Doc::raw(std::string(sb.data(), sb.size()));
		}
	}
}

Doc from_bytes(const std::string& bytes) {
	msgpack::object_handle h = msgpack::unpack(bytes.data(), bytes.size());
	return from_object(h.get());
}

MsgPack to_value(const Doc& d) {
	return MsgPack::unserialise(d.bytes());
}

// ---- what a failing fill does

const std::string kMessage = "MAP keys must be of type STR";

template <typename F>
bool throws_bad_map(F&& f) {
	try {
		f();
		return false;
	} catch (const BaseException& e) {
		return std::string(e.get_message()).find(kMessage) != std::string::npos;
	} catch (const std::exception&) {
		return false;
	}
}

// ---- reading the whole document back through the API

void check_tree(const MsgPack& m, const Doc& d, const std::string& path) {
	const std::string here = path.empty() ? "/" : path;
	switch (d.kind) {
		case Doc::NIL: if (!m.is_null()) fail("a node is not null at " + here, "?", "null"); break;
		case Doc::UINT:
			if (!m.is_number() || m.as_u64() != d.u) fail("a number differs at " + here, "?", std::to_string(d.u));
			break;
		case Doc::STR:
			if (!m.is_string() || m.str_view() != d.s) fail("a string differs at " + here, "?", d.s);
			break;
		case Doc::MAP:
			if (!m.is_map()) fail("not a map at " + here, "?", "map");
			if (m.size() != d.m.size()) fail("map size at " + here, std::to_string(m.size()), std::to_string(d.m.size()));
			for (const auto& kv : d.m) {
				if (m.count(kv.first) != 1) fail("key missing at " + here, kv.first, "present");
				check_tree(m.at(kv.first), kv.second, path + "/" + kv.first);
			}
			{
				std::size_t i = 0;
				for (auto it = m.begin(); it != m.end(); ++it, ++i) {
					if (i >= d.m.size() || (*it).str_view() != d.m[i].first) fail("iteration order at " + here, "?", i < d.m.size() ? d.m[i].first : "end");
				}
				if (i != d.m.size()) fail("iteration count at " + here, std::to_string(i), std::to_string(d.m.size()));
			}
			break;
		case Doc::ARR:
			if (!m.is_array()) fail("not an array at " + here, "?", "array");
			if (m.size() != d.a.size()) fail("array size at " + here, std::to_string(m.size()), std::to_string(d.a.size()));
			for (std::size_t i = 0; i < d.a.size(); ++i) check_tree(m.at(i), d.a[i], path + "[" + std::to_string(i) + "]");
			break;
		case Doc::RAW:
			if (is_bad_map(d)) {
				if (!throws_bad_map([&] { (void)m.count("a"); })) fail("a bad map did not throw at " + here, "no throw", kMessage);
				if (m.serialise() != d.s) fail("a bad map changed at " + here, hex(m.serialise()), hex(d.s));
			}
			break;
	}
}

// ---- the operations

enum Op {
	OP_SET_INDEX,      // m[key] = value
	OP_REPLACE,        // m.replace(key|pos, value)
	OP_PUT,            // m.put(key|pos, value)
	OP_EMPLACE,        // m.emplace(key|pos, value)
	OP_ADD,            // m.add(key|pos, value)
	OP_UPDATE,         // m.update(map)
	OP_PUSH_BACK,      // m.push_back(value)
	OP_APPEND,         // m.append(value)
	OP_ERASE_KEY,      // m.erase(key)
	OP_ERASE_POS,      // m.erase(pos)
	OP_ERASE_ITER,     // m.erase(iterator)
	OP_CLEAR,          // m.clear()
	OP_RESERVE,        // m.reserve(n)
	OP_ASSIGN,         // m = value
	OP_BULK,           // up to 40 new keys or elements at once: crosses the growth and the 16-key hash path
	OP_COPY,           // a copy, changed on its own, leaves the original
	OP_MOVE,           // moved out and back in
	OP_READ,           // find, count, at, get, begin
	N_OPS
};
const char* const op_names[N_OPS] = {
	"set_index", "replace", "put", "emplace", "add", "update", "push_back", "append", "erase_key", "erase_pos",
	"erase_iter", "clear", "reserve", "assign", "bulk", "copy", "move", "read",
};

// Model of MsgPack::update(map): the merge of o into d. A bad map is a map to update(): merging an empty
// map into it changes nothing, merging anything else has to fill it and throws (ModelStop), and a value
// that is not a map replaces it.
struct ModelStop {};

void model_update(Doc& d, const Doc& o) {
	if (o.kind != Doc::MAP) {
		d = o;
		return;
	}
	for (const auto& kv : o.m) {
		Doc* item = d.find(kv.first);
		if (!item) {
			d.set(kv.first, kv.second);
		} else if (item->kind == Doc::MAP) {
			model_update(*item, kv.second);
		} else if (is_bad_map(*item) && kv.second.kind == Doc::MAP) {
			if (!kv.second.m.empty()) throw ModelStop();
		} else {
			*item = kv.second;
		}
	}
}

struct Stats {
	std::size_t steps = 0, per_op[N_OPS] = {}, bad_map_steps = 0, injected_throws = 0, update_stopped = 0;
} stats;

bool g_threw = false;   // an injected allocation failure was thrown by the operation in progress

#ifdef HUNT_INJECT_ALLOC
// Runs f with one allocation set to fail after `after` allocations; returns whether it threw.
template <typename F>
bool with_failure(long after, F&& f) {
	g_countdown = after;
	bool threw = false;
	try {
		f();
	} catch (const std::bad_alloc&) {
		threw = true;
		g_threw = true;
	}
	g_countdown = -1;
	return threw;
}
#else
template <typename F>
bool with_failure(long, F&& f) {
	f();
	return false;
}
#endif

bool g_no_inject = false;   // set while a copy is changed: its model is not read back after a failure

bool inject_now(Rng& r) {
#ifdef HUNT_INJECT_ALLOC
	return !g_no_inject && r.below(3) == 0;
#else
	(void)r;
	return false;
#endif
}

struct Subject {
	MsgPack root;
	Doc doc;
};

// One operation on the node `m` of the model `d`. Returns false when it chose to do nothing.
bool apply(Subject& s, MsgPack& m, Doc& d, Op op, Rng& r, bool is_root) {
	const long after = static_cast<long>(r.below(30));
	const bool inject = inject_now(r);
	auto run = [&](auto&& f) -> bool {
		if (!inject) {
			f();
			return false;
		}
		return with_failure(after, f);
	};
	if (is_bad_map(d)) ++stats.bad_map_steps;

	switch (op) {
		case OP_SET_INDEX:
			if (d.kind == Doc::MAP) {
				const std::string key = key_of(r);
				Doc v = rand_doc(r, 1, true);
				if (run([&] { m[key] = to_value(v); })) return true;   // injected failure: resync by the caller
				d.set(key, v);
				return true;
			}
			return false;
		case OP_REPLACE:
		case OP_PUT: {
			Doc v = rand_doc(r, 1, true);
			if (d.kind == Doc::MAP) {
				const std::string key = key_of(r);
				if (run([&] { if (op == OP_PUT) m.put(std::string_view(key), to_value(v)); else m.replace(std::string_view(key), to_value(v)); })) return true;
				d.set(key, v);
				return true;
			}
			if (d.kind == Doc::ARR) {
				const std::size_t pos = r.below(d.a.size() + 5);   // up to four elements past the end are padded with nil
				if (run([&] { if (op == OP_PUT) m.put(pos, to_value(v)); else m.replace(pos, to_value(v)); })) return true;
				while (d.a.size() < pos) d.a.push_back(Doc::nil());
				if (pos < d.a.size()) d.a[pos] = v; else d.a.push_back(v);
				return true;
			}
			return false;
		}
		case OP_EMPLACE:
		case OP_ADD: {
			Doc v = rand_doc(r, 1, true);
			if (d.kind == Doc::MAP) {
				const std::string key = key_of(r);
				const bool had = d.find(key) != nullptr;
				if (run([&] { if (op == OP_ADD) m.add(std::string_view(key), to_value(v)); else { auto p = m.emplace(std::string_view(key), to_value(v)); if (p.second == had) fail("emplace result", std::to_string(p.second), std::to_string(!had)); } })) return true;
				if (!had) d.set(key, v);
				return true;
			}
			if (d.kind == Doc::ARR) {
				const std::size_t pos = r.below(d.a.size() + 5);
				const bool had = pos < d.a.size();
				if (run([&] { if (op == OP_ADD) m.add(pos, to_value(v)); else { auto p = m.emplace(pos, to_value(v)); if (p.second == had) fail("emplace result", std::to_string(p.second), std::to_string(!had)); } })) return true;
				if (!had) {
					while (d.a.size() < pos) d.a.push_back(Doc::nil());
					d.a.push_back(v);
				}
				return true;
			}
			return false;
		}
		case OP_UPDATE:
			if (d.kind == Doc::MAP) {
				Doc o = Doc::map();
				for (std::uint64_t i = 0, n = r.below(4); i < n; ++i) o.set(key_of(r), rand_doc(r, 1, false));
				Doc merged = d;
				bool model_stops = false;
				try { model_update(merged, o); } catch (const ModelStop&) { model_stops = true; }
				bool stopped = false;
				if (run([&] {
					try {
						m.update(to_value(o));
					} catch (const BaseException& e) {
						if (std::string(e.get_message()).find(kMessage) == std::string::npos) throw;
						stopped = true;
					}
				})) return true;
				if (stopped != model_stops) fail("update() and a bad map in its way", stopped ? "threw" : "did not throw", model_stops ? "throws" : "does not throw");
				if (stopped) {
					g_threw = true;   // the entries before the bad map were applied: the model is read back
					++stats.update_stopped;
					return true;
				}
				d = merged;
				return true;
			}
			return false;
		case OP_PUSH_BACK:
		case OP_APPEND:
			if (d.kind == Doc::ARR) {
				Doc v = rand_doc(r, 1, true);
				if (run([&] { if (op == OP_APPEND) m.append(to_value(v)); else m.push_back(to_value(v)); })) return true;
				d.a.push_back(v);
				return true;
			}
			return false;
		case OP_ERASE_KEY:
			if (d.kind == Doc::MAP) {
				const std::string key = r.below(8) == 0 ? "absent" : key_of(r);
				const bool had = d.find(key) != nullptr;
				std::size_t n = 99;
				const bool as_value = r.below(2) == 0;   // the key as a MsgPack, or as a string
				if (run([&] { n = as_value ? m.erase(MsgPack(key)) : m.erase(std::string_view(key)); })) return true;
				if (n != (had ? 1u : 0u)) fail("erase(key) returned", std::to_string(n), std::to_string(had));
				d.erase(key);
				return true;
			}
			return false;
		case OP_ERASE_POS:
			if (d.kind == Doc::ARR || d.kind == Doc::MAP) {
				const std::size_t size = d.kind == Doc::ARR ? d.a.size() : d.m.size();
				const std::size_t pos = r.below(size + 2);
				std::size_t n = 99;
				const bool as_value = r.below(2) == 0;   // the position as a MsgPack, or as a number
				if (run([&] { n = as_value ? m.erase(MsgPack(static_cast<std::uint64_t>(pos))) : m.erase(pos); })) return true;
				if (n != (pos < size ? 1u : 0u)) fail("erase(pos) returned", std::to_string(n), std::to_string(pos < size));
				if (pos < size) {
					if (d.kind == Doc::ARR) d.a.erase(d.a.begin() + pos); else d.m.erase(d.m.begin() + pos);
				}
				return true;
			}
			return false;
		case OP_ERASE_ITER:
			if ((d.kind == Doc::ARR && !d.a.empty()) || (d.kind == Doc::MAP && !d.m.empty())) {
				const std::size_t size = d.kind == Doc::ARR ? d.a.size() : d.m.size();
				const std::size_t pos = r.below(size);
				if (run([&] { auto it = m.begin(); for (std::size_t i = 0; i < pos; ++i) ++it; m.erase(it); })) return true;
				if (d.kind == Doc::ARR) d.a.erase(d.a.begin() + pos); else d.m.erase(d.m.begin() + pos);
				return true;
			}
			return false;
		case OP_CLEAR:
			if (run([&] { m.clear(); })) return true;
			if (d.kind == Doc::MAP) d.m.clear();
			else if (d.kind == Doc::ARR) d.a.clear();
			else if (d.kind == Doc::STR) d.s.clear();
			else if (is_bad_map(d)) d = Doc::map();
			return true;
		case OP_RESERVE:
			if (run([&] { m.reserve(r.below(40)); })) return true;
			return true;
		case OP_ASSIGN: {
			Doc v = rand_doc(r, 1, true);
			if (r.below(2)) {
				if (run([&] { m = to_value(v); })) return true;
			} else {
				MsgPack value = to_value(v);
				if (run([&] { m = value; })) return true;
			}
			d = v;
			return true;
		}
		case OP_BULK:
			if (d.kind == Doc::MAP || d.kind == Doc::ARR) {
				const std::size_t n = 1 + r.below(40);
				const std::size_t base = r.below(1000);
				if (run([&] {
					for (std::size_t i = 0; i < n; ++i) {
						if (d.kind == Doc::MAP) m["b" + std::to_string(base + i)] = i;
						else m.push_back(i);
					}
				})) return true;
				for (std::size_t i = 0; i < n; ++i) {
					if (d.kind == Doc::MAP) d.set("b" + std::to_string(base + i), Doc::num(i));
					else d.a.push_back(Doc::num(i));
				}
				return true;
			}
			return false;
		case OP_COPY: {
			// the copy is the same document, and changing it leaves the original alone
			MsgPack c(m);
			if (c.serialise() != d.bytes()) fail("a copy differs", hex(c.serialise()), hex(d.bytes()));
			Doc cd = d;
			Rng r2{ r.next() };
			g_no_inject = true;
			for (int i = 0; i < 3; ++i) {
				const Op o = Op(r2.below(OP_COPY));
				try {
					apply(s, c, cd, o, r2, false);
					if (g_threw) {   // an update() that stopped at a bad map: the copy's model is read back
						cd = from_bytes(c.serialise());
						g_threw = false;
					}
				} catch (...) {
					g_no_inject = false;
					throw;
				}
			}
			g_no_inject = false;
			// lock() the copy: it throws when a bad map is anywhere in it, and then nothing is locked
			bool threw = false;
			try {
				c.lock();
			} catch (const BaseException& e) {
				if (std::string(e.get_message()).find(kMessage) == std::string::npos) throw;
				threw = true;
			}
			if (threw != has_bad(cd)) fail("lock() of a copy", threw ? "threw" : "did not throw", has_bad(cd) ? "throws" : "does not throw");
			if (threw && c.locked()) fail("a copy after a lock() that threw", "locked", "not locked");
			if (!threw && !c.locked()) fail("a copy after lock()", "not locked", "locked");
			check_tree(c, cd, "(copy)");
			return true;
		}
		case OP_MOVE:
			if (is_root) {
				MsgPack t(std::move(m));
				m = std::move(t);
				return true;
			}
			return false;
		case OP_READ: {
			if (d.kind == Doc::STR) {
				// a string grows by append()
				const std::string more(r.below(4), 'y');
				if (run([&] { m.append(more); })) return true;
				d.s += more;
				return true;
			}
			if (d.kind == Doc::MAP) {
				const std::string key = key_of(r);
				const Doc* e = d.find(key);
				if ((m.find(std::string_view(key)) != m.end()) != (e != nullptr)) fail("find(" + key + ")", "?", e ? "found" : "absent");
				if (m.count(std::string_view(key)) != (e ? 1u : 0u)) fail("count(" + key + ")", "?", "?");
				if (e) check_tree(m.at(std::string_view(key)), *e, key);
			} else if (d.kind == Doc::ARR && !d.a.empty()) {
				const std::size_t i = r.below(d.a.size());
				check_tree(m.at(i), d.a[i], "[" + std::to_string(i) + "]");
			}
			return true;
		}
		default:
			return false;
	}
}

// A random node: the root, or something reached through maps and arrays that are not bad.
void pick(Subject& s, MsgPack*& m, Doc*& d, bool& is_root, Rng& r) {
	m = &s.root;
	d = &s.doc;
	is_root = true;
	while (r.below(3) != 0) {
		if (d->kind == Doc::MAP && !d->m.empty()) {
			auto& kv = d->m[r.below(d->m.size())];
			m = &m->at(std::string_view(kv.first));
			d = &kv.second;
		} else if (d->kind == Doc::ARR && !d->a.empty()) {
			const std::size_t i = r.below(d->a.size());
			m = &m->at(i);
			d = &d->a[i];
		} else {
			break;
		}
		is_root = false;
	}
}

void run_one(std::uint64_t seed, std::size_t steps) {
	Rng r{ seed };
	g_seed = seed;
	Subject s;
	s.doc = r.below(2) ? Doc::map() : Doc::arr();
	for (std::uint64_t i = 0, n = 2 + r.below(3); i < n; ++i) {
		if (s.doc.kind == Doc::MAP) s.doc.set(key_of(r), rand_doc(r, 1, true));
		else s.doc.a.push_back(rand_doc(r, 1, true));
	}
	s.root = to_value(s.doc);

	for (std::size_t step = 0; step < steps; ++step) {
		MsgPack* m;
		Doc* d;
		bool is_root;
		pick(s, m, d, is_root, r);
		Op op = Op(r.below(N_OPS));
		if (skipped().count(op_names[op])) continue;
		g_where = "step " + std::to_string(step) + ", op " + op_names[op] + ", target " + std::to_string(d->kind) + (is_bad_map(*d) ? " (bad map)" : "");
		if (tracing()) std::fprintf(stderr, "seed %" PRIu64 " %s  doc %s\n", seed, g_where.c_str(), hex(s.doc.bytes()).c_str());

		// a bad map is only read through, cleared or assigned over
		if (is_bad_map(*d) && op != OP_CLEAR && op != OP_ASSIGN && op != OP_RESERVE && op != OP_COPY && op != OP_READ) {
			// every operation that fills it throws the one exception and changes nothing
			bool threw = false;
			const std::string before = m->serialise();
			if (op == OP_ERASE_KEY || op == OP_ERASE_POS || op == OP_ERASE_ITER) threw = throws_bad_map([&] { m->erase(std::string_view("a")); });
			else threw = throws_bad_map([&] { (void)m->count("a"); });
			if (!threw) fail("an operation on a bad map did not throw", "no throw", kMessage);
			CHECK_SAME("a bad map after a throw", m->serialise(), before);
			continue;
		}

		g_threw = false;
		try {
			apply(s, *m, *d, op, r, is_root);
		} catch (const BaseException& e) {
			// update() merges into the maps it meets, and a bad map in its way throws when it is filled,
			// after the entries before it were applied (not atomic, listed in the CHANGELOG)
			if (op != OP_UPDATE || std::string(e.get_message()).find(kMessage) == std::string::npos) {
				fail(std::string("an operation threw: ") + e.get_message(), "exception", "none");
			}
			g_threw = true;
			++stats.update_stopped;
		} catch (const std::exception& e) {
			fail(std::string("an operation threw: ") + e.what(), "exception", "none");
		}
		const bool differs = s.root.serialise() != s.doc.bytes();
		bool resynced = false;
		if (g_threw) {
			// an injected allocation failure: the operation may or may not have taken effect, and what
			// the MsgPack says is read back; it must agree with its own bytes (check_tree below)
			++stats.injected_throws;
			s.doc = from_bytes(s.root.serialise());
			resynced = true;
		} else if (differs) {
			fail("serialise() after the operation", hex(s.root.serialise()), hex(s.doc.bytes()));
		}
		++stats.per_op[op];
		++stats.steps;
		if (step % 5 == 4 || resynced) {
			check_tree(s.root, s.doc, "");
			CHECK_SAME("a copy of the document", MsgPack(s.root).serialise(), s.doc.bytes());
			if (!has_bad(s.doc)) {
				// the same bytes, read again, hash and print the same
				MsgPack again = MsgPack::unserialise(s.root.serialise());
				if (s.root.hash() != again.hash()) fail("hash() of the same document", std::to_string(s.root.hash()), std::to_string(again.hash()));
				CHECK_SAME("to_string() of the same document", s.root.to_string(), again.to_string());
				// the document through RapidJSON, as a Document and as text, and back
				CHECK_SAME("the document through as_document()", MsgPack(s.root.as_document()).serialise(), s.doc.bytes());
				rapidjson::Document parsed;
				const std::string text = s.root.to_string();
				parsed.Parse(text.c_str(), text.size());
				CHECK_SAME("the document through to_string() and a parse", MsgPack(parsed).serialise(), s.doc.bytes());
			}
		}
	}
	check_tree(s.root, s.doc, "");
	CHECK_SAME("the document at the end", MsgPack::unserialise(s.root.serialise()).serialise(), s.doc.bytes());
}

// erase() says how many elements it erased: 1 for the last element as for any other, 0 for one that is not there.
void test_erase_counts() {
	for (std::size_t n = 1; n <= 4; ++n) {
		for (std::size_t victim = 0; victim < n + 1; ++victim) {
			Doc arr = Doc::arr(), map = Doc::map();
			for (std::size_t i = 0; i < n; ++i) {
				arr.a.push_back(Doc::num(i));
				map.set("k" + std::to_string(i), Doc::num(i));
			}
			const std::size_t expected = victim < n ? 1 : 0;
			g_where = "erase counts, " + std::to_string(n) + " elements, victim " + std::to_string(victim);
			MsgPack a = to_value(arr), m = to_value(map), it = to_value(arr);
			const std::size_t got_pos = a.erase(victim);
			const std::size_t got_key = m.erase(std::string_view("k" + std::to_string(victim)));
			if (got_pos != expected) fail("erase(pos) returned", std::to_string(got_pos), std::to_string(expected));
			if (got_key != expected) fail("erase(key) returned", std::to_string(got_key), std::to_string(expected));
			if (victim < n) {
				arr.a.erase(arr.a.begin() + victim);
				map.erase("k" + std::to_string(victim));
				auto next = it.begin();
				for (std::size_t i = 0; i < victim; ++i) ++next;
				next = it.erase(next);
				CHECK((next == it.end()) == (victim + 1 == n));   // the iterator after the last one is end()
			}
			CHECK_SAME("an array after erase(pos)", a.serialise(), arr.bytes());
			CHECK_SAME("a map after erase(key)", m.serialise(), map.bytes());
			if (victim < n) CHECK_SAME("an array after erase(iterator)", it.serialise(), arr.bytes());
		}
	}
}

}  // namespace

int main(int argc, char** argv) {
	std::size_t runs = 300, steps = 60;
	std::uint64_t seed = 1;
	bool single = false;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--stress") && i + 1 < argc) runs = std::strtoull(argv[++i], nullptr, 10);
		else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) { seed = std::strtoull(argv[++i], nullptr, 10); single = true; }
		else if (!std::strcmp(argv[i], "--steps") && i + 1 < argc) steps = std::strtoull(argv[++i], nullptr, 10);
	}
	test_erase_counts();
	for (std::size_t n = 0; n < (single ? 1 : runs); ++n) {
		run_one(single ? seed : 1000 + n * 104729, steps);
	}
	std::printf("hunt: %zu runs, %zu steps, %zu on a bad map, %zu injected failures", single ? std::size_t(1) : runs, stats.steps, stats.bad_map_steps, stats.injected_throws);
	std::printf(" (");
	for (int o = 0; o < N_OPS; ++o) std::printf("%s%s %zu", o ? ", " : "", op_names[o], stats.per_op[o]);
	std::printf(")\n");
	std::printf("msgpack hunt OK\n");
	return 0;
}
