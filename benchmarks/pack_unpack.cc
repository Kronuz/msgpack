// MsgPack against the msgpack-c 1.3.0 that it bundles, on the documents of corpus.h. The comparison with
// other libraries is benchmarks/compare.cc, which prints rows with the same operation names; compare.py
// joins the two outputs.
//
//   read          bytes to a value            MsgPack::unserialise      msgpack::unpack
//   read+walk     read, then visit every node
//   read+lookup3  read, then three lookups
//   lookup3       three lookups in a value that is already read (and, for MsgPack, filled)
//   write         value to bytes              MsgPack::serialise        msgpack::pack into an sbuffer
//   copy          a copy of the value
//   update        read, set one field, write
//   json-write    value to JSON text          MsgPack::to_string        (msgpack-c has none)
//   json-read     JSON text to a value        rapidjson::Document, then MsgPack(Document)
//   hash          MsgPack::hash()
//   build         the med record built through the MsgPack API
//
// Every row ends with a checksum of what the operation produced, so a library that reads a different
// document shows up. usage: msgpack_benchmark [DOCUMENT [OPERATION-SUBSTRING]]
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "bench_util.h"
#include "corpus.h"
#include "msgpack.h"
#include "rapidjson/document.h"

using bench::mix;
using bench::mix_dbl;
using bench::mix_str;

namespace {

// ---- the checksum of a document: every node, in order

std::uint64_t walk_object(std::uint64_t h, const msgpack::object& o) {
	switch (o.type) {
		case msgpack::type::NIL: return mix(h, 1);
		case msgpack::type::BOOLEAN: return mix(mix(h, 2), o.via.boolean);
		case msgpack::type::POSITIVE_INTEGER: return mix(mix(h, 3), o.via.u64);
		case msgpack::type::NEGATIVE_INTEGER: return mix(mix(h, 3), static_cast<std::uint64_t>(o.via.i64));
		case msgpack::type::FLOAT: return mix_dbl(mix(h, 4), o.via.f64);
		case msgpack::type::STR: return mix_str(mix(h, 5), o.via.str.ptr, o.via.str.size);
		case msgpack::type::ARRAY:
			h = mix(mix(h, 6), o.via.array.size);
			for (std::uint32_t i = 0; i < o.via.array.size; ++i) h = walk_object(h, o.via.array.ptr[i]);
			return h;
		case msgpack::type::MAP:
			h = mix(mix(h, 7), o.via.map.size);
			for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
				h = mix_str(h, o.via.map.ptr[i].key.via.str.ptr, o.via.map.ptr[i].key.via.str.size);
				h = walk_object(h, o.via.map.ptr[i].val);
			}
			return h;
		default: return h;
	}
}

std::uint64_t walk_value(std::uint64_t h, const MsgPack& m) {
	switch (m.get_type()) {
		case MsgPack::Type::NIL: return mix(h, 1);
		case MsgPack::Type::BOOLEAN: return mix(mix(h, 2), m.boolean());
		case MsgPack::Type::POSITIVE_INTEGER: return mix(mix(h, 3), m.u64());
		case MsgPack::Type::NEGATIVE_INTEGER: return mix(mix(h, 3), static_cast<std::uint64_t>(m.i64()));
		case MsgPack::Type::FLOAT: return mix_dbl(mix(h, 4), m.f64());
		case MsgPack::Type::STR: {
			const auto sv = m.str_view();
			return mix_str(mix(h, 5), sv.data(), sv.size());
		}
		case MsgPack::Type::ARRAY:
			h = mix(mix(h, 6), m.size());
			for (auto it = m.begin(); it != m.end(); ++it) h = walk_value(h, it.value());
			return h;
		case MsgPack::Type::MAP:
			h = mix(mix(h, 7), m.size());
			for (auto it = m.begin(); it != m.end(); ++it) {
				const auto k = (*it).str_view();
				h = mix_str(h, k.data(), k.size());
				h = walk_value(h, it.value());
			}
			return h;
		default: return h;
	}
}

// ---- lookups

const msgpack::object* find_key(const msgpack::object& m, const char* key) {
	const std::size_t n = std::strlen(key);
	for (std::uint32_t i = 0; i < m.via.map.size; ++i) {
		const auto& kv = m.via.map.ptr[i];
		if (kv.key.via.str.size == n && std::memcmp(kv.key.via.str.ptr, key, n) == 0) return &kv.val;
	}
	return nullptr;
}

std::uint64_t lookup_object(const msgpack::object& doc, const std::vector<corpus::Path>& paths) {
	std::uint64_t h = 0;
	for (const auto& path : paths) {
		const msgpack::object* c = &doc;
		for (const auto& s : path) c = s.key ? find_key(*c, s.key) : &c->via.array.ptr[s.index];
		h = walk_object(h, *c);
	}
	return h;
}

