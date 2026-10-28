#include <msgpack/bounded.hpp>
#include <iostream>
#include <string>
int main() {
    namespace bounded = msgpack::bounded;
    bounded::InlineBytes<64> wire;
    bounded::AppendPacker pack(wire);
    pack.pack_array(2).pack_uint64(42);
    pack.pack_str(std::string_view("hello"));
    bounded::BasicDecoder<bounded::InlineBytes<32>> decoder;
    auto input = std::span<const char>(wire.data(), wire.size());
    while (!input.empty()) {
        std::size_t budget = 1;
        if (auto token = decoder.next(input, budget))
            std::cout << static_cast<unsigned>(token->kind) << ": " << token->number
                      << ", " << std::string_view(token->bytes) << '\n';
    }
    decoder.finish();
}
