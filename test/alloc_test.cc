// Allocation counts of unserialise, hash() and to_string() against the frozen implementation.
//
// The counts are deterministic, unlike a timing, and they are where the cost was: an unpacker
// zone copied into a second zone, a Body per node and an 8 KB buffer per leaf for hash(), and a
// RapidJSON Document for to_string(). It counts global operator new calls and RapidJSON's
// allocations, and the malloc calls of msgpack::zone (its chunks and its operator new).
#ifndef NDEBUG
#define NDEBUG
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <new>
#include <string>
#include <vector>

static std::atomic<std::size_t> g_allocations{0};
static std::atomic<std::size_t> g_bytes{0};

static void* counted_malloc(std::size_t n) {
	++g_allocations;
	g_bytes += n;
	return std::malloc(n);
}

static void* counted_realloc(void* p, std::size_t n) {
	++g_allocations;
	g_bytes += n;
	return std::realloc(p, n);
}

void* operator new(std::size_t n) {
	++g_allocations;
	g_bytes += n;
	if (void* p = std::malloc(n ? n : 1)) {
		return p;
	}
	throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// msgpack::zone allocates with ::malloc: count those calls too.
#define malloc(size) counted_malloc(size)
#include "msgpack/zone.hpp"
#undef malloc

#define RAPIDJSON_MALLOC(size) counted_malloc(size)
#define RAPIDJSON_REALLOC(ptr, new_size) counted_realloc(ptr, new_size)
#define RAPIDJSON_FREE(ptr) std::free(ptr)

#include "msgpack.h"
#include "frozen_msgpack.h"

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			std::exit(1); \
		} \
	} while (0)


template <typename F>
static std::size_t count(F&& f) {
	std::size_t before = g_allocations;
	f();
	return g_allocations - before;
}

template <typename F>
static std::size_t bytes(F&& f) {
	std::size_t before = g_bytes;
	f();
	return g_bytes - before;
}


// A record of 0.5 KB: 15 keys, nested maps and arrays.
static std::string record_wire() {
	MsgPack doc = {
		{ "id", 123456 }, { "name", "a record with a name" }, { "active", true }, { "score", 0.75 },
		{ "tags", { "alpha", "beta", "gamma", "delta" } },
		{ "address", { { "street", "1 Main Street" }, { "city", "Springfield" }, { "zip", "12345" }, { "geo", { { "lat", 12.5 }, { "lon", -7.25 } } } } },
		{ "history", { { { "at", 1 }, { "what", "created" } }, { { "at", 2 }, { "what", "changed" } } } },
		{ "note", "short" }, { "none", nullptr }, { "ratio", 3 }, { "level", 7 }, { "kind", "x" },
		{ "flags", { 1, 2, 3, 4, 5, 6, 7, 8 } }, { "owner", "someone@example.com" }, { "rank", -5 },
	};
	return doc.serialise();
}


int main() {
	const std::string wire = record_wire();

	{ MsgPackOld warm = MsgPackOld::unserialise(wire); MsgPack warm2 = MsgPack::unserialise(wire); }

	// unserialise: one zone, not two: the second zone is an operator new and an 8 KB chunk
	std::size_t old_read = count([&] { MsgPackOld v = MsgPackOld::unserialise(wire); });
	std::size_t new_read = count([&] { MsgPack v = MsgPack::unserialise(wire); });
	std::printf("unserialise: %zu allocations, was %zu\n", new_read, old_read);
	CHECK(new_read + 2 == old_read);
	CHECK(new_read <= 5);

	MsgPackOld old_doc = MsgPackOld::unserialise(wire);
	MsgPack new_doc = MsgPack::unserialise(wire);

	// hash(): none when every key is a string, no key repeats and no value packs to more than 256 bytes
	std::size_t old_hash = count([&] { MsgPackOld v = MsgPackOld::unserialise(wire); v.hash(); }) - old_read;
	std::size_t new_hash = count([&] { MsgPack v = MsgPack::unserialise(wire); v.hash(); }) - new_read;
	std::printf("hash: %zu allocations, was %zu\n", new_hash, old_hash);
	CHECK(old_hash > 20);
	CHECK(new_hash == 0);
	CHECK(old_doc.hash() == new_doc.hash());

	// to_string(): the text goes to the writer from the tree, with no Document
	std::size_t old_json = count([&] { old_doc.to_string(); });
	std::size_t new_json = count([&] { new_doc.to_string(); });
	std::size_t old_pretty = count([&] { old_doc.to_string(2); });
	std::size_t new_pretty = count([&] { new_doc.to_string(2); });
	std::size_t old_json_bytes = bytes([&] { old_doc.to_string(); });
	std::size_t new_json_bytes = bytes([&] { new_doc.to_string(); });
	std::printf("to_string: %zu allocations (%zu bytes), was %zu (%zu); indented %zu, was %zu\n", new_json, new_json_bytes, old_json, old_json_bytes, new_pretty, old_pretty);
	CHECK(new_json < old_json);
	CHECK(new_pretty < old_pretty);
	CHECK(new_json_bytes * 8 < old_json_bytes);
	CHECK(old_doc.to_string() == new_doc.to_string());
	CHECK(old_doc.to_string(2) == new_doc.to_string(2));

	std::printf("msgpack allocations OK\n");
	return 0;
}
