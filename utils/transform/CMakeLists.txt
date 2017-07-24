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

add_cstyle(transform)

# Finc libclang. One could try to use find_package(clang) for this,
# but cmake config files in installed packages appear to be broken.
# The information provided by LLVMConfig.cmake seems to be enough
# to find libclang.
#
# Note: Before LLVM 4.0 the cmake files in the LLVM package are
# broken as well.
find_package(LLVM CONFIG REQUIRED)

find_library(LIBCLANG_PATH NAMES clang HINTS "${LLVM_LIBRARY_DIRS}")

add_library(clang UNKNOWN IMPORTED)

set_target_properties(clang PROPERTIES
	IMPORTED_LOCATION "${LIBCLANG_PATH}"
	INTERFACE_INCLUDE_DIRECTORIES "${LLVM_INCLUDE_DIRS}")

# The function_decl_finder library is a little wrapper library around
# libclang, to make it easier to use in this scenario.
add_library(function_decl_finder function_decl_finder.c)
target_link_libraries(function_decl_finder PRIVATE clang)

add_executable(transform_pmemfile_posix transform_pmemfile_posix.c)
target_link_libraries(transform_pmemfile_posix PRIVATE function_decl_finder)

add_custom_command(OUTPUT ${pmemfile_SOURCE_DIR}/src/libpmemfile/libpmemfile-posix-wrappers.h
		COMMAND $<TARGET_FILE:transform_pmemfile_posix>
			${pmemfile_SOURCE_DIR}/include/libpmemfile-posix.h
			${pmemfile_SOURCE_DIR}/src/libpmemfile/libpmemfile-posix-wrappers.h
			-xc
			-std=gnu99
			-D_LARGEFILE64_SOURCE
			-I${pmemfile_SOURCE_DIR}/include
		DEPENDS ${pmemfile_SOURCE_DIR}/include/libpmemfile-posix.h
			transform_pmemfile_posix
		WORKING_DIRECTORY "${LLVM_TOOLS_BINARY_DIR}")

add_custom_target(regenerate_libpmemfile-posix-wrappers
		DEPENDS ${pmemfile_SOURCE_DIR}/src/libpmemfile/libpmemfile-posix-wrappers.h)

add_executable(transform_pmemfile_posix2 transform_pmemfile_posix_level2.c)
target_link_libraries(transform_pmemfile_posix2 PRIVATE function_decl_finder)

add_custom_command(OUTPUT ${pmemfile_SOURCE_DIR}/src/libpmemfile/libpmemfile-posix-wrappers2.h
		COMMAND $<TARGET_FILE:transform_pmemfile_posix2>
			${pmemfile_SOURCE_DIR}/src/libpmemfile/libpmemfile-posix-wrappers.h
			${pmemfile_SOURCE_DIR}/src/libpmemfile/libpmemfile-posix-wrappers2.h
			-xc
			-std=gnu99
			-D_LARGEFILE64_SOURCE
			-I${pmemfile_SOURCE_DIR}/include
		DEPENDS ${pmemfile_SOURCE_DIR}/src/libpmemfile/libpmemfile-posix-wrappers.h
			transform_pmemfile_posix2
		WORKING_DIRECTORY "${LLVM_TOOLS_BINARY_DIR}")

add_custom_target(regenerate_libpmemfile-posix-wrappers2
		DEPENDS ${pmemfile_SOURCE_DIR}/src/libpmemfile/libpmemfile-posix-wrappers2.h)
