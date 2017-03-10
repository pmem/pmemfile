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
	callbacks.c
	data.c
	dir.c
	file.c
	inode.c
	inode_array.c
	locks.c
	pmemfile-core.c
	pool.c
	stubs.c
	out.c
	ctree.c
	consts.c
	os_thread_pthread.c
)

set(HEADERS
        callbacks.h
        ctree.h
        data.h
        dir.h
        file.h
        inode_array.h
        inode.h
        internal.h
        layout.h
        locks.h
        out.h
        pool.h
        valgrind_internal.h
)

add_c_flag(-Wmissing-field-initializers)

if(PKG_CONFIG_FOUND)
	pkg_check_modules(PMEMOBJ REQUIRED libpmemobj)
else()
	find_package(PMEMOBJ REQUIRED)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(pmemfile-core_o OBJECT ${SOURCES})

include_directories(${PMEMOBJ_INCLUDE_DIRS} .. .)
link_directories(${PMEMOBJ_LIBRARY_DIRS})

add_library(pmemfile-core_shared SHARED $<TARGET_OBJECTS:pmemfile-core_o>)
add_library(pmemfile-core_static_unscoped STATIC $<TARGET_OBJECTS:pmemfile-core_o>)

target_link_libraries(pmemfile-core_shared PRIVATE ${PMEMOBJ_LIBRARIES})
target_link_libraries(pmemfile-core_shared PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libpmemfile-core.map)

set_target_properties(pmemfile-core_static_unscoped PROPERTIES OUTPUT_NAME pmemfile-core_unscoped)
set_target_properties(pmemfile-core_shared PROPERTIES OUTPUT_NAME pmemfile-core)
set_target_properties(pmemfile-core_shared PROPERTIES VERSION ${VERSION} SOVERSION ${VERSION_MAJOR})

if(NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set_target_properties(pmemfile-core_shared PROPERTIES
                PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/include/libpmemfile-core.h;${CMAKE_SOURCE_DIR}/include/libpmemfile-core-stubs.h")
endif()

add_custom_command(OUTPUT libpmemfile-core.a
                   COMMAND objcopy --localize-hidden `sed -n "'s/^\\s*\\([a-zA-Z0-9_]*\\);$$/-G \\1/p'" ${CMAKE_CURRENT_SOURCE_DIR}/libpmemfile-core.map` libpmemfile-core_unscoped.a libpmemfile-core.a
                   DEPENDS pmemfile-core_static_unscoped)
add_custom_target(pmemfile-core_static ALL DEPENDS libpmemfile-core.a)

configure_file(libpmemfile-core.pc.in libpmemfile-core.pc)

install(TARGETS pmemfile-core_shared
	CONFIGURATIONS Release None
	DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS pmemfile-core_shared
	CONFIGURATIONS Debug
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/pmemfile_debug
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-core.a
        CONFIGURATIONS Release None
        DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-core.a
        CONFIGURATIONS Debug
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pmemfile_debug)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-core.pc
        CONFIGURATIONS Release None
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

add_cstyle(pmemfile-core)
add_check_whitespace(pmemfile-core)
