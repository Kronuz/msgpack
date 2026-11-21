// Random documents: the generator, its random source and the hex helpers.
#pragma once

struct Rng {
	std::uint64_t state;

	std::uint64_t next() {
		std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
		return z ^ (z >> 31);
	}

	std::uint64_t below(std::uint64_t n) {
		return n ? next() % n : 0;
	}

	bool chance(unsigned num, unsigned den) {
		return below(den) < num;
	}
};


struct Doc {
	std::string bytes;
	bool non_string_key = false;   // some map has a key that is not a string
	bool non_finite = false;       // some float is NaN or Inf
};


class Gen {
	Rng& r;
	Doc& doc;
	int key_mode;          // 0 unique keys, 1 a small pool (many repeats), 2 both, 3 unique with one repeat
	bool allow_non_string;
	unsigned counter = 0;

	std::string& out() { return doc.bytes; }

	void be(std::uint64_t v, int nbytes) {
		for (int i = nbytes - 1; i >= 0; --i) {
			out().push_back(static_cast<char>((v >> (8 * i)) & 0xff));
		}
	}

	void head(std::uint8_t b) { out().push_back(static_cast<char>(b)); }

	void put_uint(std::uint64_t v) {
		std::vector<int> widths;
		if (v <= 127) widths.push_back(0);
		if (v <= 0xff) widths.push_back(1);
		if (v <= 0xffff) widths.push_back(2);
		if (v <= 0xffffffffULL) widths.push_back(4);
		widths.push_back(8);
		int w = widths[r.chance(1, 2) ? 0 : r.below(widths.size())];
		switch (w) {
			case 0: head(static_cast<std::uint8_t>(v)); break;
			case 1: head(0xcc); be(v, 1); break;
			case 2: head(0xcd); be(v, 2); break;
			case 4: head(0xce); be(v, 4); break;
			default: head(0xcf); be(v, 8); break;
		}
	}

	void put_int(std::int64_t v) {
		// signed widths; a non-negative value in a signed width is legal too
		std::vector<int> widths;
		if (v >= -32 && v <= 127) widths.push_back(0);
		if (v >= -128 && v <= 127) widths.push_back(1);
		if (v >= -32768 && v <= 32767) widths.push_back(2);
		if (v >= INT32_MIN && v <= INT32_MAX) widths.push_back(4);
		widths.push_back(8);
		int w = widths[r.chance(1, 2) ? 0 : r.below(widths.size())];
		switch (w) {
			case 0: head(static_cast<std::uint8_t>(v)); break;
			case 1: head(0xd0); be(static_cast<std::uint64_t>(v), 1); break;
			case 2: head(0xd1); be(static_cast<std::uint64_t>(v), 2); break;
			case 4: head(0xd2); be(static_cast<std::uint64_t>(v), 4); break;
			default: head(0xd3); be(static_cast<std::uint64_t>(v), 8); break;
		}
	}

	std::uint64_t random_u64() {
		static const std::uint64_t edge[] = {
			0, 1, 31, 32, 126, 127, 128, 129, 254, 255, 256, 257, 65534, 65535, 65536, 65537,
			0xfffffffeULL, 0xffffffffULL, 0x100000000ULL, 0x7fffffffffffffffULL,
			0x8000000000000000ULL, 0xffffffffffffffffULL,
		};
		if (r.chance(1, 2)) return edge[r.below(sizeof(edge) / sizeof(edge[0]))];
		int bits = 1 + static_cast<int>(r.below(64));
		return r.next() >> (64 - bits);
	}

	std::int64_t random_negative() {
		static const std::int64_t edge[] = {
			-1, -2, -31, -32, -33, -34, -127, -128, -129, -32767, -32768, -32769,
			INT32_MIN + 1LL, INT32_MIN, INT32_MIN - 1LL, INT64_MIN + 1, INT64_MIN,
		};
		if (r.chance(1, 2)) return edge[r.below(sizeof(edge) / sizeof(edge[0]))];
		int bits = 1 + static_cast<int>(r.below(63));
		return -1 - static_cast<std::int64_t>(r.next() >> (64 - bits));
	}

	static double as_double(std::uint64_t bits) {
		double d;
		std::memcpy(&d, &bits, sizeof(d));
		return d;
	}

