// Differential test: MsgPack against the frozen copy of the earlier implementation
// (test/frozen_msgpack.h, class MsgPackOld).
//
// Both types read the same random documents and run the same random sequence of operations;
// every result and every exception must be equal: unserialize (value and offset), hash(),
// serialize(), to_string() with and without indent, copies, moves, walks, mutations after unserialize
// and lock(). The text of a document that holds NaN or Inf is the one thing that differs by design:
// the frozen implementation stops at the number, the current one writes null (see compare_logs).
// Documents hold every msgpack type in every width, big integers, NaN and -0.0, empty
// containers, ext, bin, and repeated keys at any depth.
//
// The one class where the two types are meant to differ is a document that repeats a string key
// in some map. The frozen implementation merges the repeats wrongly (it copies the wrong
// entry over the ones after it, leaves the tree and the index of the map out of step, and can
// read freed memory), and the current one keeps the last value in the position of the first
// (test/model.h, test/merge_test.cc). For those documents the frozen implementation reads the
// merged document instead (what a correct merge gives, built by model::merged), the current
// one reads the original and is filled before the operations start (what first fills a map
// decides what is merged); everything else, offsets included, is compared as it is.
// A document that repeats a string key and also has a key that is not a string is not
// compared: filling throws at the non-string key after merging what came before it, and the
// frozen merge is not defined there.
//
// The frozen implementation asserts on its merge in a build with assertions, so this
// translation unit turns them off.
#ifndef NDEBUG
#define NDEBUG
#endif

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include "msgpack.h"
#include "frozen_msgpack.h"
#include "model.h"

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			std::exit(1); \
		} \
	} while (0)

namespace {

#include "diff_test_gen.h"
#include "diff_test_run.h"
#include "diff_test_check.h"

}  // namespace



int main(int argc, char** argv) {
	std::size_t iterations = 1500;
	std::uint64_t base_seed = 1;
	bool single = false;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--stress") && i + 1 < argc) {
			iterations = std::strtoull(argv[++i], nullptr, 10);
		} else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) {
			base_seed = std::strtoull(argv[++i], nullptr, 10);
			single = true;
		}
	}

	std::size_t special = 0;
	for (auto& [name, doc] : special_documents()) {
		Rng r{ 12345 + special };
		check_document(1000000 + special, doc, r);
		check_prefixes(1000000 + special, doc);
		// every special document gets every order of the sensitive operations, and a lot of random ones
		for (int k = 0; k < 6; ++k) {
			Rng q{ 777 + special * 100 + k };
			check_document(2000000 + special * 100 + k, doc, q);
		}
		++special;
	}
	std::printf("special documents: %zu, failures so far %zu\n", special, failures);
	CHECK(failures == 0);

	if (single) {
		Rng r{ base_seed };
		Doc doc = random_doc(r);
		check_document(base_seed, doc, r);
		check_prefixes(base_seed, doc);
		CHECK(failures == 0);
		std::printf("seed %" PRIu64 " ok\n", base_seed);
		return 0;
	}

	for (std::size_t i = 0; i < iterations; ++i) {
		std::uint64_t seed = base_seed + i * 7919;
		Rng r{ seed };
		Doc doc = random_doc(r);
		check_document(seed, doc, r);
		if (i % 4 == 0) check_prefixes(seed, doc);
		if (failures > 5) break;
	}
	std::printf("documents %zu (read %zu, rejected %zu; %zu with repeated keys compared against the merged document, %zu with repeated and non-string keys not compared, %zu with NaN or Inf, %zu of their texts go on past the number), operations %zu, failures %zu\n",
		stats.docs, stats.parsed, stats.threw, stats.with_repeats, stats.not_compared, stats.non_finite, g_nonfinite_text, stats.steps, failures);
	CHECK(failures == 0);
	CHECK(g_nonfinite_text > 0);
	CHECK(stats.parsed > stats.docs / 2);
	CHECK(stats.with_repeats > 20);
	CHECK(stats.not_compared < stats.docs / 4);
	std::printf("msgpack differential OK\n");
	return 0;
}