std::uint64_t lookup_value(const MsgPack& doc, const std::vector<corpus::Path>& paths) {
	std::uint64_t h = 0;
	for (const auto& path : paths) {
		const MsgPack* c = &doc;
		for (const auto& s : path) c = s.key ? &c->at(std::string_view(s.key)) : &c->at(s.index);
		h = walk_value(h, *c);
	}
	return h;
}

std::uint64_t lookup_value_index(const MsgPack& doc, const std::vector<corpus::Path>& paths) {
	std::uint64_t h = 0;
	for (const auto& path : paths) {
		const MsgPack* c = &doc;
		for (const auto& s : path) c = s.key ? &(*c)[std::string_view(s.key)] : &(*c)[s.index];
		h = walk_value(h, *c);
	}
	return h;
}

// ---- building through the API

MsgPack build(const corpus::Value& v) {
	switch (v.kind) {
		case corpus::Value::NIL: return MsgPack(nullptr);
		case corpus::Value::BOOL: return MsgPack(v.b);
		case corpus::Value::INT: return MsgPack(v.i);
		case corpus::Value::DBL: return MsgPack(v.d);
		case corpus::Value::STR: return MsgPack(v.s);
		case corpus::Value::ARR: {
			MsgPack a = MsgPack::ARRAY();
			for (const auto& e : v.items) a.push_back(build(e));
			return a;
		}
		case corpus::Value::MAP: {
			MsgPack m = MsgPack::MAP();
			for (std::size_t n = 0; n < v.items.size(); ++n) m[v.keys[n]] = build(v.items[n]);
			return m;
		}
	}
	return MsgPack();
}

}  // namespace

