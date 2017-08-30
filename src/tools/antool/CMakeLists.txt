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

set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
set(DEPS "${CMAKE_CURRENT_SOURCE_DIR}/antoollib/__init__.py")

set(SETUP_PY "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
set(TIMESTAMP "${CMAKE_CURRENT_BINARY_DIR}/build/timestamp")

set(MANIFEST "install_manifest.txt")
set(MAIN_MANIFEST "${CMAKE_BINARY_DIR}/${MANIFEST}")

configure_file(${SETUP_PY_IN} ${SETUP_PY})

add_custom_command(
		COMMAND ${PYTHON} ${SETUP_PY} build
		COMMAND ${CMAKE_COMMAND} -E touch ${TIMESTAMP}
		OUTPUT  ${TIMESTAMP}
		DEPENDS ${DEPS})

add_custom_target(target ALL DEPENDS ${TIMESTAMP})

if(${CMAKE_BUILD_TYPE} STREQUAL Release OR
   ${CMAKE_BUILD_TYPE} STREQUAL None OR
   ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
	install(CODE "
		execute_process(COMMAND ${PYTHON} ${SETUP_PY} install --prefix=\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX} --record=${MANIFEST})
		file(READ ${MANIFEST} BUFFER)
		file(APPEND ${MAIN_MANIFEST} \"\${BUFFER}\")
	")
endif()
