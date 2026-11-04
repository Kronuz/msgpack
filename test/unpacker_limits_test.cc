// The streaming unpacker (and unpack()) reserve for an array or map header only what the bytes in hand can
// hold: an item takes a byte (a map entry two), and a header that declares more gets its storage as the items
// arrive. Hostile array32/map32/str32/bin32/ext32 headers with no payload, nested, and split across feeds, ask
// for a few kilobytes at most; valid documents read the same split at every byte as in one piece.
#include "malloc_count.h"
#include "msgpack.hpp"

#include <cstdio>
#include <random>

using Bytes = std::string;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static constexpr std::size_t slack = 64 * 1024;   // a few zone chunks and the unpacker's buffer
// What a fed byte may justify, worst case: a 24-byte object, times 2 for the zone's power-of-two chunks, times 2
// for the growth steps and the alignment of strings.
static constexpr std::size_t per_byte = 4 * sizeof(msgpack::object);

static Bytes be32(std::uint32_t n) {
	return Bytes{char(n >> 24), char(n >> 16), char(n >> 8), char(n)};
}
static Bytes be16(std::uint32_t n) {
	return Bytes{char(n >> 8), char(n)};
}
static const Bytes array32_max = Bytes("\xdd") + be32(0xffffffff);
static const Bytes map32_max = Bytes("\xdf") + be32(0xffffffff);
static const Bytes array16_max = Bytes("\xdc") + be16(0xffff);
static const Bytes map16_max = Bytes("\xde") + be16(0xffff);
static const Bytes str32_max = Bytes("\xdb") + be32(0xffffffff);
static const Bytes bin32_max = Bytes("\xc6") + be32(0xffffffff);
static const Bytes ext32_max = Bytes("\xc9") + be32(0xffffffff) + Bytes("\x01");

static msgpack::unpacker make_unpacker() {
	return msgpack::unpacker(nullptr, nullptr, 64);   // no reference function: every string and bin is copied
}

// Feeds the chunks one after the other; returns how many objects came out.
static std::size_t feed(msgpack::unpacker& u, const Bytes& chunk, std::vector<msgpack::object_handle>* out = nullptr) {
	std::size_t count = 0;
	u.reserve_buffer(chunk.size());
	std::memcpy(u.buffer(), chunk.data(), chunk.size());
	u.buffer_consumed(chunk.size());
	msgpack::object_handle h;
	while (u.next(h)) {
		++count;
		if (out) out->push_back(std::move(h));
	}
	return count;
}

static void test_hostile_headers_one_chunk() {
	for (const Bytes* header : {&array32_max, &map32_max, &array16_max, &map16_max, &str32_max, &bin32_max, &ext32_max}) { try {
		reset_requests();
		{
			auto u = make_unpacker();
			CHECK(feed(u, *header) == 0);
		}
		CHECK(g_requested <= slack);
		CHECK(g_largest_request <= slack);
		} catch (const std::bad_alloc&) { std::fprintf(stderr, "%s: std::bad_alloc for the header of %zu bytes starting %02x\n", __func__, header->size(), (unsigned char)(*header)[0]); ++failures; }
	}
}

static void test_hostile_headers_one_shot() {
	for (const Bytes* header : {&array32_max, &map32_max, &array16_max, &map16_max, &str32_max, &bin32_max, &ext32_max}) { try {
		reset_requests();
		bool insufficient = false;
		try {
			msgpack::unpack(header->data(), header->size());
		} catch (const msgpack::insufficient_bytes&) {
			insufficient = true;
		}
		CHECK(insufficient);
		CHECK(g_requested <= slack);
		CHECK(g_largest_request <= slack);
		} catch (const std::bad_alloc&) { std::fprintf(stderr, "%s: std::bad_alloc for the header of %zu bytes starting %02x\n", __func__, header->size(), (unsigned char)(*header)[0]); ++failures; }
	}
}

