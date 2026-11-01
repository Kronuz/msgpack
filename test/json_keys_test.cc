// A map whose key is not a string in `to_string()` and `as_document()`.
//
// JSON has string keys only. A str or bin key is written as it is, and any other key as its own
// compact JSON text: 5 is "5", -1.5 is "-1.5", true is "true", null is "null", a float that is not
// finite is "NaN", "Infinity" or "-Infinity", an ext is "null", and an array or a map is its JSON text
// (`[1,"a"]`, `{"5":1}`). Two keys can give the same text (5 and "5"), and the JSON then repeats the
// key. Reading `via.str` of a key that is not a str is undefined behavior, which the first versions of
// both functions did.
//
// The oracle is the JSON text of test/model.h (`reference_json`), written from this rule without
// RapidJSON and without MsgPack. Hand-written keys of every type, then random documents with keys of
// every type at any depth; the text of `to_string()`, compact and indented, and of `as_document()`
// written back with a Writer and a PrettyWriter must be the reference's, the document must not
// change, and the text must parse.
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "msgpack.h"
#include "model.h"

#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace {

[[noreturn]] void fail(const std::string& context, const std::string& got, const std::string& expected) {
	std::fprintf(stderr, "FAIL: %s\n  got      %s\n  expected %s\n", context.c_str(), got.c_str(), expected.c_str());
	std::exit(1);
}

#define CHECK_SAME(context, got, expected) \
	do { \
		const std::string g_ = (got); \
		const std::string e_ = (expected); \
		if (g_ != e_) fail(context, g_, e_); \
	} while (0)

struct Rng {
	std::uint64_t s;
	explicit Rng(std::uint64_t seed) : s(seed * 0x9e3779b97f4a7c15ULL + 0x1234567) {}
	std::uint64_t next() {
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s;
	}
	std::uint64_t below(std::uint64_t n) { return next() % n; }
};

using Pk = msgpack::packer<msgpack::sbuffer>;

std::string hex(const std::string& s) {
	static const char d[] = "0123456789abcdef";
	std::string r;
	for (unsigned char c : s) {
		r += d[c >> 4];
		r += d[c & 15];
	}
	return r;
}

// Whether a map has a key that is not a str: anywhere in the document, or only at the root.
bool has_odd_key(const msgpack::object& o, bool anywhere) {
	if (o.type == msgpack::type::MAP) {
		for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
			if (o.via.map.ptr[i].key.type != msgpack::type::STR) return true;
			if (anywhere && (has_odd_key(o.via.map.ptr[i].key, true) || has_odd_key(o.via.map.ptr[i].val, true))) return true;
		}
	} else if (anywhere && o.type == msgpack::type::ARRAY) {
		for (std::uint32_t i = 0; i < o.via.array.size; ++i) {
			if (has_odd_key(o.via.array.ptr[i], true)) return true;
		}
	}
	return false;
}

// Every check of one document (the bytes of a msgpack value).
std::size_t g_documents = 0, g_with_odd_keys = 0;

void check_document(const std::string& bytes, const std::string& context) {
	const std::string where = context + " " + hex(bytes);
	MsgPack m = MsgPack::unserialise(bytes);
	const std::string before = hex(m.serialise());   // (a float32 is written back as a float64)
	const std::string compact = model::reference_json(bytes);
	const std::string pretty = model::reference_json(bytes, 2);
	const std::string pretty0 = model::reference_json(bytes, 0);
	CHECK_SAME("to_string() " + where, m.to_string(), compact);
	CHECK_SAME("to_string(2) " + where, m.to_string(2), pretty);
	CHECK_SAME("to_string(0) " + where, m.to_string(0), pretty0);
	CHECK_SAME("the document after to_string() " + where, hex(m.serialise()), before);

	// as_document(), written back as text
	{
		rapidjson::Document doc = m.as_document();
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		doc.Accept(writer);
		CHECK_SAME("as_document() " + where, std::string(buffer.GetString(), buffer.GetSize()), compact);
		rapidjson::StringBuffer pbuffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> pwriter(pbuffer);
		pwriter.SetIndent(' ', 2);
		doc.Accept(pwriter);
		CHECK_SAME("as_document() indented " + where, std::string(pbuffer.GetString(), pbuffer.GetSize()), pretty);
	}

	// the text is JSON
	{
		rapidjson::Document parsed;
		parsed.Parse(compact.c_str(), compact.size());
		if (parsed.HasParseError()) fail("the text parses " + where, compact, "valid JSON");
	}

	// a copy, a read that fails, and a read of the same bytes give the same text
	CHECK_SAME("a copy " + where, MsgPack(m).to_string(), compact);
	msgpack::object_handle handle = msgpack::unpack(bytes.data(), bytes.size());
	if (has_odd_key(handle.get(), true)) ++g_with_odd_keys;
	if (has_odd_key(handle.get(), false)) {
		// the root map has a key that is not a string: filling it throws and changes nothing
		bool threw = false;
		try {
			(void)m.count("a");
		} catch (const BaseException&) {
			threw = true;
		}
		if (!threw) fail("a read of a map with a key that is not a string " + where, "no throw", "throws");
		CHECK_SAME("after a failed read " + where, m.to_string(), compact);
		CHECK_SAME("the document after a failed read " + where, hex(m.serialise()), before);
	}
	++g_documents;
}

