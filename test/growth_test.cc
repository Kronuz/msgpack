// Growing an array must not disturb the maps in it. std::vector<MsgPack> reallocates when the
// array outgrows its capacity; if it copies the elements instead of moving them, each copy owns a
// zone of its own while the tree it points into belongs to the parent, and what is written
// through it later lands in a zone that is freed with the old elements. Run under
// AddressSanitizer this reads freed memory; run without it the checks below compare the
// document with a reference model after every kind of change.
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
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

namespace {

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

MsgPack build(const model::Doc& d) {
	switch (d.kind) {
		case model::Doc::UINT: return MsgPack(d.u);
		case model::Doc::STR: return MsgPack(d.s);
		case model::Doc::ARR: {
			MsgPack m = MsgPack::ARRAY();
			for (const auto& e : d.a) m.push_back(build(e));
			return m;
		}
		case model::Doc::MAP: {
			MsgPack m = MsgPack::MAP();
			for (const auto& kv : d.m) m[kv.first] = build(kv.second);
			return m;
		}
		default: return MsgPack(nullptr);
	}
}

model::Doc record(std::uint64_t i) {
	model::Doc d = model::Doc::map();
	d.set("id", model::Doc::num(i));
	d.set("name", model::Doc::str("record number " + std::to_string(i)));
	model::Doc tags = model::Doc::arr();
	tags.a.push_back(model::Doc::str("alpha"));
	tags.a.push_back(model::Doc::str("a tag that is longer than the small string buffer holds"));
	d.set("tags", tags);
	model::Doc nested = model::Doc::map();
	nested.set("ratio", model::Doc::num(i * 3));
	nested.set("deep", model::Doc::map());
	nested.find("deep")->set("list", model::Doc::arr());
	nested.find("deep")->find("list")->a.push_back(model::Doc::num(1));
	d.set("nested", nested);
	return d;
}

void check(const MsgPack& m, const model::Doc& d, const char* what, std::uint64_t seed, int step) {
	const std::string got = m.serialise();
	const std::string expected = d.bytes();
	if (got != expected) {
		std::fprintf(stderr, "seed %" PRIu64 " step %d: %s: the document differs\n  got      %s\n  expected %s\n", seed, step, what, hex(got).c_str(), hex(expected).c_str());
		std::exit(1);
	}
}

// The case of the report: 400 records, then a record pushed and a field of another one written,
// 800 times.
void test_push_and_write(bool from_wire) {
	model::Doc d = model::Doc::arr();
	for (std::uint64_t i = 0; i < 400; ++i) d.a.push_back(record(i));
	MsgPack value = from_wire ? MsgPack::unserialise(d.bytes()) : build(d);
	for (std::uint64_t i = 400; i < 1200; ++i) {
		value.push_back(build(record(i)));
		d.a.push_back(record(i));
		const std::size_t k = static_cast<std::size_t>(i - 400);
		const std::string name(static_cast<std::size_t>(i % 90), 'n');
		value.at(k)["name"] = name;
		d.a[k].set("name", model::Doc::str(name));
		if (i % 100 == 0) check(value, d, "push and write", 0, static_cast<int>(i));
	}
	check(value, d, "push and write", 0, 1200);
	for (std::size_t k = 0; k < d.a.size(); k += 37) {
		CHECK(value.at(k)["id"].as_u64() == d.a[k].find("id")->u);
		CHECK(value.at(k)["nested"]["deep"]["list"].at(0).as_u64() == 1);
	}
}

// Random changes to an array of maps, each compared with the model.
void test_random(std::size_t iterations) {
	static const char* keys[] = { "a", "b", "c", "name", "tags" };
	for (std::size_t it = 0; it < iterations; ++it) {
		const std::uint64_t seed = 77 + it * 104729;
		Rng r{ seed };
		model::Doc d = model::Doc::arr();
		const std::size_t start = r.below(6);
		for (std::size_t i = 0; i < start; ++i) d.a.push_back(record(i));
		MsgPack m = r.below(2) ? MsgPack::unserialise(d.bytes()) : build(d);
		const int steps = 10 + static_cast<int>(r.below(60));
		for (int step = 0; step < steps; ++step) {
			switch (r.below(7)) {
				case 0: case 1: {   // push a record
					model::Doc v = record(r.below(1000));
					m.push_back(build(v));
					d.a.push_back(v);
					break;
				}
				case 2: {   // push a scalar
					const std::uint64_t v = r.below(500);
					m.push_back(v);
					d.a.push_back(model::Doc::num(v));
					break;
				}
				case 3: case 4: {   // write a field of an element that is a map
					if (d.a.empty()) break;
					const std::size_t k = r.below(d.a.size());
					if (d.a[k].kind != model::Doc::MAP) break;
					const std::string key = keys[r.below(5)];
					const std::string v(r.below(40), 'w');
					m.at(k)[key] = v;
					d.a[k].set(key, model::Doc::str(v));
					break;
				}
				case 5: {   // erase a field of an element that is a map
					if (d.a.empty()) break;
					const std::size_t k = r.below(d.a.size());
					if (d.a[k].kind != model::Doc::MAP) break;
					const std::string key = keys[r.below(5)];
					m.at(k).erase(key);
					d.a[k].erase(key);
					break;
				}
				default: {   // read a field
					if (d.a.empty()) break;
					const std::size_t k = r.below(d.a.size());
					if (d.a[k].kind != model::Doc::MAP) break;
					if (model::Doc* f = d.a[k].find("id")) {
						CHECK(m.at(k)["id"].as_u64() == f->u);
					}
					break;
				}
			}
			check(m, d, "random", seed, step);
		}
		check(MsgPack(m), d, "copy", seed, steps);
		check(MsgPack::unserialise(m.serialise()), d, "round trip", seed, steps);
	}
	std::printf("random: %zu arrays of maps\n", iterations);
}

}  // namespace

int main(int argc, char** argv) {
	std::size_t iterations = 400;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--stress") && i + 1 < argc) iterations = std::strtoull(argv[++i], nullptr, 10);
	}
	test_push_and_write(false);
	test_push_and_write(true);
	test_random(iterations);
	// std::vector moves its elements on reallocation only when the move constructor is noexcept,
	// and copies them otherwise (every copy has a zone of its own, and the copies' children
	// write into it). This is the cause of the bug the cases above show under a sanitizer; it
	// is checked last so that they, and not this line, are what fails when it is undone.
	CHECK(std::is_nothrow_move_constructible_v<MsgPack>);
	std::printf("msgpack growth OK\n");
	return 0;
}