static void test_hostile_headers_with_few_bytes_behind() {
	// The header declares 4 billion items; 100 bytes follow: at most those 100 items are ever stored.
	for (const Bytes* header : {&array32_max, &map32_max}) { try {
		reset_requests();
		{
			auto u = make_unpacker();
			Bytes data = *header + Bytes(100, '\x01');
			CHECK(feed(u, data) == 0);
		}
		CHECK(g_requested <= slack);
		CHECK(g_largest_request <= slack);
		} catch (const std::bad_alloc&) { std::fprintf(stderr, "%s: std::bad_alloc for the header of %zu bytes starting %02x\n", __func__, header->size(), (unsigned char)(*header)[0]); ++failures; }
	}
}

static void test_nested_hostile_headers() {
	// A thousand array32 headers, each declaring 4 billion items, nested: 5 KB of input. Each header may not
	// count on the bytes another already counts on (otherwise the sum is quadratic in the input).
	for (const Bytes* header : {&array32_max, &map32_max}) { try {
		Bytes data;
		for (int i = 0; i < 1000; ++i) data += *header;
		reset_requests();
		{
			auto u = make_unpacker();
			CHECK(feed(u, data) == 0);
		}
		CHECK(g_requested <= slack + per_byte * data.size());
		CHECK(g_largest_request <= slack + per_byte * data.size());
		} catch (const std::bad_alloc&) { std::fprintf(stderr, "%s: std::bad_alloc for the header of %zu bytes starting %02x\n", __func__, header->size(), (unsigned char)(*header)[0]); ++failures; }
	}
	// The same headers closed by a payload at the bottom, for a map the nested key chain.
	Bytes data;
	for (int i = 0; i < 1000; ++i) data += array32_max;
	reset_requests();
	msgpack::unpack_limit limit(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 100000);
	try {
		msgpack::unpack(data.data(), data.size(), nullptr, nullptr, limit);
	} catch (const msgpack::insufficient_bytes&) {
	}
	CHECK(g_requested <= slack + per_byte * data.size());
}

static void test_hostile_headers_split_across_feeds() {
	// The header in one feed, the rest one byte at a time: storage follows the bytes that arrived.
	for (const Bytes* header : {&array32_max, &map32_max, &str32_max, &bin32_max, &ext32_max}) { try {
		reset_requests();
		{
			auto u = make_unpacker();
			CHECK(feed(u, *header) == 0);
			for (int i = 0; i < 2000; ++i) CHECK(feed(u, Bytes("\x01")) == 0);
		}
		CHECK(g_requested <= slack + per_byte * 2100);
		CHECK(g_largest_request <= slack + per_byte * 2100);
		} catch (const std::bad_alloc&) { std::fprintf(stderr, "%s: std::bad_alloc for the header of %zu bytes starting %02x\n", __func__, header->size(), (unsigned char)(*header)[0]); ++failures; }
	}
	// The header itself split byte by byte, nested.
	reset_requests();
	{
		auto u = make_unpacker();
		Bytes data;
		for (int i = 0; i < 200; ++i) data += array32_max;
		for (char c : data) CHECK(feed(u, Bytes(1, c)) == 0);
	}
	CHECK(g_requested <= slack + per_byte * 1000);
}

static void test_hostile_after_valid_items() {
	// Valid items first, then a hostile header inside the same document, one feed per item.
	reset_requests();
	{
		auto u = make_unpacker();
		CHECK(feed(u, Bytes("\x93\x01\x02")) == 0);
		CHECK(feed(u, array32_max) == 0);
		CHECK(feed(u, map32_max) == 0);
	}
	CHECK(g_requested <= slack);
}

static void test_reservation_counts_the_bytes_of_an_item() {
	// A megabyte behind a header that declares 4 billion items: an array item takes a byte, a map entry two.
	const std::size_t bytes = 1000000;
	for (bool is_map : {false, true}) {
		Bytes data = (is_map ? map32_max : array32_max) + Bytes(bytes, '\x01');
		const std::size_t item = is_map ? sizeof(msgpack::object_kv) : sizeof(msgpack::object);
		const std::size_t items = is_map ? bytes / 2 : bytes;
		reset_requests();
		{
			auto u = make_unpacker();
			CHECK(feed(u, data) == 0);
		}
		// The zone rounds a block up to a power of two.
		CHECK(g_largest_request <= 2 * items * item + slack);
		CHECK(g_requested <= 2 * items * item + 4 * bytes + slack);
	}
}

