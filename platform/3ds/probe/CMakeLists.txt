cmake_minimum_required(VERSION 3.13)

# First homebrew for the port: checks on a real console what the engine will
# rely on (C++20 standard library and std::filesystem on newlib, SD card and
# RomFS read speed, free application memory) before any engine code is built
# for it.
#
# cmake -S platform/3ds/probe -B build-3ds-probe \
#       -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake \
#       [-DCRUSADER_3DS_TOOLS=<folder with makerom and bannertool>] \
#       [-DCRUSADER_PERSONAL_DATA=<game data folder>]
#
# Builds crusader-probe.3dsx; with makerom and bannertool also
# crusader-probe.cia; with CRUSADER_PERSONAL_DATA also a CIA bundling that
# data without the movies, for the owner's console only.
project(crusader-probe CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(../cmake/cia.cmake)
set(CRUSADER_PERSONAL_DATA "" CACHE PATH "Game data to bundle in a personal CIA (never share it)")

add_executable(crusader-probe probe.cpp)
target_compile_options(crusader-probe PRIVATE -O2 -Wall -Wextra)

ctr_generate_smdh(crusader-probe.smdh
    NAME "Crusader probe"
    DESCRIPTION "SD speed, memory and filesystem check"
    AUTHOR "crusader-no-remorse-3ds"
    ICON "${CRUSADER_3DS_META}/icon.png"
)
ctr_create_3dsx(crusader-probe SMDH crusader-probe.smdh)

crusader_add_cia(crusader-probe-cia
    ELF crusader-probe
    TITLE "Crusader probe"
    DESCRIPTION "SD speed, memory and filesystem check"
    PRODUCT_CODE CTR-H-CNRP
    UNIQUE_ID 0xC7A50
)
if(CRUSADER_PERSONAL_DATA)
    crusader_add_cia(crusader-probe-data
        ELF crusader-probe
        TITLE "Crusader probe + data"
        DESCRIPTION "Probe with the game data, no movies"
        PRODUCT_CODE CTR-H-CNRD
        UNIQUE_ID 0xC7A51
        DATA "${CRUSADER_PERSONAL_DATA}"
    )
endif()
