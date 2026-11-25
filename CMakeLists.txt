# hosts-common: code shared by every core host (host-libretro, later others):
# dynamic library loading, logging, files, the settings file format, later the
# IPC transport. Also used by frontends and multitap-net for logging, files
# and settings files. Must not depend on SDL or any frontend code. Also the project's CMake helpers
# (cmake/multitap.cmake), included here for every library and frontend.
# A library: it doesn't build alone; a frontend adds it (first).

include("${CMAKE_CURRENT_LIST_DIR}/cmake/multitap.cmake")

add_library(host_common STATIC
    host_log.cpp
    dynamic_library.cpp
    host_fs.cpp
    settings_file.cpp
)

target_include_directories(host_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(host_common PUBLIC ${CMAKE_DL_LIBS})

multitap_set_project_flags(host_common)

if(MULTITAP_BUILD_TESTS)
    add_executable(host_settings_file_test tests/settings_file_test.cpp)
    target_link_libraries(host_settings_file_test PRIVATE host_common)
    multitap_set_project_flags(host_settings_file_test)
    add_test(NAME host_settings_file
             COMMAND host_settings_file_test "${CMAKE_CURRENT_BINARY_DIR}/settings_file_test")
    set_tests_properties(host_settings_file PROPERTIES
                         FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")
endif()
