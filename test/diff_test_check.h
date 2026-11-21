// Comparing the two types on a document, every prefix and the hand-written documents.
#pragma once

struct Stats {
	std::size_t docs = 0, parsed = 0, threw = 0, with_repeats = 0, not_compared = 0, non_finite = 0, steps = 0;
};

Stats stats;
std::size_t failures = 0;


std::size_t g_nonfinite_text = 0;   // JSON texts of documents with a NaN or Inf where the two differ as they should


void compare_logs(const char* what, std::uint64_t seed, const Doc& doc, const std::vector<std::string>& a, const std::vector<std::string>& b) {
	std::size_t n = std::min(a.size(), b.size());
	for (std::size_t i = 0; i < n; ++i) {
		// The one text that differs by design: the frozen implementation stops at a NaN or Inf (the
		// Writer refuses it) and leaves the text before it, the current one writes null there and goes
		// on. So the old text must be the beginning of the new one (the same calls, in the same order,
		// up to the number), and equal to it when the number was erased before the text was written.
		if (doc.non_finite && a[i] != b[i] && a[i].rfind("json", 0) == 0 && b[i].compare(0, a[i].size(), a[i]) == 0) {
			++g_nonfinite_text;
			continue;
		}
		if (a[i] != b[i]) {
			std::fprintf(stderr, "%s: seed %" PRIu64 " step %zu differs\n  doc %s\n  old %s\n  new %s\n", what, seed, i,
				hex(doc.bytes).c_str(), a[i].substr(0, 600).c_str(), b[i].substr(0, 600).c_str());
			++failures;
			return;
		}
	}
	if (a.size() != b.size()) {
		std::fprintf(stderr, "%s: seed %" PRIu64 " logs have %zu and %zu entries\n", what, seed, a.size(), b.size());
		++failures;
	}
}


void check_document(std::uint64_t seed, const Doc& doc, Rng& r) {
	++stats.docs;
	if (std::getenv("MSGPACK_DIFF_TRACE")) {   // names the document a crash happened in
		std::fprintf(stderr, "document seed %" PRIu64 " %s\n", seed, hex(doc.bytes.substr(0, 300)).c_str());
	}
	const KeyShape shape = document_key_shape(doc.bytes);
	std::vector<Step> steps;
	std::size_t n_steps = 2 + r.below(10);
	for (std::size_t i = 0; i < n_steps; ++i) {
		steps.push_back(Step{ static_cast<Op>(r.below(N_OPS)), static_cast<int>(r.below(1000)) });
	}
	// Plain runs of the operations that are most sensitive to order: the first thing done to a
	// document decides whether hash() or a walk fills (and merges) the maps first.
	static const Op orders[][3] = {
		{ HASH, SERIALIZE, COPY }, { SERIALIZE, HASH, JSON }, { WALK, HASH, SERIALIZE }, { JSON, COPY, HASH },
	};
	const bool lock_at_end = r.chance(1, 8);
	if (shape.repeats && shape.non_string) {
		// Not compared (see the top of the file), but the current implementation still reads it, so
		// a sanitizer sees what it does with a map that throws half way through its merge.
		++stats.not_compared;
		Mode reads_only;
		reads_only.mutate = reads_only.copy_mutate = false;
		for (const auto& order : orders) {
			std::vector<Step> fixed;
			for (Op op : order) fixed.push_back(Step{ op, 0 });
			run<MsgPack>(doc.bytes, doc, fixed, false, reads_only);
		}
		run<MsgPack>(doc.bytes, doc, steps, lock_at_end, reads_only);
		return;
	}
	// A document that repeats a string key is read by the frozen implementation as the merged
	// document, by the current one as it is.
	const bool merge = shape.repeats;
	const std::string old_bytes = merge ? merged_document(doc.bytes) : doc.bytes;
	// A step that changes the value is left out where the frozen implementation is wrong by itself:
	// an array with a map in it, grown after the maps were filled, gave elements that point into
	// freed memory (test/growth_test.cc covers that against the model). A sequence on a document
	// with a key that is not a string ends at its first exception (see Mode::stop_at_throw).
	Mode mode;
	mode.merged_input = merge;
	mode.mutate = !shape.array_of_maps;
	mode.stop_at_throw = shape.non_string;
	for (const auto& order : orders) {
		std::vector<Step> fixed;
		for (Op op : order) fixed.push_back(Step{ op, 0 });
		auto a = run<MsgPackOld>(old_bytes, doc, fixed, false, mode);
		auto b = run<MsgPack>(doc.bytes, doc, fixed, false, mode);
		compare_logs("fixed order", seed, doc, a, b);
	}
	auto a = run<MsgPackOld>(old_bytes, doc, steps, lock_at_end, mode);
	auto b = run<MsgPack>(doc.bytes, doc, steps, lock_at_end, mode);
	stats.steps += steps.size();
	compare_logs("random ops", seed, doc, a, b);
	if (a[0].find("threw") != std::string::npos) ++stats.threw; else ++stats.parsed;
	if (doc.non_finite) ++stats.non_finite;

	// hash() against the definition: the first thing done to a document that repeats no key, and
	// to one that does, whose maps are merged by it (those reached through maps; an array is
	// packed as it is).
	std::size_t off = 0;
	try {
		msgpack::object_handle h = msgpack::unpack(doc.bytes.data(), doc.bytes.size(), off);
		if (shape.non_string) return;
		if (merge) ++stats.with_repeats;
		MsgPack m = MsgPack::unserialize(doc.bytes);
		std::size_t expected = merge ? model::hash_merged_maps(h.get()) : oracle_hash(h.get());
		if (m.hash() != expected) {
			std::fprintf(stderr, "hash oracle: seed %" PRIu64 " doc %s: got %zu, expected %zu\n", seed, hex(doc.bytes).c_str(), m.hash(), expected);
			++failures;
		}
	} catch (const std::exception&) {
	}
}