int main(int argc, char** argv) {
	const bench::Filter filter(argc, argv);
	const char* const OURS = "msgpack";
	const char* const BUNDLED = "msgpack-c1.3";

	for (const auto& c : corpus::make_corpus()) {
		if (!filter.wants_doc(c.name)) continue;
		const char* const dn = c.name.c_str();
		const std::string& bytes = c.mp;
		const std::size_t nb = bytes.size();
		const auto paths = corpus::lookup_paths(c.name);

		// the reference checksums, from the raw object tree, and the check that MsgPack reads the same document
		std::uint64_t ref = 0, ref_lookup = 0;
		{
			const auto h = msgpack::unpack(bytes.data(), bytes.size());
			ref = walk_object(0, h.get());
			ref_lookup = lookup_object(h.get(), paths);
			MsgPack d = MsgPack::unserialise(bytes);
			if (walk_value(0, d) != ref || lookup_value(d, paths) != ref_lookup || lookup_value_index(d, paths) != ref_lookup) {
				std::printf("MISMATCH: MsgPack reads %s differently\n", dn);
				return 1;
			}
			if (d.serialise() != bytes) {
				std::printf("MISMATCH: MsgPack writes %s differently\n", dn);
				return 1;
			}
		}

		if (filter.wants_op("read")) {
			auto r = bench::time_it([&] { auto h = msgpack::unpack(bytes.data(), bytes.size()); bench::keep(h); });
			bench::row(BUNDLED, dn, "read", r, ref, nb);
			r = bench::time_it([&] { MsgPack m = MsgPack::unserialise(bytes); bench::keep(m); });
			bench::row(OURS, dn, "read", r, ref, nb);
			// the same rows again: the spread between a row and its repeat is the noise of the machine
			r = bench::time_it([&] { auto h = msgpack::unpack(bytes.data(), bytes.size()); bench::keep(h); });
			bench::row(BUNDLED, dn, "read(A/A)", r, ref, nb);
			r = bench::time_it([&] { MsgPack m = MsgPack::unserialise(bytes); bench::keep(m); });
			bench::row(OURS, dn, "read(A/A)", r, ref, nb);
		}
		if (filter.wants_op("walk")) {
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] { auto h = msgpack::unpack(bytes.data(), bytes.size()); ck = walk_object(0, h.get()); bench::keep(ck); });
			bench::row(BUNDLED, dn, "read+walk", r, ck, nb);
			r = bench::time_it([&] { MsgPack m = MsgPack::unserialise(bytes); ck = walk_value(0, m); bench::keep(ck); });
			bench::row(OURS, dn, "read+walk", r, ck, nb);
			MsgPack filled = MsgPack::unserialise(bytes);
			walk_value(0, filled);
			r = bench::time_it([&] { ck = walk_value(0, filled); bench::keep(ck); });
			bench::row(OURS, dn, "walk(filled)", r, ck, nb);
			const auto h = msgpack::unpack(bytes.data(), bytes.size());
			r = bench::time_it([&] { ck = walk_object(0, h.get()); bench::keep(ck); });
			bench::row(BUNDLED, dn, "walk(raw)", r, ck, nb);
		}
		if (filter.wants_op("lookup")) {
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] { auto h = msgpack::unpack(bytes.data(), bytes.size()); ck = lookup_object(h.get(), paths); bench::keep(ck); });
			bench::row(BUNDLED, dn, "read+lookup3", r, ck, nb);
			r = bench::time_it([&] { MsgPack m = MsgPack::unserialise(bytes); ck = lookup_value(m, paths); bench::keep(ck); });
			bench::row(OURS, dn, "read+lookup3", r, ck, nb);
			MsgPack filled = MsgPack::unserialise(bytes);
			lookup_value(filled, paths);
			r = bench::time_it([&] { ck = lookup_value(filled, paths); bench::keep(ck); });
			bench::row(OURS, dn, "lookup3", r, ck);
			r = bench::time_it([&] { ck = lookup_value_index(filled, paths); bench::keep(ck); });
			bench::row(OURS, dn, "lookup3[]", r, ck);
			const auto h = msgpack::unpack(bytes.data(), bytes.size());
			r = bench::time_it([&] { ck = lookup_object(h.get(), paths); bench::keep(ck); });
			bench::row(BUNDLED, dn, "lookup3(linear)", r, ck);
		}
		if (filter.wants_op("write")) {
			MsgPack m = MsgPack::unserialise(bytes);
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] { auto s = m.serialise(); ck = s.size(); bench::keep(s); });
			bench::row(OURS, dn, "write", r, ck, nb);
			const auto h = msgpack::unpack(bytes.data(), bytes.size());
			r = bench::time_it([&] { msgpack::sbuffer sb; msgpack::pack(sb, h.get()); ck = sb.size(); bench::keep(sb); });
			bench::row(BUNDLED, dn, "write", r, ck, nb);
			msgpack::sbuffer reused(nb + 16);
			r = bench::time_it([&] { reused.clear(); msgpack::pack(reused, h.get()); ck = reused.size(); bench::keep(ck); });
			bench::row(BUNDLED, dn, "write(reused)", r, ck, nb);
		}
		if (filter.wants_op("copy")) {
			MsgPack m = MsgPack::unserialise(bytes);
			auto r = bench::time_it([&] { MsgPack c2(m); bench::keep(c2); });
			bench::row(OURS, dn, "copy", r, ref, nb);
			MsgPack filled = MsgPack::unserialise(bytes);
			walk_value(0, filled);
			r = bench::time_it([&] { MsgPack c2(filled); bench::keep(c2); });
			bench::row(OURS, dn, "copy(filled)", r, ref, nb);
			const auto h = msgpack::unpack(bytes.data(), bytes.size());
			r = bench::time_it([&] { msgpack::zone z; msgpack::object o(h.get(), z); bench::keep(o); });
			bench::row(BUNDLED, dn, "copy", r, ref, nb);
			const auto shared = std::make_shared<MsgPack>(m);
			r = bench::time_it([&] { auto c2 = shared; bench::keep(c2); });
			bench::row("shared_ptr", dn, "copy(floor)", r, ref);
		}
		if (filter.wants_op("update")) {
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] {
				MsgPack m = MsgPack::unserialise(bytes);
				if (c.name == "wide") m["k0500"] = 99;
				else if (c.name == "large") m[3]["score"] = 99;
				else m["score"] = 99;
				auto s = m.serialise();
				ck = s.size();
				bench::keep(s);
			});
			bench::row(OURS, dn, "update", r, ck, nb);
		}
		if (filter.wants_op("json")) {
			MsgPack m = MsgPack::unserialise(bytes);
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] { auto s = m.to_string(); ck = s.size(); bench::keep(s); });
			bench::row(OURS, dn, "json-write", r, ck, c.json.size());
			r = bench::time_it([&] {
				rapidjson::Document d;
				d.Parse(c.json.c_str(), c.json.size());
				MsgPack v(d);
				bench::keep(v);
			});
			bench::row(OURS, dn, "json-read", r, 0, c.json.size());
		}
		if (filter.wants_op("hash")) {
			MsgPack m = MsgPack::unserialise(bytes);
			const std::size_t hv = m.hash();
			const auto r = bench::time_it([&] { std::size_t x = m.hash(); bench::keep(x); });
			bench::row(OURS, dn, "hash", r, hv, nb);
		}
		if (filter.wants_op("build") && c.name == "med") {
			corpus::Value v = corpus::make_record(7);
			const auto r = bench::time_it([&] { MsgPack m = build(v); bench::keep(m); });
			bench::row(OURS, dn, "build", r, 0);
		}
	}
	return 0;
}