	void put_double() {
		static const std::uint64_t finite[] = {
			0x0000000000000000ULL,  // 0.0
			0x8000000000000000ULL,  // -0.0
			0x3ff0000000000000ULL,  // 1.0
			0xbff8000000000000ULL,  // -1.5
			0x3fb999999999999aULL,  // 0.1
			0x7e37e43c8800759cULL,  // 1e300
			0x7fefffffffffffffULL,  // DBL_MAX
			0x0010000000000000ULL,  // DBL_MIN
			0x0000000000000001ULL,  // 5e-324
			0x419d6f34540ca000ULL,  // 123456789.125
		};
		static const std::uint64_t non_finite[] = {
			0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff8000000000000ULL,
			0xfff8000000000000ULL, 0x7ff0000000000001ULL,
		};
		std::uint64_t bits;
		if (r.chance(1, 40)) {
			bits = non_finite[r.below(sizeof(non_finite) / sizeof(non_finite[0]))];
			doc.non_finite = true;
		} else if (r.chance(1, 2)) {
			bits = finite[r.below(sizeof(finite) / sizeof(finite[0]))];
		} else {
			do {
				bits = r.next();
			} while (!std::isfinite(as_double(bits)));
		}
		head(0xcb);
		be(bits, 8);
	}

	void put_float() {
		static const std::uint32_t finite[] = {
			0x00000000u, 0x80000000u, 0x3fc00000u, 0x7f7fffffu, 0x00000001u, 0xc2f70000u,
		};
		std::uint32_t bits;
		if (r.chance(1, 40)) {
			static const std::uint32_t non_finite[] = { 0x7f800000u, 0xff800000u, 0x7fc00000u };
			bits = non_finite[r.below(3)];
			doc.non_finite = true;
		} else if (r.chance(1, 2)) {
			bits = finite[r.below(sizeof(finite) / sizeof(finite[0]))];
		} else {
			do {
				bits = static_cast<std::uint32_t>(r.next());
			} while (((bits >> 23) & 0xff) == 0xff);
		}
		head(0xca);
		be(bits, 4);
	}

	std::string random_bytes(std::size_t n) {
		std::string s;
		static const char* utf8[] = { "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\xce\xbb" };
		int flavor = static_cast<int>(r.below(10));
		while (s.size() < n) {
			if (flavor < 7) {
				s.push_back(static_cast<char>('a' + r.below(26)));
			} else if (flavor == 7) {
				static const char odd[] = { 0, 1, 31, '"', '\\', '/', '\n', '\t', 127, ' ', 'z' };
				s.push_back(odd[r.below(sizeof(odd))]);
			} else if (flavor == 8) {
				s += utf8[r.below(4)];
			} else {
				s.push_back(static_cast<char>(r.below(256)));
			}
		}
		s.resize(n);
		return s;
	}

	void put_str_bytes(const std::string& s) {
		std::size_t n = s.size();
		std::vector<int> widths;
		if (n <= 31) widths.push_back(0);
		if (n <= 0xff) widths.push_back(1);
		if (n <= 0xffff) widths.push_back(2);
		widths.push_back(4);
		int w = widths[r.chance(2, 3) ? 0 : r.below(widths.size())];
		switch (w) {
			case 0: head(static_cast<std::uint8_t>(0xa0 | n)); break;
			case 1: head(0xd9); be(n, 1); break;
			case 2: head(0xda); be(n, 2); break;
			default: head(0xdb); be(n, 4); break;
		}
		out() += s;
	}

	std::size_t random_length(std::size_t big) {
		unsigned x = static_cast<unsigned>(r.below(1000));
		if (x < 600) return r.below(5);
		if (x < 850) return 5 + r.below(12);
		if (x < 970) return 17 + r.below(24);
		if (x < 995) return 41 + r.below(big);
		return 41 + r.below(big * 3);
	}

	void put_container_head(bool is_map, std::size_t n) {
		if (is_map) {
			if (n <= 15 && r.chance(3, 4)) {
				head(static_cast<std::uint8_t>(0x80 | n));
			} else if (n <= 0xffff && r.chance(1, 2)) {
				head(0xde); be(n, 2);
			} else {
				head(0xdf); be(n, 4);
			}
		} else {
			if (n <= 15 && r.chance(3, 4)) {
				head(static_cast<std::uint8_t>(0x90 | n));
			} else if (n <= 0xffff && r.chance(1, 2)) {
				head(0xdc); be(n, 2);
			} else {
				head(0xdd); be(n, 4);
			}
		}
	}