static void test_limits_still_apply() {
	msgpack::unpack_limit limit(10, 10, 10, 10, 10, 4);
	{
		auto u = msgpack::unpacker(nullptr, nullptr, 64, limit);
		bool thrown = false;
		try { feed(u, Bytes("\xdc") + be16(11)); } catch (const msgpack::array_size_overflow&) { thrown = true; }
		CHECK(thrown);
	}
	{
		auto u = msgpack::unpacker(nullptr, nullptr, 64, limit);
		bool thrown = false;
		try { feed(u, Bytes("\xde") + be16(11)); } catch (const msgpack::map_size_overflow&) { thrown = true; }
		CHECK(thrown);
	}
	{
		auto u = msgpack::unpacker(nullptr, nullptr, 64, limit);
		bool thrown = false;
		try { feed(u, Bytes("\x91\x91\x91\x91\x91\x91")); } catch (const msgpack::depth_size_overflow&) { thrown = true; }
		CHECK(thrown);
	}
}

// ---- Valid documents: the same object however the bytes are split.

struct Gen {
	std::mt19937_64 rng;
	std::size_t budget = 200000;   // items left, so the document stays a few hundred KB
	explicit Gen(std::uint64_t seed) : rng(seed) {}
	std::size_t below(std::size_t n) { return rng() % n; }

	// Header with a size n of the given kind: 0 fix, 1 16-bit, 2 32-bit (the narrowest kind that fits or wider).
	void header(Bytes& out, unsigned fix_base, unsigned fix_max, unsigned c16, unsigned c32, std::size_t n) {
		std::size_t kind = below(3);
		if (kind == 0 && n <= fix_max) out += char(fix_base + n);
		else if ((kind <= 1 || n > 0xffff) && n <= 0xffff && kind != 2) out += char(c16) + be16(n);
		else out += char(c32) + be32(n);
	}
	void scalar(Bytes& out) {
		switch (below(8)) {
			case 0: out += '\xc0'; break;
			case 1: out += '\xc3'; break;
			case 2: out += char(below(128)); break;
			case 3: out += '\xcd' + be16(below(65536)); break;
			case 4: out += '\xcb' + be32(rng()) + be32(rng()); break;
			case 5: { std::size_t n = below(40); header_str(out, n); break; }
			case 6: { std::size_t n = below(40); out += '\xc4'; out += char(n); out += Bytes(n, 'b'); break; }
			default: out += '\xff'; break;
		}
	}
	void header_str(Bytes& out, std::size_t n) {
		std::size_t kind = below(3);
		if (kind == 0 && n < 32) out += char(0xa0 + n);
		else if (kind <= 1) { out += '\xd9'; out += char(n); }
		else out += '\xdb' + be32(n);
		out += Bytes(n, 's');
	}
	void value(Bytes& out, int depth) {
		std::size_t what = depth >= 5 || budget < 100 ? 0 : below(4);
		if (what <= 1) { scalar(out); if (budget) --budget; return; }
		std::size_t n = below(what == 2 ? 300 : 120);
		if (below(10) == 0) n = below(std::min<std::size_t>(70000, budget / 2));
		n = std::min(n, budget / 2);
		budget -= n;
		if (what == 2) {
			header(out, 0x90, 15, 0xdc, 0xdd, n);
			for (std::size_t i = 0; i < n; ++i) value(out, depth + 1);
		} else {
			header(out, 0x80, 15, 0xde, 0xdf, n);
			for (std::size_t i = 0; i < n; ++i) { header_str(out, below(6)); value(out, depth + 1); }
		}
	}
};

