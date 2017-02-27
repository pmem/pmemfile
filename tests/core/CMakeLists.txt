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

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing")

include_directories(.)
if(LIBUNWIND_FOUND)
        add_definitions(-DUSE_LIBUNWIND)
endif()

add_library(pmemfile_test STATIC pmemfile_test.c)
add_library(test_backtrace STATIC ../test_backtrace.c)

function(build_test name srcs)
        add_checker_targets("tests-core-${name}" ${CMAKE_CURRENT_SOURCE_DIR}/${srcs})
        add_executable(${name} ${srcs})
        target_link_libraries(${name} pmemfile-core_shared pmemfile_test test_backtrace)
        if(LIBUNWIND_FOUND)
                target_link_libraries(${name} ${LIBUNWIND_LIBRARIES} ${CMAKE_DL_LIBS})
        endif()
        add_dependencies(tests ${name})
endfunction()

build_test(file_basic basic/basic.c)
build_test(file_crash crash/crash.c)
build_test(file_dirs dirs/dirs.c)
build_test(file_getdents getdents/getdents.c)
build_test(file_mt mt/mt.c)
build_test(file_openp openp/openp.c)
build_test(file_rw rw/rw.c)
build_test(file_stat stat/stat.c)
build_test(file_symlinks symlinks/symlinks.c)

function(add_test_generic name tracer)
        set(vg_tracers memcheck helgrind drd pmemcheck)

        if ((NOT VALGRIND_FOUND) AND ${tracer} IN_LIST vg_tracers)
                add_test(NAME ${name}_${tracer}_SKIPPED_BECAUSE_OF_MISSING_VALGRIND
                         COMMAND true)
                return()
        endif()

        if(${tracer} STREQUAL pmemcheck)
                if (VALGRIND_PMEMCHECK_NOT_FOUND)
                        add_test(NAME ${name}_${tracer}_SKIPPED_BECAUSE_OF_MISSING_PMEMCHECK
                                 COMMAND true)
                        return()
                endif()
        endif()

        add_test(NAME ${name}_${tracer}
                 COMMAND ${CMAKE_COMMAND}
			${GLOBAL_TEST_ARGS}
			-DTEST_NAME=${name}_${tracer}
			-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
			-DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}_${tracer}
			-DTRACER=${tracer}
			${ARGV2}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/${name}/test.cmake)

        set_tests_properties(${name}_${tracer} PROPERTIES
                ENVIRONMENT "LC_ALL=C;PATH=$ENV{PATH};${ARGV3}")
endfunction()

function(add_long_test_generic name tracer)
        if (LONG_TESTS)
                add_test_generic(${name} ${tracer} ${ARGN})
        else()
                add_test(NAME ${name}_${tracer}_SKIPPED_BECAUSE_LONG_TESTS_ARE_DISABLED
                         COMMAND true)
        endif()
endfunction()

add_test_generic(basic none)
add_test_generic(basic memcheck)
add_test_generic(basic helgrind)
add_test_generic(basic pmemcheck)

add_test_generic(crash none)

add_test_generic(dirs none)
add_test_generic(dirs memcheck)
add_test_generic(dirs pmemcheck)

add_test_generic(getdents none)
add_test_generic(getdents memcheck)
add_test_generic(getdents pmemcheck)

add_test_generic(mt none      -Dops=10000)
add_test_generic(mt memcheck  -Dops=10)
add_test_generic(mt helgrind  -Dops=20)
add_test_generic(mt drd       -Dops=20)
add_test_generic(mt pmemcheck -Dops=50)

add_test_generic(openp none)
add_test_generic(openp memcheck)

add_test_generic(rw none)
add_test_generic(rw none_blk4096 '' PMEMFILECORE_BLOCK_SIZE=4096)
add_test_generic(rw memcheck)
add_long_test_generic(rw pmemcheck)

add_test_generic(stat none)
add_test_generic(stat memcheck)
add_test_generic(stat pmemcheck)

add_test_generic(symlinks none)
add_test_generic(symlinks memcheck)
