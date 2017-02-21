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

include(${SRC_DIR}/../helpers.cmake)

setup()

mkfs(${DIR}/fs 16m)

execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR}/mount_point)
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR}/some_dir)
execute_process(COMMAND ln -s ../mount_point ${DIR}/some_dir/some_link)

set(ENV{LD_PRELOAD} libpmemfile.so)
set(ENV{PMEMFILE_POOLS} ${DIR}/mount_point:${DIR}/fs)
set(ENV{PMEMFILE_PRELOAD_LOG} ${BIN_DIR}/pmemfile_preload.log)
set(ENV{INTERCEPT_LOG} ${BIN_DIR}/intercept.log)
set(ENV{PMEMFILE_EXIT_ON_NOT_SUPPORTED} 1)

execute_process(COMMAND ./preload_basic ${DIR}/some_dir/some_link/a ${DIR} mount_point/b mount_point b
                OUTPUT_FILE ${DIR}/root_dir.log
                RESULT_VARIABLE res)
if(res)
        message(FATAL_ERROR "Test1 command failed: ${res}")
endif()

pf_cat(${DIR}/fs /a ${DIR}/a.dump)

cmp(${DIR}/a.dump ${SRC_DIR}/a.expected_dump)

cmp(${DIR}/root_dir.log ${SRC_DIR}/root_dir.expected_log)

cleanup()
