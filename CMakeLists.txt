# hosts-common: code shared by every core host (host-libretro, later others):
# dynamic library loading, logging, files, later the IPC transport. Must not
# depend on SDL or any frontend code. Also the project's CMake helpers
# (cmake/multitap.cmake), included here for every library and frontend.
# A library: it doesn't build alone; a frontend adds it (first).

include("${CMAKE_CURRENT_LIST_DIR}/cmake/multitap.cmake")

add_library(host_common STATIC
    host_log.cpp
    dynamic_library.cpp
    host_fs.cpp
)

target_include_directories(host_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(host_common PUBLIC ${CMAKE_DL_LIBS})

multitap_set_project_flags(host_common)
