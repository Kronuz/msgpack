// Shared pieces of the benchmark programs: the timer, the output row and the checksum helpers. No library
// is included here, so a program that links one msgpack-c can include it as well as one that links another.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace bench {

// Makes the compiler compute `v` and keep it: nothing the loop does is optimized away.
template <class T>
inline void keep(const T& v) {
	asm volatile("" : : "r,m"(v) : "memory");
}

inline std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
	h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
	return h;
}

inline std::uint64_t mix_str(std::uint64_t h, const char* p, std::size_t n) {
	std::uint64_t f = 1469598103934665603ull;
	for (std::size_t i = 0; i < n; ++i) {
		f ^= static_cast<unsigned char>(p[i]);
		f *= 1099511628211ull;
	}
	return mix(mix(h, n), f);
}

inline std::uint64_t mix_dbl(std::uint64_t h, double d) {
	std::uint64_t b;
	std::memcpy(&b, &d, sizeof b);
	return mix(h, b);
}

struct Result {
	double median_ns;
	double min_ns;
	double spread;   // (max - min) / median over the repetitions
};

// Time per call of `f`: the number of calls per repetition is chosen so that a repetition lasts about
// `target_ms`, and the median of `reps` repetitions is reported. BENCH_QUICK=1 shortens both, for a
// run that only checks that the program works.
template <class F>
Result time_it(F&& f, double target_ms = 25.0, int reps = 9) {
	using clock = std::chrono::steady_clock;
	if (std::getenv("BENCH_QUICK")) {
		target_ms = 1.0;
		reps = 3;
	}
	std::size_t iterations = 1;
	for (;;) {
		const auto t0 = clock::now();
		for (std::size_t i = 0; i < iterations; ++i) f();
		const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
		if (ms >= target_ms / 4 || iterations > (1u << 28)) {
			iterations = std::max<std::size_t>(1, static_cast<std::size_t>(target_ms / (ms / iterations)));
			break;
		}
		iterations *= 4;
	}
	std::vector<double> samples;
	for (int r = 0; r < reps; ++r) {
		const auto t0 = clock::now();
		for (std::size_t i = 0; i < iterations; ++i) f();
		samples.push_back(std::chrono::duration<double, std::nano>(clock::now() - t0).count() / iterations);
	}
	std::sort(samples.begin(), samples.end());
	Result r;
	r.median_ns = samples[samples.size() / 2];
	r.min_ns = samples.front();
	r.spread = (samples.back() - samples.front()) / r.median_ns;
	return r;
}

// One line per measurement: library, document, operation, time, spread, and a checksum of what the
// operation produced, which benchmarks/compare.py compares across libraries.
inline void row(const char* lib, const char* doc, const char* op, const Result& r, std::uint64_t check, std::size_t bytes = 0) {
	std::printf("%-14s %-6s %-18s %12.1f ns  min %12.1f  spread %5.1f%%  chk %016llx", lib, doc, op, r.median_ns, r.min_ns, r.spread * 100,
		static_cast<unsigned long long>(check));
	if (bytes) {
		std::printf("  %7.1f MB/s", bytes / r.median_ns * 1000.0);
	}
	std::printf("\n");
	std::fflush(stdout);
}

// What a program is asked to run: argv[1] a document name (all when empty), argv[2] a substring of the
// operation names (all when empty).
struct Filter {
	std::string doc, op;
	Filter(int argc, char** argv) : doc(argc > 1 ? argv[1] : ""), op(argc > 2 ? argv[2] : "") {}
	bool wants_doc(const std::string& name) const { return doc.empty() || doc == name; }
	bool wants_op(const char* name) const { return op.empty() || std::string(name).find(op) != std::string::npos; }
};

}  // namespace bench
