#include <msgpack/bounded.hpp>
#include "msgpack.h"
#include "msgpack_patcher.h"
#include <stdexcept>

int main() {
    MsgPack dom = {{"count", 42}};
    if (dom["count"].as_u64() != 42) throw std::runtime_error("DOM compatibility");
    msgpack::bounded::InlineBytes<16> wire;
    msgpack::bounded::AppendPacker(wire).pack_uint64(42);
    msgpack::bounded::BasicDecoder<msgpack::bounded::InlineBytes<16>> lexer;
    auto input = std::span<const char>(wire.data(), wire.size());
    std::size_t budget = wire.size();
    auto token = lexer.next(input, budget);
    if (!token || token->number != 42) throw std::runtime_error("bounded compatibility");
    lexer.finish();
}
