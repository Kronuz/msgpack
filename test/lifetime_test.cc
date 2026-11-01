// Lifetime tests for MsgPack::unserialise, which keeps the unpacker's zone as the value's own.
//
// The value must stay valid after the input buffer is gone, after the other values that were
// made from it are gone, and when it is copied, moved, mutated and read from other threads.
// Run under AddressSanitizer to see a premature release of the zone.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "msgpack.h"

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			std::exit(1); \
		} \
	} while (0)


static MsgPack record(int i) {
	return MsgPack({
		{ "id", i },
		{ "name", "record number " + std::to_string(i) },
		{ "tags", { "alpha", "beta", "a tag that is longer than the small string buffer holds" } },
		{ "nested", { { "ratio", 0.25 * i }, { "ok", true }, { "none", nullptr }, { "deep", { { "list", { 1, 2, 3 } } } } } },
	});
}


// A value read from a buffer that is overwritten and released straight after the read.
static void test_value_does_not_point_into_the_input() {
	MsgPack source = record(7);
	const std::string wire = source.serialise();

	std::size_t off = 0;
	char* buffer = static_cast<char*>(std::malloc(wire.size()));
	std::memcpy(buffer, wire.data(), wire.size());
	MsgPack value = MsgPack::unserialise(buffer, wire.size(), off);
	CHECK(off == wire.size());
	std::memset(buffer, 0xa5, wire.size());
	std::free(buffer);

	CHECK(value.serialise() == wire);
	CHECK(value["name"].str() == "record number 7");
	CHECK(value["tags"].at(2).str() == "a tag that is longer than the small string buffer holds");
	CHECK(value["nested"]["deep"]["list"].at(2).as_u64() == 3);
	CHECK(value.to_string() == source.to_string());
	CHECK(value.hash() == source.hash());
}


// The value is returned out of the frame that made it and out of the function that holds the bytes.
static MsgPack read_from_a_local(int i) {
	std::string wire = record(i).serialise();
	MsgPack value = MsgPack::unserialise(wire);
	for (char& c : wire) {
		c = 'x';
	}
	return value;
}

static void test_value_outlives_its_unpacker() {
	std::vector<MsgPack> values;
	for (int i = 0; i < 50; ++i) {
		values.push_back(read_from_a_local(i));
	}
	for (int i = 0; i < 50; ++i) {
		CHECK(values[i].serialise() == record(i).serialise());
		CHECK(values[i]["id"].as_i64() == i);
		CHECK(values[i]["name"].str() == "record number " + std::to_string(i));
	}
}


static void test_copies_are_independent() {
	std::string wire = record(3).serialise();
	std::unique_ptr<MsgPack> original(new MsgPack(MsgPack::unserialise(wire)));
	MsgPack copy(*original);
	MsgPack assigned;
	assigned = *original;

	// the copy and the assignment do not depend on the original
	original.reset();
	CHECK(copy.serialise() == wire);
	CHECK(assigned.serialise() == wire);
	CHECK(copy["nested"]["deep"]["list"].at(0).as_u64() == 1);

	// nor the other way around
	copy["name"] = "changed";
	copy["nested"]["deep"]["list"].push_back(4);
	CHECK(assigned["name"].str() == "record number 3");
	CHECK(assigned["nested"]["deep"]["list"].size() == 3);
	CHECK(copy["name"].str() == "changed");
	CHECK(copy["nested"]["deep"]["list"].size() == 4);

	// a copy of a part of the value
	std::unique_ptr<MsgPack> value(new MsgPack(MsgPack::unserialise(wire)));
	MsgPack part((*value)["nested"]);
	value.reset();
	CHECK(part["deep"]["list"].at(1).as_u64() == 2);
	CHECK(part["ok"].as_boolean());
}


static void test_moves() {
	std::string wire = record(4).serialise();
	MsgPack a = MsgPack::unserialise(wire);
	MsgPack b(std::move(a));
	CHECK(b.serialise() == wire);
	MsgPack c;
	{
		MsgPack d = MsgPack::unserialise(wire);
		c = std::move(d);
	}
	CHECK(c.serialise() == wire);
	std::vector<MsgPack> v;
	for (int i = 0; i < 100; ++i) {
		v.push_back(MsgPack::unserialise(wire));   // reallocations move the values
	}
	for (auto& m : v) {
		CHECK(m.serialise() == wire);
	}
}


