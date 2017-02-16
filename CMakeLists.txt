#
# Copyright 2017, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.6)
project(pmemfile C)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
endif (NOT CMAKE_BUILD_TYPE)

if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(COMMAND git describe
                        OUTPUT_VARIABLE SRCVERSION
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        if(NOT SRCVERSION)
                execute_process(COMMAND git log -1 --format=%h
                                OUTPUT_VARIABLE SRCVERSION
                                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                                OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
else()
        execute_process(COMMAND cat .version
                        OUTPUT_VARIABLE SRCVERSION
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
add_definitions(-DSRCVERSION="${SRCVERSION}")
add_definitions(-DUSE_VALGRIND)

include(CheckCCompilerFlag)
include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

include_directories(include)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-prototypes")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpointer-arith")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wunused-macros")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wsign-conversion")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wsign-compare")

check_c_compiler_flag(-Wconversion HAS_WCONVERSION)
check_c_compiler_flag(-Wunreachable-code-return HAS_WUNREACHABLE_CODE_RETURN)
check_c_compiler_flag(-Wmissing-variable-declarations HAS_WMISSING_VARIABLE_DECL)

if(HAS_WCONVERSION)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion")
endif()
if(HAS_WUNREACHABLE_CODE_RETURN)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wunreachable-code-return")
endif()
if(HAS_WMISSING_VARIABLE_DECL)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-variable-declarations")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread -fno-common")
set(CMAKE_LD_FLAGS ${CMAKE_LD_FLAGS} -Wl,-z,relro -Wl,--warn-common)

set(CMAKE_C_FLAGS_DEBUG   "-O0 -ggdb -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2")

if(DEVELOPER_MODE)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
	set(CMAKE_LD_FLAGS ${CMAKE_LD_FLAGS} -Wl,--fatal-warnings)
endif(DEVELOPER_MODE)

add_executable(check_license EXCLUDE_FROM_ALL utils/check_license/check-license.c)

function(add_checker_targets name)
        if(${ARGC} EQUAL 1)
                add_custom_target(cstyle-${name}
                        COMMAND ${CMAKE_SOURCE_DIR}/utils/cstyle ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
        else()
                add_custom_target(cstyle-${name}
                        COMMAND ${CMAKE_SOURCE_DIR}/utils/cstyle ${ARGN})
        endif()
        add_dependencies(cstyle cstyle-${name})

        if(${ARGC} EQUAL 1)
                add_custom_target(check-whitespace-${name}
                        COMMAND ${CMAKE_SOURCE_DIR}/utils/check_whitespace ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
        else()
                add_custom_target(check-whitespace-${name}
                        COMMAND ${CMAKE_SOURCE_DIR}/utils/check_whitespace ${ARGN})
        endif()
        add_dependencies(check-whitespace check-whitespace-${name})
endfunction()

add_custom_target(checkers ALL)
add_custom_target(cstyle)
add_custom_target(check-whitespace)
add_custom_target(check-license
        COMMAND ${CMAKE_SOURCE_DIR}/utils/check_license/check-headers.sh
                ${CMAKE_SOURCE_DIR}
                ${CMAKE_BINARY_DIR}/check_license
                ${CMAKE_SOURCE_DIR}/LICENSE
                -a)
add_dependencies(check-license check_license)

if(DEVELOPER_MODE)
        add_dependencies(checkers cstyle)
        add_dependencies(checkers check-whitespace)
        add_dependencies(checkers check-license)
endif(DEVELOPER_MODE)

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

add_subdirectory(src/libpmemfile-core)
add_subdirectory(src/libpmemfile)
add_subdirectory(src/tools)
add_subdirectory(tests/core)
add_subdirectory(tests/preload)
add_subdirectory(doc)
