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

function(execute name)
        if(${TRACER} STREQUAL pmemcheck)
                set(TRACE valgrind --error-exitcode=99 --tool=pmemcheck)
        elseif(${TRACER} STREQUAL memcheck)
                set(TRACE valgrind --error-exitcode=99 --tool=memcheck --leak-check=full --suppressions=${SRC_DIR}/../../ld.supp)
        elseif(${TRACER} STREQUAL helgrind)
                set(TRACE valgrind --error-exitcode=99 --tool=helgrind)
        elseif(${TRACER} STREQUAL drd)
                set(TRACE valgrind --error-exitcode=99 --tool=drd)
        elseif(${TRACER} STREQUAL kgdb)
                set(TRACE konsole -e cgdb --args)
        elseif(${TRACER} MATCHES "none.*")
                # nothing
        else()
                message(FATAL_ERROR "Unknown tracer '${TRACER}'")
        endif()

        string(REPLACE ";" " " TRACE_STR "${TRACE}")
        message(STATUS "Executing: ${TRACE_STR} ./${name} ${DIR}/testfile1 ${ARGV1}")

        execute_process(COMMAND ${TRACE} ./${name} ${DIR}/testfile1 ${ARGV1}
                        RESULT_VARIABLE HAD_ERROR)

        if(HAD_ERROR)
                message(FATAL_ERROR "Test ${name} failed: ${HAD_ERROR}")
        endif()
endfunction()
