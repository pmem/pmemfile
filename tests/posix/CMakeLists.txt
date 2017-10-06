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

# XXX Needed for using libpmemfile-posix_static
link_directories(${PMEMOBJ_LIBRARY_DIRS})

add_c_flag(-fno-strict-aliasing)

# GTest macros trigger this warning with GCC7. The code is correct
# but looks suspicious to GCC.
add_flag(-Wno-dangling-else)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(ExternalProject)
set(GTEST_VERSION 1.8.0)

if(EXISTS ${CMAKE_SOURCE_DIR}/googletest-${GTEST_VERSION}.zip)
	set(GTEST_URL ${CMAKE_SOURCE_DIR}/googletest-${GTEST_VERSION}.zip)
else()
	set(GTEST_URL https://github.com/google/googletest/archive/release-${GTEST_VERSION}.zip)
endif()

set(GTEST_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest)
set(GTEST_BINARY_DIR ${GTEST_PREFIX}/src/gtest-build)
set(GTEST_SOURCE_DIR ${GTEST_PREFIX}/src/gtest)

ExternalProject_Add(
	gtest
	URL ${GTEST_URL}
	URL_HASH SHA256=f3ed3b58511efd272eb074a3a6d6fb79d7c2e6a0e374323d1e6bcbcc1ef141bf
	PREFIX ${GTEST_PREFIX}
	BINARY_DIR ${GTEST_BINARY_DIR}
	SOURCE_DIR ${GTEST_SOURCE_DIR}
	DOWNLOAD_NAME googletest-${GTEST_VERSION}.zip
	DOWNLOAD_DIR ${CMAKE_SOURCE_DIR}
	INSTALL_COMMAND ""
	CMAKE_ARGS
		-DBUILD_GTEST=ON
		-DBUILD_GMOCK=OFF
		-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
		-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
		-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
		-G ${CMAKE_GENERATOR}
	BUILD_BYPRODUCTS "${GTEST_BINARY_DIR}/googletest/${CMAKE_STATIC_LIBRARY_PREFIX}gtest${CMAKE_STATIC_LIBRARY_SUFFIX}"
)
add_library(libgtest IMPORTED STATIC GLOBAL)
add_dependencies(libgtest gtest)
set_target_properties(libgtest PROPERTIES
	IMPORTED_LOCATION "${GTEST_BINARY_DIR}/googletest/${CMAKE_STATIC_LIBRARY_PREFIX}gtest${CMAKE_STATIC_LIBRARY_SUFFIX}"
	IMPORTED_LINK_INTERFACE_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}"
)
include_directories("${GTEST_SOURCE_DIR}/googletest/include")

include_directories(.)
if(LIBUNWIND_FOUND)
	add_definitions(-DUSE_LIBUNWIND)
endif()

add_library(pmemfile_test STATIC pmemfile_test.cpp)
add_library(test_backtrace STATIC ../test_backtrace.c)
target_link_libraries(pmemfile_test PRIVATE libgtest)

