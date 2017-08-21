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

set(EXPORTED_SYMBOLS
	pmemfile_access
	pmemfile_chdir
	pmemfile_chmod
	pmemfile_chown
	pmemfile_close
	pmemfile_clrcap
	pmemfile_copy_file_range
	pmemfile_create
	pmemfile_dup
	pmemfile_dup2
	pmemfile_errormsg
	pmemfile_euidaccess
	pmemfile_faccessat
	pmemfile_fallocate
	pmemfile_fchdir
	pmemfile_fchmod
	pmemfile_fchmodat
	pmemfile_fchown
	pmemfile_fchownat
	pmemfile_fcntl
	pmemfile_flock
	pmemfile_fstat
	pmemfile_fstatat
	pmemfile_ftruncate
	pmemfile_futimens
	pmemfile_futimes
	pmemfile_futimesat
	pmemfile_getcwd
	pmemfile_getdents
	pmemfile_getdents64
	pmemfile_getegid
	pmemfile_geteuid
	pmemfile_getgid
	pmemfile_getgroups
	pmemfile_getuid
	pmemfile_get_dir_path
	pmemfile_lchown
	pmemfile_link
	pmemfile_linkat
	pmemfile_lseek
	pmemfile_lstat
	pmemfile_lstat
	pmemfile_lutimes
	pmemfile_mkdir
	pmemfile_mkdirat
	pmemfile_mknodat
	pmemfile_open_parent
	pmemfile_open
	pmemfile_openat
	pmemfile_pool_close
	pmemfile_pool_create
	pmemfile_pool_open
	pmemfile_posix_fallocate
	pmemfile_pread
	pmemfile_preadv
	pmemfile_pwrite
	pmemfile_pwritev
	pmemfile_read
	pmemfile_readlink
	pmemfile_readlinkat
	pmemfile_readv
	pmemfile_rename
	pmemfile_renameat
	pmemfile_renameat2
	pmemfile_rmdir
	pmemfile_setcap
	pmemfile_setgroups
	pmemfile_setegid
	pmemfile_seteuid
	pmemfile_setfsgid
	pmemfile_setfsuid
	pmemfile_setreuid
	pmemfile_setregid
	pmemfile_setgid
	pmemfile_setuid
	pmemfile_stat
	pmemfile_symlink
	pmemfile_symlinkat
	pmemfile_truncate
	pmemfile_umask
	pmemfile_unlink
	pmemfile_unlinkat
	pmemfile_utimensat
	pmemfile_utime
	pmemfile_utimes
	pmemfile_write
	pmemfile_writev

	pmemfile_stats

	pmemfile_inject_fault_at)

join(";\n\t\t" LINKER_SCRIPT_EXPORTED_SYMBOLS "${EXPORTED_SYMBOLS}")
join(";-G;" OBJDUMP_EXPORTED_SYMBOLS "${EXPORTED_SYMBOLS}")
set(OBJDUMP_EXPORTED_SYMBOLS "-G;${OBJDUMP_EXPORTED_SYMBOLS}")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../../src/libpmemfile-posix/libpmemfile-posix.map.in libpmemfile-posix.map)

add_library(pmemfile-posix_shared SHARED ${SOURCES})
target_include_directories(pmemfile-posix_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../../src)

target_link_libraries(pmemfile-posix_shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(pmemfile-posix_shared PRIVATE -Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-posix.map)
set_target_properties(pmemfile-posix_shared PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-posix.map)

set_target_properties(pmemfile-posix_shared PROPERTIES OUTPUT_NAME pmemfile-posix)
set_target_properties(pmemfile-posix_shared PROPERTIES VERSION ${VERSION} SOVERSION ${VERSION_MAJOR})

add_cstyle(pmemfile-posix)
add_check_whitespace(pmemfile-posix)
