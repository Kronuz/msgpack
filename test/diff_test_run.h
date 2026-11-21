// The operations run on both types and the walks that read a value through the API.
#pragma once

enum Op {
	HASH, SERIALIZE, JSON, JSON_INDENT, COPY, COPY_MUTATE, WALK, MUTATE, ERASE, MOVE, ROUNDTRIP, SIZE, N_OPS
};

struct Step {
	Op op;
	int arg;
};


// Appends the structure of `m` read through the public API (which fills every map and array on
// the way); a map is walked by position and by key.
template <typename M>
void walk(M& m, std::string& out, int depth) {
	if (depth > 12) {
		out += "...";
		return;
	}
	if (m.is_map()) {
		out += '{';
		for (auto it = m.begin(); it != m.end(); ++it) {
			out += hex(it->serialize());
			out += ':';
			walk(it.value(), out, depth + 1);
			out += ',';
		}
		out += '}';
	} else if (m.is_array()) {
		out += '[';
		for (std::size_t i = 0; i < m.size(); ++i) {
			walk(m.at(i), out, depth + 1);
			out += ',';
		}
		out += ']';
	} else {
		out += hex(m.serialize());
	}
}


template <typename M>
void mutate(M& m, int arg) {
	if (m.is_map()) {
		m["zz_new"] = arg;
		m["zz_text"] = "a string long enough to leave the small buffers behind";
		m["zz_text"] = "replaced";
	} else if (m.is_array()) {
		m.push_back(arg);
		m.push_back("tail");
	} else {
		m = arg;
	}
}


// What the maps of a document do with their keys.
struct KeyShape {
	bool repeats = false;        // some map holds a string key twice
	bool non_string = false;     // some map has a key that is not a string
	bool array_of_maps = false;  // the document is an array with a map somewhere inside
};

bool contains_map(const msgpack::object& o) {
	if (o.type == msgpack::type::MAP) return true;
	if (o.type == msgpack::type::ARRAY) {
		for (std::uint32_t i = 0; i < o.via.array.size; ++i) {
			if (contains_map(o.via.array.ptr[i])) return true;
		}
	}
	return false;
}

void key_shape(const msgpack::object& o, KeyShape& shape) {
	if (o.type == msgpack::type::MAP) {
		for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
			const auto& a = o.via.map.ptr[i].key;
			if (a.type != msgpack::type::STR) {
				shape.non_string = true;
			} else {
				for (std::uint32_t j = 0; j < i; ++j) {
					const auto& b = o.via.map.ptr[j].key;
					if (b.type == msgpack::type::STR && a.via.str.size == b.via.str.size &&
						std::memcmp(a.via.str.ptr, b.via.str.ptr, a.via.str.size) == 0) {
						shape.repeats = true;
					}
				}
			}
			key_shape(o.via.map.ptr[i].val, shape);
		}
	} else if (o.type == msgpack::type::ARRAY) {
		for (std::uint32_t i = 0; i < o.via.array.size; ++i) {
			key_shape(o.via.array.ptr[i], shape);
		}
	}
}


// The shape of the document at the start of `bytes`; a document that does not parse has none.
KeyShape document_key_shape(const std::string& bytes) {
	KeyShape shape;
	try {
		std::size_t off = 0;
		msgpack::object_handle h = msgpack::unpack(bytes.data(), bytes.size(), off);
		key_shape(h.get(), shape);
		shape.array_of_maps = h.get().type == msgpack::type::ARRAY && contains_map(h.get());
	} catch (const std::exception&) {
		shape = KeyShape();
	}
	return shape;
}


// The document at the start of `bytes` with every map merged as test/model.h says (no byte after
// it): what the frozen implementation reads in place of a document that repeats a key.
std::string merged_document(const std::string& bytes) {
	std::size_t off = 0;
	msgpack::object_handle h = msgpack::unpack(bytes.data(), bytes.size(), off);
	msgpack::sbuffer buf;
	msgpack::packer<msgpack::sbuffer> pk(&buf);
	model::pack_merged(pk, h.get());
	return std::string(buf.data(), buf.size());
}


// Touches every node through the API, which fills (and merges) every map and array.
template <typename M>
void fill_all(M& m) {
	if (m.is_map()) {
		for (auto it = m.begin(); it != m.end(); ++it) {
			fill_all(it.value());
		}
	} else if (m.is_array()) {
		for (std::size_t i = 0; i < m.size(); ++i) {
			fill_all(m.at(i));
		}
	}
}


// What `run` does with a document.
struct Mode {
	bool merged_input = false;   // the document repeats keys and the current implementation reads it as it is:
	                             // the offset is not logged (the frozen side reads the merged bytes, which are
	                             // shorter) and every map is filled first, so the merge has been done before the
	                             // first operation, as in the frozen side's input
	bool mutate = true;          // run the steps that change the value (mutate, erase)
	bool copy_mutate = true;     // run the step that changes a copy
	bool stop_at_throw = false;  // end the sequence at the first step that throws: filling a map that has a key
	                             // that is not a string throws, and the next fill of that map gives something else
	                             // in each implementation (see "Known problems" in CHANGELOG)
};


