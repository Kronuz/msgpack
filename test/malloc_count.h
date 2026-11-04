// Counts what msgpack's headers ask ::malloc for (the zone's chunks, the unpacker's buffer), and refuses a
// request over 256 MiB so a hostile header costs a counted request, not memory. Include before any msgpack
// header; the standard headers come first so the macro touches msgpack's code only.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

inline std::size_t g_requested = 0;      // bytes asked for since the last reset
inline std::size_t g_largest_request = 0;

inline void* counting_malloc(std::size_t n) {
	g_requested += n;
	g_largest_request = std::max(g_largest_request, n);
	if (n > (std::size_t(256) << 20)) return nullptr;
	return std::malloc(n);
}
inline void reset_requests() { g_requested = 0; g_largest_request = 0; }

#define malloc counting_malloc
