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

include(${SRC_DIR}/../../helpers.cmake)

function(setup size)
	if (USE_FUSE)
		# Cleanup after previous failure. We have to do it before
		# common_setup, because unlinking directory with a mount point
		# fails with EBUSY.
		execute_process(COMMAND ${FUSERMOUNT} -u ${DIR}/mount_point)
	endif()

	common_setup()

	if (TEST_PROCESS_SWITCHING)
		set(ENV{PMEMFILE_PRELOAD_PROCESS_SWITCHING} 1)
	endif()

	if(TESTS_USE_FORCED_PMEM)
		set(ENV{PMEM_IS_PMEM_FORCE} 1)
	endif()

	mkfs(${DIR}/fs ${size})

	execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR}/mount_point)

	if (USE_FUSE)
		execute(${PMEMFILE_FUSE} -b ${DIR}/fs ${DIR}/mount_point)
	else()
		set(ENV{LD_PRELOAD} ${PRELOAD_LIB})
		set(ENV{PMEMFILE_POOLS} ${DIR}/mount_point:${DIR}/fs)
		set(ENV{PMEMFILE_PRELOAD_LOG} ${BIN_DIR}/pmemfile_preload.log)
		set(ENV{INTERCEPT_LOG} ${BIN_DIR}/intercept.log)
	endif()
endfunction()

function(cleanup)
	if (USE_FUSE)
		execute(${FUSERMOUNT} -z -u ${DIR}/mount_point)
	else()
		unset(ENV{LD_PRELOAD})
	endif()

	if(TESTS_USE_FORCED_PMEM)
		unset(ENV{PMEM_IS_PMEM_FORCE})
	endif()

	common_cleanup()
endfunction()