	std::string key_string() {
		static const std::string pool[] = { "", "a", "b", "ab", std::string("a\0", 2), "key with space", "a\\b" };
		switch (key_mode) {
			case 1:
				return pool[r.below(sizeof(pool) / sizeof(pool[0]))];
			case 2:
				if (r.chance(1, 3)) return pool[r.below(sizeof(pool) / sizeof(pool[0]))];
				[[fallthrough]];
			default:
				return "k" + std::to_string(counter++) + random_bytes(r.below(4));
		}
	}

public:
	Gen(Rng& rng, Doc& d) : r(rng), doc(d), key_mode(static_cast<int>(rng.below(4))), allow_non_string(rng.chance(1, 20)) { }

	void map(int depth, std::size_t n) {
		std::vector<std::string> keys;
		for (std::size_t i = 0; i < n; ++i) {
			keys.push_back(key_string());
		}
		if (key_mode == 3 && n >= 2 && r.chance(1, 2)) {
			keys[r.below(n)] = keys[r.below(n)];   // one repeat among many
		}
		put_container_head(true, n);
		for (std::size_t i = 0; i < n; ++i) {
			if (allow_non_string && r.chance(1, 6)) {
				doc.non_string_key = true;
				switch (r.below(4)) {
					case 0: head(0xc0); break;
					case 1: put_uint(r.below(300)); break;
					case 2: head(0xc4); head(2); out() += "xy"; break;
					default: head(0xc3); break;
				}
			} else {
				put_str_bytes(keys[i]);
			}
			value(depth + 1);
		}
	}

	void array(int depth, std::size_t n) {
		put_container_head(false, n);
		for (std::size_t i = 0; i < n; ++i) {
			value(depth + 1);
		}
	}

	void value(int depth) {
		unsigned x = static_cast<unsigned>(r.below(100));
		bool container = depth < 5 && x < (depth == 0 ? 90u : 28u);
		if (container) {
			std::size_t n = random_length(depth == 0 ? 40 : 20);
			if (r.chance(1, 2)) map(depth, n); else array(depth, n);
			return;
		}
		switch (r.below(14)) {
			case 0: head(0xc0); break;
			case 1: head(0xc2); break;
			case 2: head(0xc3); break;
			case 3: case 4: put_uint(random_u64()); break;
			case 5: case 6: put_int(random_negative()); break;
			case 7: put_double(); break;
			case 8: put_float(); break;
			case 9: case 10: put_str_bytes(random_bytes(random_length(300))); break;
			case 11: {
				std::string b = random_bytes(random_length(300));
				std::size_t n = b.size();
				if (n <= 0xff && r.chance(2, 3)) { head(0xc4); be(n, 1); }
				else if (n <= 0xffff && r.chance(1, 2)) { head(0xc5); be(n, 2); }
				else { head(0xc6); be(n, 4); }
				out() += b;
				break;
			}
			case 12: {
				static const std::size_t fixed[] = { 1, 2, 4, 8, 16 };
				std::int8_t type = static_cast<std::int8_t>(r.chance(1, 4) ? r.below(256) : r.below(4));
				if (r.chance(1, 2)) {
					std::size_t n = fixed[r.below(5)];
					static const std::uint8_t fix[] = { 0xd4, 0xd5, 0xd6, 0xd7, 0xd8 };
					head(fix[n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : n == 8 ? 3 : 4]);
					head(static_cast<std::uint8_t>(type));
					out() += random_bytes(n);
				} else {
					std::size_t n = random_length(40);
					if (n <= 0xff) { head(0xc7); be(n, 1); }
					else if (n <= 0xffff) { head(0xc8); be(n, 2); }
					else { head(0xc9); be(n, 4); }
					head(static_cast<std::uint8_t>(type));
					out() += random_bytes(n);
				}
				break;
			}
			default: {
				std::size_t n = random_length(10);
				if (r.chance(1, 2)) map(depth, n); else array(depth, n);
				break;
			}
		}
	}
};


Doc random_doc(Rng& r) {
	Doc doc;
	Gen g(r, doc);
	g.value(0);
	return doc;
}


std::string current_exception_text() {
	try {
		throw;
	} catch (const BaseException& e) {
		return std::string(typeid(e).name()) + ": " + e.get_message();
	} catch (const std::exception& e) {
		return std::string(typeid(e).name()) + ": " + e.what();
	} catch (...) {
		return "unknown exception";
	}
}


std::string hex(const std::string& s) {
	static const char* digits = "0123456789abcdef";
	std::string r;
	for (unsigned char c : s) {
		r.push_back(digits[c >> 4]);
		r.push_back(digits[c & 15]);
	}
	return r;
}