static void test_valid_documents_split_every_way() {
	for (std::uint64_t seed = 1; seed <= 60; ++seed) {
		Gen gen(seed);
		Bytes data;
		gen.value(data, 0);
		if (data.size() > 600000) continue;

		msgpack::object_handle whole = msgpack::unpack(data.data(), data.size());

		for (std::size_t chunk : {std::size_t(1), std::size_t(3), std::size_t(7), std::size_t(64), std::size_t(1000), data.size()}) {
			if (chunk == 1 && data.size() > 20000) continue;
			reset_requests();
			std::vector<msgpack::object_handle> out;
			{
				auto u = make_unpacker();
				for (std::size_t at = 0; at < data.size(); at += chunk) feed(u, data.substr(at, chunk), &out);
			}
			CHECK(out.size() == 1);
			if (out.size() == 1) CHECK(out[0].get() == whole.get());
			CHECK(g_requested <= slack + per_byte * data.size());
		}
	}
}

static void test_random_split_points() {
	for (std::uint64_t seed = 100; seed < 160; ++seed) {
		Gen gen(seed);
		Bytes data;
		gen.value(data, 0);
		if (data.size() > 100000) continue;
		msgpack::object_handle whole = msgpack::unpack(data.data(), data.size());
		auto u = make_unpacker();
		std::vector<msgpack::object_handle> out;
		for (std::size_t at = 0; at < data.size();) {
			std::size_t n = 1 + gen.below(gen.below(2) ? 8 : 4096);
			feed(u, data.substr(at, n), &out);
			at += n;
		}
		CHECK(out.size() == 1);
		if (out.size() == 1) CHECK(out[0].get() == whole.get());
	}
}

static void test_valid_document_is_stored_without_regrowth() {
	// 1000 arrays of 100 ints (one piece): each header's items are counted on, so no array outgrows its
	// storage and the total is the documents's items.
	Bytes data = Bytes("\xdd") + be32(1000);
	for (int i = 0; i < 1000; ++i) {
		data += Bytes("\xdd") + be32(100);
		data += Bytes(100, '\x07');
	}
	const std::size_t exact = (1000 + 1000 * 100) * sizeof(msgpack::object);
	reset_requests();
	{
		auto u = make_unpacker();
		std::vector<msgpack::object_handle> out;
		feed(u, data, &out);
		CHECK(out.size() == 1);
	}
	CHECK(g_requested <= exact + exact / 8 + data.size() * 2 + slack);
	CHECK(g_calls <= exact / 2048 + 30);   // zone chunks only: no array moved out of the zone to grow
	// Nested to depth 50, each level one item: all counted on, none regrown.
	Bytes nested;
	for (int i = 0; i < 50; ++i) nested += '\x91';
	nested += '\x01';
	reset_requests();
	{
		auto u = make_unpacker();
		std::vector<msgpack::object_handle> out;
		feed(u, nested, &out);
		CHECK(out.size() == 1);
	}
	CHECK(g_requested <= 51 * sizeof(msgpack::object) + slack);
	CHECK(g_calls <= 8);   // the buffer, the zones and their chunks
}

static void test_large_streamed_array_grows_in_steps() {
	// 200000 items declared up front, fed 1 KB at a time: the storage follows the bytes and ends at the
	// items it holds, in one block that is resized as it grows.
	const std::size_t n = 200000;
	Bytes data = Bytes("\xdd") + be32(n) + Bytes(n, '\x05');
	reset_requests();
	std::vector<msgpack::object_handle> out;
	{
		auto u = make_unpacker();
		for (std::size_t at = 0; at < data.size(); at += 1000) feed(u, data.substr(at, 1000), &out);
	}
	CHECK(out.size() == 1);
	if (out.size() == 1) {
		CHECK(out[0].get().type == msgpack::type::ARRAY);
		CHECK(out[0].get().via.array.size == n);
		for (std::size_t i = 0; i < n; i += 997) CHECK(out[0].get().via.array.ptr[i].via.u64 == 5);
	}
	CHECK(g_requested <= n * sizeof(msgpack::object) + 2 * data.size() + slack);
}