add_cppstyle("tests-posix-test")
add_check_whitespace("tests-posix-test" ${CMAKE_CURRENT_SOURCE_DIR}/*.*pp)

add_cstyle(tests-posix-test_bt ${CMAKE_CURRENT_SOURCE_DIR}/../test_backtrace.c)
add_check_whitespace(tests-posix-test_bt ${CMAKE_CURRENT_SOURCE_DIR}/../test_backtrace.c)

function(compile_test_source name srcs)
	add_cppstyle(tests-posix-${name} ${CMAKE_CURRENT_SOURCE_DIR}/${srcs})
	add_check_whitespace(tests-posix-${name} ${CMAKE_CURRENT_SOURCE_DIR}/${srcs})
	add_library(${name} OBJECT ${srcs})
	add_dependencies(${name} libgtest)
endfunction()

compile_test_source(file_basic_o basic/basic.cpp)
compile_test_source(file_pointer_caching_o pointer_caching/pointer_caching.cpp)
compile_test_source(file_crash_o crash/crash.cpp)
compile_test_source(file_dirs_o dirs/dirs.cpp)
compile_test_source(file_fcntl_o fcntl/fcntl.cpp)
compile_test_source(file_getdents_o getdents/getdents.cpp)
compile_test_source(file_mt_o mt/mt.cpp)
compile_test_source(file_openp_o openp/openp.cpp)
compile_test_source(file_permissions_o permissions/permissions.cpp)
compile_test_source(file_rw_o rw/rw.cpp)
compile_test_source(file_stat_o stat/stat.cpp)
compile_test_source(file_symlinks_o symlinks/symlinks.cpp)
compile_test_source(file_timestamps_o timestamps/timestamps.cpp)

function(build_test name posix_lib obj_lib_name)
	add_executable(${name} $<TARGET_OBJECTS:${obj_lib_name}>)
	target_link_libraries(${name} ${posix_lib} pmemfile_test test_backtrace)
	if(LIBUNWIND_FOUND)
		target_link_libraries(${name} ${LIBUNWIND_LIBRARIES} ${CMAKE_DL_LIBS})
	endif()
	add_dependencies(tests ${name})
endfunction()

function(build_test_using_shared name obj_lib_name)
	# Build an executable for testing, linked to the pmemfile-posix shared library
	build_test(${name} pmemfile-posix_shared ${obj_lib_name})
	if(BUILD_LIBPMEMFILE_POP)
		build_test(${name}_using_pop pmemfile-pop_shared ${obj_lib_name})
	endif()
endfunction()

function(build_test_using_static name obj_lib_name)
	# Build an executable for testing, linked with the pmemfile-posix static library
	build_test(${name} pmemfile-posix_static ${obj_lib_name})
endfunction()


build_test_using_shared(file_basic file_basic_o)
build_test_using_static(file_basic_using_static file_basic_o)
build_test_using_shared(file_pointer_caching file_pointer_caching_o)
build_test_using_shared(file_crash file_crash_o)
build_test_using_shared(file_dirs file_dirs_o)
build_test_using_shared(file_fcntl file_fcntl_o)
build_test_using_shared(file_getdents file_getdents_o)
build_test_using_shared(file_mt file_mt_o)
build_test_using_shared(file_openp file_openp_o)
build_test_using_shared(file_permissions file_permissions_o)
build_test_using_shared(file_rw file_rw_o)
build_test_using_shared(file_stat file_stat_o)
build_test_using_shared(file_symlinks file_symlinks_o)
build_test_using_shared(file_timestamps file_timestamps_o)

# Configures test ${name} using tracer ${tracer} and gtest filter ${filter}
# Optional next argument is passed as is to test.
# Optional next argument is appended to environment variables.
function(add_test_with_filter name filter tracer executable)
	if ("${executable}" STREQUAL "")
		set(executable ${name})
	endif()

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

	if ((USE_ASAN OR USE_UBSAN) AND ${tracer} IN_LIST vg_tracers)
		return()
	endif()

	add_test(NAME ${executable}${filter_postfix}_${tracer}
		COMMAND ${CMAKE_COMMAND}
			${GLOBAL_TEST_ARGS}
			-DTEST_NAME=${executable}${filter_postfix}_${tracer}
			-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
			-DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}/${executable}${filter_postfix}_${tracer}
			-DTEST_EXECUTABLE=$<TARGET_FILE:file_${executable}>
			-DTRACER=${tracer}
			-DLONG_TESTS=${LONG_TESTS}
			${ARGV4}
			${filter_cmd}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.cmake)

	set_tests_properties(${name}${filter_postfix}_${tracer} PROPERTIES
		ENVIRONMENT "LC_ALL=C;PATH=$ENV{PATH};${ARGV4}"
		FAIL_REGULAR_EXPRESSION Sanitizer)

	# pmemcheck is a special snowflake and it doesn't set exit code when it
	# detects an error, so we have to look at its output
	if (${tracer} STREQUAL pmemcheck)
		set_tests_properties(${name}${filter_postfix}_${tracer} PROPERTIES
			PASS_REGULAR_EXPRESSION "ERROR SUMMARY: 0")
	endif()

	if (${tracer} STREQUAL pmemcheck)
		set_tests_properties(${name}${filter_postfix}_${tracer} PROPERTIES
			COST 100)
	elseif(${tracer} IN_LIST vg_tracers)
		set_tests_properties(${name}${filter_postfix}_${tracer} PROPERTIES
			COST 50)
	endif()
endfunction()

function(add_test_generic name tracer)
	add_test_with_filter(${name} "" ${tracer} ${name} ${ARGN})
	if(BUILD_LIBPMEMFILE_POP)
		add_test_with_filter(${name} "" ${tracer} ${name}_using_pop ${ARGN})
	endif()
endfunction()

function(add_test_generic_with_exe name tracer executable)
	add_test_with_filter(${name} "" ${tracer} ${executable} ${ARGN})
endfunction()

add_test_generic(basic none)
add_test_generic_with_exe(basic none basic_using_static)
add_test_generic(basic memcheck)
add_test_generic(basic helgrind)
add_test_generic(basic pmemcheck)

# Reproducing pointer caching issues can be rather tricky, one
# would need to rely on very specific details of pmemfile-posix
# and pmemobj to do it without valgrind.
add_test_generic(pointer_caching memcheck)
add_test_generic(pointer_caching helgrind)
add_test_generic(pointer_caching pmemcheck)

add_test_generic(crash none)

add_test_generic(dirs none)
add_test_generic(dirs memcheck)
add_test_generic(dirs helgrind -Dops=10)
add_test_generic(dirs drd)
add_test_generic(dirs pmemcheck -Dops=10)

add_test_generic(fcntl none)

add_test_generic(getdents none)
add_test_generic(getdents memcheck)
add_test_generic(getdents pmemcheck)

function(add_mt_test tracer ops)
	add_test_with_filter(mt open_close_create_unlink ${tracer} "" -Dops=${ops})
	add_test_with_filter(mt pread                    ${tracer} "" -Dops=${ops})
	add_test_with_filter(mt rename                   ${tracer} "" -Dops=${ops})
	add_test_with_filter(mt rename_random_paths      ${tracer} "" -Dops=${ops})
	add_test_with_filter(mt exchange_random_paths    ${tracer} "" -Dops=${ops})

	if(BUILD_LIBPMEMFILE_POP)
		add_test_with_filter(mt open_close_create_unlink ${tracer} "mt_using_pop" -Dops=${ops})
		add_test_with_filter(mt pread                    ${tracer} "mt_using_pop" -Dops=${ops})
		add_test_with_filter(mt rename                   ${tracer} "mt_using_pop" -Dops=${ops})
		add_test_with_filter(mt rename_random_paths      ${tracer} "mt_using_pop" -Dops=${ops})
		add_test_with_filter(mt exchange_random_paths    ${tracer} "mt_using_pop" -Dops=${ops})
	endif()
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
if(NOT LIBPMEMFILE_POP)
	add_test_generic(rw none_blk16384 '' PMEMFILE_BLOCK_SIZE=16384)
endif()
add_test_generic(rw memcheck)
add_test_generic(rw pmemcheck)

add_test_generic(stat none)
add_test_generic(stat memcheck)
add_test_generic(stat pmemcheck)

add_test_generic(symlinks none)
add_test_generic(symlinks memcheck)

add_test_generic(timestamps none)
add_test_generic(timestamps memcheck)

if(NOT LONG_TESTS)
	add_test(NAME SOME_TESTS_WERE_SKIPPED_BECAUSE_LONG_TESTS_ARE_DISABLED
		COMMAND true)
endif()
