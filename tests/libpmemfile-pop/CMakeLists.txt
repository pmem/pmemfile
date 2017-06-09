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

set(SOURCES
	pmemfilepool.c
	utils.c
)

set(HEADERS
	pmemfilepool.h
	utils.h
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(pmemfile-posix_o OBJECT ${SOURCES})

add_library(pmemfile-posix_shared SHARED $<TARGET_OBJECTS:pmemfile-posix_o>)
target_include_directories(pmemfile-posix_o PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../../src)

target_link_libraries(pmemfile-posix_shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(pmemfile-posix_shared PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/../../src/libpmemfile-posix/libpmemfile-posix.map)
set_target_properties(pmemfile-posix_shared PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../src/libpmemfile-posix/libpmemfile-posix.map)

set_target_properties(pmemfile-posix_shared PROPERTIES OUTPUT_NAME pmemfile-posix)
set_target_properties(pmemfile-posix_shared PROPERTIES VERSION ${VERSION} SOVERSION ${VERSION_MAJOR})

add_cstyle(pmemfile-posix)
add_check_whitespace(pmemfile-posix)