// One key and one value, as the map {key: value}.
std::string one(const std::string& key, const std::string& value) {
	return model::map_head(1) + key + value;
}

std::string raw_double(double d) {
	msgpack::sbuffer buf;
	Pk pk(&buf);
	pk.pack_double(d);
	return std::string(buf.data(), buf.size());
}

std::string raw_float(float f) {
	msgpack::sbuffer buf;
	Pk pk(&buf);
	pk.pack_float(f);
	return std::string(buf.data(), buf.size());
}

void test_hand_written() {
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();
	struct Case { std::string name, key, text; };
	const std::vector<Case> cases = {
		{"uint", model::uint_bytes(5), "5"},
		{"uint16", model::uint_bytes(300), "300"},
		{"uint64 max", std::string("\xcf") + std::string(8, '\xff'), "18446744073709551615"},
		{"int -1", "\xff", "-1"},
		{"int8 -128", std::string("\xd0\x80", 2), "-128"},
		{"int64 min", std::string("\xd3\x80", 2) + std::string(7, '\0'), "-9223372036854775808"},
		{"nil", "\xc0", "null"},
		{"true", "\xc3", "true"},
		{"false", "\xc2", "false"},
		{"float64 1.5", raw_double(1.5), "1.5"},
		{"float32 -2.25", raw_float(-2.25f), "-2.25"},
		{"float64 -0.0", raw_double(-0.0), "-0.0"},
		{"float64 0.0", raw_double(0.0), "0.0"},
		{"float64 1e300", raw_double(1e300), "1e300"},
		{"float64 NaN", raw_double(nan), "NaN"},
		{"float64 Inf", raw_double(inf), "Infinity"},
		{"float64 -Inf", raw_double(-inf), "-Infinity"},
		{"bin", std::string("\xc4\x03xyz", 5), "xyz"},
		{"empty bin", std::string("\xc4\x00", 2), ""},
		{"ext", std::string("\xd4\x05\x07", 3), "null"},
		{"empty array", "\x90", "[]"},
		{"array", std::string("\x92\x01\xa1" "a", 4), "[1,\"a\"]"},
		{"array with a NaN", std::string("\x91") + raw_double(nan), "[NaN]"},
		{"empty map", "\x80", "{}"},
		{"map", std::string("\x81\xa1" "a\x01", 4), "{\"a\":1}"},
		{"map with a key that is not a string", std::string("\x81\x05\x01", 3), "{\"5\":1}"},
		{"map in an array in a key", std::string("\x91\x81\xc3\x91\xc0", 5), "[{\"true\":[null]}]"},
		{"string", model::str_bytes("s"), "s"},
		{"string with a quote", model::str_bytes("q\"uote\\"), "q\"uote\\"},
	};
	for (const auto& c : cases) {
		const bool nonfinite = c.text == "NaN" || c.text == "Infinity" || c.text == "-Infinity" || c.text == "[NaN]";
		for (const std::string& value : {model::uint_bytes(1), model::str_bytes("v"), std::string("\x92\x01\x02", 3)}) {
			// the key alone, then among others (the strings before and after it)
			check_document(one(c.key, value), "key " + c.name);
			std::string many = model::map_head(3) + model::str_bytes("first") + value + c.key + value + model::str_bytes("last") + value;
			check_document(many, "key " + c.name + " among others");
			// the key as a value, in a map and in an array (a float that is not finite as a value stops
			// the text, as it always did, and is only a key here)
			if (!nonfinite) {
				check_document(one(model::str_bytes("v"), c.key), "value " + c.name);
				check_document(model::array_head(2) + one(c.key, value) + c.key, "in an array " + c.name);
			}
		}
	}

	// exact text, as the README states it
	struct Exact { std::string bytes, text; };
	const std::vector<Exact> exact = {
		{std::string("\x82\x05\x01\xa1" "5\x02", 6), "{\"5\":1,\"5\":2}"},
		{std::string("\x83\xff\x01\xc0\x02\xc3\x03", 7), "{\"-1\":1,\"null\":2,\"true\":3}"},
		{std::string("\x81\x81\xa1" "a\x01\x02", 6), "{\"{\\\"a\\\":1}\":2}"},
		{std::string("\x81\x92\x01\xa1" "a\x02", 6), "{\"[1,\\\"a\\\"]\":2}"},
		{std::string("\x81") + std::string("\x81") + std::string("\x05\x01", 2) + std::string("\x02", 1), "{\"{\\\"5\\\":1}\":2}"},
	};
	for (const auto& e : exact) {
		MsgPack m = MsgPack::unserialise(e.bytes);
		CHECK_SAME("exact text " + hex(e.bytes), m.to_string(), e.text);
		check_document(e.bytes, "exact");
	}
}

