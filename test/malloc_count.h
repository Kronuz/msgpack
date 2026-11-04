// Counts what msgpack's headers ask ::malloc (and, by what it adds, ::realloc) for (the zone's chunks, the unpacker's buffer), and refuses a
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
#include <fstream>
#include <functional>
#include <iomanip>
#include <list>
#include <locale>
#include <sstream>
#include <ostream>
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
inline std::size_t g_calls = 0;          // calls to malloc since the last reset

inline std::unordered_map<void*, std::size_t> g_sizes;   // live blocks, to count only what a realloc adds

inline void* counting_malloc(std::size_t n) {
	++g_calls;
	g_requested += n;
	g_largest_request = std::max(g_largest_request, n);
	if (n > (std::size_t(256) << 20)) return nullptr;
	void* p = std::malloc(n);
	if (p) g_sizes[p] = n;
	return p;
}
inline void* counting_realloc(void* p, std::size_t n) {
	const auto old = g_sizes.find(p);
	if (n > (old == g_sizes.end() ? 0 : old->second)) g_requested += n - (old == g_sizes.end() ? 0 : old->second);
	g_largest_request = std::max(g_largest_request, n);
	if (n > (std::size_t(256) << 20)) return nullptr;
	void* q = std::realloc(p, n);
	if (q) { g_sizes.erase(p); g_sizes[q] = n; }
	return q;
}
inline void reset_requests() { g_requested = 0; g_largest_request = 0; g_calls = 0; }

#define malloc counting_malloc
#define realloc counting_realloc
