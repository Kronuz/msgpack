// The comparison libraries on the documents of corpus.h: msgpack-c (the current C++ release),
// nlohmann::json, mpack and RapidJSON. benchmarks/pack_unpack.cc runs MsgPack on the same documents and
// prints rows with the same operation names; compare.py joins the two outputs.
//
// This program does not include msgpack.h: msgpack-c's headers and the ones bundled with MsgPack use the
// same names, so the two live in separate programs.
//
//   read          bytes to a value            msgpack::unpack   json::from_msgpack   mpack_tree_parse
//   read+walk     read, then visit every node
//   read+lookup3  read, then three lookups
//   lookup3       three lookups in a value that is already read
//   write         value to bytes              msgpack::pack     json::to_msgpack
//   copy          a copy of the value
//   update        read, set one field, write
//   json-write    value to JSON text          json::dump        rapidjson::Writer over a DOM
//   json-read     JSON text to a value        json::parse       rapidjson::Document::Parse
//
// usage: msgpack_compare [DOCUMENT [OPERATION-SUBSTRING]]
#include <cstdio>
#include <string>

#include "bench_util.h"
#include "corpus.h"

#include <msgpack.hpp>
#include <nlohmann/json.hpp>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "mpack.h"

using bench::mix;
using bench::mix_dbl;
using bench::mix_str;
using nlohmann::json;

