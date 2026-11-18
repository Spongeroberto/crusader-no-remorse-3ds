# cmake -DSRC=<data> -DDST=<folder> -DEXCLUDE=<name> -P stage_data.cmake
# Copies every entry at the top level of SRC into DST except EXCLUDE
# (compared without case, as the game's DOS file names are).

file(GLOB entries RELATIVE "${SRC}" "${SRC}/*")
string(TOUPPER "${EXCLUDE}" exclude)
foreach(entry IN LISTS entries)
    string(TOUPPER "${entry}" upper)
    if(upper STREQUAL exclude)
        continue()
    endif()
    file(COPY "${SRC}/${entry}" DESTINATION "${DST}")
endforeach()
