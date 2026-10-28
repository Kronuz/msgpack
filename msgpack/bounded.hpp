/*
 * Copyright (c) 2026 Germán Méndez Bravo (Kronuz)
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "pack.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace msgpack::bounded {
enum class Kind { Array, Unsigned, Signed, String, Binary, Boolean, Nil };
// Fixed lexical scratch for schemas whose fields have a small sealed bound.
// Unused bytes are never initialized or copied; only the populated prefix is read.
template <std::size_t Capacity> class InlineBytes {
  public:
	InlineBytes() noexcept {}
	InlineBytes(const InlineBytes &other) noexcept : size_(other.size_) {
		if (size_)
			std::memcpy(data_, other.data_, size_);
	}
	InlineBytes &operator=(const InlineBytes &other) noexcept {
		if (this != &other) {
			size_ = other.size_;
			if (size_)
				std::memcpy(data_, other.data_, size_);
		}
		return *this;
	}
	static constexpr std::size_t capacity() noexcept { return Capacity; }
	void push_back(char byte) {
		if (size_ == Capacity)
			throw std::length_error("MessagePack inline scratch limit exceeded");
		data_[size_++] = byte;
	}
	void append(std::string_view bytes) {
		if (bytes.size() > Capacity - size_)
			throw std::length_error("MessagePack inline scratch limit exceeded");
		if (!bytes.empty())
			std::memcpy(data_ + size_, bytes.data(), bytes.size());
		size_ += bytes.size();
	}
	void clear() noexcept { size_ = 0; }
	std::size_t size() const noexcept { return size_; }
	bool empty() const noexcept { return !size_; }
	const char *data() const noexcept { return data_; }
	const char *begin() const noexcept { return data_; }
	const char *end() const noexcept { return data_ + size_; }
	operator std::string_view() const noexcept { return {data_, size_}; }

  private:
	static_assert(Capacity > 0);
	char data_[Capacity];
	std::size_t size_ = 0;
};
template <class Bytes> struct BasicToken {
	Kind kind = Kind::Nil;
	std::uint64_t number = 0;
	Bytes bytes;
	std::int64_t signed_value() const {
		if (kind == Kind::Signed)
			return std::bit_cast<std::int64_t>(number);
		if (kind != Kind::Unsigned || number > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
			throw std::invalid_argument("MessagePack integer is outside signed range");
		return static_cast<std::int64_t>(number);
	}
};
using Token = BasicToken<std::string>;
struct Limits {
	std::uint64_t input_bytes = 256ull * 1024 * 1024;
	std::uint32_t string_bytes = 16384, binary_bytes = 32, array_elements = 500000;
};

// Incremental lexical decoder. It emits array lengths, not recursive objects;
// the schema owner validates tuple arity, nesting and aggregate record counts.
// Byte budgets limit input consumption; storage controls allocation and copy work.
template <class Bytes> class BasicDecoder {
  public:
	explicit BasicDecoder(Limits limits = {}, Bytes bytes = {})
		: limits_(limits), token_{Kind::Nil, 0, std::move(bytes)} {}
	std::optional<BasicToken<Bytes>> next(std::span<const char> &input, std::size_t &budget) {
		if (failed_)
			throw std::logic_error("MessagePack decoder fenced");
		try {
			while (!input.empty() && budget) {
				if (total_ == limits_.input_bytes)
					throw std::length_error("MessagePack input limit exceeded");
				const auto byte = static_cast<unsigned char>(input.front());
				input = input.subspan(1);
				--budget;
				++total_;
				if (payload_) {
					token_.bytes.push_back(static_cast<char>(byte));
					if (--remaining_ == 0)
						return emit();
				} else if (needed_) {
					value_ = (value_ << 8) | byte;
					if (--needed_ == 0) {
						if (length_) {
							if (begin_payload())
								return emit();
						} else {
							if (token_.kind == Kind::Signed && width_ < 8 &&
								(value_ & (1ull << (width_ * 8 - 1))))
								value_ |= (~std::uint64_t{0}) << (width_ * 8);
							token_.number = value_;
							if (token_.kind == Kind::Array && value_ > limits_.array_elements)
								throw std::length_error("MessagePack array limit exceeded");
							return emit();
						}
					}
				} else {
					token_.kind = Kind::Nil;
					token_.number = 0;
					token_.bytes.clear();
					value_ = 0;
					if (byte <= 0x7f) {
						token_.kind = Kind::Unsigned;
						token_.number = byte;
						return emit();
					}
					if (byte >= 0xe0) {
						token_.kind = Kind::Signed;
						token_.number = std::bit_cast<std::uint64_t>(std::int64_t(byte) - 256);
						return emit();
					}
					if ((byte & 0xf0) == 0x90) {
						token_.kind = Kind::Array;
						token_.number = byte & 15;
						if (token_.number > limits_.array_elements)
							throw std::length_error("MessagePack array limit exceeded");
						return emit();
					}
					if ((byte & 0xe0) == 0xa0) {
						token_.kind = Kind::String;
						value_ = byte & 31;
						if (begin_payload())
							return emit();
						continue;
					}
					switch (byte) {
					case 0xc0:
						token_.kind = Kind::Nil;
						return emit();
					case 0xc2:
					case 0xc3:
						token_.kind = Kind::Boolean;
						token_.number = byte == 0xc3;
						return emit();
					case 0xcc:
					case 0xcd:
					case 0xce:
					case 0xcf:
						token_.kind = Kind::Unsigned;
						width_ = needed_ = 1u << (byte - 0xcc);
						break;
					case 0xd0:
					case 0xd1:
					case 0xd2:
					case 0xd3:
						token_.kind = Kind::Signed;
						width_ = needed_ = 1u << (byte - 0xd0);
						break;
					case 0xd9:
					case 0xda:
					case 0xdb:
						token_.kind = Kind::String;
						needed_ = 1u << (byte - 0xd9);
						length_ = true;
						break;
					case 0xc4:
					case 0xc5:
					case 0xc6:
						token_.kind = Kind::Binary;
						needed_ = 1u << (byte - 0xc4);
						length_ = true;
						break;
					case 0xdc:
					case 0xdd:
						token_.kind = Kind::Array;
						needed_ = width_ = 2u << (byte - 0xdc);
						break;
					default:
						throw std::invalid_argument("unsupported MessagePack type");
					}
				}
			}
			return std::nullopt;
		} catch (...) {
			failed_ = true;
			throw;
		}
	}
	void finish() {
		if (failed_ || payload_ || needed_) {
			failed_ = true;
			throw std::invalid_argument("truncated MessagePack token");
		}
	}
	std::uint64_t consumed() const noexcept { return total_; }

  private:
	bool begin_payload() {
		const auto maximum = token_.kind == Kind::String ? limits_.string_bytes : limits_.binary_bytes;
		if (value_ > maximum || value_ > limits_.input_bytes - total_)
			throw std::length_error("MessagePack payload limit exceeded");
		if constexpr (requires { Bytes::capacity(); }) {
			if (value_ > Bytes::capacity())
				throw std::length_error("MessagePack inline payload limit exceeded");
		}
		remaining_ = value_;
		length_ = false;
		payload_ = remaining_ != 0;
		return !payload_;
	}
	BasicToken<Bytes> emit() {
		payload_ = false;
		length_ = false;
		needed_ = 0;
		value_ = 0;
		if constexpr (requires { token_.bytes.get_allocator(); }) {
			BasicToken<Bytes> result{token_.kind, token_.number, Bytes(token_.bytes.get_allocator())};
			result.bytes.swap(token_.bytes);
			return result;
		} else {
			return std::move(token_);
		}
	}
	Limits limits_;
	BasicToken<Bytes> token_;
	std::uint64_t total_ = 0, value_ = 0, remaining_ = 0;
	unsigned needed_ = 0, width_ = 0;
	bool length_ = false, payload_ = false, failed_ = false;
};

using Decoder = BasicDecoder<std::string>;

// Adapt append-style outputs to the canonical packer's synchronous write seam.
template <class Output> class AppendPacker : public ::msgpack::packer<AppendPacker<Output>> {
  public:
	explicit AppendPacker(Output &output)
		: ::msgpack::packer<AppendPacker<Output>>(*this), output_(output) {}
	using Base = ::msgpack::packer<AppendPacker<Output>>;
	using Base::pack_str;
	using Base::pack_bin;
	AppendPacker &pack_str(std::string_view value) {
		const auto size = checked_size(value);
		Base::pack_str(size);
		if (size) Base::pack_str_body(value.data(), size);
		return *this;
	}
	AppendPacker &pack_bin(std::string_view value) {
		const auto size = checked_size(value);
		Base::pack_bin(size);
		if (size) Base::pack_bin_body(value.data(), size);
		return *this;
	}
	AppendPacker(const AppendPacker &) = delete;
	AppendPacker &operator=(const AppendPacker &) = delete;
	void write(const char *data, std::size_t size) { output_.append(std::string_view(data, size)); }
  private:
	static std::uint32_t checked_size(std::string_view value) {
		if (value.size() > (std::numeric_limits<std::uint32_t>::max)())
			throw std::length_error("MessagePack payload length exceeds uint32");
		return static_cast<std::uint32_t>(value.size());
	}
	Output &output_;
};
} // namespace msgpack::bounded