namespace {

// ---- the checksum of a document: every node, in order (the recipe of pack_unpack.cc)

std::uint64_t walk_object(std::uint64_t h, const msgpack::object& o) {
	switch (o.type) {
		case msgpack::type::NIL: return mix(h, 1);
		case msgpack::type::BOOLEAN: return mix(mix(h, 2), o.via.boolean);
		case msgpack::type::POSITIVE_INTEGER: return mix(mix(h, 3), o.via.u64);
		case msgpack::type::NEGATIVE_INTEGER: return mix(mix(h, 3), static_cast<std::uint64_t>(o.via.i64));
		case msgpack::type::FLOAT32:
		case msgpack::type::FLOAT64: return mix_dbl(mix(h, 4), o.via.f64);
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

std::uint64_t walk_json(std::uint64_t h, const json& j) {
	switch (j.type()) {
		case json::value_t::null: return mix(h, 1);
		case json::value_t::boolean: return mix(mix(h, 2), j.get<bool>());
		case json::value_t::number_unsigned: return mix(mix(h, 3), j.get<std::uint64_t>());
		case json::value_t::number_integer: return mix(mix(h, 3), static_cast<std::uint64_t>(j.get<std::int64_t>()));
		case json::value_t::number_float: return mix_dbl(mix(h, 4), j.get<double>());
		case json::value_t::string: {
			const auto& s = j.get_ref<const std::string&>();
			return mix_str(mix(h, 5), s.data(), s.size());
		}
		case json::value_t::array:
			h = mix(mix(h, 6), j.size());
			for (const auto& e : j) h = walk_json(h, e);
			return h;
		case json::value_t::object:
			h = mix(mix(h, 7), j.size());
			for (auto it = j.begin(); it != j.end(); ++it) {
				h = mix_str(h, it.key().data(), it.key().size());
				h = walk_json(h, it.value());
			}
			return h;
		default: return h;
	}
}

std::uint64_t walk_dom(std::uint64_t h, const rapidjson::Value& v) {
	switch (v.GetType()) {
		case rapidjson::kNullType: return mix(h, 1);
		case rapidjson::kFalseType: return mix(mix(h, 2), 0);
		case rapidjson::kTrueType: return mix(mix(h, 2), 1);
		case rapidjson::kNumberType:
			if (v.IsUint64()) return mix(mix(h, 3), v.GetUint64());
			if (v.IsInt64()) return mix(mix(h, 3), static_cast<std::uint64_t>(v.GetInt64()));
			return mix_dbl(mix(h, 4), v.GetDouble());
		case rapidjson::kStringType: return mix_str(mix(h, 5), v.GetString(), v.GetStringLength());
		case rapidjson::kArrayType:
			h = mix(mix(h, 6), v.Size());
			for (const auto& e : v.GetArray()) h = walk_dom(h, e);
			return h;
		case rapidjson::kObjectType:
			h = mix(mix(h, 7), v.MemberCount());
			for (const auto& m : v.GetObject()) {
				h = mix_str(h, m.name.GetString(), m.name.GetStringLength());
				h = walk_dom(h, m.value);
			}
			return h;
	}
	return h;
}

std::uint64_t walk_node(std::uint64_t h, mpack_node_t n) {
	switch (mpack_node_type(n)) {
		case mpack_type_nil: return mix(h, 1);
		case mpack_type_bool: return mix(mix(h, 2), mpack_node_bool(n));
		case mpack_type_uint: return mix(mix(h, 3), mpack_node_u64(n));
		case mpack_type_int: return mix(mix(h, 3), static_cast<std::uint64_t>(mpack_node_i64(n)));
		case mpack_type_float:
		case mpack_type_double: return mix_dbl(mix(h, 4), mpack_node_double(n));
		case mpack_type_str: return mix_str(mix(h, 5), mpack_node_str(n), mpack_node_strlen(n));
		case mpack_type_array: {
			const std::size_t count = mpack_node_array_length(n);
			h = mix(mix(h, 6), count);
			for (std::size_t i = 0; i < count; ++i) h = walk_node(h, mpack_node_array_at(n, i));
			return h;
		}
		case mpack_type_map: {
			const std::size_t count = mpack_node_map_count(n);
			h = mix(mix(h, 7), count);
			for (std::size_t i = 0; i < count; ++i) {
				const auto k = mpack_node_map_key_at(n, i);
				h = mix_str(h, mpack_node_str(k), mpack_node_strlen(k));
				h = walk_node(h, mpack_node_map_value_at(n, i));
			}
			return h;
		}
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

std::uint64_t lookup_json(const json& doc, const std::vector<corpus::Path>& paths) {
	std::uint64_t h = 0;
	for (const auto& path : paths) {
		const json* c = &doc;
		for (const auto& s : path) c = s.key ? &c->at(s.key) : &c->at(s.index);
		h = walk_json(h, *c);
	}
	return h;
}

std::uint64_t lookup_node(mpack_node_t doc, const std::vector<corpus::Path>& paths) {
	std::uint64_t h = 0;
	for (const auto& path : paths) {
		mpack_node_t c = doc;
		for (const auto& s : path) c = s.key ? mpack_node_map_cstr(c, s.key) : mpack_node_array_at(c, s.index);
		h = walk_node(h, c);
	}
	return h;
}

std::uint64_t lookup_dom(const rapidjson::Value& doc, const std::vector<corpus::Path>& paths) {
	std::uint64_t h = 0;
	for (const auto& path : paths) {
		const rapidjson::Value* c = &doc;
		for (const auto& s : path) c = s.key ? &(*c)[s.key] : &(*c)[static_cast<rapidjson::SizeType>(s.index)];
		h = walk_dom(h, *c);
	}
	return h;
}

// mpack parses into a tree it owns; the tree is freed on every pass, as the other libraries free theirs.
template <class F>
std::uint64_t with_tree(const std::string& bytes, F&& f) {
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, bytes.data(), bytes.size());
	mpack_tree_parse(&tree);
	const std::uint64_t r = f(tree);
	mpack_tree_destroy(&tree);
	return r;
}

}  // namespace

int main(int argc, char** argv) {
	const bench::Filter filter(argc, argv);
	const char* const MC = "msgpack-c9";
	const char* const NL = "nlohmann";
	const char* const MK = "mpack";
	const char* const RJ = "rapidjson";

	for (const auto& c : corpus::make_corpus()) {
		if (!filter.wants_doc(c.name)) continue;
		const char* const dn = c.name.c_str();
		const std::string& bytes = c.mp;
		const std::size_t nb = bytes.size();
		const auto paths = corpus::lookup_paths(c.name);

		std::uint64_t ref = 0, ref_lookup = 0;
		{
			const auto h = msgpack::unpack(bytes.data(), bytes.size());
			ref = walk_object(0, h.get());
			ref_lookup = lookup_object(h.get(), paths);
		}
		// every library must read the same document before any row is trusted
		{
			const json j = json::from_msgpack(bytes);
			if (walk_json(0, j) != ref || lookup_json(j, paths) != ref_lookup) {
				std::printf("MISMATCH: nlohmann reads %s differently\n", dn);
				return 1;
			}
			const std::uint64_t node_walk = with_tree(bytes, [&](mpack_tree_t& t) { return walk_node(0, mpack_tree_root(&t)); });
			const std::uint64_t node_lookup = with_tree(bytes, [&](mpack_tree_t& t) { return lookup_node(mpack_tree_root(&t), paths); });
			if (node_walk != ref || node_lookup != ref_lookup) {
				std::printf("MISMATCH: mpack reads %s differently\n", dn);
				return 1;
			}
			// RapidJSON's default parse does not round every 17-digit double exactly, so a last-bit
			// difference in the JSON rows is possible and only reported.
			rapidjson::Document d;
			d.Parse(c.json.c_str(), c.json.size());
			if (walk_dom(0, d) != ref || lookup_dom(d, paths) != ref_lookup) {
				std::printf("# WARN rapidjson reads the JSON text of %s differently (the last bit of some doubles)\n", dn);
			}
			if (walk_json(0, json::parse(c.json)) != ref) {
				std::printf("# WARN nlohmann reads the JSON text of %s differently\n", dn);
			}
		}

		if (filter.wants_op("read")) {
			auto r = bench::time_it([&] { auto h = msgpack::unpack(bytes.data(), bytes.size()); bench::keep(h); });
			bench::row(MC, dn, "read", r, ref, nb);
			r = bench::time_it([&] { json j = json::from_msgpack(bytes); bench::keep(j); });
			bench::row(NL, dn, "read", r, ref, nb);
			r = bench::time_it([&] { with_tree(bytes, [&](mpack_tree_t& t) { bench::keep(t); return 0; }); });
			bench::row(MK, dn, "read", r, ref, nb);
		}
		if (filter.wants_op("walk")) {
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] { auto h = msgpack::unpack(bytes.data(), bytes.size()); ck = walk_object(0, h.get()); bench::keep(ck); });
			bench::row(MC, dn, "read+walk", r, ck, nb);
			r = bench::time_it([&] { json j = json::from_msgpack(bytes); ck = walk_json(0, j); bench::keep(ck); });
			bench::row(NL, dn, "read+walk", r, ck, nb);
			r = bench::time_it([&] { ck = with_tree(bytes, [&](mpack_tree_t& t) { return walk_node(0, mpack_tree_root(&t)); }); bench::keep(ck); });
			bench::row(MK, dn, "read+walk", r, ck, nb);
			const json j = json::from_msgpack(bytes);
			r = bench::time_it([&] { ck = walk_json(0, j); bench::keep(ck); });
			bench::row(NL, dn, "walk(filled)", r, ck, nb);
		}
		if (filter.wants_op("lookup")) {
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] { auto h = msgpack::unpack(bytes.data(), bytes.size()); ck = lookup_object(h.get(), paths); bench::keep(ck); });
			bench::row(MC, dn, "read+lookup3", r, ck, nb);
			r = bench::time_it([&] { json j = json::from_msgpack(bytes); ck = lookup_json(j, paths); bench::keep(ck); });
			bench::row(NL, dn, "read+lookup3", r, ck, nb);
			r = bench::time_it([&] { ck = with_tree(bytes, [&](mpack_tree_t& t) { return lookup_node(mpack_tree_root(&t), paths); }); bench::keep(ck); });
			bench::row(MK, dn, "read+lookup3", r, ck, nb);
			const json j = json::from_msgpack(bytes);
			r = bench::time_it([&] { ck = lookup_json(j, paths); bench::keep(ck); });
			bench::row(NL, dn, "lookup3", r, ck);
			rapidjson::Document d;
			d.Parse(c.json.c_str(), c.json.size());
			r = bench::time_it([&] { ck = lookup_dom(d, paths); bench::keep(ck); });
			bench::row(RJ, dn, "lookup3", r, ck);
		}
		if (filter.wants_op("write")) {
			const json j = json::from_msgpack(bytes);
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] { auto v = json::to_msgpack(j); ck = v.size(); bench::keep(v); });
			bench::row(NL, dn, "write", r, ck, nb);
			const auto h = msgpack::unpack(bytes.data(), bytes.size());
			r = bench::time_it([&] { msgpack::sbuffer sb; msgpack::pack(sb, h.get()); ck = sb.size(); bench::keep(sb); });
			bench::row(MC, dn, "write", r, ck, nb);
		}
		if (filter.wants_op("copy")) {
			const json j = json::from_msgpack(bytes);
			auto r = bench::time_it([&] { json c2 = j; bench::keep(c2); });
			bench::row(NL, dn, "copy", r, ref, nb);
			const auto h = msgpack::unpack(bytes.data(), bytes.size());
			r = bench::time_it([&] { msgpack::zone z; msgpack::object o(h.get(), z); bench::keep(o); });
			bench::row(MC, dn, "copy", r, ref, nb);
		}
		if (filter.wants_op("update")) {
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] {
				json j = json::from_msgpack(bytes);
				if (c.name == "wide") j["k0500"] = 99;
				else if (c.name == "large") j[3]["score"] = 99;
				else j["score"] = 99;
				auto v = json::to_msgpack(j);
				ck = v.size();
				bench::keep(v);
			});
			bench::row(NL, dn, "update", r, ck, nb);
		}
		if (filter.wants_op("json")) {
			const json j = json::from_msgpack(bytes);
			std::uint64_t ck = 0;
			auto r = bench::time_it([&] { auto s = j.dump(); ck = s.size(); bench::keep(s); });
			bench::row(NL, dn, "json-write", r, ck, c.json.size());
			rapidjson::Document d;
			d.Parse(c.json.c_str(), c.json.size());
			r = bench::time_it([&] {
				rapidjson::StringBuffer sb;
				rapidjson::Writer<rapidjson::StringBuffer> w(sb);
				d.Accept(w);
				ck = sb.GetSize();
				bench::keep(ck);
			});
			bench::row(RJ, dn, "json-write", r, ck, c.json.size());
			r = bench::time_it([&] { json p = json::parse(c.json); bench::keep(p); });
			bench::row(NL, dn, "json-read", r, 0, c.json.size());
			r = bench::time_it([&] {
				rapidjson::Document d2;
				d2.Parse(c.json.c_str(), c.json.size());
				bench::keep(d2);
			});
			bench::row(RJ, dn, "json-read", r, 0, c.json.size());
		}
	}
	return 0;
}