// A refused allocation (the hostile header's request) is a failure, not a crash.
#define RUN(test) do { try { test(); } catch (const std::bad_alloc&) { std::fprintf(stderr, "%s: std::bad_alloc\n", #test); ++failures; } catch (const std::exception& e) { std::fprintf(stderr, "%s: %s\n", #test, e.what()); ++failures; } } while (0)

static void test_streamed_array_with_strings_is_not_retained() {
	// 100000 one-character strings, the count declared, fed 1 KB at a time. The strings sit in the zone after
	// the array's storage, which grows meanwhile: the storage it outgrew is not left behind (about 24 + 8
	// bytes an item, not three times that).
	const std::size_t n = 100000;
	Bytes data = Bytes("\xdd") + be32(n);
	for (std::size_t i = 0; i < n; ++i) data += Bytes("\xa1x");
	reset_requests();
	std::vector<msgpack::object_handle> out;
	{
		auto u = make_unpacker();
		for (std::size_t at = 0; at < data.size(); at += 1000) feed(u, data.substr(at, 1000), &out);
	}
	CHECK(out.size() == 1);
	if (out.size() == 1) {
		CHECK(out[0].get().via.array.size == n);
		CHECK(out[0].get().via.array.ptr[n - 1].via.str.size == 1);
	}
	CHECK(g_requested <= 2 * (sizeof(msgpack::object) + 8) * n + 2 * data.size() + slack);
}

static void test_a_second_document_does_not_reuse_the_storage_of_the_first() {
	// The same unpacker reads two arrays that outgrow their reservation, the first one's result released
	// before the second starts.
	const std::size_t n = 5000;
	Bytes data = Bytes("\xdd") + be32(n) + Bytes(n, '\x03');
	auto u = make_unpacker();
	for (int round = 0; round < 3; ++round) {
		std::vector<msgpack::object_handle> out;
		for (std::size_t at = 0; at < data.size(); at += 100) feed(u, data.substr(at, 100), &out);
		CHECK(out.size() == 1);
		if (out.size() == 1) {
			CHECK(out[0].get().via.array.size == n);
			CHECK(out[0].get().via.array.ptr[n - 1].via.u64 == 3);
		}
	}
}

static void test_streamed_hostile_items_stay_under_the_bound() {
	// A header that declares 4 billion items, then cheap items one byte at a time: the storage follows the
	// bytes, within the bound for one piece. (An array item or half a map entry per byte.)
	for (const Bytes* header : {&array32_max, &map32_max}) {
		for (std::size_t total : {std::size_t(2000), std::size_t(8000), std::size_t(33000)}) {
			Bytes data = *header + Bytes(total, '\xc0');
			reset_requests();
			{
				auto u = make_unpacker();
				for (char c : data) CHECK(feed(u, Bytes(1, c)) == 0);
			}
			CHECK(g_requested <= slack + per_byte * data.size());
		}
	}
}

int main() {
	RUN(test_hostile_headers_one_chunk);
	RUN(test_hostile_headers_one_shot);
	RUN(test_hostile_headers_with_few_bytes_behind);
	RUN(test_nested_hostile_headers);
	RUN(test_hostile_headers_split_across_feeds);
	RUN(test_hostile_after_valid_items);
	RUN(test_reservation_counts_the_bytes_of_an_item);
	RUN(test_limits_still_apply);
	RUN(test_valid_documents_split_every_way);
	RUN(test_random_split_points);
	RUN(test_valid_document_is_stored_without_regrowth);
	RUN(test_large_streamed_array_grows_in_steps);
	RUN(test_streamed_hostile_items_stay_under_the_bound);
	RUN(test_streamed_array_with_strings_is_not_retained);
	RUN(test_a_second_document_does_not_reuse_the_storage_of_the_first);
	if (failures) { std::fprintf(stderr, "%d checks failed\n", failures); return 1; }
	std::puts("msgpack_unpacker_limits: ok");
	return 0;
}
