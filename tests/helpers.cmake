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

# Prepares environment for test execution.
function(common_setup)
	execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${PARENT_DIR}/${TEST_NAME})
	execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${PARENT_DIR}/${TEST_NAME})
	execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${BIN_DIR})
	execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${BIN_DIR})
endfunction()

# Cleans up environment.
function(common_cleanup)
	execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${PARENT_DIR}/${TEST_NAME})
endfunction()

# Creates pool.
function(mkfs path size)
	unset(TEMP_STORE_LD_PRELOAD)
	if(ENV{LD_PRELOAD})
		set(TEMP_STORE_LD_PRELOAD ENV{LD_PRELOAD})
		unset(ENV{LD_PRELOAD})
	endif()
	execute_process(COMMAND ${MKFS_EXECUTABLE} ${path} ${size}
			RESULT_VARIABLE HAD_ERROR)
	if(HAD_ERROR)
		message(FATAL_ERROR "mkfs(${path}, ${size}) failed: ${HAD_ERROR}")
	endif()
	if(TEMP_STORE_LD_PRELOAD)
		set(ENV{LD_PRELOAD} ${TEMP_STORE_LD_PRELOAD})
	endif()
endfunction()

# Verifies ${log_file} matches ${match_file} using "match".
function(match log_file match_file)
	unset(TEMP_STORE_LD_PRELOAD)
	if(ENV{LD_PRELOAD})
		set(TEMP_STORE_LD_PRELOAD ENV{LD_PRELOAD})
		unset(ENV{LD_PRELOAD})
	endif()
	execute_process(COMMAND
			${PERL_EXECUTABLE} ${MATCH_SCRIPT} -o ${log_file} ${match_file}
			RESULT_VARIABLE MATCH_ERROR)

	if(MATCH_ERROR)
		message(FATAL_ERROR "Log does not match: ${MATCH_ERROR}")
	endif()
	if(TEMP_STORE_LD_PRELOAD)
		set(ENV{LD_PRELOAD} ${TEMP_STORE_LD_PRELOAD})
	endif()
endfunction()

# Gets the content of file using pmemfile-cat and returns in out variable.
function(pf_cat pool file out)
	unset(TEMP_STORE_LD_PRELOAD)
	if(ENV{LD_PRELOAD})
		set(TEMP_STORE_LD_PRELOAD ENV{LD_PRELOAD})
		unset(ENV{LD_PRELOAD})
	endif()
	execute_process(COMMAND ${CAT_EXECUTABLE} ${pool} ${file}
		OUTPUT_FILE ${out}
		RESULT_VARIABLE res)
	if(res)
		message(FATAL_ERROR "pmemfile-cat(${pool}, ${file}) failed: ${res}")
	endif()
	if(TEMP_STORE_LD_PRELOAD)
		set(ENV{LD_PRELOAD} ${TEMP_STORE_LD_PRELOAD})
	endif()
endfunction()

# Generic command executor which handles failures.
function(execute cmd)
	execute_process(COMMAND ${cmd} ${ARGN}
			RESULT_VARIABLE res)
	if(res)
		message(FATAL_ERROR "${cmd} ${ARGN} failed: ${res}")
	endif()
endfunction()

# Generic command executor which handles failures and returns command output.
function(execute_with_output out cmd)
	execute_process(COMMAND ${cmd} ${ARGN}
			OUTPUT_FILE ${out}
			RESULT_VARIABLE res)
	if(res)
		message(FATAL_ERROR "${cmd} ${ARGN} > ${out} failed: ${res}")
	endif()
endfunction()

# Executes command expecting it to fail.
function(execute_expect_failure cmd)
	execute_process(COMMAND ${cmd} ${ARGN}
			RESULT_VARIABLE res)
	if(NOT res)
		message(FATAL_ERROR "${cmd} ${ARGN} unexpectedly succeeded")
	endif()
endfunction()

# Compares 2 files using "cmp" command.
function(cmp file1 file2)
	execute(cmp ${file1} ${file2})
endfunction()

# Gets file content using "cat" command.
function(cat in out)
	execute_with_output(${out} cat ${in})
endfunction()

# List files in directory in semi-parsable format.
function(list_files out dir)
	execute_process(COMMAND ls -a -l -G -g --time-style=+ ${dir}
			COMMAND sed "s/\\s\\+/ /g"
			OUTPUT_FILE ${BIN_DIR}/${out}
			RESULT_VARIABLE res)
	if(res)
		message(FATAL_ERROR "ls ${dir} > ${out} failed: ${res}")
	endif()

	if(EXISTS ${SRC_DIR}/${out}.match)
		match(${BIN_DIR}/${out} ${SRC_DIR}/${out}.match)
	endif()
endfunction()

# Creates directory using "mkdir" command.
function(mkdir dir)
	execute(mkdir ${dir})
endfunction()

# Creates directory using "mkdir" command expecting it to fail.
function(mkdir_expect_failure dir)
	execute_expect_failure(mkdir ${dir})
endfunction()

# removes directory using "rmdir" command.
function(rmdir dir)
	execute(rmdir ${dir})
endfunction()
