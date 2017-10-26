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
	access.c
	block_array.c
	blocks.c
	callbacks.c
	chdir.c
	chmod.c
	chown.c
	copy_file_range.c
	creds.c
	data.c
	dir.c
	fallocate.c
	fcntl.c
	file.c
	flock.c
	getdents.c
	hash_map.c
	inode.c
	inode_array.c
	link.c
	locks.c
	lseek.c
	mkdir.c
	mknod.c
	mmap.c
	offset_mapping.c
	os_thread_pthread.c
	os_util_linux.c
	out.c
	pmemfile-posix.c
	pool.c
	read.c
	readlink.c
	rename.c
	rmdir.c
	stat.c
	statfs.c
	stats.c
	symlink.c
	timestamps.c
	truncate.c
	unlink.c
	utils.c
	write.c
)

set(EXPORTED_SYMBOLS
	pmemfile_access
	pmemfile_chdir
	pmemfile_chmod
	pmemfile_chown
	pmemfile_close
	pmemfile_clrcap
	pmemfile_copy_file_range
	pmemfile_create
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
	pmemfile_mmap
	pmemfile_mprotect
	pmemfile_mremap
	pmemfile_msync
	pmemfile_munmap
	pmemfile_open_parent
	pmemfile_open
	pmemfile_open_root
	pmemfile_openat
	pmemfile_pool_close
	pmemfile_pool_create
	pmemfile_pool_open
	pmemfile_pool_resume
	pmemfile_pool_root_count
	pmemfile_pool_set_device
	pmemfile_pool_suspend
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
	pmemfile_statfs
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

	pmemfile_stats)

if(FAULT_INJECTION)
	set(SOURCES ${SOURCES} alloc.c)
	set(EXPORTED_SYMBOLS ${EXPORTED_SYMBOLS} _pmemfile_inject_fault_at _pmemfile_fault_injection_enabled)
endif()

if(PKG_CONFIG_FOUND)
	pkg_check_modules(PMEMOBJ REQUIRED libpmemobj>=1.3)
else()
	find_package(PMEMOBJ REQUIRED)
endif()

join(";\n\t\t" LINKER_SCRIPT_EXPORTED_SYMBOLS "${EXPORTED_SYMBOLS}")
join(";-G;" OBJDUMP_EXPORTED_SYMBOLS "${EXPORTED_SYMBOLS}")
set(OBJDUMP_EXPORTED_SYMBOLS "-G;${OBJDUMP_EXPORTED_SYMBOLS}")

configure_file(libpmemfile-posix.map.in libpmemfile-posix.map)
configure_file(libpmemfile-posix.pc.in libpmemfile-posix.pc)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(pmemfile-posix_o OBJECT ${SOURCES})

include_directories(${PMEMOBJ_INCLUDE_DIRS} .. .)
link_directories(${PMEMOBJ_LIBRARY_DIRS})

add_library(pmemfile-posix_shared SHARED $<TARGET_OBJECTS:pmemfile-posix_o>)
add_library(pmemfile-posix_static_unscoped STATIC $<TARGET_OBJECTS:pmemfile-posix_o>)

target_link_libraries(pmemfile-posix_shared PRIVATE ${PMEMOBJ_LIBRARIES})
target_link_libraries(pmemfile-posix_shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(pmemfile-posix_shared PRIVATE -Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-posix.map)
set_target_properties(pmemfile-posix_shared PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-posix.map)

set_target_properties(pmemfile-posix_shared PROPERTIES OUTPUT_NAME pmemfile-posix)
set_target_properties(pmemfile-posix_shared PROPERTIES VERSION ${VERSION} SOVERSION ${VERSION_MAJOR})

if(NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
	set_target_properties(pmemfile-posix_shared PROPERTIES
		PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/include/libpmemfile-posix.h;${CMAKE_SOURCE_DIR}/include/libpmemfile-posix-stubs.h")
endif()

set(posix_unscoped_a $<TARGET_FILE:pmemfile-posix_static_unscoped>)

add_custom_command(OUTPUT pmemfile-posix_unscoped.o
		COMMAND ${CMAKE_LINKER} -r --whole-archive ${posix_unscoped_a}
					-o pmemfile-posix_unscoped.o
		DEPENDS pmemfile-posix_static_unscoped)

add_custom_command(OUTPUT pmemfile-posix_scoped.o
		COMMAND ${CMAKE_OBJCOPY} --localize-hidden
				${OBJDUMP_EXPORTED_SYMBOLS}
				pmemfile-posix_unscoped.o pmemfile-posix_scoped.o
		DEPENDS pmemfile-posix_unscoped.o
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-posix.map)

add_library(pmemfile-posix_static STATIC pmemfile-posix_scoped.o)
set_target_properties(pmemfile-posix_static PROPERTIES LINKER_LANGUAGE C)
set_target_properties(pmemfile-posix_static PROPERTIES OUTPUT_NAME pmemfile-posix)
target_link_libraries(pmemfile-posix_static INTERFACE ${PMEMOBJ_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS pmemfile-posix_shared
	CONFIGURATIONS Release None RelWithDebInfo
	DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS pmemfile-posix_shared
	CONFIGURATIONS Debug
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/pmemfile_debug
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-posix.a
	CONFIGURATIONS Release None RelWithDebInfo
	DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-posix.a
	CONFIGURATIONS Debug
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/pmemfile_debug)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libpmemfile-posix.pc
	CONFIGURATIONS Release None RelWithDebInfo
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

add_cstyle(pmemfile-posix)
add_check_whitespace(pmemfile-posix)
