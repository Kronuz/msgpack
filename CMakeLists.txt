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
FetchContent_Declare(enum_reflection  GIT_REPOSITORY https://github.com/Kronuz/enum-reflection.git     GIT_TAG 1be88746f663cb1fd08df36ad22d50ff5367a188)
FetchContent_Declare(strict_stox      GIT_REPOSITORY https://github.com/Kronuz/strict-stox.git         GIT_TAG 21b7941c71064b5a4e52ffb50a2701ee4aae4c6c)
FetchContent_Declare(located_exception GIT_REPOSITORY https://github.com/Kronuz/located-exception.git  GIT_TAG 1d20a4468c2574a8b1d03dfc93622c833e5ce3dd)
FetchContent_Declare(constexpr_phf    GIT_REPOSITORY https://github.com/Kronuz/constexpr-phf.git       GIT_TAG 4ec7da023b2f5c9a68b942609aa278d353619486)
FetchContent_Declare(hashes           GIT_REPOSITORY https://github.com/Kronuz/hashes.git              GIT_TAG d8751b2fa5fccc20885bd366173fb0cd63c5bc20)
FetchContent_Declare(repr             GIT_REPOSITORY https://github.com/Kronuz/repr.git                GIT_TAG caac62878fac752c3598e175456eb491f1862b4a)
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

	add_executable(msgpack_demo examples/demo.cc)
	target_link_libraries(msgpack_demo PRIVATE msgpack)

	add_executable(msgpack_benchmark benchmarks/pack_unpack.cc)
	target_link_libraries(msgpack_benchmark PRIVATE msgpack)
endif()
