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

add_executable(preload_dummy_prog preload/dummy_prog.c)

add_checker_targets("tests-preload" ${CMAKE_CURRENT_SOURCE_DIR}/preload/*.c)

function(add_test_generic name)
        add_test(NAME ${name}
                 COMMAND ${CMAKE_COMMAND} ${GLOBAL_TEST_ARGS} -DTEST_NAME=${name} -DPARENT_DIR=${TEST_DIR}/ -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name} -DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name} -P ${CMAKE_CURRENT_SOURCE_DIR}/${name}/test.cmake)

        set_tests_properties(${name} PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/src/libpmemfile:${CMAKE_BINARY_DIR}/src/libpmemfile-core;${ARGV2}")
endfunction()

find_program(SQLITE3 sqlite3)
if (SQLITE3)
        add_test_generic(sqlite)
else()
        add_test(NAME sqlite_SKIPPED_BECAUSE_OF_MISSING_SQLITE3 COMMAND true)
endif()