// A part taken out of the value keeps the whole tree alive.
static void test_a_part_outlives_the_value() {
	std::string wire = record(5).serialise();
	std::unique_ptr<MsgPack> value(new MsgPack(MsgPack::unserialise(wire)));
	MsgPack nested = std::move((*value)["nested"]);
	value.reset();
	CHECK(nested["deep"]["list"].at(2).as_u64() == 3);
	CHECK(nested["ratio"].as_f64() == 1.25);
	nested["extra"] = "added after the value is gone";
	CHECK(nested["extra"].str() == "added after the value is gone");
	CHECK(nested.to_string().find("added after") != std::string::npos);
	CHECK(nested.hash() == nested.hash());
}


// Mutation allocates in the zone the unpacker filled: grow it well past one chunk.
// (Pushing records onto an array of maps after touching some of them is not exercised: that
// sequence reads freed memory in the frozen implementation too, see CHANGELOG.md.)
static void test_growth_after_unserialise() {
	MsgPack numbers = MsgPack::ARRAY();
	for (int i = 0; i < 400; ++i) {
		numbers.push_back(i * 1000003LL);
	}
	std::string wire = numbers.serialise();
	MsgPack value = MsgPack::unserialise(wire);
	CHECK(value.size() == 400);
	for (int i = 400; i < 4000; ++i) {
		value.push_back(i * 1000003LL);
	}
	CHECK(value.size() == 4000);
	for (int i = 0; i < 4000; i += 37) {
		CHECK(value.at(static_cast<std::size_t>(i)).as_i64() == i * 1000003LL);
	}
	MsgPack again = MsgPack::unserialise(value.serialise());
	CHECK(again == value);

	MsgPack map = MsgPack::unserialise(record(1).serialise());
	for (int i = 0; i < 2000; ++i) {
		map["key" + std::to_string(i)] = std::string(static_cast<std::size_t>(i % 200), 'v');
	}
	for (int i = 0; i < 500; ++i) {
		map["tags"].push_back("tag " + std::to_string(i));
	}
	CHECK(map.size() == 2004);
	CHECK(map["key1999"].str().size() == 199);
	CHECK(map["tags"].size() == 503);
	CHECK(map["tags"].at(502).str() == "tag 499");
	CHECK(map["id"].as_u64() == 1);
	CHECK(MsgPack::unserialise(map.serialise()) == map);
}


// A read that fails frees what it allocated, and leaves the offset alone.
static void test_failed_reads() {
	const std::string first = record(1).serialise();
	const std::string two = first + record(2).serialise();
	for (std::size_t len = first.size(); len < two.size(); ++len) {
		std::size_t off = first.size();
		bool threw = false;
		try {
			MsgPack::unserialise(two.data(), len, off);
		} catch (const std::exception&) {
			threw = true;
		}
		CHECK(threw);
		CHECK(off == first.size());
	}
	std::string bad = "\xc1";
	bool threw = false;
	try {
		MsgPack::unserialise(bad);
	} catch (const std::exception&) {
		threw = true;
	}
	CHECK(threw);

	// the offset advances past one value, and the next reads the next
	std::size_t off = 0;
	MsgPack one = MsgPack::unserialise(two.data(), two.size(), off);
	MsgPack second = MsgPack::unserialise(two.data(), two.size(), off);
	CHECK(off == two.size());
	CHECK(one["id"].as_u64() == 1 && second["id"].as_u64() == 2);
}


// Values made on one thread are read and released on others.
static void test_threads() {
	std::vector<std::string> wires;
	for (int i = 0; i < 8; ++i) {
		wires.push_back(record(i).serialise());
	}
	std::vector<std::unique_ptr<MsgPack>> made(8);
	{
		std::vector<std::thread> makers;
		for (int i = 0; i < 8; ++i) {
			makers.emplace_back([&, i] {
				MsgPack m;
				for (int k = 0; k < 200; ++k) {
					m = MsgPack();
					MsgPack v = MsgPack::unserialise(wires[i]);
					CHECK(v.hash() == record(i).hash());
					m = MsgPack::unserialise(wires[i]);
				}
				made[i].reset(new MsgPack(MsgPack::unserialise(wires[i])));
			});
		}
		for (auto& t : makers) {
			t.join();
		}
	}
	std::vector<std::thread> readers;
	for (int i = 0; i < 8; ++i) {
		readers.emplace_back([&, i] {
			MsgPack mine(std::move(*made[i]));
			CHECK(mine.serialise() == wires[i]);
			CHECK(mine["id"].as_i64() == i);
		});
	}
	for (auto& t : readers) {
		t.join();
	}
}


int main() {
	test_value_does_not_point_into_the_input();
	test_value_outlives_its_unpacker();
	test_copies_are_independent();
	test_moves();
	test_a_part_outlives_the_value();
	test_growth_after_unserialise();
	test_failed_reads();
	test_threads();
	std::printf("msgpack lifetime OK\n");
	return 0;
}