// Every prefix of a document: both types either throw the same exception or read the same value.
void check_prefixes(std::uint64_t seed, const Doc& doc) {
	std::size_t n = doc.bytes.size();
	std::size_t stride = n > 400 ? n / 200 : 1;
	for (std::size_t len = 0; len < n; len += stride) {
		const char* data = doc.bytes.data();
		std::string a, b;
		std::size_t oa = 0, ob = 0;
		try {
			MsgPackOld m = MsgPackOld::unserialize(data, len, oa);
			a = "ok " + std::to_string(oa) + " " + hex(m.serialize());
		} catch (...) {
			a = "threw " + current_exception_text() + " " + std::to_string(oa);
		}
		try {
			MsgPack m = MsgPack::unserialize(data, len, ob);
			b = "ok " + std::to_string(ob) + " " + hex(m.serialize());
		} catch (...) {
			b = "threw " + current_exception_text() + " " + std::to_string(ob);
		}
		if (a != b) {
			std::fprintf(stderr, "prefix %zu: seed %" PRIu64 "\n  old %s\n  new %s\n", len, seed, a.substr(0, 300).c_str(), b.substr(0, 300).c_str());
			++failures;
			return;
		}
	}
}


std::string str_header(std::size_t n) {
	std::string h;
	if (n <= 31) {
		h.push_back(static_cast<char>(0xa0 | n));
	} else if (n <= 0xff) {
		h.push_back(static_cast<char>(0xd9)); h.push_back(static_cast<char>(n));
	} else if (n <= 0xffff) {
		h.push_back(static_cast<char>(0xda)); h.push_back(static_cast<char>(n >> 8)); h.push_back(static_cast<char>(n));
	} else {
		h.push_back(static_cast<char>(0xdb));
		for (int i = 3; i >= 0; --i) h.push_back(static_cast<char>(n >> (8 * i)));
	}
	return h;
}

std::string str_obj(const std::string& s) {
	return str_header(s.size()) + s;
}

std::string map_head(std::size_t n) {
	std::string h;
	if (n <= 15) {
		h.push_back(static_cast<char>(0x80 | n));
	} else {
		h.push_back(static_cast<char>(0xdf));
		for (int i = 3; i >= 0; --i) h.push_back(static_cast<char>(n >> (8 * i)));
	}
	return h;
}

std::string array_head(std::size_t n) {
	std::string h;
	if (n <= 15) {
		h.push_back(static_cast<char>(0x90 | n));
	} else {
		h.push_back(static_cast<char>(0xdd));
		for (int i = 3; i >= 0; --i) h.push_back(static_cast<char>(n >> (8 * i)));
	}
	return h;
}


