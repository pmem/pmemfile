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

include(${SRC_DIR}/../preload-helpers.cmake)

setup()

mkfs(${DIR}/fs 16m)

execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR}/mount_point)
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR}/some_dir)
execute_process(COMMAND ln -s ../mount_point ${DIR}/some_dir/some_link)

set(ENV{LD_PRELOAD} ${PRELOAD_LIB})
set(ENV{PMEMFILE_POOLS} ${DIR}/mount_point:${DIR}/fs)
set(ENV{PMEMFILE_PRELOAD_LOG} ${BIN_DIR}/pmemfile_preload.log)
set(ENV{INTERCEPT_LOG} ${BIN_DIR}/intercept.log)

execute_expect_failure(${MAIN_EXECUTABLE} ${DIR}/some_dir/some_link/a)

cleanup()
