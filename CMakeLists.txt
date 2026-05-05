cmake_minimum_required(VERSION 3.28)
set (PROJECT_NAME mm)
project(${PROJECT_NAME} C CXX)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)


# Запрещаем CMake добавлять флаги модулей
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP OFF)

# set lld as default linker if exist
find_program(LLD lld)
if(LLD)
    message(STATUS "Using lld linker: ${LLD}")
    set(CMAKE_LINKER ${LLD})
    # Также добавляем флаг для компилятора (если нужно)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
else()
    message(WARNING "lld linker not found")
endif()


set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(FetchContent)

find_package(Threads REQUIRED)

FetchContent_Declare(gtest_main
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG main
)
FetchContent_MakeAvailable(gtest_main)


option(USE_BENCHMARK_MM "Enable Google Benchmark support" OFF)

if(USE_BENCHMARK_MM)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Suppressing benchmark's tests" FORCE)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(benchmark)
    set_target_properties(benchmark PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_FOR_3RD_PARTY}")
    set_target_properties(benchmark PROPERTIES FOLDER "Hide3rdPartyLibrary")
    set_target_properties(benchmark PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_target_properties(benchmark_main PROPERTIES FOLDER "Hide3rdPartyLibrary")
    set_target_properties(benchmark_main PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

set_target_properties(gtest_main PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_FOR_3RD_PARTY}")
set_target_properties(gtest PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_FOR_3RD_PARTY}")
set_target_properties(gmock_main PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_FOR_3RD_PARTY}")
set_target_properties(gmock PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_FOR_3RD_PARTY}")


# set(DOWNLOAD_DEPENDENCIES ON CACHE BOOL "Download dependencies for mlpack")

set_target_properties(gtest PROPERTIES FOLDER "Hide3rdPartyLibrary")
set_target_properties(gtest_main PROPERTIES FOLDER "Hide3rdPartyLibrary")
set_target_properties(gmock PROPERTIES FOLDER "Hide3rdPartyLibrary")
set_target_properties(gmock_main PROPERTIES FOLDER "Hide3rdPartyLibrary")

file(GLOB LIST_H
     "mm/*h"
)
file(GLOB LIST_CPP
     "src/*.cpp"
)

add_library (${PROJECT_NAME}
    ${LIST_CPP}
)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
           $<INSTALL_INTERFACE:include>
           ${CMAKE_BINARY_DIR}
)

add_subdirectory(examples)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
    CXX_MODULE_STD_ONLY OFF
)


option(BUILD_TESTS_MM "Build tests" ON)
if(BUILD_TESTS_MM)
    enable_testing()
    include(GoogleTest)
    add_subdirectory(tests)
else()
    message(STATUS "Tests of ${PROJECT_NAME} are skipped.")
endif()

add_custom_target(stats
    COMMAND ${CMAKE_COMMAND} -E echo "=== Build statistics ==="
    COMMAND ${CMAKE_COMMAND} -E echo "Total build time: $<TARGET_PROPERTY:${PROJECT_NAME},TIME_BUILD>"
)