// Documents chosen by hand: the shapes the random generator reaches only by luck.
std::vector<std::pair<std::string, Doc>> special_documents() {
	std::vector<std::pair<std::string, Doc>> docs;
	auto add = [&](std::string name, std::string bytes, bool non_string = false, bool non_finite = false) {
		Doc d;
		d.bytes = std::move(bytes);
		d.non_string_key = non_string;
		d.non_finite = non_finite;
		docs.emplace_back(std::move(name), std::move(d));
	};
	const std::string one = "\x01";
	// {"a": 1, "a": 2, "b": 3, "c": 4}: the repeat is merged, and the entries after it must follow
	add("repeat then two", map_head(4) + str_obj("a") + one + str_obj("a") + "\x02" + str_obj("b") + "\x03" + str_obj("c") + "\x04");
	add("repeat last", map_head(3) + str_obj("a") + one + str_obj("b") + "\x02" + str_obj("a") + "\x03");
	add("repeat first two", map_head(2) + str_obj("a") + one + str_obj("a") + "\x02");
	add("three equal", map_head(3) + str_obj("a") + one + str_obj("a") + "\x02" + str_obj("a") + "\x03");
	add("nested repeat", map_head(2) + str_obj("x") + map_head(2) + str_obj("k") + one + str_obj("k") + "\x02" + str_obj("y") + one);
	add("repeat under array", map_head(1) + str_obj("x") + array_head(1) + map_head(2) + str_obj("k") + one + str_obj("k") + "\x02");
	add("repeat under array under map", array_head(1) + map_head(2) + str_obj("k") + one + str_obj("k") + "\x02");
	add("empty key twice", map_head(2) + str_obj("") + one + str_obj("") + "\x02");
	add("prefix keys", map_head(3) + str_obj("a") + one + str_obj(std::string("a\0", 2)) + "\x02" + str_obj("ab") + "\x03");
	add("empty map", "\x80");
	add("empty array", "\x90");
	add("empty str", "\xa0");
	add("empty map in map", map_head(1) + str_obj("m") + "\x80");
	add("non string key", map_head(2) + str_obj("a") + one + "\x05" + "\x02", true);
	add("non string key nested", map_head(1) + str_obj("a") + map_head(1) + "\xc0" + one, true);
	add("nan", "\xcb\x7f\xf8\x00\x00\x00\x00\x00\x00", false, true);
	add("nan in array", array_head(3) + one + "\xcb\x7f\xf8\x00\x00\x00\x00\x00\x00" + one, false, true);
	add("inf in map", map_head(2) + str_obj("a") + one + str_obj("b") + "\xca\x7f\x80\x00\x00", false, true);
	add("minus zero", "\xcb\x80\x00\x00\x00\x00\x00\x00\x00");
	add("denormal", "\xcb\x00\x00\x00\x00\x00\x00\x00\x01");
	add("uint64 max", "\xcf\xff\xff\xff\xff\xff\xff\xff\xff");
	add("int64 min", "\xd3\x80\x00\x00\x00\x00\x00\x00\x00");
	add("uint64 in int64 form", "\xd3\x00\x00\x00\x00\x00\x00\x00\x07");
	add("ext 5", "\xd4\x05\x07");
	add("ext undefined type", "\xd4\x00\x00");
	add("ext negative type", "\xd4\xff\x01");
	add("ext in map", map_head(1) + str_obj("e") + "\xc7\x03\x7f" + "abc");
	add("bin in map", map_head(1) + str_obj("b") + "\xc4\x03" + "abc");
	add("nul and control", map_head(1) + str_obj(std::string("k\0\x01\x1f", 4)) + str_obj(std::string("v\0\"\\\n\t\x7f", 7)));
	add("invalid utf8", str_obj("\xff\xfe\xc3"));

	// 200 levels of maps and of arrays
	{
		std::string m, a;
		for (int i = 0; i < 200; ++i) { m += map_head(1) + str_obj("k"); a += array_head(1); }
		add("deep maps", m + "\x07");
		add("deep arrays", a + "\x07");
		std::string mixed;
		for (int i = 0; i < 200; ++i) mixed += (i % 2 ? array_head(1) : map_head(1) + str_obj("k"));
		add("deep mixed", mixed + "\x07");
	}
	// a map of 40, 300 and 3000 distinct keys; the same with the last key repeating the first
	for (std::size_t n : { 17u, 40u, 300u, 3000u }) {
		std::string distinct = map_head(n), repeated = map_head(n);
		for (std::size_t i = 0; i < n; ++i) {
			distinct += str_obj("key" + std::to_string(i)) + one;
			repeated += str_obj(i + 1 == n ? "key0" : "key" + std::to_string(i)) + one;
		}
		std::string tag = std::to_string(n);
		add("map of " + tag, distinct);
		add("map of " + tag + " repeating", repeated);
	}
	// long strings, as leaves, keys and bin; and an array root holding a long packed value
	{
		std::string big(70000, 'x'), mid(300, 'y'), edge(250, 'z');
		add("70 KB string", str_obj(big));
		add("string in map", map_head(2) + str_obj("big") + str_obj(big) + str_obj(mid) + str_obj(edge));
		add("long key", map_head(1) + str_obj(big) + one);
		add("array root of long values", array_head(3) + str_obj(big) + str_obj(mid) + str_obj(edge));
		add("array root of 20000 ints", array_head(20000) + std::string(20000, '\x05'));
		std::string packed_edge;   // packed values of exactly 255, 256 and 257 bytes
		for (std::size_t n : { 252u, 253u, 254u, 255u }) packed_edge += str_obj(std::string(n, 'e'));
		add("packed size around 256", array_head(4) + packed_edge);
		add("map of values around 256", map_head(4) + str_obj("a") + str_obj(std::string(250, 'q')) + str_obj("b") + str_obj(std::string(251, 'q')) +
			str_obj("c") + str_obj(std::string(252, 'q')) + str_obj("d") + str_obj(std::string(253, 'q')));
	}
	return docs;
}
