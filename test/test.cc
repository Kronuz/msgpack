#include "msgpack.h"
#include "msgpack_patcher.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "rapidjson/document.h"

static rapidjson::Document parse_json(const char* json) {
	rapidjson::Document doc;
	doc.Parse(json);
	assert(!doc.HasParseError());
	return doc;
}

static void test_construct_copy_index_roundtrip() {
	MsgPack value = {
		{ "name", "Neo" },
		{ "count", 1 },
		{ "nested", { { "flag", true } } },
		{ "items", { 1, 2, 3 } },
	};

	assert(value.is_map());
	assert(value["name"].str() == "Neo");
	assert(value["count"].as_u64() == 1);
	assert(value["items"].is_array());
	assert(value["items"].at(1).as_u64() == 2);

	MsgPack copy = value;
	copy["nested"]["flag"] = false;
	copy["items"].push_back(4);
	assert(value["nested"]["flag"].as_boolean());
	assert(value["items"].size() == 3);
	assert(!copy["nested"]["flag"].as_boolean());
	assert(copy["items"].size() == 4);

	std::string wire = value.serialise();
	MsgPack round_trip = MsgPack::unserialise(wire);
	assert(round_trip == value);
	assert(round_trip.to_string() == value.to_string());
}

static void test_type_coverage() {
	MsgPack nil(nullptr);
	MsgPack boolean(true);
	MsgPack negative(-7);
	MsgPack positive(static_cast<unsigned>(7));
	MsgPack number(1.5);
	MsgPack string_value("42");
	MsgPack array = { 1, "two", false };
	MsgPack map = { { "a", 1 }, { "b", 2 } };

	assert(nil.is_null());
	assert(boolean.is_boolean());
	assert(boolean.boolean());
	assert(negative.is_integer());
	assert(negative.i64() == -7);
	assert(positive.is_integer());
	assert(positive.u64() == 7);
	assert(number.is_float());
	assert(number.f64() == 1.5);
	assert(string_value.is_string());
	assert(string_value.as_u64() == 42);
	assert(array.is_array());
	assert(array.size() == 3);
	assert(map.is_map());
	assert(map.count("a") == 1);
}

static void test_rapidjson_roundtrip() {
	auto doc = parse_json(R"json(
	{
		// Xapiand's RapidJSON mode accepts comments and trailing commas.
		"name": "Morpheus",
		"score": 9,
	}
	)json");

	MsgPack value(doc);
	assert(value["name"].str() == "Morpheus");
	assert(value["score"].as_u64() == 9);

	rapidjson::Document out = value.as_document();
	assert(out.IsObject());
	assert(out["name"].GetString() == std::string("Morpheus"));
	assert(out["score"].GetUint() == 9);
}

static void test_json_patch() {
	MsgPack object = {
		{ "name", "Neo" },
		{ "tags", { "chosen" } },
		{ "old", 1 },
	};
	MsgPack patch = {
		MsgPack({ { "op", "replace" }, { "path", "/name" }, { "value", "Trinity" } }),
		MsgPack({ { "op", "add" }, { "path", "/tags/-" }, { "value", "operator" } }),
		MsgPack({ { "op", "remove" }, { "path", "/old" } }),
	};

	apply_patch(patch, object);
	assert(object["name"].str() == "Trinity");
	assert(object["tags"].size() == 2);
	assert(object["tags"].at(1).str() == "operator");
	assert(object.count("old") == 0);
	assert(object.to_string() == "{\"name\":\"Trinity\",\"tags\":[\"chosen\",\"operator\"]}");
}

static void test_string_view_adaptor() {
	std::string_view input("view-data");
	msgpack::sbuffer buffer;
	msgpack::pack(buffer, input);

	auto handle = msgpack::unpack(buffer.data(), buffer.size());
	std::string_view output;
	handle.get().convert(output);
	assert(output == input);
}

// Iterating an array yields its elements in order; iterating a map yields its
// keys, and at(key) yields the value. Ported from Xapiand's oldtests msgpack
// iterator case.
static void test_iterate() {
	MsgPack arr = { 10, 20, 30 };
	assert(arr.is_array());
	std::vector<uint64_t> elems;
	for (auto it = arr.cbegin(); it != arr.cend(); ++it) {
		elems.push_back(it->as_u64());
	}
	assert((elems == std::vector<uint64_t>{ 10, 20, 30 }));

	MsgPack m = { { "a", 1 }, { "b", 2 }, { "c", 3 } };
	assert(m.is_map());
	size_t count = 0;
	uint64_t sum = 0;
	for (auto it = m.cbegin(); it != m.cend(); ++it) {
		sum += m.at(*it).as_u64();   // *it is the key, at(key) is the value
		++count;
	}
	assert(count == 3);
	assert(sum == 6);
}

// erase() removes a map key or an array index and reports how many it removed.
// Ported from Xapiand's oldtests msgpack erase case.
static void test_erase() {
	MsgPack m = { { "a", 1 }, { "b", 2 }, { "c", 3 } };
	assert(m.erase("b") == 1);
	assert(m.size() == 2);
	bool gone = true;
	try { m.at("b"); gone = false; } catch (...) { }
	assert(gone);
	assert(m.at("a").as_u64() == 1 && m.at("c").as_u64() == 3);

	MsgPack arr = { 10, 20, 30 };
	arr.erase(static_cast<size_t>(1));   // drop the middle element
	assert(arr.size() == 2);
	assert(arr.at(0).as_u64() == 10 && arr.at(1).as_u64() == 30);
}

// A MsgPack& (or a sub-object taken by reference) aliases the same value, unlike
// a copy which is copy-on-write. Ported from Xapiand's oldtests msgpack reference
// case, and paired here with the COW copy behavior above.
static void test_reference_aliasing() {
	MsgPack obj = { { "x", "X" }, { "y", "Y" } };
	MsgPack& alias = obj;
	alias["x"] = "X2";
	obj["z"] = "Z";
	assert(obj["x"].str() == "X2");   // change through alias is visible on obj
	assert(alias["z"].str() == "Z");  // and vice versa
	assert(obj.to_string() == alias.to_string());

	// A sub-object taken by reference mutates the parent in place (COW happens on
	// copy, not on a reference).
	MsgPack doc = { { "inner", { { "k", 1 } } } };
	MsgPack& inner = doc["inner"];
	inner["k"] = 2;
	inner["k2"] = 3;
	assert(doc["inner"]["k"].as_u64() == 2);
	assert(doc["inner"]["k2"].as_u64() == 3);
}

int main() {
	test_construct_copy_index_roundtrip();
	test_type_coverage();
	test_rapidjson_roundtrip();
	test_json_patch();
	test_string_view_adaptor();
	test_iterate();
	test_erase();
	test_reference_aliasing();
	std::cout << "msgpack OK" << std::endl;
	return 0;
}
