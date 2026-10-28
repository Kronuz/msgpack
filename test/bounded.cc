#include <msgpack/bounded.hpp>
#include <iostream>
#include <vector>
namespace m = msgpack::bounded;
using FixedDecoder = m::BasicDecoder<m::InlineBytes<200>>;
using FixedToken = m::BasicToken<m::InlineBytes<200>>;

void require(bool condition) {
	if (!condition)
		throw std::runtime_error("MessagePack invariant failed");
}
#include "bounded_storage.hh"
std::vector<m::Token> decode(std::string_view bytes, std::size_t fragment = 1, m::Limits limits = {}) {
	m::Decoder decoder(limits);
	std::vector<m::Token> result;
	for (std::size_t offset = 0; offset < bytes.size();) {
		auto input = std::span(bytes.data() + offset, std::min(fragment, bytes.size() - offset));
		offset += input.size();
		while (!input.empty()) {
			std::size_t budget = 1;
			if (auto token = decoder.next(input, budget))
				result.push_back(std::move(*token));
			require(budget == 0);
		}
	}
	decoder.finish();
	require(decoder.consumed() == bytes.size());
	return result;
}
void emit_vector(std::string_view bytes) {
	constexpr char hex[] = "0123456789abcdef";
	std::cout << "hex ";
	for (unsigned char byte : bytes)
		std::cout << hex[byte >> 4] << hex[byte & 15];
	std::cout << '\n';
}
int main(int argc, char **argv) {
	try {
		storage_tests();
		const bool vectors = argc == 2 && std::string_view(argv[1]) == "--vectors";
		for (std::uint64_t value : {0ull, 127ull, 128ull, 255ull, 256ull, 65535ull, 65536ull, 0xffffffffull,
									0x100000000ull, ~0ull}) {
			std::string bytes;
			m::AppendPacker(bytes).pack_uint64(value);
			if (vectors)
				emit_vector(bytes);
			auto tokens = decode(bytes);
			require(tokens.size() == 1 && tokens[0].kind == m::Kind::Unsigned && tokens[0].number == value);
		}
		for (std::int64_t value :
			 {std::int64_t(-1), std::int64_t(-32), std::int64_t(-33), std::int64_t(-128), std::int64_t(-129),
			  std::int64_t(-32768), std::int64_t(-32769), std::int64_t(-2147483648ll),
			  std::int64_t(-2147483649ll), std::numeric_limits<std::int64_t>::min()}) {
			std::string bytes;
			m::AppendPacker(bytes).pack_int64(value);
			if (vectors)
				emit_vector(bytes);
			auto tokens = decode(bytes);
			require(tokens.size() == 1 && tokens[0].signed_value() == value);
		}
		for (std::size_t size : {0u, 1u, 31u, 32u, 255u, 256u, 16384u}) {
			std::string value(size, 'x'), bytes;
			m::AppendPacker(bytes).pack_str(value);
			if (vectors)
				emit_vector(bytes);
			for (auto fragment : {1u, 7u, 65536u}) {
				auto tokens = decode(bytes, fragment);
				require(tokens.size() == 1 && tokens[0].kind == m::Kind::String && tokens[0].bytes == value);
			}
		}
		for (std::uint32_t count : {0u, 15u, 16u, 65535u, 65536u, 500000u}) {
			std::string bytes;
			m::AppendPacker(bytes).pack_array(count);
			if (vectors)
				emit_vector(bytes);
			auto tokens = decode(bytes);
			require(tokens.size() == 1 && tokens[0].kind == m::Kind::Array && tokens[0].number == count);
		}
		for (std::size_t size : {0u, 1u, 32u, 199u, 200u, 201u}) {
			std::string value(size, 'x'), wire;
			if (size > 1)
				value[size / 2] = '\0';
			m::AppendPacker(wire).pack_str(value);
			FixedDecoder decoder;
			auto input = std::span<const char>(wire);
			std::optional<FixedToken> token;
			bool pressure = false;
			try {
				while (!input.empty()) {
					std::size_t budget = 1;
					if (auto next = decoder.next(input, budget))
						token = std::move(next);
				}
				decoder.finish();
			} catch (const std::length_error &) {
				pressure = true;
			}
			require(pressure == (size == 201));
			if (!pressure) {
				require(token && std::string_view(token->bytes) == value);
				auto copy = *token;
				token.reset();
				require(std::string_view(copy.bytes) == value);
			} else {
				bool fenced = false;
				try {
					decoder.finish();
				} catch (const std::invalid_argument &) {
					fenced = true;
				}
				require(fenced);
			}
		}
		std::string binary;
		m::AppendPacker(binary).pack_bin(std::string(16, '\0'));
		if (vectors)
			emit_vector(binary);
		require(decode(binary)[0].kind == m::Kind::Binary);
		for (auto bytes :
			 {std::string("\xc1", 1), std::string("\xca", 1), std::string("\xcb", 1), std::string("\x80", 1),
			  std::string("\xd4", 1), std::string("\xdb\xff\xff\xff\xff", 5), std::string("\xdc\x00", 2),
			  std::string("\xa1", 1)}) {
			bool rejected = false;
			try {
				decode(bytes);
			} catch (const std::exception &) {
				rejected = true;
			}
			require(rejected);
		}
		m::Decoder fenced;
		const char unsupported = '\xc1';
		auto input = std::span(&unsupported, 1);
		std::size_t budget = 1;
		try {
			fenced.next(input, budget);
		} catch (const std::invalid_argument &) {
		}
		bool rejected = false;
		try {
			fenced.finish();
		} catch (const std::invalid_argument &) {
			rejected = true;
		}
		require(rejected);
		m::Limits tiny;
		tiny.input_bytes = 2;
		std::string three;
		m::AppendPacker(three).pack_str("xx");
		rejected = false;
		try {
			decode(three, 1, tiny);
		} catch (const std::length_error &) {
			rejected = true;
		}
		require(rejected);
		std::cout << "incremental MessagePack primitives passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
