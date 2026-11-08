#pragma once
#include <msgpack/bounded.hpp>
#include <cstdlib>
#include <memory>
#include <memory_resource>
#include <type_traits>

class TestResource final : public std::pmr::memory_resource {
  public:
	std::size_t calls = 0, fail_at = 0, live = 0;
  private:
	void *do_allocate(std::size_t size, std::size_t alignment) override {
		if (++calls == fail_at) throw std::bad_alloc();
		auto *result = std::pmr::new_delete_resource()->allocate(size, alignment);
		live += size;
		return result;
	}
	void do_deallocate(void *pointer, std::size_t size, std::size_t alignment) override {
		live -= size;
		std::pmr::new_delete_resource()->deallocate(pointer, size, alignment);
	}
	bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};
template <class T> struct TestAllocator {
	using value_type = T;
	using propagate_on_container_move_assignment = std::true_type;
	using propagate_on_container_swap = std::true_type;
	std::shared_ptr<TestResource> owner;
	TestAllocator() = default;
	explicit TestAllocator(std::shared_ptr<TestResource> value) : owner(std::move(value)) {}
	template <class U> TestAllocator(const TestAllocator<U> &other) : owner(other.owner) {}
	T *allocate(std::size_t count) {
		if (!owner) throw std::logic_error("decoder lost allocator ownership");
		if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(T)) throw std::bad_array_new_length();
		return static_cast<T *>(owner->allocate(count * sizeof(T), alignof(T)));
	}
	void deallocate(T *pointer, std::size_t count) noexcept {
		if (!owner) std::abort();
		owner->deallocate(pointer, count * sizeof(T), alignof(T));
	}
	template <class U> bool operator==(const TestAllocator<U> &other) const noexcept {
		return owner == other.owner;
	}
};
inline void storage_tests() {
	namespace bounded = msgpack::bounded;
	static_assert(!std::is_copy_constructible_v<bounded::AppendPacker<std::string>>);
	static_assert(!std::is_move_constructible_v<bounded::AppendPacker<std::string>>);
	std::string wire;
	bounded::AppendPacker pack(wire);
	pack.pack_str(std::string(512, 'a')).pack_str(std::string(512, 'b')).pack_uint64(0);
	TestResource resource;
	{
		bounded::BasicDecoder<std::pmr::string> decoder({}, std::pmr::string(&resource));
		auto input = std::span<const char>(wire.data(), wire.size());
		std::size_t allowance = 0;
		require(!decoder.next(input, allowance) && decoder.consumed() == 0 && input.size() == wire.size());
		unsigned tokens = 0;
		while (!input.empty()) {
			allowance = 7;
			if (auto token = decoder.next(input, allowance)) {
				require(token->bytes.get_allocator().resource() == &resource);
				if (tokens < 2) {
					require(token->kind == bounded::Kind::String && token->bytes.size() == 512);
					require(token->bytes.front() == (tokens ? 'b' : 'a'));
					const auto consumed = wire.size() - input.size();
					std::fill(wire.begin(), wire.begin() + consumed, 'x');
					require(token->bytes.back() == (tokens ? 'b' : 'a'));
				} else require(token->kind == bounded::Kind::Unsigned && token->number == 0 && token->bytes.empty());
				++tokens;
			}
		}
		decoder.finish();
		require(tokens == 3 && resource.calls > 0);
	}
	require(resource.live == 0);
	{
		using OwnedString = std::basic_string<char, std::char_traits<char>, TestAllocator<char>>;
		std::optional<bounded::BasicToken<OwnedString>> escaped;
		std::weak_ptr<TestResource> weak;
		{
			auto owner = std::make_shared<TestResource>();
			weak = owner;
			bounded::BasicDecoder<OwnedString> decoder({}, OwnedString{TestAllocator<char>(owner)});
			owner.reset();
			std::string encoded;
			bounded::AppendPacker(encoded).pack_str(std::string(512, 'a')).pack_str(std::string(512, 'b'));
			auto input = std::span<const char>(encoded.data(), encoded.size());
			unsigned tokens = 0;
			while (!input.empty()) {
				std::size_t allowance = 7;
				if (auto token = decoder.next(input, allowance)) {
					require(token->bytes.get_allocator().owner && token->bytes.size() == 512);
					require(token->bytes.front() == (tokens ? 'b' : 'a'));
					escaped.emplace(std::move(*token));
					++tokens;
				}
			}
			decoder.finish();
			require(tokens == 2);
		}
		require(!weak.expired());
		escaped.reset();
		require(weak.expired());
	}
	std::string input_wire;
	bounded::AppendPacker(input_wire).pack_str(std::string(512, 'z'));
	bool reached_success = false;
	for (std::size_t fail_at = 1; fail_at != 16; ++fail_at) {
		TestResource failing;
		failing.fail_at = fail_at;
		bool refused = false;
		{
			bounded::BasicDecoder<std::pmr::string> decoder({}, std::pmr::string(&failing));
			auto input = std::span<const char>(input_wire.data(), input_wire.size());
			try {
				while (!input.empty()) {
					std::size_t allowance = 7;
					if (auto token = decoder.next(input, allowance)) require(token->bytes.size() == 512);
				}
				decoder.finish();
			} catch (const std::bad_alloc &) { refused = true; }
			if (refused) {
				bool fenced = false;
				std::span<const char> empty;
				std::size_t allowance = 0;
				try { (void)decoder.next(empty, allowance); }
				catch (const std::logic_error &) { fenced = true; }
				require(fenced);
			} else reached_success = true;
		}
		require(failing.live == 0);
		if (reached_success) break;
	}
	require(reached_success);
	{
		bounded::Decoder empty;
		empty.finish();
		const std::string array_header(1, '\x91');
		auto input = std::span<const char>(array_header.data(), array_header.size());
		std::size_t allowance = 1;
		const auto token = empty.next(input, allowance);
		require(token && token->kind == bounded::Kind::Array && token->number == 1);
		empty.finish();
	}
	{
		std::string encoded;
		bounded::AppendPacker(encoded).pack_str(std::string_view{}).pack_bin(std::string_view{});
		require(encoded == std::string("\xa0\xc4\x00", 3));
		bounded::InlineBytes<3> scratch;
		bool refused = false;
		try { bounded::AppendPacker(scratch).pack_str(std::string_view("long")); }
		catch (const std::length_error &) { refused = true; }
		require(refused && scratch.size() == 1);
	}
}
