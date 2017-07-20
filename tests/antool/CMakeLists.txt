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

set(ANTOOL_SRC_DIR ${CMAKE_SOURCE_DIR}/tests/antool)

if(NOT ANTOOL_TESTS)
	set(ANTOOL_TESTS SAVED)
endif()

if(${ANTOOL_TESTS} STREQUAL SKIP)
	message(STATUS "skipping antool tests")

else()
	# check if bzip2 is installed
	execute_process(COMMAND which bzip2
			RESULT_VARIABLE BZIP2_FOUND
			ERROR_QUIET)

	if(${ANTOOL_TESTS} STREQUAL SAVED)
		if(${BZIP2_FOUND} EQUAL 0)
			set(VLTRACE_FOUND -1)
			set(VLTRACE_BIN "")
			set(DECOMPRESSOR ${ANTOOL_SRC_DIR}/decompress-bin-arch.sh)
			execute_process(COMMAND ${DECOMPRESSOR} ${ANTOOL_SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR})
		else()
			message(FATAL_ERROR "bzip2 compressor not found, antool tests cannot be performed.")
		endif()

	else()
		# check if vltrace is installed
		execute_process(COMMAND which vltrace
				RESULT_VARIABLE VLTRACE_FOUND
				OUTPUT_VARIABLE VLTRACE_BIN
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_QUIET)

		if(${VLTRACE_FOUND} EQUAL 0)
			# vltrace found
			add_executable(test_syscalls test_syscalls.c)
			target_link_libraries(test_syscalls ${CMAKE_THREAD_LIBS_INIT})
			set(TEST_APP $<TARGET_FILE:test_syscalls>)

			if(${BZIP2_FOUND} EQUAL 0)
				set(REGENERATOR ${ANTOOL_SRC_DIR}/regenerate-bin-arch.sh)
				add_custom_target(regenerate
						COMMAND ${REGENERATOR} ${ANTOOL_SRC_DIR})
			else()
				message(WARNING "bzip2 compressor not found, "
						"regeneration of antool test files will not be possible.")
			endif()

		else()
			message(FATAL_ERROR "vltrace not found, antool tests cannot be performed. "
				"Rerun cmake with one of the following options:\n"
				"   -DANTOOL_TESTS=SAVED - if you want to test antool using\n"
				"                          saved vltrace's binary logs or\n"
				"   -DANTOOL_TESTS=SKIP  - if you want to skip antool's tests")
		endif()
	endif()

	set(TEST_ANTOOL ${ANTOOL_SRC_DIR}/test-antool.sh)
	set(TEST_PARSER ${ANTOOL_SRC_DIR}/test-parser.sh)

	add_test(NAME "antool"
		COMMAND ${TEST_ANTOOL} "${VLTRACE_BIN}" "${TEST_APP}" 15)

	foreach(STR_LEN 126 382)
		# 1
		add_test(NAME "antool-${STR_LEN}-basic-syscalls"
			COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" ${STR_LEN} "${TEST_APP}" 0)

		# 2
		add_test(NAME "antool-${STR_LEN}-basic-syscalls-with-fork"
			COMMAND ${TEST_PARSER} -f "${VLTRACE_BIN}" ${STR_LEN} "${TEST_APP}" 1)

		# 3
		add_test(NAME "antool-${STR_LEN}-basic-syscalls-with-double-fork"
			COMMAND ${TEST_PARSER} -f "${VLTRACE_BIN}" ${STR_LEN} "${TEST_APP}" 5)

		# 4
		add_test(NAME "antool-${STR_LEN}-other-syscalls"
			COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" ${STR_LEN} "${TEST_APP}" 2)

		# 5
		add_test(NAME "antool-${STR_LEN}-other-syscalls-with-fork"
			COMMAND ${TEST_PARSER} -f "${VLTRACE_BIN}" ${STR_LEN} "${TEST_APP}" 3)

		# 6
		add_test(NAME "antool-${STR_LEN}-other-syscalls-with-double-fork"
			COMMAND ${TEST_PARSER} -f "${VLTRACE_BIN}" ${STR_LEN} "${TEST_APP}" 6)

		# 7
		add_test(NAME "antool-${STR_LEN}-syscall-vfork"
			COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" ${STR_LEN} "${TEST_APP}" 4)

		# 8
		add_test(NAME "antool-${STR_LEN}-syscall-sigaction"
			COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" ${STR_LEN} "${TEST_APP}" 7)
	endforeach(STR_LEN)

	add_test(NAME "antool-126-syscalls-string-arguments-126"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 126 "${TEST_APP}" 8)
	add_test(NAME "antool-126-syscalls-string-arguments-382"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 382 "${TEST_APP}" 8)
	add_test(NAME "antool-126-syscalls-string-arguments-765"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 765 "${TEST_APP}" 8)
	add_test(NAME "antool-126-syscalls-string-arguments-1148"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 1148 "${TEST_APP}" 8)
	add_test(NAME "antool-126-syscalls-string-arguments-1531"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 1531 "${TEST_APP}" 8)

	add_test(NAME "antool-382-syscalls-string-arguments-382"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 382 "${TEST_APP}" 9)
	add_test(NAME "antool-382-syscalls-string-arguments-765"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 765 "${TEST_APP}" 9)
	add_test(NAME "antool-382-syscalls-string-arguments-1148"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 1148 "${TEST_APP}" 9)
	add_test(NAME "antool-382-syscalls-string-arguments-1531"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 1531 "${TEST_APP}" 9)

	add_test(NAME "antool-765-syscalls-string-arguments-765"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 765 "${TEST_APP}" 10)
	add_test(NAME "antool-765-syscalls-string-arguments-1148"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 1148 "${TEST_APP}" 10)
	add_test(NAME "antool-765-syscalls-string-arguments-1531"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 1531 "${TEST_APP}" 10)

	add_test(NAME "antool-1148-syscalls-string-arguments-1148"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 1148 "${TEST_APP}" 11)
	add_test(NAME "antool-1148-syscalls-string-arguments-1531"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 1531 "${TEST_APP}" 11)

	add_test(NAME "antool-1531-syscalls-string-arguments-1531"
		COMMAND ${TEST_PARSER} "${VLTRACE_BIN}" 1531 "${TEST_APP}" 12)

	add_test(NAME "antool-1531-syscalls-string-arguments-with-fork"
		COMMAND ${TEST_PARSER} -f "${VLTRACE_BIN}" 1531 "${TEST_APP}" 13)
	add_test(NAME "antool-1531-syscalls-string-arguments-with-double-fork"
		COMMAND ${TEST_PARSER} -f "${VLTRACE_BIN}" 1531 "${TEST_APP}" 14)
endif()
