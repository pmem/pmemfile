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

add_c_flag(-fno-strict-aliasing)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FindThreads)
include(ExternalProject)

include(gtest.cmake)
if(NOT GTEST_FOUND)
	message(FATAL "unable to locate gtest, tests can not be built")
endif()

include_directories(${GTEST_INCLUDE_DIRS})

include_directories(.)
if(LIBUNWIND_FOUND)
	add_definitions(-DUSE_LIBUNWIND)
endif()

add_library(pmemfile_test STATIC pmemfile_test.cpp)
target_link_libraries(pmemfile_test ${GTEST_LIBRARIES})
add_library(test_backtrace STATIC ../test_backtrace.c)

add_cppstyle("tests-posix-test")
add_check_whitespace("tests-posix-test" ${CMAKE_CURRENT_SOURCE_DIR}/*.*pp)

add_cstyle(tests-posix-test_bt ${CMAKE_CURRENT_SOURCE_DIR}/../test_backtrace.c)
add_check_whitespace(tests-posix-test_bt ${CMAKE_CURRENT_SOURCE_DIR}/../test_backtrace.c)

function(build_test name srcs)
	add_cppstyle(tests-posix-${name} ${CMAKE_CURRENT_SOURCE_DIR}/${srcs})
	add_check_whitespace(tests-posix-${name} ${CMAKE_CURRENT_SOURCE_DIR}/${srcs})
	add_executable(${name} ${srcs})
	target_link_libraries(${name} pmemfile-posix_shared pmemfile_test test_backtrace)
	if(LIBUNWIND_FOUND)
		target_link_libraries(${name} ${LIBUNWIND_LIBRARIES} ${CMAKE_DL_LIBS})
	endif()
	add_dependencies(tests ${name})
endfunction()

build_test(file_basic basic/basic.cpp)
build_test(file_crash crash/crash.cpp)
build_test(file_dirs dirs/dirs.cpp)
build_test(file_getdents getdents/getdents.cpp)
build_test(file_mt mt/mt.cpp)
build_test(file_openp openp/openp.cpp)
build_test(file_permissions permissions/permissions.cpp)
build_test(file_rw rw/rw.cpp)
build_test(file_stat stat/stat.cpp)
build_test(file_symlinks symlinks/symlinks.cpp)

function(add_test_with_filter name filter tracer)
	set(vg_tracers memcheck helgrind drd pmemcheck)
	if ("${filter}" STREQUAL "")
		set(filter_postfix)
		set(filter_cmd)
	else()
		set(filter_postfix _${filter})
		set(filter_cmd -Dfilter=--gtest_filter=${name}.${filter})
	endif()

	if ((NOT VALGRIND_FOUND) AND ${tracer} IN_LIST vg_tracers)
		add_test(NAME ${name}${filter_postfix}_${tracer}_SKIPPED_BECAUSE_OF_MISSING_VALGRIND
			COMMAND true)
		return()
	endif()

	if(${tracer} STREQUAL pmemcheck)
		if (VALGRIND_PMEMCHECK_NOT_FOUND)
			add_test(NAME ${name}${filter_postfix}_${tracer}_SKIPPED_BECAUSE_OF_MISSING_PMEMCHECK
				COMMAND true)
			return()
		endif()
	endif()

	if (SANITIZERS AND ${tracer} IN_LIST vg_tracers)
		return()
	endif()

	add_test(NAME ${name}${filter_postfix}_${tracer}
		COMMAND ${CMAKE_COMMAND}
			${GLOBAL_TEST_ARGS}
			-DTEST_NAME=${name}${filter_postfix}_${tracer}
			-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
			-DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}${filter_postfix}_${tracer}
			-DTEST_EXECUTABLE=$<TARGET_FILE:file_${name}>
			-DTRACER=${tracer}
			-DLONG_TESTS=${LONG_TESTS}
			${ARGV3}
			${filter_cmd}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.cmake)

	set_tests_properties(${name}${filter_postfix}_${tracer} PROPERTIES
		ENVIRONMENT "LC_ALL=C;PATH=$ENV{PATH};${ARGV4}")
endfunction()

function(add_test_generic name tracer)
	add_test_with_filter(${name} "" ${tracer} ${ARGN})
endfunction()

add_test_generic(basic none)
add_test_generic(basic memcheck)
add_test_generic(basic helgrind)
add_test_generic(basic pmemcheck)

add_test_generic(crash none)

add_test_generic(dirs none)
add_test_generic(dirs memcheck)
add_test_generic(dirs helgrind  -Dops=10)
add_test_generic(dirs drd)
add_test_generic(dirs pmemcheck -Dops=10)

add_test_generic(getdents none)
add_test_generic(getdents memcheck)
add_test_generic(getdents pmemcheck)

function(add_mt_test tracer ops)
	add_test_with_filter(mt open_close_create_unlink ${tracer} -Dops=${ops})
	add_test_with_filter(mt pread                    ${tracer} -Dops=${ops})
	add_test_with_filter(mt rename                   ${tracer} -Dops=${ops})
	add_test_with_filter(mt rename_random_paths      ${tracer} -Dops=${ops})
	add_test_with_filter(mt exchange_random_paths    ${tracer} -Dops=${ops})
endfunction()

if(LONG_TESTS)
	add_mt_test(none      100000)
	add_mt_test(memcheck  10000)
	add_mt_test(helgrind  500)
	add_mt_test(drd       2000)
	add_mt_test(pmemcheck 500)
else()
	add_mt_test(none      10000)
	add_mt_test(memcheck  10)
	add_mt_test(helgrind  20)
	add_mt_test(drd       20)
	add_mt_test(pmemcheck 50)
endif()

add_test_generic(openp none)
add_test_generic(openp memcheck)

add_test_generic(permissions none)
add_test_generic(permissions memcheck)
add_test_generic(permissions pmemcheck)

add_test_generic(rw none)
add_test_generic(rw none_blk4096 '' PMEMFILE_BLOCK_SIZE=4096)
add_test_generic(rw memcheck)
add_test_generic(rw pmemcheck)

add_test_generic(stat none)
add_test_generic(stat memcheck)
add_test_generic(stat pmemcheck)

add_test_generic(symlinks none)
add_test_generic(symlinks memcheck)

if(NOT LONG_TESTS)
	add_test(NAME SOME_TESTS_WERE_SKIPPED_BECAUSE_LONG_TESTS_ARE_DISABLED
		COMMAND true)
endif()
