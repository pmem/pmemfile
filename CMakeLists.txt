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

cmake_minimum_required(VERSION 3.3)
project(pmemfile C CXX)

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

# XXX rename libpmemfile, since the whole repo is also just
# called pmemfile!
option(BUILD_LIBPMEMFILE
	"build libpmemfile (requires libsyscall_intercept)" ON)

option(TRACE_TESTS
	"more verbose test outputs" OFF)

option(LONG_TESTS "build long running tests as well" OFF)

set(TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/test
	CACHE STRING "working directory for tests")

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
include(CheckCXXCompilerFlag)
include(GNUInstallDirs)
find_package(PkgConfig QUIET)

include_directories(include)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

macro(add_c_flag flag)
	if(${ARGC} GREATER 2)
		set(check_name ${ARGV2})
	else()
		string(REPLACE - _ flag2 ${flag})
		set(check_name C_HAS_${flag2})
	endif()

	check_c_compiler_flag("${flag}" ${check_name})

	if (${${check_name}})
		if (${ARGC} EQUAL 1)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${flag}")
		else()
			set(CMAKE_C_FLAGS_${ARGV1} "${CMAKE_C_FLAGS_${ARGV1}} ${flag}")
		endif()
	endif()
endmacro()

macro(add_cxx_flag flag)
	if(${ARGC} GREATER 2)
		set(check_name ${ARGV2})
	else()
		string(REPLACE - _ flag2 ${flag})
		set(check_name CXX_HAS_${flag2})
	endif()

	check_cxx_compiler_flag(${flag} ${check_name})

	if (${${check_name}})
		if (${ARGC} EQUAL 1)
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}")
		else()
			set(CMAKE_CXX_FLAGS_${ARGV1} "${CMAKE_CXX_FLAGS_${ARGV1}} ${flag}")
		endif()
	endif()
endmacro()

add_c_flag(-Wall)
add_c_flag(-Wmissing-prototypes)
add_c_flag(-Wpointer-arith)
add_c_flag(-Wunused-macros)
add_c_flag(-Wsign-conversion)
add_c_flag(-Wsign-compare)
add_c_flag(-Wconversion)
add_c_flag(-Wunreachable-code-return)
add_c_flag(-Wmissing-variable-declarations)

add_cxx_flag(-Wall)
add_cxx_flag(-Wpointer-arith)
add_cxx_flag(-Wunused-macros)
add_cxx_flag(-Wsign-conversion)
add_cxx_flag(-Wsign-compare)
add_cxx_flag(-Wconversion)
add_cxx_flag(-Wunreachable-code-return)
add_cxx_flag(-Wmissing-variable-declarations)

add_c_flag(-pthread)
add_c_flag(-fno-common)
add_cxx_flag(-pthread)
add_cxx_flag(-fno-common)

check_c_compiler_flag(-Wl,-z,relro LINKER_HAS_RELRO)
if(LINKER_HAS_RELRO)
	set(CMAKE_LD_FLAGS  "${CMAKE_LD_FLAGS}  -Wl,-z,relro")
endif()

check_c_compiler_flag(-Wl,--warn-common LINKER_HAS_WARN_COMMON)
if(LINKER_HAS_WARN_COMMON)
	set(CMAKE_LD_FLAGS  "${CMAKE_LD_FLAGS} -Wl,--warn-common")
endif()

add_c_flag(-O0 DEBUG)
add_c_flag(-ggdb DEBUG)
add_c_flag(-DDEBUG DEBUG)

add_cxx_flag(-O0 DEBUG)
add_cxx_flag(-ggdb DEBUG)
add_cxx_flag(-DDEBUG DEBUG)

add_c_flag("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2" RELEASE C_CAN_FORTIFY)
add_cxx_flag("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2" RELEASE CXX_CAN_FORTIFY)

option(SANITIZERS_ENABLED "enable AddressSanitizer and UndefinedBehaviorSanitizer (debugging)" OFF)

if(SANITIZERS_ENABLED)
	set(SAVED_CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
	set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES} -fsanitize=address,undefined")

	check_c_compiler_flag("-fsanitize=address,undefined" C_HAS_ASAN_UBSAN)
	if(C_HAS_ASAN_UBSAN)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined")
	endif()

	check_cxx_compiler_flag("-fsanitize=address,undefined" CXX_HAS_ASAN_UBSAN)
	if(CXX_HAS_ASAN_UBSAN)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
	endif()

	if(C_HAS_ASAN_UBSAN OR CXX_HAS_ASAN_UBSAN)
		set(CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} -fsanitize=address,undefined")
	endif()

	set(CMAKE_REQUIRED_LIBRARIES ${SAVED_CMAKE_REQUIRED_LIBRARIES})
endif()

if(DEVELOPER_MODE)
	add_c_flag(-Werror)
	add_cxx_flag(-Werror)

	check_c_compiler_flag(-Wl,--warn-common LINKER_HAS_FATAL_WARNINGS)
	if(LINKER_HAS_FATAL_WARNINGS)
		set(CMAKE_LD_FLAGS  "${CMAKE_LD_FLAGS} -Wl,--fatal-warnings")
	endif()
endif(DEVELOPER_MODE)

add_executable(check_license EXCLUDE_FROM_ALL utils/check_license/check-license.c)

