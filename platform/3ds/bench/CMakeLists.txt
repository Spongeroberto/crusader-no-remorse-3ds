cmake_minimum_required(VERSION 3.21)

# The engine's benchmark on a 3DS, built from upstream's platform-free
# libraries (CRUSADER_LIBRARIES_ONLY) and the shared loop in
# src/tools/bench_run.cpp.
#
# cmake -S platform/3ds/bench -B build-3ds-bench \
#       -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake \
#       -DCRUSADER_3DS_TOOLS=<folder with makerom and bannertool> \
#       [-DCRUSADER_PERSONAL_DATA=<game data folder>]
project(crusader-bench CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(repo "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(CRUSADER_LIBRARIES_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory("${repo}" engine)

include(../cmake/cia.cmake)
set(CRUSADER_PERSONAL_DATA "" CACHE PATH "Game data to bundle in a personal CIA (never share it)")
# A second build of the bench (other compiler settings, say) installs beside
# the first under its own title: "Crusader bench + data <variant>".
set(CRUSADER_BENCH_VARIANT "" CACHE STRING "Suffix for the bench's title; also gives it its own title id")

add_executable(crusader-bench main.cpp "${repo}/src/tools/bench_run.cpp")
target_link_libraries(crusader-bench PRIVATE crusader_game)
target_include_directories(crusader-bench PRIVATE "${repo}/src")

ctr_generate_smdh(crusader-bench.smdh
    NAME "Crusader bench"
    DESCRIPTION "Engine frame time on this console"
    AUTHOR "crusader-no-remorse-3ds"
    ICON "${CRUSADER_3DS_META}/icon.png"
)
ctr_create_3dsx(crusader-bench SMDH crusader-bench.smdh)

if(CRUSADER_BENCH_VARIANT)
    set(bench_suffix " ${CRUSADER_BENCH_VARIANT}")
    set(bench_unique_id 0xC7A53)
else()
    set(bench_suffix "")
    set(bench_unique_id 0xC7A52)
endif()

if(CRUSADER_PERSONAL_DATA)
    crusader_add_cia(crusader-bench-data
        ELF crusader-bench
        TITLE "Crusader bench + data${bench_suffix}"
        DESCRIPTION "Engine frame time, game data inside"
        PRODUCT_CODE CTR-H-CNRB
        UNIQUE_ID ${bench_unique_id}
        DATA "${CRUSADER_PERSONAL_DATA}"
    )
endif()
