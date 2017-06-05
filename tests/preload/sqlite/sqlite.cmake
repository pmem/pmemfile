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

mkfs(${DIR}/fs 100m)

execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR}/mount_point)

set(ENV{PMEMFILE_POOLS} ${DIR}/mount_point:${DIR}/fs)
set(ENV{PMEMFILE_PRELOAD_LOG} ${BIN_DIR}/pmemfile_preload.log)
set(ENV{LIBC_HOOK_CMDLINE_FILTER} sqlite3)
set(ENV{INTERCEPT_LOG} ${BIN_DIR}/intercept.log)
set(ENV{PMEMFILE_EXIT_ON_NOT_SUPPORTED} 1)

set(ENV{LD_PRELOAD} ${PRELOAD_LIB})
execute_process(COMMAND sqlite3 ${DIR}/mount_point/sqlitedb
                INPUT_FILE ${SRC_DIR}/ins0.sql
                RESULT_VARIABLE sql_ins_res)
unset(ENV{LD_PRELOAD})

if (sql_ins_res)
        if (sql_ins_res EQUAL 95)
                message(WARNING "Insert crashed because of unsupported feature")
        else()
                message(FATAL_ERROR "Insert failed: ${sql_ins_res}")
        endif()
endif()

set(ENV{LD_PRELOAD} ${PRELOAD_LIB})
execute_process(COMMAND sqlite3 ${DIR}/mount_point/sqlitedb
                INPUT_FILE ${SRC_DIR}/sel0.sql
                OUTPUT_FILE ${BIN_DIR}/out0.log
                RESULT_VARIABLE sql_sel_res)

unset(ENV{LD_PRELOAD})

if (sql_sel_res)
        if (sql_sel_res EQUAL 95)
                message(WARNING "Select crashed because of unsupported feature")
        else()
                message(FATAL_ERROR "Select failed: ${sql_sel_res}")
        endif()
endif()

match(${BIN_DIR}/out0.log ${SRC_DIR}/out0.log.match)

cleanup()
