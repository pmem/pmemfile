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

if(PKG_CONFIG_FOUND)
	pkg_check_modules(FUSE QUIET fuse)
else()
	find_package(FUSE QUIET)
endif()

if (NOT FUSE_FOUND)
	message(FATAL_ERROR
		"Fuse not found - needed by pmemfile-fuse.
		To skip building it, set BUILD_PMEMFILE_FUSE option to OFF")
endif()

include_directories(${FUSE_INCLUDE_DIRS})
link_directories(${FUSE_LIBRARY_DIRS})

add_executable(pmemfile-fuse main.c)
target_link_libraries(pmemfile-fuse PRIVATE ${FUSE_LIBRARIES})
target_compile_definitions(pmemfile-fuse PRIVATE FUSE_USE_VERSION=28)
target_compile_definitions(pmemfile-fuse PRIVATE _FILE_OFFSET_BITS=64)
target_link_libraries(pmemfile-fuse PRIVATE pmemfile-posix_shared)

install(TARGETS pmemfile-fuse
	CONFIGURATIONS Release None RelWithDebInfo
	DESTINATION ${CMAKE_INSTALL_BINDIR}
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

add_cstyle(pmemfile-fuse ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_check_whitespace(pmemfile-fuse ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
