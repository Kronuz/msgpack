#include "msgpack.h"

#include <chrono>
#include <cstddef>
#include <iostream>

int main() {
	MsgPack value = {
		{ "name", "benchmark" },
		{ "numbers", { 1, 2, 3, 4, 5, 6, 7, 8 } },
		{ "nested", { { "a", 1 }, { "b", 2 } } },
	};

	constexpr std::size_t iterations = 50000;
	std::size_t bytes = 0;
	auto start = std::chrono::steady_clock::now();
	for (std::size_t i = 0; i < iterations; ++i) {
		std::string wire = value.serialise();
		bytes += wire.size();
		MsgPack copy = MsgPack::unserialise(wire);
		bytes += copy.size();
	}
	auto stop = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
	std::cout << iterations << " pack/unpack round trips in " << elapsed << "us, touched " << bytes << " bytes\n";
}
