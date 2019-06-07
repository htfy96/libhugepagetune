cmake_minimum_required(VERSION 3.12)

project(tlbautotune 
	VERSION 0.1 
	DESCRIPTION "A library automatically merging huge pages based on fine-grained memory access patterns"
)

cmake_policy(SET CMP0057 NEW)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules;${CMAKE_MODULE_PATH}")

set(CMAKE_CXX_STANDARD 14)

find_package(Libpfm REQUIRED)
find_package(TBB REQUIRED)
find_package(Threads REQUIRED)
find_package(Filesystem REQUIRED COMPONENTS Experimental)

add_library(perf perf.hpp perf.cpp)
target_include_directories(perf PRIVATE ${LIBPFM_INCLUDE_DIR})
target_link_libraries(perf PRIVATE ${LIBPFM_LIBRARY})
set_property(TARGET perf PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library(tlbautotune SHARED libtlbautotune.cpp)
target_link_libraries(tlbautotune PRIVATE perf)
target_link_libraries(tlbautotune PRIVATE tbb)
target_link_libraries(tlbautotune PRIVATE Threads::Threads)
target_link_libraries(tlbautotune PRIVATE std::filesystem)

enable_testing()
add_executable(test-record-data test-record-data.cpp)
target_link_libraries(test-record-data PRIVATE perf)
target_link_libraries(test-record-data PRIVATE Threads::Threads)
add_test(test-record-data test-record-data)