const char* const kStrings[] = {"", "a", "b", "5", "null", "q\"uote", "back\\slash", "new\nline", "tab\t", "\x01" "ctl", "\xc3\xa9", "\xe6\x97\xa5\xe6\x9c\xac", "/slash", "\x7f"};
const double kDoubles[] = {0.5, -2.25, 1.0, 100.0, 1.5, 0.001, 123456.789, 1e300, -1.5e-7, 3.0, -0.0, 0.0};

void gen(Pk& pk, Rng& r, int depth, bool in_key) {
	const std::uint64_t kind = r.below(depth > 0 ? 15 : 11);
	switch (kind) {
		case 0: pk.pack_nil(); break;
		case 1: pk.pack_true(); break;
		case 2: pk.pack_false(); break;
		case 3: pk.pack_uint8(static_cast<std::uint8_t>(r.below(256))); break;
		case 4: pk.pack_uint64(r.next()); break;
		case 5: pk.pack_int8(static_cast<std::int8_t>(-1 - static_cast<int>(r.below(128)))); break;
		case 6: pk.pack_int64(-1 - static_cast<std::int64_t>(r.next() >> 2)); break;
		case 7: {
			if (in_key && r.below(3) == 0) {
				const double specials[] = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
				pk.pack_double(specials[r.below(3)]);
			} else if (r.below(2)) {
				pk.pack_double(kDoubles[r.below(sizeof(kDoubles) / sizeof(*kDoubles))]);
			} else {
				pk.pack_float(0.5f);
			}
			break;
		}
		case 8: {
			const char* s = kStrings[r.below(sizeof(kStrings) / sizeof(*kStrings))];
			pk.pack_str(static_cast<std::uint32_t>(std::strlen(s)));
			pk.pack_str_body(s, static_cast<std::uint32_t>(std::strlen(s)));
			break;
		}
		case 9: {
			const char* s = kStrings[r.below(sizeof(kStrings) / sizeof(*kStrings))];
			pk.pack_bin(static_cast<std::uint32_t>(std::strlen(s)));
			pk.pack_bin_body(s, static_cast<std::uint32_t>(std::strlen(s)));
			break;
		}
		case 10: pk.pack_ext(1, static_cast<std::int8_t>(r.below(100))); pk.pack_ext_body("x", 1); break;
		case 11: case 12: {
			const std::uint64_t n = r.below(4);
			pk.pack_array(static_cast<std::uint32_t>(n));
			for (std::uint64_t i = 0; i < n; ++i) gen(pk, r, depth - 1, in_key);
			break;
		}
		default: {
			const std::uint64_t n = r.below(4);
			pk.pack_map(static_cast<std::uint32_t>(n));
			for (std::uint64_t i = 0; i < n; ++i) {
				if (r.below(3) == 0) {
					gen(pk, r, depth - 1, true);   // a key of any type, containers included
				} else {
					const char* s = kStrings[r.below(sizeof(kStrings) / sizeof(*kStrings))];
					pk.pack_str(static_cast<std::uint32_t>(std::strlen(s)));
					pk.pack_str_body(s, static_cast<std::uint32_t>(std::strlen(s)));
				}
				gen(pk, r, depth - 1, in_key);
			}
			break;
		}
	}
}

void test_random(std::size_t n) {
	std::size_t with_maps = 0;
	for (std::size_t i = 0; i < n; ++i) {
		Rng r(i + 1);
		msgpack::sbuffer buf;
		Pk pk(&buf);
		gen(pk, r, 3, false);
		const std::string bytes(buf.data(), buf.size());
		// a document whose root is a map is read through the API too; others only through the text
		check_document(bytes, "random " + std::to_string(i));
		if (bytes[0] != 0 && (static_cast<unsigned char>(bytes[0]) & 0xf0) == 0x80) ++with_maps;
	}
	std::printf("random: %zu documents, %zu with a map at the root\n", n, with_maps);
}

}  // namespace

int main(int argc, char** argv) {
	std::size_t n = 4000;
	if (argc > 1) n = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
	test_hand_written();
	std::printf("hand-written: %zu documents so far\n", g_documents);
	test_random(n);
	std::printf("documents %zu, %zu with keys of any type\n", g_documents, g_with_odd_keys);
	std::puts("msgpack json keys OK");
	return 0;
}
