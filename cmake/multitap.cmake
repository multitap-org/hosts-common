# multitap's CMake helpers, shared by every library and frontend of the
# project (hosts-common, host-libretro, frontends-common, frontend-libretro,
# piccolo, libretro-test-frontend...). hosts-common's CMakeLists.txt includes
# this file, so a frontend gets them once it adds hosts-common, its first
# library. They read the frontend's options:
#   MULTITAP_WARNINGS_AS_ERRORS  warnings in our code are errors
#   MULTITAP_SANITIZE            our code with ASan + UBSan
# A library checks what it needs from the frontend with multitap_require().

include_guard(GLOBAL)

# Stops with a clear message if the frontend didn't provide one of the targets
# (or, for names starting with "VAR:", the variable) a library needs.
function(multitap_require library)
    foreach(need IN LISTS ARGN)
        if(need MATCHES "^VAR:(.*)$")
            if(NOT DEFINED ${CMAKE_MATCH_1} OR "${${CMAKE_MATCH_1}}" STREQUAL "")
                message(FATAL_ERROR "${library} needs the frontend to set ${CMAKE_MATCH_1} before adding it")
            endif()
        elseif(NOT TARGET ${need})
            message(FATAL_ERROR "${library} needs the target ${need}: the frontend must add it (or the library that makes it) first")
        endif()
    endforeach()
endfunction()

# ---------------------------------------------------------------------------
# Flags for OUR code only. Apply with: multitap_set_project_flags(<target>)
# ---------------------------------------------------------------------------
function(multitap_set_project_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(MULTITAP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wconversion -Wsign-conversion -Wshadow
            -Wold-style-cast -Wcast-qual -Wnull-dereference
            # Designated initializers leave unnamed fields zeroed on purpose.
            -Wno-missing-field-initializers
            -fno-exceptions -fno-rtti
        )
        if(MULTITAP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(MULTITAP_SANITIZE)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

# Copies `target`'s file into `directory` after every build. Written to a
# temporary name first, then renamed over the old one: a program or core that
# is running keeps its old copy (overwriting a running executable fails, and
# overwriting a loaded .so in place can crash the program using it).
# An optional third argument names the copy step, for copying one target to
# several folders.
function(multitap_copy_after_build target directory)
    if(NOT directory)
        return()
    endif()
    set(step_name "copy_${target}")
    if(ARGC GREATER 2)
        set(step_name "copy_${target}_${ARGV2}")
    endif()
    add_custom_target(${step_name} ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> "${directory}/$<TARGET_FILE_NAME:${target}>.new"
        COMMAND ${CMAKE_COMMAND} -E rename "${directory}/$<TARGET_FILE_NAME:${target}>.new" "${directory}/$<TARGET_FILE_NAME:${target}>"
        DEPENDS ${target}
        COMMENT "Copying ${target} to ${directory}"
        VERBATIM)
endfunction()

# Version string of a component whose sources are in `source_dir`: `base` on
# main (or master), `base-<branch>` on any other branch, `base-<commit>` when
# detached, and `base` alone without git (a source tarball). CMake configures
# again when the branch changes, so the string follows it.
function(multitap_version out_var base source_dir)
    set(version "${base}")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --abbrev-ref HEAD
                        OUTPUT_VARIABLE branch OUTPUT_STRIP_TRAILING_WHITESPACE
                        RESULT_VARIABLE result ERROR_QUIET)
        if(result EQUAL 0 AND branch)
            if(branch STREQUAL "HEAD")
                execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --short HEAD
                                OUTPUT_VARIABLE branch OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            endif()
            if(NOT branch STREQUAL "main" AND NOT branch STREQUAL "master")
                set(version "${base}-${branch}")
            endif()
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --path-format=absolute --git-path HEAD
                            OUTPUT_VARIABLE head_file OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(head_file AND EXISTS "${head_file}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${head_file}")
            endif()
        endif()
    endif()
    set(${out_var} "${version}" PARENT_SCOPE)
endfunction()

# Writes <directory>/<name>_version.h with `#define <MACRO> "<version>"`
# (rewritten only when the string changes).
function(multitap_write_version_header directory name macro version)
    file(CONFIGURE OUTPUT "${directory}/${name}_version.h"
         CONTENT "// Generated by CMake (multitap_version). Do not edit.\n#pragma once\n#define ${macro} \"${version}\"\n"
         @ONLY)
endfunction()
