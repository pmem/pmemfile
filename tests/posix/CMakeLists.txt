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

function(build_test name posix_lib srcs)
	add_cppstyle(tests-posix-${name} ${CMAKE_CURRENT_SOURCE_DIR}/${srcs})
	add_check_whitespace(tests-posix-${name} ${CMAKE_CURRENT_SOURCE_DIR}/${srcs})
	add_executable(${name} ${srcs})
	target_link_libraries(${name} ${posix_lib} pmemfile_test test_backtrace)
	if(LIBUNWIND_FOUND)
		target_link_libraries(${name} ${LIBUNWIND_LIBRARIES} ${CMAKE_DL_LIBS})
	endif()
	add_dependencies(tests ${name})
endfunction()

function(build_test_using_shared name srcs)
	# Build an executable for testing, linked to the pmemfile-posix shared library
	build_test(${name} pmemfile-posix_shared ${srcs})
endfunction()

function(build_test_using_static name srcs)
	# Build an executable for testing, linked with the pmemfile-posix static library
	build_test(${name} pmemfile-posix_static ${srcs})
endfunction()

# We use basic/basic.cpp twice: running the same test once using the dynamically loaded
# shared library, and once using the static library (libpmemfile-posix.a).
build_test_using_shared(file_basic basic/basic.cpp)
if (NOT LIBPMEMFILE_POP)
	build_test_using_static(file_basic_using_static basic/basic.cpp)
endif ()
build_test_using_shared(file_pointer_caching pointer_caching/pointer_caching.cpp)
build_test_using_shared(file_crash crash/crash.cpp)
build_test_using_shared(file_dirs dirs/dirs.cpp)
build_test_using_shared(file_getdents getdents/getdents.cpp)
build_test_using_shared(file_mt mt/mt.cpp)
build_test_using_shared(file_openp openp/openp.cpp)
build_test_using_shared(file_permissions permissions/permissions.cpp)
build_test_using_shared(file_rw rw/rw.cpp)
build_test_using_shared(file_stat stat/stat.cpp)
build_test_using_shared(file_symlinks symlinks/symlinks.cpp)
build_test_using_shared(file_timestamps timestamps/timestamps.cpp)

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
endfunction()

function(add_test_generic name tracer)
	add_test_with_filter(${name} "" ${tracer} ${name} ${ARGN})
endfunction()

function(add_test_generic_with_exe name tracer executable)
	add_test_with_filter(${name} "" ${tracer} ${executable} ${ARGN})
endfunction()

add_test_generic(basic none)
if (NOT LIBPMEMFILE_POP)
	add_test_generic_with_exe(basic none basic_using_static)
endif ()
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

add_test_generic(getdents none)
add_test_generic(getdents memcheck)
add_test_generic(getdents pmemcheck)

function(add_mt_test tracer ops)
	add_test_with_filter(mt open_close_create_unlink ${tracer} "" -Dops=${ops})
	add_test_with_filter(mt pread                    ${tracer} "" -Dops=${ops})
	add_test_with_filter(mt rename                   ${tracer} "" -Dops=${ops})
	add_test_with_filter(mt rename_random_paths      ${tracer} "" -Dops=${ops})
	add_test_with_filter(mt exchange_random_paths    ${tracer} "" -Dops=${ops})
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
	add_test_generic(rw none_blk4096 '' PMEMFILE_BLOCK_SIZE=4096)
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
