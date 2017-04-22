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
        fd_pool.c
        path_resolve.c
        preload.c
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-field-initializers")

pkg_check_modules(SYSCALL_INTERCEPT libsyscall_intercept)

if(NOT SYSCALL_INTERCEPT_FOUND)
	message(FATAL_ERROR
		"libsyscall_intercept not found - needed by libpmemfile
		to skip building libpmemfile, set the BUILD_LIBPMEMFILE option to OFF")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# XXX OBJECT library type is not supported on some old cmake versions
add_library(pmemfile_o OBJECT ${SOURCES})

include_directories(${SYSCALL_INTERCEPT_INCLUDE_DIRS} ..)
link_directories(${SYSCALL_INTERCEPT_LIBRARY_DIRS})

add_library(pmemfile_shared SHARED $<TARGET_OBJECTS:pmemfile_o>)
add_library(pmemfile_static_unscoped STATIC $<TARGET_OBJECTS:pmemfile_o>)

target_link_libraries(pmemfile_shared ${SYSCALL_INTERCEPT_LIBRARIES})
target_link_libraries(pmemfile_shared pmemfile-core_shared)
target_link_libraries(pmemfile_shared -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libpmemfile.map)

set_target_properties(pmemfile_static_unscoped PROPERTIES OUTPUT_NAME pmemfile_unscoped)
set_target_properties(pmemfile_shared PROPERTIES OUTPUT_NAME pmemfile)
set_target_properties(pmemfile_shared PROPERTIES VERSION ${VERSION} SOVERSION ${VERSION_MAJOR})

add_custom_command(OUTPUT libpmemfile.a
                   COMMAND objcopy --localize-hidden `sed -n "'s/^\\s*\\([a-zA-Z0-9_]*\\);$$/-G \\1/p'" ${CMAKE_CURRENT_SOURCE_DIR}/libpmemfile.map` libpmemfile_unscoped.a libpmemfile.a
                   DEPENDS pmemfile_static_unscoped)
add_custom_target(pmemfile_static ALL DEPENDS libpmemfile.a)

install(TARGETS pmemfile_shared LIBRARY
        CONFIGURATIONS Release None
        DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(TARGETS pmemfile_shared LIBRARY
        CONFIGURATIONS Debug
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pmemfile_debug)

#install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile.a
#        CONFIGURATIONS Release None
#        DESTINATION ${CMAKE_INSTALL_LIBDIR})

#install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile.a
#        CONFIGURATIONS Debug
#        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pmemfile_debug)

add_checker_targets("pmemfile")
