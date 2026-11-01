cmake_minimum_required(VERSION 3.16)
project(msgpack LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# --- Dependencies (the Kronuz family, tracked at tip) ---
FetchContent_Declare(atomic_shared_ptr GIT_REPOSITORY https://github.com/Kronuz/atomic-shared-ptr.git GIT_TAG b7bf5efcec1dd437caf0692263cedfe368adceb2)
FetchContent_Declare(enum_reflection  GIT_REPOSITORY https://github.com/Kronuz/enum-reflection.git     GIT_TAG 7d394c5ca3d275ca36f6de82e220d1bab9444253)
FetchContent_Declare(strict_stox      GIT_REPOSITORY https://github.com/Kronuz/strict-stox.git         GIT_TAG 5d979797b9799afcdacc0c004c229e2d61af1341)
FetchContent_Declare(located_exception GIT_REPOSITORY https://github.com/Kronuz/located-exception.git  GIT_TAG 1d20a4468c2574a8b1d03dfc93622c833e5ce3dd)
FetchContent_Declare(constexpr_phf    GIT_REPOSITORY https://github.com/Kronuz/constexpr-phf.git       GIT_TAG 1a035230c03982a822637f7c741656bd688bd9e3)
FetchContent_Declare(hashes           GIT_REPOSITORY https://github.com/Kronuz/hashes.git              GIT_TAG 7ab0c23325debfbaf4104044987ba6583d14808f)
FetchContent_Declare(repr             GIT_REPOSITORY https://github.com/Kronuz/repr.git                GIT_TAG b47e947fde294d9e1847560ef9df23f95947ecf6)
FetchContent_MakeAvailable(
	atomic_shared_ptr
	enum_reflection
	strict_stox
	located_exception
	constexpr_phf
	hashes
	repr
)

# RapidJSON is header-only for this library. Populate sources instead of running
# RapidJSON's own CMake project: SOURCE_SUBDIR points at a non-existent subdir so
# FetchContent sets rapidjson_SOURCE_DIR but skips add_subdirectory. This avoids
# RapidJSON's ancient cmake_minimum_required (CMake 4.x rejects it outright, and it
# would also build a bundled gtest), so no -DCMAKE_POLICY_VERSION_MINIMUM is needed.
FetchContent_Declare(rapidjson GIT_REPOSITORY https://github.com/Tencent/rapidjson.git GIT_TAG 24b5e7a8b27f42fa16b96fc70aade9106cf7102f SOURCE_SUBDIR do-not-configure)
FetchContent_MakeAvailable(rapidjson)
add_library(rapidjson_headers INTERFACE)
target_include_directories(rapidjson_headers INTERFACE "${rapidjson_SOURCE_DIR}/include")

# --- Library target ---
add_library(msgpack STATIC msgpack_patcher.cc)
target_include_directories(msgpack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(msgpack PUBLIC cxx_std_20)
target_compile_definitions(msgpack PUBLIC "RAPIDJSON_PARSE_DEFAULT_FLAGS=(kParseCommentsFlag|kParseTrailingCommasFlag|kParseNanAndInfFlag)")
target_link_libraries(msgpack
	PUBLIC
		atomic-shared-ptr::atomic-shared-ptr
		enum_reflection::enum_reflection
		strict_stox::strict_stox
		located_exception
		constexpr_phf
		hashes::hashes
		repr::repr
		rapidjson_headers
)
add_library(msgpack::msgpack ALIAS msgpack)

# Build tests, example, and benchmark only when this repo is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	enable_testing()

	add_executable(msgpack_test test/test.cc)
	target_link_libraries(msgpack_test PRIVATE msgpack)
	add_test(NAME msgpack COMMAND msgpack_test)

	# Every result of unserialise, hash(), to_string() and the operations around them against the
	# frozen implementation in test/frozen_msgpack.h (class MsgPackOld), on random documents.
	add_executable(msgpack_diff_test test/diff_test.cc)
	target_link_libraries(msgpack_diff_test PRIVATE msgpack)
	target_include_directories(msgpack_diff_test PRIVATE test)
	add_test(NAME msgpack_diff COMMAND msgpack_diff_test)

	# A map holds each key once: the last value wins, in the position of the first. Against a
	# reference model (test/model.h), on hand-written, exhaustive and random documents.
	add_executable(msgpack_merge_test test/merge_test.cc)
	target_link_libraries(msgpack_merge_test PRIVATE msgpack)
	target_include_directories(msgpack_merge_test PRIVATE test)
	add_test(NAME msgpack_merge COMMAND msgpack_merge_test)

	# Growing an array of maps leaves the maps as they were; under AddressSanitizer it also
	# reads no freed memory.
	add_executable(msgpack_growth_test test/growth_test.cc)
	target_link_libraries(msgpack_growth_test PRIVATE msgpack)
	target_include_directories(msgpack_growth_test PRIVATE test)
	add_test(NAME msgpack_growth COMMAND msgpack_growth_test)

	# A fill that throws (a map with a key that is not a string) leaves the map as it was, and the
	# next fill throws the same way.
	add_executable(msgpack_failed_fill_test test/failed_fill_test.cc)
	target_link_libraries(msgpack_failed_fill_test PRIVATE msgpack)
	target_include_directories(msgpack_failed_fill_test PRIVATE test)
	add_test(NAME msgpack_failed_fill COMMAND msgpack_failed_fill_test)

	# replace() over a value that was already read, and a lock() that throws.
	add_executable(msgpack_replace_lock_test test/replace_lock_test.cc)
	target_link_libraries(msgpack_replace_lock_test PRIVATE msgpack)
	target_include_directories(msgpack_replace_lock_test PRIVATE test)
	add_test(NAME msgpack_replace_lock COMMAND msgpack_replace_lock_test)

	# to_string() and as_document() of a map whose key is not a string.
	add_executable(msgpack_json_keys_test test/json_keys_test.cc)
	target_link_libraries(msgpack_json_keys_test PRIVATE msgpack)
	target_include_directories(msgpack_json_keys_test PRIVATE test)
	add_test(NAME msgpack_json_keys COMMAND msgpack_json_keys_test)

	# update(): the merge against a model, with maps that throw when they are read.
	add_executable(msgpack_update_test test/update_test.cc)
	target_link_libraries(msgpack_update_test PRIVATE msgpack)
	target_include_directories(msgpack_update_test PRIVATE test)
	add_test(NAME msgpack_update COMMAND msgpack_update_test)

	# A bounded bug hunt: random sequences of every mutating operation against the model, with bad maps
	# (a fill that throws) and, in the second program, allocations that fail (a replaced operator new).
	add_executable(msgpack_hunt_test test/hunt_test.cc)
	target_link_libraries(msgpack_hunt_test PRIVATE msgpack)
	target_include_directories(msgpack_hunt_test PRIVATE test)
	add_test(NAME msgpack_hunt COMMAND msgpack_hunt_test)

	# The value outlives the input buffer, the other values and the threads it came from.
	find_package(Threads REQUIRED)
	add_executable(msgpack_lifetime_test test/lifetime_test.cc)
	target_link_libraries(msgpack_lifetime_test PRIVATE msgpack Threads::Threads)
	add_test(NAME msgpack_lifetime COMMAND msgpack_lifetime_test)

	# Allocation counts against the frozen implementation, and the hunt with allocations that fail. They
	# replace global operator new, which the address, thread and memory sanitizer runtimes also define,
	# so they are not built under those (UBSan alone is fine).
	if(NOT "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}} ${CMAKE_EXE_LINKER_FLAGS}" MATCHES "sanitize=[a-z,]*(address|thread|memory)")
		add_executable(msgpack_alloc_test test/alloc_test.cc)
		target_link_libraries(msgpack_alloc_test PRIVATE msgpack)
		target_include_directories(msgpack_alloc_test PRIVATE test)
		add_test(NAME msgpack_alloc COMMAND msgpack_alloc_test)

		add_executable(msgpack_hunt_alloc_test test/hunt_test.cc)
		target_compile_definitions(msgpack_hunt_alloc_test PRIVATE HUNT_INJECT_ALLOC)
		target_link_libraries(msgpack_hunt_alloc_test PRIVATE msgpack)
		target_include_directories(msgpack_hunt_alloc_test PRIVATE test)
		add_test(NAME msgpack_hunt_alloc COMMAND msgpack_hunt_alloc_test)
	endif()

	add_executable(msgpack_demo examples/demo.cc)
	target_link_libraries(msgpack_demo PRIVATE msgpack)

	# The benchmarks. OFF by default: the comparison libraries below are fetched only when it is ON, by the
	# benchmark targets alone; the library target and the tests never see them.
	#   msgpack_benchmark  MsgPack against the msgpack-c it bundles (benchmarks/pack_unpack.cc)
	#   msgpack_compare    msgpack-c (current), nlohmann::json, mpack, RapidJSON (benchmarks/compare.cc)
	# benchmarks/compare.py joins their output.
	option(MSGPACK_BUILD_BENCH "Build the benchmarks and fetch the comparison libraries" OFF)
	if(MSGPACK_BUILD_BENCH)
		add_executable(msgpack_benchmark benchmarks/pack_unpack.cc)
		target_link_libraries(msgpack_benchmark PRIVATE msgpack)
		target_include_directories(msgpack_benchmark PRIVATE benchmarks)

		# Header-only comparators are populated, not configured (SOURCE_SUBDIR names a directory that does not
		# exist, so FetchContent sets <name>_SOURCE_DIR and skips their own CMake projects).
		FetchContent_Declare(msgpack_c_current GIT_REPOSITORY https://github.com/msgpack/msgpack-c.git GIT_TAG cpp-9.0.0 SOURCE_SUBDIR do-not-configure)
		FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0 SOURCE_SUBDIR do-not-configure)
		FetchContent_Declare(mpack GIT_REPOSITORY https://github.com/ludocode/mpack.git GIT_TAG v1.1.1 SOURCE_SUBDIR do-not-configure)
		FetchContent_MakeAvailable(msgpack_c_current nlohmann_json mpack)

		enable_language(C)   # mpack is C
		add_library(msgpack_bench_mpack STATIC
			"${mpack_SOURCE_DIR}/src/mpack/mpack-common.c"
			"${mpack_SOURCE_DIR}/src/mpack/mpack-expect.c"
			"${mpack_SOURCE_DIR}/src/mpack/mpack-node.c"
			"${mpack_SOURCE_DIR}/src/mpack/mpack-platform.c"
			"${mpack_SOURCE_DIR}/src/mpack/mpack-reader.c"
			"${mpack_SOURCE_DIR}/src/mpack/mpack-writer.c")
		target_compile_definitions(msgpack_bench_mpack PUBLIC MPACK_HAS_CONFIG=1)
		target_include_directories(msgpack_bench_mpack PUBLIC benchmarks "${mpack_SOURCE_DIR}/src/mpack")

		add_executable(msgpack_compare benchmarks/compare.cc)
		target_compile_definitions(msgpack_compare PRIVATE MSGPACK_NO_BOOST)
		target_include_directories(msgpack_compare PRIVATE
			benchmarks
			"${msgpack_c_current_SOURCE_DIR}/include"
			"${nlohmann_json_SOURCE_DIR}/single_include")
		target_link_libraries(msgpack_compare PRIVATE msgpack_bench_mpack rapidjson_headers)
	endif()
endif()