function(add_cstyle name)
	if(${ARGC} EQUAL 1)
		add_custom_target(cstyle-${name}
			COMMAND ${CMAKE_SOURCE_DIR}/utils/cstyle
				${CMAKE_CURRENT_SOURCE_DIR}/*.c
				${CMAKE_CURRENT_SOURCE_DIR}/*.h)
	else()
		add_custom_target(cstyle-${name}
			COMMAND ${CMAKE_SOURCE_DIR}/utils/cstyle ${ARGN})
	endif()
	add_dependencies(cstyle cstyle-${name})
endfunction()

function(add_cppstyle name)
	if(NOT CLANG_FORMAT)
		return()
	endif()

	if(${ARGC} EQUAL 1)
		add_custom_target(cppstyle-${name}
			COMMAND ${CMAKE_SOURCE_DIR}/utils/cppstyle
				${CLANG_FORMAT}
				check
				${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
				${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
			)
		add_custom_target(cppformat-${name}
			COMMAND ${CMAKE_SOURCE_DIR}/utils/cppstyle
				${CLANG_FORMAT}
				format
				${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
				${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
			)
	else()
		add_custom_target(cppstyle-${name}
			COMMAND ${CMAKE_SOURCE_DIR}/utils/cppstyle
				${CLANG_FORMAT}
				check
				${ARGN}
			)
		add_custom_target(cppformat-${name}
			COMMAND ${CMAKE_SOURCE_DIR}/utils/cppstyle
				${CLANG_FORMAT}
				format
				${ARGN}
			)
	endif()

	add_dependencies(cppstyle cppstyle-${name})
	add_dependencies(cppformat cppformat-${name})
endfunction()

function(add_check_whitespace name)
	if(${ARGC} EQUAL 1)
		add_custom_target(check-whitespace-${name}
			COMMAND ${CMAKE_SOURCE_DIR}/utils/check_whitespace
				${CMAKE_CURRENT_SOURCE_DIR}/*.c
				${CMAKE_CURRENT_SOURCE_DIR}/*.h)
	else()
		add_custom_target(check-whitespace-${name}
			COMMAND ${CMAKE_SOURCE_DIR}/utils/check_whitespace ${ARGN})
	endif()
	add_dependencies(check-whitespace check-whitespace-${name})
endfunction()

add_custom_target(checkers ALL)
add_custom_target(cstyle)
add_custom_target(cppstyle)
add_custom_target(cppformat)
add_custom_target(check-whitespace)
add_custom_target(check-license
	COMMAND ${CMAKE_SOURCE_DIR}/utils/check_license/check-headers.sh
		${CMAKE_SOURCE_DIR}
		${CMAKE_BINARY_DIR}/check_license
		${CMAKE_SOURCE_DIR}/LICENSE
		-a)
add_dependencies(check-license check_license)
add_cstyle(check_license ${CMAKE_SOURCE_DIR}/utils/check_license/*.c)
add_check_whitespace(check_license ${CMAKE_SOURCE_DIR}/utils/check_license/*.c)

add_custom_target(check-whitespace-main
		COMMAND ${CMAKE_SOURCE_DIR}/utils/check_whitespace
			${CMAKE_SOURCE_DIR}/utils/check_license/*.sh
			${CMAKE_SOURCE_DIR}/utils/*.sh
			${CMAKE_SOURCE_DIR}/README.md
			${CMAKE_SOURCE_DIR}/*.spec
			${CMAKE_SOURCE_DIR}/debian/*
			${CMAKE_SOURCE_DIR}/debian/*/*
			${CMAKE_SOURCE_DIR}/doc/*.md)
add_dependencies(check-whitespace check-whitespace-main)

add_custom_target(tests)

option(DEVELOPER_MODE "enable developer checks" OFF)
if(DEVELOPER_MODE)
	find_program(CLANG_FORMAT NAMES clang-format-3.9 clang-format-3.8 clang-format)
	if (NOT CLANG_FORMAT)
		message(WARNING "clang-format not found - C++ sources will not be checked")
	endif()

	add_dependencies(checkers cstyle)
	add_dependencies(checkers cppstyle)
	add_dependencies(checkers check-whitespace)
	add_dependencies(checkers check-license)
endif(DEVELOPER_MODE)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
	IMMEDIATE @ONLY)

add_custom_target(uninstall
	COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

if(PKG_CONFIG_FOUND)
	pkg_check_modules(VALGRIND QUIET valgrind)
else()
	find_package(VALGRIND QUIET)
endif()

if(VALGRIND_FOUND)
	set(ENV{PATH} ${VALGRIND_PREFIX}/bin:$ENV{PATH})
	execute_process(COMMAND valgrind --tool=pmemcheck --help
			RESULT_VARIABLE VALGRIND_PMEMCHECK_NOT_FOUND
			OUTPUT_QUIET
			ERROR_QUIET)
	if(VALGRIND_PMEMCHECK_NOT_FOUND)
		message(WARNING "Valgrind pmemcheck NOT found. Pmemcheck tests will not be performed.")
	endif()
else()
	message(WARNING "Valgrind not found. Valgrind tests will not be performed.")
endif()

if(PKG_CONFIG_FOUND)
	pkg_check_modules(LIBUNWIND QUIET libunwind)
else()
	find_package(LIBUNWIND QUIET)
endif()
if(NOT LIBUNWIND_FOUND)
	message(WARNING "libunwind not found. Any stack traces from tests will not be reliable")
endif()

if(TEST_DIR)
	enable_testing()
else()
	message(WARNING "TEST_DIR is empty - 'make test' will not work")
endif()

add_subdirectory(src/libpmemfile-core)
if(BUILD_LIBPMEMFILE)
add_subdirectory(src/libpmemfile)
endif()
add_subdirectory(src/tools)
add_subdirectory(tests)
add_subdirectory(doc)
