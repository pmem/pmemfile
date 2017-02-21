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

set(DIR ${PARENT_DIR}/${TEST_NAME})
set(MATCH_SCRIPT ${SRC_DIR}/../../match)

function(setup)
        execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${PARENT_DIR}/${TEST_NAME})
        execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${PARENT_DIR}/${TEST_NAME})
        execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${BIN_DIR})
        execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${BIN_DIR})
endfunction()

function(cleanup)
        execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${PARENT_DIR}/${TEST_NAME})
endfunction()

function(mkfs path size)
        execute_process(COMMAND ${BIN_DIR}/../../../src/tools/mkfs.pmemfile ${path} ${size}
                        RESULT_VARIABLE HAD_ERROR)
        if(HAD_ERROR)
                message(FATAL_ERROR "mkfs(${path}, ${size}) failed: ${HAD_ERROR}")
        endif()
endfunction()

function(match log_file match_file)
        execute_process(COMMAND
                        ${MATCH_SCRIPT} -o ${log_file} ${match_file}
                        RESULT_VARIABLE MATCH_ERROR)

        if(MATCH_ERROR)
                message(FATAL_ERROR "Log does not match: ${MATCH_ERROR}")
        endif()
endfunction()

function(pf_cat pool file out)
        execute_process(COMMAND ${BIN_DIR}/../../../src/tools/pmemfile-cat ${pool} ${file}
                OUTPUT_FILE ${out}
                RESULT_VARIABLE res)
        if(res)
                message(FATAL_ERROR "pmemfile-cat(${pool}, ${file}) failed: ${res}")
        endif()
endfunction()

function(execute cmd)
        execute_process(COMMAND ${cmd} ${ARGN}
                        RESULT_VARIABLE res)
        if(res)
                message(FATAL_ERROR "${cmd} ${ARGN} failed: ${res}")
        endif()
endfunction()

function(execute_with_output out cmd)
        execute_process(COMMAND ${cmd} ${ARGN}
                        OUTPUT_FILE ${out}
                        RESULT_VARIABLE res)
        if(res)
                message(FATAL_ERROR "${cmd} ${ARGN} > ${out} failed: ${res}")
        endif()
endfunction()

function(execute_expect_failure cmd)
        execute_process(COMMAND ${cmd} ${ARGN}
                        RESULT_VARIABLE res)
        if(NOT res)
                message(FATAL_ERROR "${cmd} ${ARGN} unexpectedly succeeded")
        endif()
endfunction()

function(cmp file1 file2)
        execute(cmp ${file1} ${file2})
endfunction()

function(cat in out)
        execute_with_output(${out} cat ${in})
endfunction()

function(list_files out dir)
        execute_process(COMMAND ls -a -l -G -g --time-style=+ ${dir}
                        COMMAND sed "s/\\s\\+/ /g"
                        OUTPUT_FILE ${BIN_DIR}/${out}
                        RESULT_VARIABLE res)
        if(res)
                message(FATAL_ERROR "ls ${dir} > ${out} failed: ${res}")
        endif()
        if(EXISTS ${SRC_DIR}/${out}.match)
                execute_process(COMMAND
                ${MATCH_SCRIPT} -o ${BIN_DIR}/${out} ${SRC_DIR}/${out}.match
                RESULT_VARIABLE MATCH_ERROR)

                if(MATCH_ERROR)
                        message(FATAL_ERROR "Log does not match: ${MATCH_ERROR}")
                endif()
        endif()
endfunction()

function(mkdir dir)
        execute(mkdir ${dir})
endfunction()

function(mkdir_expect_failure dir)
        execute_expect_failure(mkdir ${dir})
endfunction()

function(rmdir dir)
        execute(rmdir ${dir})
endfunction()