// Runs `steps` on the document in `bytes`.
template <typename M>
std::vector<std::string> run(const std::string& bytes, const Doc& doc, const std::vector<Step>& steps, bool lock_at_end, const Mode& mode) {
	const bool merged_input = mode.merged_input;
	std::vector<std::string> log;
	auto step = [&](const char* name, auto&& f) {
		try {
			log.push_back(std::string(name) + ": " + f());
		} catch (...) {
			log.push_back(std::string(name) + " threw " + current_exception_text());
		}
	};

	std::size_t off = 0;
	std::unique_ptr<M> root;   // the value as unserialize returns it, not assigned into another
	try {
		root.reset(new M(M::unserialize(bytes.data(), bytes.size(), off)));
		log.push_back(merged_input ? std::string("unserialize") : "unserialize off " + std::to_string(off));
	} catch (...) {
		log.push_back("unserialize threw " + current_exception_text() + (merged_input ? std::string() : " off " + std::to_string(off)));
		return log;
	}
	M& cur = *root;
	if (merged_input) fill_all(cur);

	for (const Step& s : steps) {
		switch (s.op) {
			case HASH:
				step("hash", [&] { return std::to_string(cur.hash()); });
				break;
			case SERIALIZE:
				step("serialize", [&] { return hex(cur.serialize()); });
				break;
			case JSON:
				if (!doc.non_string_key) step("json", [&] { return cur.to_string(); });
				break;
			case JSON_INDENT:
				if (!doc.non_string_key) step("json indent", [&] { return cur.to_string(s.arg % 5); });
				break;
			case COPY:
				step("copy", [&] {
					M c(cur);
					std::string r = hex(c.serialize());
					r += " " + std::to_string(c.hash());
					return r;
				});
				break;
			case COPY_MUTATE:
				if (mode.copy_mutate) step("copy mutate", [&] {
					M c(cur);
					mutate(c, s.arg);
					return hex(c.serialize()) + " | " + hex(cur.serialize());
				});
				break;
			case WALK:
				step("walk", [&] { std::string r; walk(cur, r, 0); return r; });
				break;
			case MUTATE:
				if (mode.mutate) step("mutate", [&] { mutate(cur, s.arg); return hex(cur.serialize()); });
				break;
			case ERASE:
				// What erase() returns is not compared: the frozen implementation returns 0 for the last
				// element of a map or an array, which it did erase (see the CHANGELOG). The change in size
				// says how many were erased.
				if (mode.mutate) step("erase", [&] {
					if (cur.is_map()) {
						const std::size_t before = cur.size();
						cur.erase(std::string_view("a"));
						return std::to_string(before - cur.size()) + " " + hex(cur.serialize());
					}
					if (cur.is_array()) {
						const std::size_t before = cur.size();
						cur.erase(static_cast<std::size_t>(0));
						return std::to_string(before - cur.size()) + " " + hex(cur.serialize());
					}
					return std::string("scalar");
				});
				break;
			case MOVE:
				step("move", [&] {
					M t(std::move(cur));
					cur = std::move(t);
					return hex(cur.serialize());
				});
				break;
			case ROUNDTRIP:
				step("roundtrip", [&] {
					std::string wire = cur.serialize();
					std::string padded = wire + "\xc1junk";
					std::size_t o = 0;
					M u = M::unserialize(padded.data(), padded.size(), o);
					std::string r = std::to_string(o) + " " + hex(u.serialize()) + " " + std::to_string(u.hash());
					std::size_t o2 = 0;
					M v = M::unserialize(std::string_view(wire), o2);
					r += " " + std::to_string(o2) + " " + hex(v.serialize());
					return r;
				});
				break;
			case SIZE:
				step("size", [&] { return std::to_string(cur.size()); });
				break;
			default:
				break;
		}
		if (mode.stop_at_throw && log.back().find(" threw ") != std::string::npos) return log;
	}
	if (lock_at_end) {
		step("lock", [&] { cur.lock(); return std::to_string(cur.locked()); });
		step("hash locked", [&] { return std::to_string(cur.hash()); });
		step("serialize locked", [&] { return hex(cur.serialize()); });
		if (!doc.non_string_key) step("json locked", [&] { return cur.to_string(); });
	}
	return log;
}


// The value `hash()` must have for a document without repeated keys: the XOR over every map of
// hash(key) ^ hash(value), and the hash of the packed bytes of everything else. Written from
// the definition, on the msgpack tree, with a std::string and an sbuffer.
std::size_t oracle_hash(const msgpack::object& o) {
	if (o.type == msgpack::type::MAP) {
		std::size_t h = 0;
		for (std::uint32_t i = 0; i < o.via.map.size; ++i) {
			h ^= std::hash<std::string>()(std::string(o.via.map.ptr[i].key.via.str.ptr, o.via.map.ptr[i].key.via.str.size));
			h ^= oracle_hash(o.via.map.ptr[i].val);
		}
		return h;
	}
	msgpack::sbuffer buf;
	msgpack::pack(&buf, o);
	return std::hash<std::string>()(std::string(buf.data(), buf.size()));
}
