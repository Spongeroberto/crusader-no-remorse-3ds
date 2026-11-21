# CIA packaging for the 3DS port: an app the HOME Menu installs, with the
# port's own icon, banner and jingle (platform/3ds/meta).
#
# Needs makerom (Project_CTR) and bannertool, which devkitPro does not ship.
# They are looked up on PATH and in CRUSADER_3DS_TOOLS (a folder).
#
# crusader_add_cia(<name>
#     ELF <target>              the 3DS executable target
#     TITLE <text>              HOME Menu title
#     DESCRIPTION <text>
#     PRODUCT_CODE <CTR-H-XXXX>
#     UNIQUE_ID <0xNNNNN>       distinct per CIA, so builds install side by side
#     [SYSTEM_MODE 64MB|80MB|96MB]
#     [DATA <folder>]           game data to bundle in RomFS as romfs:/data,
#     [MOVIES])                 with the movies (FLICS, about 450 MB) when
#                               given, else without them
#
# A CIA built with DATA holds the user's own copy of the game: it is for their
# console only. Its file name says so, and build folders are git-ignored.

set(CRUSADER_3DS_META "${CMAKE_CURRENT_LIST_DIR}/../meta")
set(CRUSADER_3DS_TOOLS "" CACHE PATH "Folder holding makerom and bannertool")

# The host tools (platform/3ds/tools), built once with the computer's own
# compiler, not the console's.
include(ExternalProject)
macro(crusader_host_tools)
    if(NOT TARGET crusader_host_tools)
        set(host_dir "${CMAKE_BINARY_DIR}/host-tools")
        ExternalProject_Add(crusader_host_tools
            SOURCE_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools"
            BINARY_DIR "${host_dir}"
            CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${host_dir}/shape_reach"
            BUILD_ALWAYS OFF
        )
    endif()
    set(CRUSADER_SHAPE_REACH_EXE "${CMAKE_BINARY_DIR}/host-tools/shape_reach")
endmacro()

function(crusader_add_cia name)
    cmake_parse_arguments(PARSE_ARGV 1 CIA "MOVIES" "ELF;TITLE;DESCRIPTION;PRODUCT_CODE;UNIQUE_ID;SYSTEM_MODE;DATA" "")
    if(NOT CIA_SYSTEM_MODE)
        set(CIA_SYSTEM_MODE 64MB)
    endif()

    find_program(MAKEROM_EXE makerom HINTS "${CRUSADER_3DS_TOOLS}")
    find_program(BANNERTOOL_EXE bannertool HINTS "${CRUSADER_3DS_TOOLS}")
    if(NOT MAKEROM_EXE OR NOT BANNERTOOL_EXE)
        message(WARNING "${name}: makerom or bannertool not found (set CRUSADER_3DS_TOOLS); no CIA is built")
        return()
    endif()

    set(dir "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    set(romfs "${dir}/romfs")
    set(smdh "${dir}/icon.smdh")
    set(bnr "${dir}/banner.bnr")
    file(MAKE_DIRECTORY "${dir}")

    ctr_generate_smdh("${smdh}"
        NAME "${CIA_TITLE}"
        DESCRIPTION "${CIA_DESCRIPTION}"
        AUTHOR "crusader-no-remorse-3ds"
        ICON "${CRUSADER_3DS_META}/icon.png"
    )
    add_custom_command(
        OUTPUT "${bnr}"
        COMMAND "${BANNERTOOL_EXE}" makebanner -i "${CRUSADER_3DS_META}/banner.png"
                -a "${CRUSADER_3DS_META}/banner.wav" -o "${bnr}"
        DEPENDS "${CRUSADER_3DS_META}/banner.png" "${CRUSADER_3DS_META}/banner.wav"
        VERBATIM
    )

    if(CIA_DATA)
        crusader_host_tools()
        get_filename_component(data "${CIA_DATA}" ABSOLUTE)
        if(NOT EXISTS "${data}/STATIC")
            message(FATAL_ERROR "${name}: no game data in ${data}")
        endif()
        set(output "${CMAKE_CURRENT_BINARY_DIR}/${name}-PERSONAL-do-not-share.cia")
        # Everything at the data folder's top level, the movies only with MOVIES.
        if(CIA_MOVIES)
            set(exclude "")
        else()
            set(exclude FLICS)
        endif()
        # Rewritten only when it changes: switching MOVIES stages the data again.
        file(CONFIGURE OUTPUT "${dir}/exclude.txt" CONTENT "${exclude}")
        add_custom_command(
            OUTPUT "${dir}/romfs.stamp"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${romfs}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${romfs}/data"
            COMMAND "${CMAKE_COMMAND}" -DSRC=${data} -DDST=${romfs}/data "-DEXCLUDE=${exclude}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/stage_data.cmake"
            # The display list's bound for these shapes (shape_reach.hpp).
            COMMAND "${CRUSADER_SHAPE_REACH_EXE}" "${romfs}/data/STATIC/SHAPES.FLX"
                    "${romfs}/data/STATIC/SHAPES.REACH"
            COMMAND "${CMAKE_COMMAND}" -E touch "${dir}/romfs.stamp"
            DEPENDS crusader_host_tools "${dir}/exclude.txt"
            VERBATIM
        )
    else()
        set(output "${CMAKE_CURRENT_BINARY_DIR}/${name}.cia")
        add_custom_command(
            OUTPUT "${dir}/romfs.stamp"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${romfs}"
            COMMAND "${CMAKE_COMMAND}" -E touch "${dir}/romfs.stamp"
            VERBATIM
        )
    endif()

    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${MAKEROM_EXE}" -f cia -o "${output}" -target t -exefslogo
                -elf "$<TARGET_FILE:${CIA_ELF}>"
                -rsf "${CRUSADER_3DS_META}/app.rsf"
                -icon "${smdh}" -banner "${bnr}"
                "-DAPP_TITLE=${CIA_TITLE}"
                "-DAPP_PRODUCT_CODE=${CIA_PRODUCT_CODE}"
                "-DAPP_UNIQUE_ID=${CIA_UNIQUE_ID}"
                "-DAPP_SYSTEM_MODE=${CIA_SYSTEM_MODE}"
                "-DAPP_ROMFS=${romfs}"
        DEPENDS ${CIA_ELF} "${smdh}" "${bnr}" "${dir}/romfs.stamp" "${CRUSADER_3DS_META}/app.rsf"
        COMMENT "Building CIA ${output}"
        VERBATIM
    )
    add_custom_target(${name} ALL DEPENDS "${output}")
endfunction()
