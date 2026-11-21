cmake_minimum_required(VERSION 3.21)

# The game on a 3DS, built from upstream's platform-free libraries
# (CRUSADER_LIBRARIES_ONLY).
#
# cmake -S platform/3ds/game -B build-3ds-game \
#       -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake \
#       -DCRUSADER_3DS_TOOLS=<folder with makerom and bannertool> \
#       [-DCRUSADER_PERSONAL_DATA=<game data folder>] [-DCRUSADER_3DS_MOVIES=OFF]
project(crusader-3ds CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(repo "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(CRUSADER_LIBRARIES_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory("${repo}" engine)

include(../cmake/cia.cmake)
set(CRUSADER_PERSONAL_DATA "" CACHE PATH "Game data to bundle in a personal CIA (never share it)")
option(CRUSADER_3DS_MOVIES "Bundle the cutscenes (FLICS, about 450 MB) in the personal CIA" ON)

add_executable(crusader-3ds main.cpp)
target_link_libraries(crusader-3ds PRIVATE crusader_game)
target_include_directories(crusader-3ds PRIVATE "${repo}/src" "${CMAKE_CURRENT_SOURCE_DIR}/../common")

ctr_generate_smdh(crusader-3ds.smdh
    NAME "Crusader 3DS"
    DESCRIPTION "Crusader: No Remorse"
    AUTHOR "crusader-no-remorse-3ds"
    ICON "${CRUSADER_3DS_META}/icon.png"
)
ctr_create_3dsx(crusader-3ds SMDH crusader-3ds.smdh)

if(CRUSADER_PERSONAL_DATA)
    set(movies "")
    if(CRUSADER_3DS_MOVIES)
        set(movies MOVIES)
    endif()
    crusader_add_cia(crusader-3ds-data
        ELF crusader-3ds
        TITLE "Crusader 3DS"
        DESCRIPTION "Crusader: No Remorse, game data inside"
        PRODUCT_CODE CTR-H-CNR3
        UNIQUE_ID 0xC7A50
        DATA "${CRUSADER_PERSONAL_DATA}"
        ${movies}
    )
endif()
