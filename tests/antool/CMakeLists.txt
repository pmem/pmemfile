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

include(ExternalProject)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
add_executable(test_syscalls test_syscalls.c)

set(VLTRACE_GIT_URL git@github.com:ldorau/vltrace.git)

set(VLTRACE_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/vltrace)
set(VLTRACE_BINARY_DIR ${VLTRACE_PREFIX}/src/vltrace-build)
set(VLTRACE_SOURCE_DIR ${VLTRACE_PREFIX}/src/vltrace)

set(VLTRACE_BIN ${VLTRACE_BINARY_DIR}/src/vltrace)
set(VLTRACE_TEST ${CMAKE_CURRENT_BINARY_DIR}/test_syscalls)

ExternalProject_Add(
	vltrace
	PREFIX ${VLTRACE_PREFIX}
	BINARY_DIR ${VLTRACE_BINARY_DIR}
	SOURCE_DIR ${VLTRACE_SOURCE_DIR}
	GIT_REPOSITORY ${VLTRACE_GIT_URL}
	INSTALL_COMMAND ""
	CMAKE_ARGS
		-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
		-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
		-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
		-G ${CMAKE_GENERATOR}
)

add_test(NAME "antool-1-vltrace"
		COMMAND ${VLTRACE_BIN} --help)
add_test(NAME "antool-2-test_syscalls"
		COMMAND ${VLTRACE_TEST} 0 dir path1 path2)
add_test(NAME "antool-3-analyzing-tool"
		COMMAND ${CMAKE_SOURCE_DIR}/tests/antool/test-antool ${VLTRACE_BIN} ${VLTRACE_TEST} 0)
