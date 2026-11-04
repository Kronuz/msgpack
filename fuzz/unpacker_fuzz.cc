// libFuzzer target for the unpacker. The first bytes pick the limits and the chunk sizes; the rest is the
// document. The document is read in one piece by unpack() and in chunks by the streaming unpacker: both must
// end the same way (the same first object, a parse error, a limit error, or not enough bytes), and neither may
// ask ::malloc for more than the bytes it was given can hold.
#include "malloc_count.h"
#include "msgpack.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

enum class End { Object, Parse, Limit, Incomplete };

constexpr std::size_t slack = 64 * 1024;
// What a fed byte may justify, worst case: a 24-byte object, times 2 for the zone's power-of-two chunks, times 2
// for the growth steps and the alignment of strings.
constexpr std::size_t per_byte = 4 * sizeof(msgpack::object);

[[noreturn]] void fail(const char* what) {
	std::fprintf(stderr, "unpacker_fuzz: %s\n", what);
	std::abort();
}

End classify() {
	try {
		throw;
	} catch (const msgpack::parse_error&) {
		return End::Parse;
	} catch (const msgpack::unpack_error&) {
		return End::Limit;   // array/map/str/bin/ext/depth overflow
	}
}

// The bytes of an object, so that a NaN equals itself.
std::string repack(const msgpack::object& o) {
	msgpack::sbuffer buffer;
	msgpack::pack(buffer, o);
	return std::string(buffer.data(), buffer.size());
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	if (size < 4) return 0;
	const unsigned mode = data[0];
	const std::size_t chunk_a = 1 + data[1] % 16;
	const std::size_t chunk_b = 1 + data[2] * 4u;
	const char* doc = reinterpret_cast<const char*>(data) + 3;
	const std::size_t len = size - 3;

	std::size_t cap = 0xffffffff;
	if (mode & 1) cap = len;           // the body-size limit msgpack_load uses
	if (mode & 2) cap = data[1];       // a small limit
	const std::size_t depth = (mode & 4) ? 1 + data[2] % 8 : 0xffffffff;
	const msgpack::unpack_limit limit(cap, cap, cap, cap, cap, depth);

	// One piece.
	End whole_end = End::Object;
	msgpack::object_handle whole;
	std::size_t whole_off = 0;
	reset_requests();
	try {
		whole = msgpack::unpack(doc, len, whole_off, nullptr, nullptr, limit);
	} catch (const msgpack::insufficient_bytes&) {
		whole_end = End::Incomplete;
	} catch (const std::bad_alloc&) {
		fail("unpack() asked for more than 256 MiB");
	} catch (...) {
		whole_end = classify();
	}
	if (g_requested > slack + per_byte * len) {
		std::fprintf(stderr, "unpack(): %zu bytes in, %zu requested, largest %zu\n", len, g_requested, g_largest_request);
		fail("unpack() asked for more than its bytes can hold");
	}

	// In chunks.
	End end = End::Incomplete;
	msgpack::object_handle first;
	reset_requests();
	try {
		msgpack::unpacker u(nullptr, nullptr, 64, limit);
		for (std::size_t at = 0, i = 0; at < len; ++i) {
			std::size_t n = std::min(len - at, (i & 1) ? chunk_b : chunk_a);
			u.reserve_buffer(n);
			std::memcpy(u.buffer(), doc + at, n);
			u.buffer_consumed(n);
			at += n;
			if (u.next(first)) {
				end = End::Object;
				break;
			}
		}
	} catch (const std::bad_alloc&) {
		fail("the unpacker asked for more than 256 MiB");
	} catch (...) {
		end = classify();
	}
	if (g_requested > slack + per_byte * len) {
		std::fprintf(stderr, "unpacker: %zu bytes in, %zu requested, largest %zu\n", len, g_requested, g_largest_request);
		fail("the unpacker asked for more than its bytes can hold");
	}

	if (whole_end != end) fail("one piece and chunks end differently");
	if (end == End::Object && repack(whole.get()) != repack(first.get())) fail("one piece and chunks read different objects");
	return 0;
}
