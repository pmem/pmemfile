/*
 * Copyright 2016-2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * preload.c - The main code controlling the preloadable version of pmemfile. To
 * understand the code start from the routine pmemfile_preload_constructor() -
 * this should run before the application starts, and while there is only a
 * single thread of execution in the process. To understand the syscall
 * routing logic look at the routine hook(...), this is called by the libc
 * syscall intercepting code every time libc would issue a syscall instruction.
 * The hook() routine decides if a syscall should be handled by kernel, or
 * by pmemfile ( and which pmemfile pool ).
 */

#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <syscall.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <limits.h>

#include <asm-generic/errno.h>

#include "compiler_utils.h"
#include "libsyscall_intercept_hook_point.h"
#include "libpmemfile-posix.h"
#include "sys_util.h"
#include "preload.h"
#include "syscall_early_filter.h"

#define SYSCALL_ARRAY_SIZE 0x200

static int hook(long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5,
			long *result);

static bool syscall_number_filter[SYSCALL_ARRAY_SIZE];
static bool syscall_needs_fd_rlock[SYSCALL_ARRAY_SIZE];
static bool syscall_needs_fd_wlock[SYSCALL_ARRAY_SIZE];
static bool syscall_needs_pmem_cwd_rlock[SYSCALL_ARRAY_SIZE];
static bool syscall_has_fd_first_arg[SYSCALL_ARRAY_SIZE];

static struct pool_description pools[0x100];
static int pool_count;

static bool is_memfd_syscall_available;

#define PMEMFILE_MAX_FD 0x8000

/*
 * The associations between user visible fd numbers and
 * pmemfile pointers. This is a global table, with a single
 * global lock -- thus reading one fd blocks other threads from
 * writing to other fds.
 * XXX - improve this situation
 */
static struct fd_association fd_table[PMEMFILE_MAX_FD + 1];
static pthread_rwlock_t fd_table_lock = PTHREAD_RWLOCK_INITIALIZER;

/*
 * A separate place to keep track of fds used to hold mount points open, in
 * the previous array. The application should not be aware of these, thus
 * whenever these file descriptors are encountered during interposing, -EBADF
 * must be returned. The contents of this array does not change after startup.
 */
static bool mount_point_fds[PMEMFILE_MAX_FD + 1];


static bool
is_fd_in_table(long fd)
{
	if (fd < 0 || fd > PMEMFILE_MAX_FD)
		return false;

	return !is_fda_null(fd_table + fd);
}

static pthread_rwlock_t pmem_cwd_lock = PTHREAD_RWLOCK_INITIALIZER;
static struct pool_description *volatile cwd_pool;

static int exit_on_ENOTSUP;
static long check_errno(long e)
{
	if (e == -ENOTSUP && exit_on_ENOTSUP) {
		const char *str = "ENOTSUP";

		syscall_no_intercept(SYS_write, 2, str, strlen(str));
		exit_group_no_intercept(95);
	}

	return e;
}

static struct fd_desc
cwd_desc(void)
{
	struct fd_desc result;

	result.kernel_fd = AT_FDCWD;
	result.pmem_fda.pool = cwd_pool;
	result.pmem_fda.file = PMEMFILE_AT_CWD;

	return result;
}

static struct fd_desc
fetch_fd(long fd)
{
	struct fd_desc result;

	result.kernel_fd = fd;

	if ((int)fd == AT_FDCWD) {
		result.pmem_fda.pool = cwd_pool;
		result.pmem_fda.file = PMEMFILE_AT_CWD;
	} else if (is_fd_in_table(fd)) {
		result.pmem_fda = fd_table[fd];
	} else {
		result.pmem_fda.pool = NULL;
		result.pmem_fda.file = NULL;
	}

	return result;
}

#ifdef SYS_memfd_create

static void
check_memfd_syscall(void)
{
	long fd = syscall_no_intercept(SYS_memfd_create, "check", 0);
	if (fd >= 0) {
		is_memfd_syscall_available = true;
		syscall_no_intercept(SYS_close, fd);
	}
}

#else

#define SYS_memfd_create 0
#define check_memfd_syscall()

#endif

/*
 * acquire_new_fd - grab a new file descriptor from the kernel
 */
static long
acquire_new_fd(const char *path)
{
	long fd;

	if (is_memfd_syscall_available)
		fd = syscall_no_intercept(SYS_memfd_create, path, 0);
	else
		fd = syscall_no_intercept(SYS_open, "/dev/null", O_RDONLY);

	if (fd > PMEMFILE_MAX_FD) {
		syscall_no_intercept(SYS_close, fd);
		return -ENFILE;
	}

	return fd;
}

/*
 * The reenter flag allows pmemfile to prevent the hooking of its own
 * syscalls. E.g. while handling an open syscall, libpmemfile might
 * call pmemfile_pool_open, which in turn uses an open syscall internally.
 * This internally used open syscall is once again forwarded to libpmemfile,
 * but using this flag libpmemfile can notice this case of reentering itself.
 */
static __thread bool reenter = false;

static void log_init(const char *path, const char *trunc);
static void log_write(const char *fmt, ...) pf_printf_like(1, 2);

static void establish_mount_points(const char *);
static void init_hooking(void);

static volatile int pause_at_start;

pf_constructor void
pmemfile_preload_constructor(void)
{
	if (!syscall_hook_in_process_allowed())
		return;

	syscall_early_filter_init();
	check_memfd_syscall();
	log_init(getenv("PMEMFILE_PRELOAD_LOG"),
			getenv("PMEMFILE_PRELOAD_LOG_TRUNC"));

	const char *env_str = getenv("PMEMFILE_EXIT_ON_NOT_SUPPORTED");
	exit_on_ENOTSUP = env_str ? env_str[0] == '1' : 0;

	establish_mount_points(getenv("PMEMFILE_POOLS"));

	if (pool_count == 0)
		/* No pools mounted. XXX prevent syscall interception */
		return;

	if (getenv("PMEMFILE_PRELOAD_PAUSE_AT_START")) {
		pause_at_start = 1;
		while (pause_at_start)
			;
	}

	/*
	 * Must be the last step, the callback can be called anytime
	 * after the call to init_hooking()
	 */
	init_hooking();

	char *cd = getenv("PMEMFILE_CD");
	if (cd && chdir(cd)) {
		perror("chdir");
		exit(1);
	}
}

static void
init_hooking(void)
{
	/* XXX: move this filtering to the intercepting library */
	syscall_number_filter[SYS_access] = true;
	syscall_number_filter[SYS_chmod] = true;
	syscall_number_filter[SYS_chown] = true;
	syscall_number_filter[SYS_close] = true;
	syscall_number_filter[SYS_faccessat] = true;
	syscall_number_filter[SYS_fadvise64] = true;
	syscall_number_filter[SYS_fallocate] = true;
	syscall_number_filter[SYS_fchmodat] = true;
	syscall_number_filter[SYS_fchmod] = true;
	syscall_number_filter[SYS_fchownat] = true;
	syscall_number_filter[SYS_fchown] = true;
	syscall_number_filter[SYS_fcntl] = true;
	syscall_number_filter[SYS_fdatasync] = true;
	syscall_number_filter[SYS_fgetxattr] = true;
	syscall_number_filter[SYS_flock] = true;
	syscall_number_filter[SYS_fsetxattr] = true;
	syscall_number_filter[SYS_fstat] = true;
	syscall_number_filter[SYS_fsync] = true;
	syscall_number_filter[SYS_ftruncate] = true;
	syscall_number_filter[SYS_getdents64] = true;
	syscall_number_filter[SYS_getdents] = true;
	syscall_number_filter[SYS_getxattr] = true;
	syscall_number_filter[SYS_lchown] = true;
	syscall_number_filter[SYS_lgetxattr] = true;
	syscall_number_filter[SYS_linkat] = true;
	syscall_number_filter[SYS_link] = true;
	syscall_number_filter[SYS_lseek] = true;
	syscall_number_filter[SYS_lsetxattr] = true;
	syscall_number_filter[SYS_lstat] = true;
	syscall_number_filter[SYS_mkdirat] = true;
	syscall_number_filter[SYS_mkdir] = true;
	syscall_number_filter[SYS_newfstatat] = true;
	syscall_number_filter[SYS_openat] = true;
	syscall_number_filter[SYS_open] = true;
	syscall_number_filter[SYS_pread64] = true;
	syscall_number_filter[SYS_pwrite64] = true;
	syscall_number_filter[SYS_read] = true;
	syscall_number_filter[SYS_readlinkat] = true;
	syscall_number_filter[SYS_readlink] = true;
	syscall_number_filter[SYS_renameat2] = true;
	syscall_number_filter[SYS_renameat] = true;
	syscall_number_filter[SYS_rename] = true;
	syscall_number_filter[SYS_rmdir] = true;
	syscall_number_filter[SYS_setxattr] = true;
	syscall_number_filter[SYS_stat] = true;
	syscall_number_filter[SYS_symlinkat] = true;
	syscall_number_filter[SYS_symlink] = true;
	syscall_number_filter[SYS_syncfs] = true;
	syscall_number_filter[SYS_truncate] = true;
	syscall_number_filter[SYS_unlinkat] = true;
	syscall_number_filter[SYS_unlink] = true;
	syscall_number_filter[SYS_write] = true;

	/* Syscalls not handled yet */
	syscall_number_filter[SYS_chroot] = true;
	syscall_number_filter[SYS_copy_file_range] = true;
	syscall_number_filter[SYS_dup2] = true;
	syscall_number_filter[SYS_dup3] = true;
	syscall_number_filter[SYS_dup] = true;
	syscall_number_filter[SYS_execveat] = true;
	syscall_number_filter[SYS_execve] = true;
	syscall_number_filter[SYS_flistxattr] = true;
	syscall_number_filter[SYS_fremovexattr] = true;
	syscall_number_filter[SYS_futimesat] = true;
	syscall_number_filter[SYS_listxattr] = true;
	syscall_number_filter[SYS_llistxattr] = true;
	syscall_number_filter[SYS_lremovexattr] = true;
	syscall_number_filter[SYS_mmap] = true;
	syscall_number_filter[SYS_name_to_handle_at] = true;
	/* No handle can come from pmemfile, so just forward this to kernel */
	syscall_number_filter[SYS_open_by_handle_at] = false;
	syscall_number_filter[SYS_preadv2] = true;
	syscall_number_filter[SYS_pwritev2] = true;
	syscall_number_filter[SYS_readahead] = true;
	syscall_number_filter[SYS_readv] = true;
	syscall_number_filter[SYS_removexattr] = true;
	syscall_number_filter[SYS_sendfile] = true;
	syscall_number_filter[SYS_splice] = true;
	syscall_number_filter[SYS_utimensat] = true;
	syscall_number_filter[SYS_utimes] = true;
	syscall_number_filter[SYS_utime] = true;
	syscall_number_filter[SYS_writev] = true;

	syscall_needs_fd_rlock[SYS_faccessat] = true;
	syscall_needs_fd_rlock[SYS_fadvise64] = true;
	syscall_needs_fd_rlock[SYS_fallocate] = true;
	syscall_needs_fd_rlock[SYS_fchmodat] = true;
	syscall_needs_fd_rlock[SYS_fchmod] = true;
	syscall_needs_fd_rlock[SYS_fchownat] = true;
	syscall_needs_fd_rlock[SYS_fchown] = true;
	syscall_needs_fd_rlock[SYS_fcntl] = true;
	syscall_needs_fd_rlock[SYS_fdatasync] = true;
	syscall_needs_fd_rlock[SYS_fgetxattr] = true;
	syscall_needs_fd_rlock[SYS_fremovexattr] = true;
	syscall_needs_fd_rlock[SYS_flock] = true;
	syscall_needs_fd_rlock[SYS_fsetxattr] = true;
	syscall_needs_fd_rlock[SYS_flistxattr] = true;
	syscall_needs_fd_rlock[SYS_fstat] = true;
	syscall_needs_fd_rlock[SYS_fsync] = true;
	syscall_needs_fd_rlock[SYS_ftruncate] = true;
	syscall_needs_fd_rlock[SYS_getdents64] = true;
	syscall_needs_fd_rlock[SYS_getdents64] = true;
	syscall_needs_fd_rlock[SYS_getdents] = true;
	syscall_needs_fd_rlock[SYS_getdents] = true;
	syscall_needs_fd_rlock[SYS_linkat] = true;
	syscall_needs_fd_rlock[SYS_lseek] = true;
	syscall_needs_fd_rlock[SYS_mkdirat] = true;
	syscall_needs_fd_rlock[SYS_newfstatat] = true;
	syscall_needs_fd_rlock[SYS_pread64] = true;
	syscall_needs_fd_rlock[SYS_preadv2] = true;
	syscall_needs_fd_rlock[SYS_pwrite64] = true;
	syscall_needs_fd_rlock[SYS_pwritev2] = true;
	syscall_needs_fd_rlock[SYS_readlinkat] = true;
	syscall_needs_fd_rlock[SYS_read] = true;
	syscall_needs_fd_rlock[SYS_readahead] = true;
	syscall_needs_fd_rlock[SYS_readv] = true;
	syscall_needs_fd_rlock[SYS_renameat2] = true;
	syscall_needs_fd_rlock[SYS_renameat] = true;
	syscall_needs_fd_rlock[SYS_sendfile] = true;
	syscall_needs_fd_rlock[SYS_symlinkat] = true;
	syscall_needs_fd_rlock[SYS_syncfs] = true;
	syscall_needs_fd_rlock[SYS_unlinkat] = true;
	syscall_needs_fd_rlock[SYS_write] = true;
	syscall_needs_fd_rlock[SYS_writev] = true;
	syscall_needs_fd_rlock[SYS_mmap] = true;
	syscall_needs_fd_rlock[SYS_dup] = true;
	syscall_needs_fd_rlock[SYS_dup2] = true;
	syscall_needs_fd_rlock[SYS_dup3] = true;
	syscall_needs_fd_rlock[SYS_futimesat] = true;

	syscall_needs_fd_wlock[SYS_close] = true;
	syscall_needs_fd_wlock[SYS_openat] = true;
	syscall_needs_fd_wlock[SYS_open] = true;

	syscall_needs_pmem_cwd_rlock[SYS_access] = true;
	syscall_needs_pmem_cwd_rlock[SYS_chmod] = true;
	syscall_needs_pmem_cwd_rlock[SYS_chown] = true;
	syscall_needs_pmem_cwd_rlock[SYS_chroot] = true;
	syscall_needs_pmem_cwd_rlock[SYS_execve] = true;
	syscall_needs_pmem_cwd_rlock[SYS_execveat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_faccessat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_fchmodat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_fchownat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_getxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_lchown] = true;
	syscall_needs_pmem_cwd_rlock[SYS_lgetxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_linkat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_link] = true;
	syscall_needs_pmem_cwd_rlock[SYS_listxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_llistxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_lremovexattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_lsetxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_lstat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_mkdirat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_mkdir] = true;
	syscall_needs_pmem_cwd_rlock[SYS_newfstatat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_openat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_open] = true;
	syscall_needs_pmem_cwd_rlock[SYS_readlink] = true;
	syscall_needs_pmem_cwd_rlock[SYS_readlinkat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_removexattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_renameat2] = true;
	syscall_needs_pmem_cwd_rlock[SYS_renameat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_rename] = true;
	syscall_needs_pmem_cwd_rlock[SYS_rmdir] = true;
	syscall_needs_pmem_cwd_rlock[SYS_setxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_stat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_symlinkat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_symlink] = true;
	syscall_needs_pmem_cwd_rlock[SYS_syncfs] = true;
	syscall_needs_pmem_cwd_rlock[SYS_truncate] = true;
	syscall_needs_pmem_cwd_rlock[SYS_unlinkat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_unlink] = true;
	syscall_needs_pmem_cwd_rlock[SYS_utime] = true;
	syscall_needs_pmem_cwd_rlock[SYS_utimes] = true;
	syscall_needs_pmem_cwd_rlock[SYS_futimesat] = true;

	syscall_has_fd_first_arg[SYS_close] = true;
	syscall_has_fd_first_arg[SYS_dup2] = true;
	syscall_has_fd_first_arg[SYS_dup3] = true;
	syscall_has_fd_first_arg[SYS_dup] = true;
	syscall_has_fd_first_arg[SYS_fadvise64] = true;
	syscall_has_fd_first_arg[SYS_fallocate] = true;
	syscall_has_fd_first_arg[SYS_fchmod] = true;
	syscall_has_fd_first_arg[SYS_fchown] = true;
	syscall_has_fd_first_arg[SYS_fcntl] = true;
	syscall_has_fd_first_arg[SYS_fdatasync] = true;
	syscall_has_fd_first_arg[SYS_fgetxattr] = true;
	syscall_has_fd_first_arg[SYS_flistxattr] = true;
	syscall_has_fd_first_arg[SYS_flock] = true;
	syscall_has_fd_first_arg[SYS_fremovexattr] = true;
	syscall_has_fd_first_arg[SYS_fsetxattr] = true;
	syscall_has_fd_first_arg[SYS_fstat] = true;
	syscall_has_fd_first_arg[SYS_fsync] = true;
	syscall_has_fd_first_arg[SYS_ftruncate] = true;
	syscall_has_fd_first_arg[SYS_getdents64] = true;
	syscall_has_fd_first_arg[SYS_getdents] = true;
	syscall_has_fd_first_arg[SYS_lseek] = true;
	syscall_has_fd_first_arg[SYS_pread64] = true;
	syscall_has_fd_first_arg[SYS_preadv2] = true;
	syscall_has_fd_first_arg[SYS_pwrite64] = true;
	syscall_has_fd_first_arg[SYS_pwritev2] = true;
	syscall_has_fd_first_arg[SYS_readahead] = true;
	syscall_has_fd_first_arg[SYS_readv] = true;
	syscall_has_fd_first_arg[SYS_read] = true;
	syscall_has_fd_first_arg[SYS_syncfs] = true;
	syscall_has_fd_first_arg[SYS_writev] = true;
	syscall_has_fd_first_arg[SYS_write] = true;

	/*
	 * Install the callback to be calleb by the syscall intercepting library
	 */
	intercept_hook_point = &hook;

#ifndef NDEBUG
	/* XXX temporary -- checking the unified syscall filter table */
	struct syscall_early_filter_entry f;

	f = get_early_filter_entry(0);
	assert(syscall_number_filter[0] == f.must_handle);
	assert(syscall_needs_fd_rlock[0] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[0] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[0] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[0] == f.cwd_rlock);

	f = get_early_filter_entry(1);
	assert(syscall_number_filter[1] == f.must_handle);
	assert(syscall_needs_fd_rlock[1] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[1] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[1] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[1] == f.cwd_rlock);

	f = get_early_filter_entry(2);
	assert(syscall_number_filter[2] == f.must_handle);
	assert(syscall_needs_fd_rlock[2] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[2] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[2] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[2] == f.cwd_rlock);

	f = get_early_filter_entry(3);
	assert(syscall_number_filter[3] == f.must_handle);
	assert(syscall_needs_fd_rlock[3] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[3] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[3] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[3] == f.cwd_rlock);

	f = get_early_filter_entry(4);
	assert(syscall_number_filter[4] == f.must_handle);
	assert(syscall_needs_fd_rlock[4] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[4] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[4] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[4] == f.cwd_rlock);

	f = get_early_filter_entry(5);
	assert(syscall_number_filter[5] == f.must_handle);
	assert(syscall_needs_fd_rlock[5] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[5] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[5] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[5] == f.cwd_rlock);

	f = get_early_filter_entry(6);
	assert(syscall_number_filter[6] == f.must_handle);
	assert(syscall_needs_fd_rlock[6] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[6] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[6] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[6] == f.cwd_rlock);

	f = get_early_filter_entry(7);
	assert(syscall_number_filter[7] == f.must_handle);
	assert(syscall_needs_fd_rlock[7] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[7] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[7] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[7] == f.cwd_rlock);

	f = get_early_filter_entry(8);
	assert(syscall_number_filter[8] == f.must_handle);
	assert(syscall_needs_fd_rlock[8] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[8] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[8] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[8] == f.cwd_rlock);

	f = get_early_filter_entry(9);
	assert(syscall_number_filter[9] == f.must_handle);
	assert(syscall_needs_fd_rlock[9] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[9] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[9] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[9] == f.cwd_rlock);

	f = get_early_filter_entry(10);
	assert(syscall_number_filter[10] == f.must_handle);
	assert(syscall_needs_fd_rlock[10] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[10] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[10] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[10] == f.cwd_rlock);

	f = get_early_filter_entry(11);
	assert(syscall_number_filter[11] == f.must_handle);
	assert(syscall_needs_fd_rlock[11] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[11] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[11] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[11] == f.cwd_rlock);

	f = get_early_filter_entry(12);
	assert(syscall_number_filter[12] == f.must_handle);
	assert(syscall_needs_fd_rlock[12] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[12] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[12] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[12] == f.cwd_rlock);

	f = get_early_filter_entry(13);
	assert(syscall_number_filter[13] == f.must_handle);
	assert(syscall_needs_fd_rlock[13] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[13] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[13] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[13] == f.cwd_rlock);

	f = get_early_filter_entry(14);
	assert(syscall_number_filter[14] == f.must_handle);
	assert(syscall_needs_fd_rlock[14] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[14] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[14] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[14] == f.cwd_rlock);

	f = get_early_filter_entry(15);
	assert(syscall_number_filter[15] == f.must_handle);
	assert(syscall_needs_fd_rlock[15] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[15] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[15] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[15] == f.cwd_rlock);

	f = get_early_filter_entry(16);
	assert(syscall_number_filter[16] == f.must_handle);
	assert(syscall_needs_fd_rlock[16] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[16] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[16] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[16] == f.cwd_rlock);

	f = get_early_filter_entry(17);
	assert(syscall_number_filter[17] == f.must_handle);
	assert(syscall_needs_fd_rlock[17] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[17] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[17] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[17] == f.cwd_rlock);

	f = get_early_filter_entry(18);
	assert(syscall_number_filter[18] == f.must_handle);
	assert(syscall_needs_fd_rlock[18] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[18] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[18] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[18] == f.cwd_rlock);

	f = get_early_filter_entry(19);
	assert(syscall_number_filter[19] == f.must_handle);
	assert(syscall_needs_fd_rlock[19] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[19] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[19] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[19] == f.cwd_rlock);

	f = get_early_filter_entry(20);
	assert(syscall_number_filter[20] == f.must_handle);
	assert(syscall_needs_fd_rlock[20] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[20] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[20] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[20] == f.cwd_rlock);

	f = get_early_filter_entry(21);
	assert(syscall_number_filter[21] == f.must_handle);
	assert(syscall_needs_fd_rlock[21] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[21] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[21] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[21] == f.cwd_rlock);

	f = get_early_filter_entry(22);
	assert(syscall_number_filter[22] == f.must_handle);
	assert(syscall_needs_fd_rlock[22] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[22] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[22] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[22] == f.cwd_rlock);

	f = get_early_filter_entry(23);
	assert(syscall_number_filter[23] == f.must_handle);
	assert(syscall_needs_fd_rlock[23] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[23] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[23] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[23] == f.cwd_rlock);

	f = get_early_filter_entry(24);
	assert(syscall_number_filter[24] == f.must_handle);
	assert(syscall_needs_fd_rlock[24] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[24] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[24] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[24] == f.cwd_rlock);

	f = get_early_filter_entry(25);
	assert(syscall_number_filter[25] == f.must_handle);
	assert(syscall_needs_fd_rlock[25] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[25] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[25] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[25] == f.cwd_rlock);

	f = get_early_filter_entry(26);
	assert(syscall_number_filter[26] == f.must_handle);
	assert(syscall_needs_fd_rlock[26] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[26] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[26] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[26] == f.cwd_rlock);

	f = get_early_filter_entry(27);
	assert(syscall_number_filter[27] == f.must_handle);
	assert(syscall_needs_fd_rlock[27] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[27] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[27] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[27] == f.cwd_rlock);

	f = get_early_filter_entry(28);
	assert(syscall_number_filter[28] == f.must_handle);
	assert(syscall_needs_fd_rlock[28] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[28] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[28] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[28] == f.cwd_rlock);

	f = get_early_filter_entry(29);
	assert(syscall_number_filter[29] == f.must_handle);
	assert(syscall_needs_fd_rlock[29] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[29] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[29] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[29] == f.cwd_rlock);

	f = get_early_filter_entry(30);
	assert(syscall_number_filter[30] == f.must_handle);
	assert(syscall_needs_fd_rlock[30] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[30] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[30] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[30] == f.cwd_rlock);

	f = get_early_filter_entry(31);
	assert(syscall_number_filter[31] == f.must_handle);
	assert(syscall_needs_fd_rlock[31] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[31] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[31] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[31] == f.cwd_rlock);

	f = get_early_filter_entry(32);
	assert(syscall_number_filter[32] == f.must_handle);
	assert(syscall_needs_fd_rlock[32] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[32] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[32] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[32] == f.cwd_rlock);

	f = get_early_filter_entry(33);
	assert(syscall_number_filter[33] == f.must_handle);
	assert(syscall_needs_fd_rlock[33] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[33] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[33] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[33] == f.cwd_rlock);

	f = get_early_filter_entry(34);
	assert(syscall_number_filter[34] == f.must_handle);
	assert(syscall_needs_fd_rlock[34] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[34] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[34] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[34] == f.cwd_rlock);

	f = get_early_filter_entry(35);
	assert(syscall_number_filter[35] == f.must_handle);
	assert(syscall_needs_fd_rlock[35] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[35] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[35] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[35] == f.cwd_rlock);

	f = get_early_filter_entry(36);
	assert(syscall_number_filter[36] == f.must_handle);
	assert(syscall_needs_fd_rlock[36] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[36] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[36] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[36] == f.cwd_rlock);

	f = get_early_filter_entry(37);
	assert(syscall_number_filter[37] == f.must_handle);
	assert(syscall_needs_fd_rlock[37] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[37] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[37] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[37] == f.cwd_rlock);

	f = get_early_filter_entry(38);
	assert(syscall_number_filter[38] == f.must_handle);
	assert(syscall_needs_fd_rlock[38] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[38] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[38] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[38] == f.cwd_rlock);

	f = get_early_filter_entry(39);
	assert(syscall_number_filter[39] == f.must_handle);
	assert(syscall_needs_fd_rlock[39] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[39] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[39] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[39] == f.cwd_rlock);

	f = get_early_filter_entry(40);
	assert(syscall_number_filter[40] == f.must_handle);
	assert(syscall_needs_fd_rlock[40] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[40] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[40] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[40] == f.cwd_rlock);

	f = get_early_filter_entry(41);
	assert(syscall_number_filter[41] == f.must_handle);
	assert(syscall_needs_fd_rlock[41] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[41] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[41] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[41] == f.cwd_rlock);

	f = get_early_filter_entry(42);
	assert(syscall_number_filter[42] == f.must_handle);
	assert(syscall_needs_fd_rlock[42] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[42] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[42] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[42] == f.cwd_rlock);

	f = get_early_filter_entry(43);
	assert(syscall_number_filter[43] == f.must_handle);
	assert(syscall_needs_fd_rlock[43] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[43] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[43] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[43] == f.cwd_rlock);

	f = get_early_filter_entry(44);
	assert(syscall_number_filter[44] == f.must_handle);
	assert(syscall_needs_fd_rlock[44] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[44] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[44] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[44] == f.cwd_rlock);

	f = get_early_filter_entry(45);
	assert(syscall_number_filter[45] == f.must_handle);
	assert(syscall_needs_fd_rlock[45] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[45] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[45] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[45] == f.cwd_rlock);

	f = get_early_filter_entry(46);
	assert(syscall_number_filter[46] == f.must_handle);
	assert(syscall_needs_fd_rlock[46] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[46] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[46] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[46] == f.cwd_rlock);

	f = get_early_filter_entry(47);
	assert(syscall_number_filter[47] == f.must_handle);
	assert(syscall_needs_fd_rlock[47] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[47] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[47] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[47] == f.cwd_rlock);

	f = get_early_filter_entry(48);
	assert(syscall_number_filter[48] == f.must_handle);
	assert(syscall_needs_fd_rlock[48] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[48] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[48] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[48] == f.cwd_rlock);

	f = get_early_filter_entry(49);
	assert(syscall_number_filter[49] == f.must_handle);
	assert(syscall_needs_fd_rlock[49] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[49] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[49] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[49] == f.cwd_rlock);

	f = get_early_filter_entry(50);
	assert(syscall_number_filter[50] == f.must_handle);
	assert(syscall_needs_fd_rlock[50] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[50] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[50] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[50] == f.cwd_rlock);

	f = get_early_filter_entry(51);
	assert(syscall_number_filter[51] == f.must_handle);
	assert(syscall_needs_fd_rlock[51] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[51] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[51] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[51] == f.cwd_rlock);

	f = get_early_filter_entry(52);
	assert(syscall_number_filter[52] == f.must_handle);
	assert(syscall_needs_fd_rlock[52] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[52] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[52] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[52] == f.cwd_rlock);

	f = get_early_filter_entry(53);
	assert(syscall_number_filter[53] == f.must_handle);
	assert(syscall_needs_fd_rlock[53] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[53] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[53] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[53] == f.cwd_rlock);

	f = get_early_filter_entry(54);
	assert(syscall_number_filter[54] == f.must_handle);
	assert(syscall_needs_fd_rlock[54] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[54] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[54] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[54] == f.cwd_rlock);

	f = get_early_filter_entry(55);
	assert(syscall_number_filter[55] == f.must_handle);
	assert(syscall_needs_fd_rlock[55] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[55] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[55] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[55] == f.cwd_rlock);

	f = get_early_filter_entry(56);
	assert(syscall_number_filter[56] == f.must_handle);
	assert(syscall_needs_fd_rlock[56] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[56] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[56] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[56] == f.cwd_rlock);

	f = get_early_filter_entry(57);
	assert(syscall_number_filter[57] == f.must_handle);
	assert(syscall_needs_fd_rlock[57] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[57] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[57] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[57] == f.cwd_rlock);

	f = get_early_filter_entry(58);
	assert(syscall_number_filter[58] == f.must_handle);
	assert(syscall_needs_fd_rlock[58] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[58] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[58] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[58] == f.cwd_rlock);

	f = get_early_filter_entry(59);
	assert(syscall_number_filter[59] == f.must_handle);
	assert(syscall_needs_fd_rlock[59] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[59] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[59] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[59] == f.cwd_rlock);

	f = get_early_filter_entry(60);
	assert(syscall_number_filter[60] == f.must_handle);
	assert(syscall_needs_fd_rlock[60] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[60] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[60] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[60] == f.cwd_rlock);

	f = get_early_filter_entry(61);
	assert(syscall_number_filter[61] == f.must_handle);
	assert(syscall_needs_fd_rlock[61] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[61] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[61] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[61] == f.cwd_rlock);

	f = get_early_filter_entry(62);
	assert(syscall_number_filter[62] == f.must_handle);
	assert(syscall_needs_fd_rlock[62] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[62] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[62] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[62] == f.cwd_rlock);

	f = get_early_filter_entry(63);
	assert(syscall_number_filter[63] == f.must_handle);
	assert(syscall_needs_fd_rlock[63] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[63] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[63] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[63] == f.cwd_rlock);

	f = get_early_filter_entry(64);
	assert(syscall_number_filter[64] == f.must_handle);
	assert(syscall_needs_fd_rlock[64] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[64] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[64] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[64] == f.cwd_rlock);

	f = get_early_filter_entry(65);
	assert(syscall_number_filter[65] == f.must_handle);
	assert(syscall_needs_fd_rlock[65] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[65] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[65] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[65] == f.cwd_rlock);

	f = get_early_filter_entry(66);
	assert(syscall_number_filter[66] == f.must_handle);
	assert(syscall_needs_fd_rlock[66] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[66] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[66] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[66] == f.cwd_rlock);

	f = get_early_filter_entry(67);
	assert(syscall_number_filter[67] == f.must_handle);
	assert(syscall_needs_fd_rlock[67] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[67] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[67] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[67] == f.cwd_rlock);

	f = get_early_filter_entry(68);
	assert(syscall_number_filter[68] == f.must_handle);
	assert(syscall_needs_fd_rlock[68] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[68] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[68] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[68] == f.cwd_rlock);

	f = get_early_filter_entry(69);
	assert(syscall_number_filter[69] == f.must_handle);
	assert(syscall_needs_fd_rlock[69] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[69] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[69] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[69] == f.cwd_rlock);

	f = get_early_filter_entry(70);
	assert(syscall_number_filter[70] == f.must_handle);
	assert(syscall_needs_fd_rlock[70] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[70] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[70] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[70] == f.cwd_rlock);

	f = get_early_filter_entry(71);
	assert(syscall_number_filter[71] == f.must_handle);
	assert(syscall_needs_fd_rlock[71] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[71] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[71] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[71] == f.cwd_rlock);

	f = get_early_filter_entry(72);
	assert(syscall_number_filter[72] == f.must_handle);
	assert(syscall_needs_fd_rlock[72] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[72] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[72] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[72] == f.cwd_rlock);

	f = get_early_filter_entry(73);
	assert(syscall_number_filter[73] == f.must_handle);
	assert(syscall_needs_fd_rlock[73] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[73] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[73] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[73] == f.cwd_rlock);

	f = get_early_filter_entry(74);
	assert(syscall_number_filter[74] == f.must_handle);
	assert(syscall_needs_fd_rlock[74] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[74] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[74] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[74] == f.cwd_rlock);

	f = get_early_filter_entry(75);
	assert(syscall_number_filter[75] == f.must_handle);
	assert(syscall_needs_fd_rlock[75] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[75] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[75] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[75] == f.cwd_rlock);

	f = get_early_filter_entry(76);
	assert(syscall_number_filter[76] == f.must_handle);
	assert(syscall_needs_fd_rlock[76] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[76] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[76] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[76] == f.cwd_rlock);

	f = get_early_filter_entry(77);
	assert(syscall_number_filter[77] == f.must_handle);
	assert(syscall_needs_fd_rlock[77] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[77] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[77] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[77] == f.cwd_rlock);

	f = get_early_filter_entry(78);
	assert(syscall_number_filter[78] == f.must_handle);
	assert(syscall_needs_fd_rlock[78] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[78] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[78] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[78] == f.cwd_rlock);

	f = get_early_filter_entry(79);
	assert(syscall_number_filter[79] == f.must_handle);
	assert(syscall_needs_fd_rlock[79] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[79] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[79] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[79] == f.cwd_rlock);

	f = get_early_filter_entry(80);
	assert(syscall_number_filter[80] == f.must_handle);
	assert(syscall_needs_fd_rlock[80] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[80] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[80] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[80] == f.cwd_rlock);

	f = get_early_filter_entry(81);
	assert(syscall_number_filter[81] == f.must_handle);
	assert(syscall_needs_fd_rlock[81] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[81] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[81] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[81] == f.cwd_rlock);

	f = get_early_filter_entry(82);
	assert(syscall_number_filter[82] == f.must_handle);
	assert(syscall_needs_fd_rlock[82] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[82] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[82] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[82] == f.cwd_rlock);

	f = get_early_filter_entry(83);
	assert(syscall_number_filter[83] == f.must_handle);
	assert(syscall_needs_fd_rlock[83] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[83] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[83] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[83] == f.cwd_rlock);

	f = get_early_filter_entry(84);
	assert(syscall_number_filter[84] == f.must_handle);
	assert(syscall_needs_fd_rlock[84] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[84] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[84] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[84] == f.cwd_rlock);

	f = get_early_filter_entry(85);
	assert(syscall_number_filter[85] == f.must_handle);
	assert(syscall_needs_fd_rlock[85] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[85] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[85] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[85] == f.cwd_rlock);

	f = get_early_filter_entry(86);
	assert(syscall_number_filter[86] == f.must_handle);
	assert(syscall_needs_fd_rlock[86] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[86] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[86] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[86] == f.cwd_rlock);

	f = get_early_filter_entry(87);
	assert(syscall_number_filter[87] == f.must_handle);
	assert(syscall_needs_fd_rlock[87] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[87] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[87] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[87] == f.cwd_rlock);

	f = get_early_filter_entry(88);
	assert(syscall_number_filter[88] == f.must_handle);
	assert(syscall_needs_fd_rlock[88] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[88] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[88] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[88] == f.cwd_rlock);

	f = get_early_filter_entry(89);
	assert(syscall_number_filter[89] == f.must_handle);
	assert(syscall_needs_fd_rlock[89] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[89] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[89] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[89] == f.cwd_rlock);

	f = get_early_filter_entry(90);
	assert(syscall_number_filter[90] == f.must_handle);
	assert(syscall_needs_fd_rlock[90] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[90] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[90] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[90] == f.cwd_rlock);

	f = get_early_filter_entry(91);
	assert(syscall_number_filter[91] == f.must_handle);
	assert(syscall_needs_fd_rlock[91] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[91] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[91] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[91] == f.cwd_rlock);

	f = get_early_filter_entry(92);
	assert(syscall_number_filter[92] == f.must_handle);
	assert(syscall_needs_fd_rlock[92] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[92] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[92] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[92] == f.cwd_rlock);

	f = get_early_filter_entry(93);
	assert(syscall_number_filter[93] == f.must_handle);
	assert(syscall_needs_fd_rlock[93] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[93] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[93] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[93] == f.cwd_rlock);

	f = get_early_filter_entry(94);
	assert(syscall_number_filter[94] == f.must_handle);
	assert(syscall_needs_fd_rlock[94] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[94] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[94] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[94] == f.cwd_rlock);

	f = get_early_filter_entry(95);
	assert(syscall_number_filter[95] == f.must_handle);
	assert(syscall_needs_fd_rlock[95] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[95] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[95] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[95] == f.cwd_rlock);

	f = get_early_filter_entry(96);
	assert(syscall_number_filter[96] == f.must_handle);
	assert(syscall_needs_fd_rlock[96] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[96] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[96] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[96] == f.cwd_rlock);

	f = get_early_filter_entry(97);
	assert(syscall_number_filter[97] == f.must_handle);
	assert(syscall_needs_fd_rlock[97] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[97] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[97] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[97] == f.cwd_rlock);

	f = get_early_filter_entry(98);
	assert(syscall_number_filter[98] == f.must_handle);
	assert(syscall_needs_fd_rlock[98] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[98] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[98] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[98] == f.cwd_rlock);

	f = get_early_filter_entry(99);
	assert(syscall_number_filter[99] == f.must_handle);
	assert(syscall_needs_fd_rlock[99] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[99] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[99] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[99] == f.cwd_rlock);

	f = get_early_filter_entry(100);
	assert(syscall_number_filter[100] == f.must_handle);
	assert(syscall_needs_fd_rlock[100] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[100] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[100] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[100] == f.cwd_rlock);

	f = get_early_filter_entry(101);
	assert(syscall_number_filter[101] == f.must_handle);
	assert(syscall_needs_fd_rlock[101] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[101] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[101] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[101] == f.cwd_rlock);

	f = get_early_filter_entry(102);
	assert(syscall_number_filter[102] == f.must_handle);
	assert(syscall_needs_fd_rlock[102] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[102] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[102] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[102] == f.cwd_rlock);

	f = get_early_filter_entry(103);
	assert(syscall_number_filter[103] == f.must_handle);
	assert(syscall_needs_fd_rlock[103] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[103] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[103] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[103] == f.cwd_rlock);

	f = get_early_filter_entry(104);
	assert(syscall_number_filter[104] == f.must_handle);
	assert(syscall_needs_fd_rlock[104] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[104] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[104] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[104] == f.cwd_rlock);

	f = get_early_filter_entry(105);
	assert(syscall_number_filter[105] == f.must_handle);
	assert(syscall_needs_fd_rlock[105] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[105] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[105] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[105] == f.cwd_rlock);

	f = get_early_filter_entry(106);
	assert(syscall_number_filter[106] == f.must_handle);
	assert(syscall_needs_fd_rlock[106] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[106] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[106] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[106] == f.cwd_rlock);

	f = get_early_filter_entry(107);
	assert(syscall_number_filter[107] == f.must_handle);
	assert(syscall_needs_fd_rlock[107] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[107] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[107] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[107] == f.cwd_rlock);

	f = get_early_filter_entry(108);
	assert(syscall_number_filter[108] == f.must_handle);
	assert(syscall_needs_fd_rlock[108] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[108] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[108] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[108] == f.cwd_rlock);

	f = get_early_filter_entry(109);
	assert(syscall_number_filter[109] == f.must_handle);
	assert(syscall_needs_fd_rlock[109] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[109] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[109] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[109] == f.cwd_rlock);

	f = get_early_filter_entry(110);
	assert(syscall_number_filter[110] == f.must_handle);
	assert(syscall_needs_fd_rlock[110] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[110] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[110] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[110] == f.cwd_rlock);

	f = get_early_filter_entry(111);
	assert(syscall_number_filter[111] == f.must_handle);
	assert(syscall_needs_fd_rlock[111] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[111] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[111] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[111] == f.cwd_rlock);

	f = get_early_filter_entry(112);
	assert(syscall_number_filter[112] == f.must_handle);
	assert(syscall_needs_fd_rlock[112] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[112] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[112] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[112] == f.cwd_rlock);

	f = get_early_filter_entry(113);
	assert(syscall_number_filter[113] == f.must_handle);
	assert(syscall_needs_fd_rlock[113] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[113] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[113] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[113] == f.cwd_rlock);

	f = get_early_filter_entry(114);
	assert(syscall_number_filter[114] == f.must_handle);
	assert(syscall_needs_fd_rlock[114] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[114] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[114] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[114] == f.cwd_rlock);

	f = get_early_filter_entry(115);
	assert(syscall_number_filter[115] == f.must_handle);
	assert(syscall_needs_fd_rlock[115] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[115] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[115] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[115] == f.cwd_rlock);

	f = get_early_filter_entry(116);
	assert(syscall_number_filter[116] == f.must_handle);
	assert(syscall_needs_fd_rlock[116] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[116] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[116] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[116] == f.cwd_rlock);

	f = get_early_filter_entry(117);
	assert(syscall_number_filter[117] == f.must_handle);
	assert(syscall_needs_fd_rlock[117] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[117] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[117] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[117] == f.cwd_rlock);

	f = get_early_filter_entry(118);
	assert(syscall_number_filter[118] == f.must_handle);
	assert(syscall_needs_fd_rlock[118] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[118] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[118] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[118] == f.cwd_rlock);

	f = get_early_filter_entry(119);
	assert(syscall_number_filter[119] == f.must_handle);
	assert(syscall_needs_fd_rlock[119] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[119] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[119] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[119] == f.cwd_rlock);

	f = get_early_filter_entry(120);
	assert(syscall_number_filter[120] == f.must_handle);
	assert(syscall_needs_fd_rlock[120] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[120] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[120] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[120] == f.cwd_rlock);

	f = get_early_filter_entry(121);
	assert(syscall_number_filter[121] == f.must_handle);
	assert(syscall_needs_fd_rlock[121] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[121] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[121] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[121] == f.cwd_rlock);

	f = get_early_filter_entry(122);
	assert(syscall_number_filter[122] == f.must_handle);
	assert(syscall_needs_fd_rlock[122] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[122] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[122] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[122] == f.cwd_rlock);

	f = get_early_filter_entry(123);
	assert(syscall_number_filter[123] == f.must_handle);
	assert(syscall_needs_fd_rlock[123] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[123] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[123] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[123] == f.cwd_rlock);

	f = get_early_filter_entry(124);
	assert(syscall_number_filter[124] == f.must_handle);
	assert(syscall_needs_fd_rlock[124] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[124] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[124] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[124] == f.cwd_rlock);

	f = get_early_filter_entry(125);
	assert(syscall_number_filter[125] == f.must_handle);
	assert(syscall_needs_fd_rlock[125] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[125] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[125] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[125] == f.cwd_rlock);

	f = get_early_filter_entry(126);
	assert(syscall_number_filter[126] == f.must_handle);
	assert(syscall_needs_fd_rlock[126] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[126] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[126] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[126] == f.cwd_rlock);

	f = get_early_filter_entry(127);
	assert(syscall_number_filter[127] == f.must_handle);
	assert(syscall_needs_fd_rlock[127] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[127] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[127] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[127] == f.cwd_rlock);

	f = get_early_filter_entry(128);
	assert(syscall_number_filter[128] == f.must_handle);
	assert(syscall_needs_fd_rlock[128] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[128] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[128] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[128] == f.cwd_rlock);

	f = get_early_filter_entry(129);
	assert(syscall_number_filter[129] == f.must_handle);
	assert(syscall_needs_fd_rlock[129] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[129] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[129] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[129] == f.cwd_rlock);

	f = get_early_filter_entry(130);
	assert(syscall_number_filter[130] == f.must_handle);
	assert(syscall_needs_fd_rlock[130] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[130] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[130] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[130] == f.cwd_rlock);

	f = get_early_filter_entry(131);
	assert(syscall_number_filter[131] == f.must_handle);
	assert(syscall_needs_fd_rlock[131] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[131] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[131] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[131] == f.cwd_rlock);

	f = get_early_filter_entry(132);
	assert(syscall_number_filter[132] == f.must_handle);
	assert(syscall_needs_fd_rlock[132] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[132] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[132] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[132] == f.cwd_rlock);

	f = get_early_filter_entry(133);
	assert(syscall_number_filter[133] == f.must_handle);
	assert(syscall_needs_fd_rlock[133] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[133] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[133] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[133] == f.cwd_rlock);

	f = get_early_filter_entry(134);
	assert(syscall_number_filter[134] == f.must_handle);
	assert(syscall_needs_fd_rlock[134] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[134] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[134] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[134] == f.cwd_rlock);

	f = get_early_filter_entry(135);
	assert(syscall_number_filter[135] == f.must_handle);
	assert(syscall_needs_fd_rlock[135] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[135] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[135] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[135] == f.cwd_rlock);

	f = get_early_filter_entry(136);
	assert(syscall_number_filter[136] == f.must_handle);
	assert(syscall_needs_fd_rlock[136] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[136] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[136] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[136] == f.cwd_rlock);

	f = get_early_filter_entry(137);
	assert(syscall_number_filter[137] == f.must_handle);
	assert(syscall_needs_fd_rlock[137] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[137] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[137] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[137] == f.cwd_rlock);

	f = get_early_filter_entry(138);
	assert(syscall_number_filter[138] == f.must_handle);
	assert(syscall_needs_fd_rlock[138] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[138] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[138] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[138] == f.cwd_rlock);

	f = get_early_filter_entry(139);
	assert(syscall_number_filter[139] == f.must_handle);
	assert(syscall_needs_fd_rlock[139] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[139] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[139] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[139] == f.cwd_rlock);

	f = get_early_filter_entry(140);
	assert(syscall_number_filter[140] == f.must_handle);
	assert(syscall_needs_fd_rlock[140] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[140] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[140] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[140] == f.cwd_rlock);

	f = get_early_filter_entry(141);
	assert(syscall_number_filter[141] == f.must_handle);
	assert(syscall_needs_fd_rlock[141] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[141] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[141] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[141] == f.cwd_rlock);

	f = get_early_filter_entry(142);
	assert(syscall_number_filter[142] == f.must_handle);
	assert(syscall_needs_fd_rlock[142] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[142] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[142] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[142] == f.cwd_rlock);

	f = get_early_filter_entry(143);
	assert(syscall_number_filter[143] == f.must_handle);
	assert(syscall_needs_fd_rlock[143] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[143] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[143] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[143] == f.cwd_rlock);

	f = get_early_filter_entry(144);
	assert(syscall_number_filter[144] == f.must_handle);
	assert(syscall_needs_fd_rlock[144] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[144] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[144] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[144] == f.cwd_rlock);

	f = get_early_filter_entry(145);
	assert(syscall_number_filter[145] == f.must_handle);
	assert(syscall_needs_fd_rlock[145] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[145] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[145] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[145] == f.cwd_rlock);

	f = get_early_filter_entry(146);
	assert(syscall_number_filter[146] == f.must_handle);
	assert(syscall_needs_fd_rlock[146] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[146] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[146] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[146] == f.cwd_rlock);

	f = get_early_filter_entry(147);
	assert(syscall_number_filter[147] == f.must_handle);
	assert(syscall_needs_fd_rlock[147] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[147] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[147] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[147] == f.cwd_rlock);

	f = get_early_filter_entry(148);
	assert(syscall_number_filter[148] == f.must_handle);
	assert(syscall_needs_fd_rlock[148] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[148] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[148] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[148] == f.cwd_rlock);

	f = get_early_filter_entry(149);
	assert(syscall_number_filter[149] == f.must_handle);
	assert(syscall_needs_fd_rlock[149] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[149] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[149] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[149] == f.cwd_rlock);

	f = get_early_filter_entry(150);
	assert(syscall_number_filter[150] == f.must_handle);
	assert(syscall_needs_fd_rlock[150] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[150] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[150] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[150] == f.cwd_rlock);

	f = get_early_filter_entry(151);
	assert(syscall_number_filter[151] == f.must_handle);
	assert(syscall_needs_fd_rlock[151] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[151] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[151] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[151] == f.cwd_rlock);

	f = get_early_filter_entry(152);
	assert(syscall_number_filter[152] == f.must_handle);
	assert(syscall_needs_fd_rlock[152] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[152] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[152] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[152] == f.cwd_rlock);

	f = get_early_filter_entry(153);
	assert(syscall_number_filter[153] == f.must_handle);
	assert(syscall_needs_fd_rlock[153] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[153] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[153] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[153] == f.cwd_rlock);

	f = get_early_filter_entry(154);
	assert(syscall_number_filter[154] == f.must_handle);
	assert(syscall_needs_fd_rlock[154] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[154] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[154] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[154] == f.cwd_rlock);

	f = get_early_filter_entry(155);
	assert(syscall_number_filter[155] == f.must_handle);
	assert(syscall_needs_fd_rlock[155] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[155] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[155] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[155] == f.cwd_rlock);

	f = get_early_filter_entry(156);
	assert(syscall_number_filter[156] == f.must_handle);
	assert(syscall_needs_fd_rlock[156] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[156] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[156] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[156] == f.cwd_rlock);

	f = get_early_filter_entry(157);
	assert(syscall_number_filter[157] == f.must_handle);
	assert(syscall_needs_fd_rlock[157] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[157] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[157] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[157] == f.cwd_rlock);

	f = get_early_filter_entry(158);
	assert(syscall_number_filter[158] == f.must_handle);
	assert(syscall_needs_fd_rlock[158] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[158] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[158] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[158] == f.cwd_rlock);

	f = get_early_filter_entry(159);
	assert(syscall_number_filter[159] == f.must_handle);
	assert(syscall_needs_fd_rlock[159] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[159] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[159] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[159] == f.cwd_rlock);

	f = get_early_filter_entry(160);
	assert(syscall_number_filter[160] == f.must_handle);
	assert(syscall_needs_fd_rlock[160] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[160] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[160] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[160] == f.cwd_rlock);

	f = get_early_filter_entry(161);
	assert(syscall_number_filter[161] == f.must_handle);
	assert(syscall_needs_fd_rlock[161] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[161] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[161] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[161] == f.cwd_rlock);

	f = get_early_filter_entry(162);
	assert(syscall_number_filter[162] == f.must_handle);
	assert(syscall_needs_fd_rlock[162] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[162] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[162] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[162] == f.cwd_rlock);

	f = get_early_filter_entry(163);
	assert(syscall_number_filter[163] == f.must_handle);
	assert(syscall_needs_fd_rlock[163] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[163] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[163] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[163] == f.cwd_rlock);

	f = get_early_filter_entry(164);
	assert(syscall_number_filter[164] == f.must_handle);
	assert(syscall_needs_fd_rlock[164] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[164] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[164] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[164] == f.cwd_rlock);

	f = get_early_filter_entry(165);
	assert(syscall_number_filter[165] == f.must_handle);
	assert(syscall_needs_fd_rlock[165] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[165] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[165] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[165] == f.cwd_rlock);

	f = get_early_filter_entry(166);
	assert(syscall_number_filter[166] == f.must_handle);
	assert(syscall_needs_fd_rlock[166] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[166] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[166] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[166] == f.cwd_rlock);

	f = get_early_filter_entry(167);
	assert(syscall_number_filter[167] == f.must_handle);
	assert(syscall_needs_fd_rlock[167] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[167] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[167] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[167] == f.cwd_rlock);

	f = get_early_filter_entry(168);
	assert(syscall_number_filter[168] == f.must_handle);
	assert(syscall_needs_fd_rlock[168] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[168] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[168] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[168] == f.cwd_rlock);

	f = get_early_filter_entry(169);
	assert(syscall_number_filter[169] == f.must_handle);
	assert(syscall_needs_fd_rlock[169] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[169] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[169] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[169] == f.cwd_rlock);

	f = get_early_filter_entry(170);
	assert(syscall_number_filter[170] == f.must_handle);
	assert(syscall_needs_fd_rlock[170] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[170] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[170] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[170] == f.cwd_rlock);

	f = get_early_filter_entry(171);
	assert(syscall_number_filter[171] == f.must_handle);
	assert(syscall_needs_fd_rlock[171] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[171] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[171] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[171] == f.cwd_rlock);

	f = get_early_filter_entry(172);
	assert(syscall_number_filter[172] == f.must_handle);
	assert(syscall_needs_fd_rlock[172] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[172] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[172] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[172] == f.cwd_rlock);

	f = get_early_filter_entry(173);
	assert(syscall_number_filter[173] == f.must_handle);
	assert(syscall_needs_fd_rlock[173] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[173] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[173] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[173] == f.cwd_rlock);

	f = get_early_filter_entry(174);
	assert(syscall_number_filter[174] == f.must_handle);
	assert(syscall_needs_fd_rlock[174] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[174] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[174] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[174] == f.cwd_rlock);

	f = get_early_filter_entry(175);
	assert(syscall_number_filter[175] == f.must_handle);
	assert(syscall_needs_fd_rlock[175] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[175] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[175] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[175] == f.cwd_rlock);

	f = get_early_filter_entry(176);
	assert(syscall_number_filter[176] == f.must_handle);
	assert(syscall_needs_fd_rlock[176] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[176] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[176] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[176] == f.cwd_rlock);

	f = get_early_filter_entry(177);
	assert(syscall_number_filter[177] == f.must_handle);
	assert(syscall_needs_fd_rlock[177] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[177] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[177] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[177] == f.cwd_rlock);

	f = get_early_filter_entry(178);
	assert(syscall_number_filter[178] == f.must_handle);
	assert(syscall_needs_fd_rlock[178] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[178] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[178] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[178] == f.cwd_rlock);

	f = get_early_filter_entry(179);
	assert(syscall_number_filter[179] == f.must_handle);
	assert(syscall_needs_fd_rlock[179] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[179] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[179] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[179] == f.cwd_rlock);

	f = get_early_filter_entry(180);
	assert(syscall_number_filter[180] == f.must_handle);
	assert(syscall_needs_fd_rlock[180] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[180] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[180] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[180] == f.cwd_rlock);

	f = get_early_filter_entry(181);
	assert(syscall_number_filter[181] == f.must_handle);
	assert(syscall_needs_fd_rlock[181] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[181] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[181] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[181] == f.cwd_rlock);

	f = get_early_filter_entry(182);
	assert(syscall_number_filter[182] == f.must_handle);
	assert(syscall_needs_fd_rlock[182] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[182] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[182] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[182] == f.cwd_rlock);

	f = get_early_filter_entry(183);
	assert(syscall_number_filter[183] == f.must_handle);
	assert(syscall_needs_fd_rlock[183] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[183] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[183] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[183] == f.cwd_rlock);

	f = get_early_filter_entry(184);
	assert(syscall_number_filter[184] == f.must_handle);
	assert(syscall_needs_fd_rlock[184] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[184] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[184] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[184] == f.cwd_rlock);

	f = get_early_filter_entry(185);
	assert(syscall_number_filter[185] == f.must_handle);
	assert(syscall_needs_fd_rlock[185] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[185] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[185] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[185] == f.cwd_rlock);

	f = get_early_filter_entry(186);
	assert(syscall_number_filter[186] == f.must_handle);
	assert(syscall_needs_fd_rlock[186] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[186] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[186] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[186] == f.cwd_rlock);

	f = get_early_filter_entry(187);
	assert(syscall_number_filter[187] == f.must_handle);
	assert(syscall_needs_fd_rlock[187] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[187] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[187] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[187] == f.cwd_rlock);

	f = get_early_filter_entry(188);
	assert(syscall_number_filter[188] == f.must_handle);
	assert(syscall_needs_fd_rlock[188] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[188] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[188] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[188] == f.cwd_rlock);

	f = get_early_filter_entry(189);
	assert(syscall_number_filter[189] == f.must_handle);
	assert(syscall_needs_fd_rlock[189] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[189] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[189] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[189] == f.cwd_rlock);

	f = get_early_filter_entry(190);
	assert(syscall_number_filter[190] == f.must_handle);
	assert(syscall_needs_fd_rlock[190] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[190] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[190] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[190] == f.cwd_rlock);

	f = get_early_filter_entry(191);
	assert(syscall_number_filter[191] == f.must_handle);
	assert(syscall_needs_fd_rlock[191] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[191] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[191] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[191] == f.cwd_rlock);

	f = get_early_filter_entry(192);
	assert(syscall_number_filter[192] == f.must_handle);
	assert(syscall_needs_fd_rlock[192] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[192] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[192] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[192] == f.cwd_rlock);

	f = get_early_filter_entry(193);
	assert(syscall_number_filter[193] == f.must_handle);
	assert(syscall_needs_fd_rlock[193] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[193] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[193] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[193] == f.cwd_rlock);

	f = get_early_filter_entry(194);
	assert(syscall_number_filter[194] == f.must_handle);
	assert(syscall_needs_fd_rlock[194] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[194] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[194] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[194] == f.cwd_rlock);

	f = get_early_filter_entry(195);
	assert(syscall_number_filter[195] == f.must_handle);
	assert(syscall_needs_fd_rlock[195] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[195] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[195] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[195] == f.cwd_rlock);

	f = get_early_filter_entry(196);
	assert(syscall_number_filter[196] == f.must_handle);
	assert(syscall_needs_fd_rlock[196] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[196] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[196] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[196] == f.cwd_rlock);

	f = get_early_filter_entry(197);
	assert(syscall_number_filter[197] == f.must_handle);
	assert(syscall_needs_fd_rlock[197] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[197] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[197] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[197] == f.cwd_rlock);

	f = get_early_filter_entry(198);
	assert(syscall_number_filter[198] == f.must_handle);
	assert(syscall_needs_fd_rlock[198] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[198] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[198] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[198] == f.cwd_rlock);

	f = get_early_filter_entry(199);
	assert(syscall_number_filter[199] == f.must_handle);
	assert(syscall_needs_fd_rlock[199] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[199] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[199] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[199] == f.cwd_rlock);

	f = get_early_filter_entry(200);
	assert(syscall_number_filter[200] == f.must_handle);
	assert(syscall_needs_fd_rlock[200] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[200] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[200] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[200] == f.cwd_rlock);

	f = get_early_filter_entry(201);
	assert(syscall_number_filter[201] == f.must_handle);
	assert(syscall_needs_fd_rlock[201] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[201] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[201] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[201] == f.cwd_rlock);

	f = get_early_filter_entry(202);
	assert(syscall_number_filter[202] == f.must_handle);
	assert(syscall_needs_fd_rlock[202] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[202] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[202] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[202] == f.cwd_rlock);

	f = get_early_filter_entry(203);
	assert(syscall_number_filter[203] == f.must_handle);
	assert(syscall_needs_fd_rlock[203] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[203] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[203] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[203] == f.cwd_rlock);

	f = get_early_filter_entry(204);
	assert(syscall_number_filter[204] == f.must_handle);
	assert(syscall_needs_fd_rlock[204] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[204] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[204] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[204] == f.cwd_rlock);

	f = get_early_filter_entry(205);
	assert(syscall_number_filter[205] == f.must_handle);
	assert(syscall_needs_fd_rlock[205] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[205] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[205] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[205] == f.cwd_rlock);

	f = get_early_filter_entry(206);
	assert(syscall_number_filter[206] == f.must_handle);
	assert(syscall_needs_fd_rlock[206] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[206] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[206] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[206] == f.cwd_rlock);

	f = get_early_filter_entry(207);
	assert(syscall_number_filter[207] == f.must_handle);
	assert(syscall_needs_fd_rlock[207] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[207] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[207] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[207] == f.cwd_rlock);

	f = get_early_filter_entry(208);
	assert(syscall_number_filter[208] == f.must_handle);
	assert(syscall_needs_fd_rlock[208] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[208] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[208] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[208] == f.cwd_rlock);

	f = get_early_filter_entry(209);
	assert(syscall_number_filter[209] == f.must_handle);
	assert(syscall_needs_fd_rlock[209] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[209] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[209] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[209] == f.cwd_rlock);

	f = get_early_filter_entry(210);
	assert(syscall_number_filter[210] == f.must_handle);
	assert(syscall_needs_fd_rlock[210] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[210] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[210] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[210] == f.cwd_rlock);

	f = get_early_filter_entry(211);
	assert(syscall_number_filter[211] == f.must_handle);
	assert(syscall_needs_fd_rlock[211] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[211] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[211] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[211] == f.cwd_rlock);

	f = get_early_filter_entry(212);
	assert(syscall_number_filter[212] == f.must_handle);
	assert(syscall_needs_fd_rlock[212] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[212] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[212] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[212] == f.cwd_rlock);

	f = get_early_filter_entry(213);
	assert(syscall_number_filter[213] == f.must_handle);
	assert(syscall_needs_fd_rlock[213] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[213] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[213] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[213] == f.cwd_rlock);

	f = get_early_filter_entry(214);
	assert(syscall_number_filter[214] == f.must_handle);
	assert(syscall_needs_fd_rlock[214] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[214] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[214] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[214] == f.cwd_rlock);

	f = get_early_filter_entry(215);
	assert(syscall_number_filter[215] == f.must_handle);
	assert(syscall_needs_fd_rlock[215] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[215] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[215] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[215] == f.cwd_rlock);

	f = get_early_filter_entry(216);
	assert(syscall_number_filter[216] == f.must_handle);
	assert(syscall_needs_fd_rlock[216] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[216] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[216] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[216] == f.cwd_rlock);

	f = get_early_filter_entry(217);
	assert(syscall_number_filter[217] == f.must_handle);
	assert(syscall_needs_fd_rlock[217] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[217] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[217] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[217] == f.cwd_rlock);

	f = get_early_filter_entry(218);
	assert(syscall_number_filter[218] == f.must_handle);
	assert(syscall_needs_fd_rlock[218] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[218] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[218] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[218] == f.cwd_rlock);

	f = get_early_filter_entry(219);
	assert(syscall_number_filter[219] == f.must_handle);
	assert(syscall_needs_fd_rlock[219] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[219] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[219] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[219] == f.cwd_rlock);

	f = get_early_filter_entry(220);
	assert(syscall_number_filter[220] == f.must_handle);
	assert(syscall_needs_fd_rlock[220] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[220] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[220] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[220] == f.cwd_rlock);

	f = get_early_filter_entry(221);
	assert(syscall_number_filter[221] == f.must_handle);
	assert(syscall_needs_fd_rlock[221] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[221] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[221] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[221] == f.cwd_rlock);

	f = get_early_filter_entry(222);
	assert(syscall_number_filter[222] == f.must_handle);
	assert(syscall_needs_fd_rlock[222] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[222] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[222] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[222] == f.cwd_rlock);

	f = get_early_filter_entry(223);
	assert(syscall_number_filter[223] == f.must_handle);
	assert(syscall_needs_fd_rlock[223] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[223] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[223] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[223] == f.cwd_rlock);

	f = get_early_filter_entry(224);
	assert(syscall_number_filter[224] == f.must_handle);
	assert(syscall_needs_fd_rlock[224] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[224] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[224] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[224] == f.cwd_rlock);

	f = get_early_filter_entry(225);
	assert(syscall_number_filter[225] == f.must_handle);
	assert(syscall_needs_fd_rlock[225] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[225] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[225] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[225] == f.cwd_rlock);

	f = get_early_filter_entry(226);
	assert(syscall_number_filter[226] == f.must_handle);
	assert(syscall_needs_fd_rlock[226] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[226] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[226] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[226] == f.cwd_rlock);

	f = get_early_filter_entry(227);
	assert(syscall_number_filter[227] == f.must_handle);
	assert(syscall_needs_fd_rlock[227] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[227] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[227] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[227] == f.cwd_rlock);

	f = get_early_filter_entry(228);
	assert(syscall_number_filter[228] == f.must_handle);
	assert(syscall_needs_fd_rlock[228] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[228] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[228] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[228] == f.cwd_rlock);

	f = get_early_filter_entry(229);
	assert(syscall_number_filter[229] == f.must_handle);
	assert(syscall_needs_fd_rlock[229] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[229] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[229] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[229] == f.cwd_rlock);

	f = get_early_filter_entry(230);
	assert(syscall_number_filter[230] == f.must_handle);
	assert(syscall_needs_fd_rlock[230] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[230] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[230] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[230] == f.cwd_rlock);

	f = get_early_filter_entry(231);
	assert(syscall_number_filter[231] == f.must_handle);
	assert(syscall_needs_fd_rlock[231] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[231] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[231] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[231] == f.cwd_rlock);

	f = get_early_filter_entry(232);
	assert(syscall_number_filter[232] == f.must_handle);
	assert(syscall_needs_fd_rlock[232] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[232] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[232] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[232] == f.cwd_rlock);

	f = get_early_filter_entry(233);
	assert(syscall_number_filter[233] == f.must_handle);
	assert(syscall_needs_fd_rlock[233] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[233] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[233] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[233] == f.cwd_rlock);

	f = get_early_filter_entry(234);
	assert(syscall_number_filter[234] == f.must_handle);
	assert(syscall_needs_fd_rlock[234] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[234] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[234] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[234] == f.cwd_rlock);

	f = get_early_filter_entry(235);
	assert(syscall_number_filter[235] == f.must_handle);
	assert(syscall_needs_fd_rlock[235] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[235] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[235] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[235] == f.cwd_rlock);

	f = get_early_filter_entry(236);
	assert(syscall_number_filter[236] == f.must_handle);
	assert(syscall_needs_fd_rlock[236] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[236] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[236] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[236] == f.cwd_rlock);

	f = get_early_filter_entry(237);
	assert(syscall_number_filter[237] == f.must_handle);
	assert(syscall_needs_fd_rlock[237] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[237] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[237] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[237] == f.cwd_rlock);

	f = get_early_filter_entry(238);
	assert(syscall_number_filter[238] == f.must_handle);
	assert(syscall_needs_fd_rlock[238] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[238] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[238] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[238] == f.cwd_rlock);

	f = get_early_filter_entry(239);
	assert(syscall_number_filter[239] == f.must_handle);
	assert(syscall_needs_fd_rlock[239] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[239] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[239] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[239] == f.cwd_rlock);

	f = get_early_filter_entry(240);
	assert(syscall_number_filter[240] == f.must_handle);
	assert(syscall_needs_fd_rlock[240] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[240] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[240] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[240] == f.cwd_rlock);

	f = get_early_filter_entry(241);
	assert(syscall_number_filter[241] == f.must_handle);
	assert(syscall_needs_fd_rlock[241] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[241] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[241] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[241] == f.cwd_rlock);

	f = get_early_filter_entry(242);
	assert(syscall_number_filter[242] == f.must_handle);
	assert(syscall_needs_fd_rlock[242] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[242] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[242] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[242] == f.cwd_rlock);

	f = get_early_filter_entry(243);
	assert(syscall_number_filter[243] == f.must_handle);
	assert(syscall_needs_fd_rlock[243] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[243] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[243] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[243] == f.cwd_rlock);

	f = get_early_filter_entry(244);
	assert(syscall_number_filter[244] == f.must_handle);
	assert(syscall_needs_fd_rlock[244] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[244] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[244] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[244] == f.cwd_rlock);

	f = get_early_filter_entry(245);
	assert(syscall_number_filter[245] == f.must_handle);
	assert(syscall_needs_fd_rlock[245] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[245] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[245] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[245] == f.cwd_rlock);

	f = get_early_filter_entry(246);
	assert(syscall_number_filter[246] == f.must_handle);
	assert(syscall_needs_fd_rlock[246] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[246] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[246] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[246] == f.cwd_rlock);

	f = get_early_filter_entry(247);
	assert(syscall_number_filter[247] == f.must_handle);
	assert(syscall_needs_fd_rlock[247] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[247] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[247] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[247] == f.cwd_rlock);

	f = get_early_filter_entry(248);
	assert(syscall_number_filter[248] == f.must_handle);
	assert(syscall_needs_fd_rlock[248] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[248] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[248] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[248] == f.cwd_rlock);

	f = get_early_filter_entry(249);
	assert(syscall_number_filter[249] == f.must_handle);
	assert(syscall_needs_fd_rlock[249] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[249] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[249] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[249] == f.cwd_rlock);

	f = get_early_filter_entry(250);
	assert(syscall_number_filter[250] == f.must_handle);
	assert(syscall_needs_fd_rlock[250] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[250] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[250] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[250] == f.cwd_rlock);

	f = get_early_filter_entry(251);
	assert(syscall_number_filter[251] == f.must_handle);
	assert(syscall_needs_fd_rlock[251] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[251] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[251] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[251] == f.cwd_rlock);

	f = get_early_filter_entry(252);
	assert(syscall_number_filter[252] == f.must_handle);
	assert(syscall_needs_fd_rlock[252] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[252] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[252] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[252] == f.cwd_rlock);

	f = get_early_filter_entry(253);
	assert(syscall_number_filter[253] == f.must_handle);
	assert(syscall_needs_fd_rlock[253] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[253] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[253] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[253] == f.cwd_rlock);

	f = get_early_filter_entry(254);
	assert(syscall_number_filter[254] == f.must_handle);
	assert(syscall_needs_fd_rlock[254] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[254] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[254] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[254] == f.cwd_rlock);

	f = get_early_filter_entry(255);
	assert(syscall_number_filter[255] == f.must_handle);
	assert(syscall_needs_fd_rlock[255] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[255] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[255] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[255] == f.cwd_rlock);

	f = get_early_filter_entry(256);
	assert(syscall_number_filter[256] == f.must_handle);
	assert(syscall_needs_fd_rlock[256] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[256] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[256] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[256] == f.cwd_rlock);

	f = get_early_filter_entry(257);
	assert(syscall_number_filter[257] == f.must_handle);
	assert(syscall_needs_fd_rlock[257] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[257] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[257] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[257] == f.cwd_rlock);

	f = get_early_filter_entry(258);
	assert(syscall_number_filter[258] == f.must_handle);
	assert(syscall_needs_fd_rlock[258] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[258] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[258] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[258] == f.cwd_rlock);

	f = get_early_filter_entry(259);
	assert(syscall_number_filter[259] == f.must_handle);
	assert(syscall_needs_fd_rlock[259] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[259] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[259] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[259] == f.cwd_rlock);

	f = get_early_filter_entry(260);
	assert(syscall_number_filter[260] == f.must_handle);
	assert(syscall_needs_fd_rlock[260] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[260] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[260] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[260] == f.cwd_rlock);

	f = get_early_filter_entry(261);
	assert(syscall_number_filter[261] == f.must_handle);
	assert(syscall_needs_fd_rlock[261] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[261] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[261] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[261] == f.cwd_rlock);

	f = get_early_filter_entry(262);
	assert(syscall_number_filter[262] == f.must_handle);
	assert(syscall_needs_fd_rlock[262] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[262] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[262] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[262] == f.cwd_rlock);

	f = get_early_filter_entry(263);
	assert(syscall_number_filter[263] == f.must_handle);
	assert(syscall_needs_fd_rlock[263] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[263] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[263] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[263] == f.cwd_rlock);

	f = get_early_filter_entry(264);
	assert(syscall_number_filter[264] == f.must_handle);
	assert(syscall_needs_fd_rlock[264] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[264] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[264] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[264] == f.cwd_rlock);

	f = get_early_filter_entry(265);
	assert(syscall_number_filter[265] == f.must_handle);
	assert(syscall_needs_fd_rlock[265] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[265] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[265] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[265] == f.cwd_rlock);

	f = get_early_filter_entry(266);
	assert(syscall_number_filter[266] == f.must_handle);
	assert(syscall_needs_fd_rlock[266] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[266] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[266] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[266] == f.cwd_rlock);

	f = get_early_filter_entry(267);
	assert(syscall_number_filter[267] == f.must_handle);
	assert(syscall_needs_fd_rlock[267] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[267] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[267] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[267] == f.cwd_rlock);

	f = get_early_filter_entry(268);
	assert(syscall_number_filter[268] == f.must_handle);
	assert(syscall_needs_fd_rlock[268] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[268] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[268] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[268] == f.cwd_rlock);

	f = get_early_filter_entry(269);
	assert(syscall_number_filter[269] == f.must_handle);
	assert(syscall_needs_fd_rlock[269] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[269] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[269] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[269] == f.cwd_rlock);

	f = get_early_filter_entry(270);
	assert(syscall_number_filter[270] == f.must_handle);
	assert(syscall_needs_fd_rlock[270] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[270] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[270] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[270] == f.cwd_rlock);

	f = get_early_filter_entry(271);
	assert(syscall_number_filter[271] == f.must_handle);
	assert(syscall_needs_fd_rlock[271] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[271] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[271] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[271] == f.cwd_rlock);

	f = get_early_filter_entry(272);
	assert(syscall_number_filter[272] == f.must_handle);
	assert(syscall_needs_fd_rlock[272] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[272] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[272] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[272] == f.cwd_rlock);

	f = get_early_filter_entry(273);
	assert(syscall_number_filter[273] == f.must_handle);
	assert(syscall_needs_fd_rlock[273] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[273] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[273] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[273] == f.cwd_rlock);

	f = get_early_filter_entry(274);
	assert(syscall_number_filter[274] == f.must_handle);
	assert(syscall_needs_fd_rlock[274] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[274] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[274] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[274] == f.cwd_rlock);

	f = get_early_filter_entry(275);
	assert(syscall_number_filter[275] == f.must_handle);
	assert(syscall_needs_fd_rlock[275] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[275] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[275] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[275] == f.cwd_rlock);

	f = get_early_filter_entry(276);
	assert(syscall_number_filter[276] == f.must_handle);
	assert(syscall_needs_fd_rlock[276] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[276] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[276] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[276] == f.cwd_rlock);

	f = get_early_filter_entry(277);
	assert(syscall_number_filter[277] == f.must_handle);
	assert(syscall_needs_fd_rlock[277] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[277] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[277] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[277] == f.cwd_rlock);

	f = get_early_filter_entry(278);
	assert(syscall_number_filter[278] == f.must_handle);
	assert(syscall_needs_fd_rlock[278] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[278] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[278] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[278] == f.cwd_rlock);

	f = get_early_filter_entry(279);
	assert(syscall_number_filter[279] == f.must_handle);
	assert(syscall_needs_fd_rlock[279] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[279] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[279] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[279] == f.cwd_rlock);

	f = get_early_filter_entry(280);
	assert(syscall_number_filter[280] == f.must_handle);
	assert(syscall_needs_fd_rlock[280] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[280] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[280] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[280] == f.cwd_rlock);

	f = get_early_filter_entry(281);
	assert(syscall_number_filter[281] == f.must_handle);
	assert(syscall_needs_fd_rlock[281] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[281] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[281] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[281] == f.cwd_rlock);

	f = get_early_filter_entry(282);
	assert(syscall_number_filter[282] == f.must_handle);
	assert(syscall_needs_fd_rlock[282] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[282] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[282] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[282] == f.cwd_rlock);

	f = get_early_filter_entry(283);
	assert(syscall_number_filter[283] == f.must_handle);
	assert(syscall_needs_fd_rlock[283] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[283] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[283] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[283] == f.cwd_rlock);

	f = get_early_filter_entry(284);
	assert(syscall_number_filter[284] == f.must_handle);
	assert(syscall_needs_fd_rlock[284] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[284] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[284] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[284] == f.cwd_rlock);

	f = get_early_filter_entry(285);
	assert(syscall_number_filter[285] == f.must_handle);
	assert(syscall_needs_fd_rlock[285] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[285] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[285] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[285] == f.cwd_rlock);

	f = get_early_filter_entry(286);
	assert(syscall_number_filter[286] == f.must_handle);
	assert(syscall_needs_fd_rlock[286] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[286] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[286] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[286] == f.cwd_rlock);

	f = get_early_filter_entry(287);
	assert(syscall_number_filter[287] == f.must_handle);
	assert(syscall_needs_fd_rlock[287] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[287] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[287] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[287] == f.cwd_rlock);

	f = get_early_filter_entry(288);
	assert(syscall_number_filter[288] == f.must_handle);
	assert(syscall_needs_fd_rlock[288] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[288] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[288] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[288] == f.cwd_rlock);

	f = get_early_filter_entry(289);
	assert(syscall_number_filter[289] == f.must_handle);
	assert(syscall_needs_fd_rlock[289] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[289] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[289] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[289] == f.cwd_rlock);

	f = get_early_filter_entry(290);
	assert(syscall_number_filter[290] == f.must_handle);
	assert(syscall_needs_fd_rlock[290] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[290] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[290] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[290] == f.cwd_rlock);

	f = get_early_filter_entry(291);
	assert(syscall_number_filter[291] == f.must_handle);
	assert(syscall_needs_fd_rlock[291] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[291] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[291] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[291] == f.cwd_rlock);

	f = get_early_filter_entry(292);
	assert(syscall_number_filter[292] == f.must_handle);
	assert(syscall_needs_fd_rlock[292] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[292] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[292] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[292] == f.cwd_rlock);

	f = get_early_filter_entry(293);
	assert(syscall_number_filter[293] == f.must_handle);
	assert(syscall_needs_fd_rlock[293] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[293] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[293] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[293] == f.cwd_rlock);

	f = get_early_filter_entry(294);
	assert(syscall_number_filter[294] == f.must_handle);
	assert(syscall_needs_fd_rlock[294] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[294] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[294] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[294] == f.cwd_rlock);

	f = get_early_filter_entry(295);
	assert(syscall_number_filter[295] == f.must_handle);
	assert(syscall_needs_fd_rlock[295] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[295] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[295] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[295] == f.cwd_rlock);

	f = get_early_filter_entry(296);
	assert(syscall_number_filter[296] == f.must_handle);
	assert(syscall_needs_fd_rlock[296] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[296] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[296] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[296] == f.cwd_rlock);

	f = get_early_filter_entry(297);
	assert(syscall_number_filter[297] == f.must_handle);
	assert(syscall_needs_fd_rlock[297] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[297] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[297] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[297] == f.cwd_rlock);

	f = get_early_filter_entry(298);
	assert(syscall_number_filter[298] == f.must_handle);
	assert(syscall_needs_fd_rlock[298] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[298] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[298] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[298] == f.cwd_rlock);

	f = get_early_filter_entry(299);
	assert(syscall_number_filter[299] == f.must_handle);
	assert(syscall_needs_fd_rlock[299] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[299] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[299] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[299] == f.cwd_rlock);

	f = get_early_filter_entry(300);
	assert(syscall_number_filter[300] == f.must_handle);
	assert(syscall_needs_fd_rlock[300] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[300] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[300] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[300] == f.cwd_rlock);

	f = get_early_filter_entry(301);
	assert(syscall_number_filter[301] == f.must_handle);
	assert(syscall_needs_fd_rlock[301] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[301] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[301] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[301] == f.cwd_rlock);

	f = get_early_filter_entry(302);
	assert(syscall_number_filter[302] == f.must_handle);
	assert(syscall_needs_fd_rlock[302] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[302] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[302] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[302] == f.cwd_rlock);

	f = get_early_filter_entry(303);
	assert(syscall_number_filter[303] == f.must_handle);
	assert(syscall_needs_fd_rlock[303] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[303] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[303] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[303] == f.cwd_rlock);

	f = get_early_filter_entry(304);
	assert(syscall_number_filter[304] == f.must_handle);
	assert(syscall_needs_fd_rlock[304] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[304] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[304] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[304] == f.cwd_rlock);

	f = get_early_filter_entry(305);
	assert(syscall_number_filter[305] == f.must_handle);
	assert(syscall_needs_fd_rlock[305] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[305] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[305] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[305] == f.cwd_rlock);

	f = get_early_filter_entry(306);
	assert(syscall_number_filter[306] == f.must_handle);
	assert(syscall_needs_fd_rlock[306] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[306] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[306] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[306] == f.cwd_rlock);

	f = get_early_filter_entry(307);
	assert(syscall_number_filter[307] == f.must_handle);
	assert(syscall_needs_fd_rlock[307] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[307] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[307] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[307] == f.cwd_rlock);

	f = get_early_filter_entry(308);
	assert(syscall_number_filter[308] == f.must_handle);
	assert(syscall_needs_fd_rlock[308] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[308] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[308] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[308] == f.cwd_rlock);

	f = get_early_filter_entry(309);
	assert(syscall_number_filter[309] == f.must_handle);
	assert(syscall_needs_fd_rlock[309] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[309] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[309] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[309] == f.cwd_rlock);

	f = get_early_filter_entry(310);
	assert(syscall_number_filter[310] == f.must_handle);
	assert(syscall_needs_fd_rlock[310] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[310] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[310] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[310] == f.cwd_rlock);

	f = get_early_filter_entry(311);
	assert(syscall_number_filter[311] == f.must_handle);
	assert(syscall_needs_fd_rlock[311] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[311] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[311] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[311] == f.cwd_rlock);

	f = get_early_filter_entry(312);
	assert(syscall_number_filter[312] == f.must_handle);
	assert(syscall_needs_fd_rlock[312] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[312] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[312] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[312] == f.cwd_rlock);

	f = get_early_filter_entry(313);
	assert(syscall_number_filter[313] == f.must_handle);
	assert(syscall_needs_fd_rlock[313] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[313] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[313] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[313] == f.cwd_rlock);

	f = get_early_filter_entry(314);
	assert(syscall_number_filter[314] == f.must_handle);
	assert(syscall_needs_fd_rlock[314] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[314] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[314] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[314] == f.cwd_rlock);

	f = get_early_filter_entry(315);
	assert(syscall_number_filter[315] == f.must_handle);
	assert(syscall_needs_fd_rlock[315] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[315] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[315] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[315] == f.cwd_rlock);

	f = get_early_filter_entry(316);
	assert(syscall_number_filter[316] == f.must_handle);
	assert(syscall_needs_fd_rlock[316] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[316] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[316] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[316] == f.cwd_rlock);

	f = get_early_filter_entry(317);
	assert(syscall_number_filter[317] == f.must_handle);
	assert(syscall_needs_fd_rlock[317] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[317] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[317] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[317] == f.cwd_rlock);

	f = get_early_filter_entry(318);
	assert(syscall_number_filter[318] == f.must_handle);
	assert(syscall_needs_fd_rlock[318] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[318] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[318] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[318] == f.cwd_rlock);

	f = get_early_filter_entry(319);
	assert(syscall_number_filter[319] == f.must_handle);
	assert(syscall_needs_fd_rlock[319] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[319] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[319] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[319] == f.cwd_rlock);

	f = get_early_filter_entry(320);
	assert(syscall_number_filter[320] == f.must_handle);
	assert(syscall_needs_fd_rlock[320] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[320] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[320] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[320] == f.cwd_rlock);

	f = get_early_filter_entry(321);
	assert(syscall_number_filter[321] == f.must_handle);
	assert(syscall_needs_fd_rlock[321] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[321] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[321] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[321] == f.cwd_rlock);

	f = get_early_filter_entry(322);
	assert(syscall_number_filter[322] == f.must_handle);
	assert(syscall_needs_fd_rlock[322] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[322] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[322] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[322] == f.cwd_rlock);

	f = get_early_filter_entry(323);
	assert(syscall_number_filter[323] == f.must_handle);
	assert(syscall_needs_fd_rlock[323] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[323] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[323] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[323] == f.cwd_rlock);

	f = get_early_filter_entry(324);
	assert(syscall_number_filter[324] == f.must_handle);
	assert(syscall_needs_fd_rlock[324] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[324] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[324] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[324] == f.cwd_rlock);

	f = get_early_filter_entry(325);
	assert(syscall_number_filter[325] == f.must_handle);
	assert(syscall_needs_fd_rlock[325] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[325] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[325] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[325] == f.cwd_rlock);

	f = get_early_filter_entry(326);
	assert(syscall_number_filter[326] == f.must_handle);
	assert(syscall_needs_fd_rlock[326] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[326] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[326] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[326] == f.cwd_rlock);

	f = get_early_filter_entry(327);
	assert(syscall_number_filter[327] == f.must_handle);
	assert(syscall_needs_fd_rlock[327] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[327] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[327] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[327] == f.cwd_rlock);

	f = get_early_filter_entry(328);
	assert(syscall_number_filter[328] == f.must_handle);
	assert(syscall_needs_fd_rlock[328] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[328] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[328] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[328] == f.cwd_rlock);

	f = get_early_filter_entry(329);
	assert(syscall_number_filter[329] == f.must_handle);
	assert(syscall_needs_fd_rlock[329] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[329] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[329] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[329] == f.cwd_rlock);

	f = get_early_filter_entry(330);
	assert(syscall_number_filter[330] == f.must_handle);
	assert(syscall_needs_fd_rlock[330] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[330] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[330] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[330] == f.cwd_rlock);

	f = get_early_filter_entry(331);
	assert(syscall_number_filter[331] == f.must_handle);
	assert(syscall_needs_fd_rlock[331] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[331] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[331] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[331] == f.cwd_rlock);

	f = get_early_filter_entry(332);
	assert(syscall_number_filter[332] == f.must_handle);
	assert(syscall_needs_fd_rlock[332] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[332] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[332] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[332] == f.cwd_rlock);

	f = get_early_filter_entry(333);
	assert(syscall_number_filter[333] == f.must_handle);
	assert(syscall_needs_fd_rlock[333] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[333] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[333] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[333] == f.cwd_rlock);

	f = get_early_filter_entry(334);
	assert(syscall_number_filter[334] == f.must_handle);
	assert(syscall_needs_fd_rlock[334] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[334] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[334] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[334] == f.cwd_rlock);

	f = get_early_filter_entry(335);
	assert(syscall_number_filter[335] == f.must_handle);
	assert(syscall_needs_fd_rlock[335] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[335] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[335] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[335] == f.cwd_rlock);

	f = get_early_filter_entry(336);
	assert(syscall_number_filter[336] == f.must_handle);
	assert(syscall_needs_fd_rlock[336] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[336] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[336] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[336] == f.cwd_rlock);

	f = get_early_filter_entry(337);
	assert(syscall_number_filter[337] == f.must_handle);
	assert(syscall_needs_fd_rlock[337] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[337] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[337] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[337] == f.cwd_rlock);

	f = get_early_filter_entry(338);
	assert(syscall_number_filter[338] == f.must_handle);
	assert(syscall_needs_fd_rlock[338] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[338] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[338] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[338] == f.cwd_rlock);

	f = get_early_filter_entry(339);
	assert(syscall_number_filter[339] == f.must_handle);
	assert(syscall_needs_fd_rlock[339] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[339] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[339] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[339] == f.cwd_rlock);

	f = get_early_filter_entry(340);
	assert(syscall_number_filter[340] == f.must_handle);
	assert(syscall_needs_fd_rlock[340] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[340] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[340] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[340] == f.cwd_rlock);

	f = get_early_filter_entry(341);
	assert(syscall_number_filter[341] == f.must_handle);
	assert(syscall_needs_fd_rlock[341] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[341] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[341] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[341] == f.cwd_rlock);

	f = get_early_filter_entry(342);
	assert(syscall_number_filter[342] == f.must_handle);
	assert(syscall_needs_fd_rlock[342] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[342] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[342] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[342] == f.cwd_rlock);

	f = get_early_filter_entry(343);
	assert(syscall_number_filter[343] == f.must_handle);
	assert(syscall_needs_fd_rlock[343] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[343] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[343] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[343] == f.cwd_rlock);

	f = get_early_filter_entry(344);
	assert(syscall_number_filter[344] == f.must_handle);
	assert(syscall_needs_fd_rlock[344] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[344] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[344] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[344] == f.cwd_rlock);

	f = get_early_filter_entry(345);
	assert(syscall_number_filter[345] == f.must_handle);
	assert(syscall_needs_fd_rlock[345] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[345] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[345] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[345] == f.cwd_rlock);

	f = get_early_filter_entry(346);
	assert(syscall_number_filter[346] == f.must_handle);
	assert(syscall_needs_fd_rlock[346] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[346] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[346] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[346] == f.cwd_rlock);

	f = get_early_filter_entry(347);
	assert(syscall_number_filter[347] == f.must_handle);
	assert(syscall_needs_fd_rlock[347] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[347] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[347] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[347] == f.cwd_rlock);

	f = get_early_filter_entry(348);
	assert(syscall_number_filter[348] == f.must_handle);
	assert(syscall_needs_fd_rlock[348] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[348] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[348] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[348] == f.cwd_rlock);

	f = get_early_filter_entry(349);
	assert(syscall_number_filter[349] == f.must_handle);
	assert(syscall_needs_fd_rlock[349] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[349] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[349] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[349] == f.cwd_rlock);

	f = get_early_filter_entry(350);
	assert(syscall_number_filter[350] == f.must_handle);
	assert(syscall_needs_fd_rlock[350] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[350] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[350] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[350] == f.cwd_rlock);

	f = get_early_filter_entry(351);
	assert(syscall_number_filter[351] == f.must_handle);
	assert(syscall_needs_fd_rlock[351] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[351] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[351] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[351] == f.cwd_rlock);

	f = get_early_filter_entry(352);
	assert(syscall_number_filter[352] == f.must_handle);
	assert(syscall_needs_fd_rlock[352] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[352] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[352] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[352] == f.cwd_rlock);

	f = get_early_filter_entry(353);
	assert(syscall_number_filter[353] == f.must_handle);
	assert(syscall_needs_fd_rlock[353] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[353] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[353] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[353] == f.cwd_rlock);

	f = get_early_filter_entry(354);
	assert(syscall_number_filter[354] == f.must_handle);
	assert(syscall_needs_fd_rlock[354] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[354] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[354] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[354] == f.cwd_rlock);

	f = get_early_filter_entry(355);
	assert(syscall_number_filter[355] == f.must_handle);
	assert(syscall_needs_fd_rlock[355] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[355] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[355] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[355] == f.cwd_rlock);

	f = get_early_filter_entry(356);
	assert(syscall_number_filter[356] == f.must_handle);
	assert(syscall_needs_fd_rlock[356] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[356] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[356] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[356] == f.cwd_rlock);

	f = get_early_filter_entry(357);
	assert(syscall_number_filter[357] == f.must_handle);
	assert(syscall_needs_fd_rlock[357] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[357] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[357] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[357] == f.cwd_rlock);

	f = get_early_filter_entry(358);
	assert(syscall_number_filter[358] == f.must_handle);
	assert(syscall_needs_fd_rlock[358] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[358] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[358] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[358] == f.cwd_rlock);

	f = get_early_filter_entry(359);
	assert(syscall_number_filter[359] == f.must_handle);
	assert(syscall_needs_fd_rlock[359] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[359] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[359] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[359] == f.cwd_rlock);

	f = get_early_filter_entry(360);
	assert(syscall_number_filter[360] == f.must_handle);
	assert(syscall_needs_fd_rlock[360] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[360] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[360] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[360] == f.cwd_rlock);

	f = get_early_filter_entry(361);
	assert(syscall_number_filter[361] == f.must_handle);
	assert(syscall_needs_fd_rlock[361] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[361] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[361] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[361] == f.cwd_rlock);

	f = get_early_filter_entry(362);
	assert(syscall_number_filter[362] == f.must_handle);
	assert(syscall_needs_fd_rlock[362] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[362] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[362] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[362] == f.cwd_rlock);

	f = get_early_filter_entry(363);
	assert(syscall_number_filter[363] == f.must_handle);
	assert(syscall_needs_fd_rlock[363] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[363] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[363] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[363] == f.cwd_rlock);

	f = get_early_filter_entry(364);
	assert(syscall_number_filter[364] == f.must_handle);
	assert(syscall_needs_fd_rlock[364] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[364] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[364] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[364] == f.cwd_rlock);

	f = get_early_filter_entry(365);
	assert(syscall_number_filter[365] == f.must_handle);
	assert(syscall_needs_fd_rlock[365] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[365] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[365] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[365] == f.cwd_rlock);

	f = get_early_filter_entry(366);
	assert(syscall_number_filter[366] == f.must_handle);
	assert(syscall_needs_fd_rlock[366] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[366] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[366] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[366] == f.cwd_rlock);

	f = get_early_filter_entry(367);
	assert(syscall_number_filter[367] == f.must_handle);
	assert(syscall_needs_fd_rlock[367] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[367] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[367] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[367] == f.cwd_rlock);

	f = get_early_filter_entry(368);
	assert(syscall_number_filter[368] == f.must_handle);
	assert(syscall_needs_fd_rlock[368] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[368] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[368] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[368] == f.cwd_rlock);

	f = get_early_filter_entry(369);
	assert(syscall_number_filter[369] == f.must_handle);
	assert(syscall_needs_fd_rlock[369] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[369] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[369] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[369] == f.cwd_rlock);

	f = get_early_filter_entry(370);
	assert(syscall_number_filter[370] == f.must_handle);
	assert(syscall_needs_fd_rlock[370] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[370] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[370] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[370] == f.cwd_rlock);

	f = get_early_filter_entry(371);
	assert(syscall_number_filter[371] == f.must_handle);
	assert(syscall_needs_fd_rlock[371] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[371] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[371] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[371] == f.cwd_rlock);

	f = get_early_filter_entry(372);
	assert(syscall_number_filter[372] == f.must_handle);
	assert(syscall_needs_fd_rlock[372] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[372] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[372] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[372] == f.cwd_rlock);

	f = get_early_filter_entry(373);
	assert(syscall_number_filter[373] == f.must_handle);
	assert(syscall_needs_fd_rlock[373] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[373] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[373] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[373] == f.cwd_rlock);

	f = get_early_filter_entry(374);
	assert(syscall_number_filter[374] == f.must_handle);
	assert(syscall_needs_fd_rlock[374] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[374] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[374] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[374] == f.cwd_rlock);

	f = get_early_filter_entry(375);
	assert(syscall_number_filter[375] == f.must_handle);
	assert(syscall_needs_fd_rlock[375] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[375] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[375] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[375] == f.cwd_rlock);

	f = get_early_filter_entry(376);
	assert(syscall_number_filter[376] == f.must_handle);
	assert(syscall_needs_fd_rlock[376] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[376] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[376] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[376] == f.cwd_rlock);

	f = get_early_filter_entry(377);
	assert(syscall_number_filter[377] == f.must_handle);
	assert(syscall_needs_fd_rlock[377] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[377] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[377] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[377] == f.cwd_rlock);

	f = get_early_filter_entry(378);
	assert(syscall_number_filter[378] == f.must_handle);
	assert(syscall_needs_fd_rlock[378] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[378] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[378] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[378] == f.cwd_rlock);

	f = get_early_filter_entry(379);
	assert(syscall_number_filter[379] == f.must_handle);
	assert(syscall_needs_fd_rlock[379] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[379] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[379] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[379] == f.cwd_rlock);

	f = get_early_filter_entry(380);
	assert(syscall_number_filter[380] == f.must_handle);
	assert(syscall_needs_fd_rlock[380] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[380] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[380] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[380] == f.cwd_rlock);

	f = get_early_filter_entry(381);
	assert(syscall_number_filter[381] == f.must_handle);
	assert(syscall_needs_fd_rlock[381] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[381] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[381] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[381] == f.cwd_rlock);

	f = get_early_filter_entry(382);
	assert(syscall_number_filter[382] == f.must_handle);
	assert(syscall_needs_fd_rlock[382] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[382] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[382] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[382] == f.cwd_rlock);

	f = get_early_filter_entry(383);
	assert(syscall_number_filter[383] == f.must_handle);
	assert(syscall_needs_fd_rlock[383] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[383] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[383] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[383] == f.cwd_rlock);

	f = get_early_filter_entry(384);
	assert(syscall_number_filter[384] == f.must_handle);
	assert(syscall_needs_fd_rlock[384] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[384] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[384] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[384] == f.cwd_rlock);

	f = get_early_filter_entry(385);
	assert(syscall_number_filter[385] == f.must_handle);
	assert(syscall_needs_fd_rlock[385] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[385] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[385] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[385] == f.cwd_rlock);

	f = get_early_filter_entry(386);
	assert(syscall_number_filter[386] == f.must_handle);
	assert(syscall_needs_fd_rlock[386] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[386] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[386] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[386] == f.cwd_rlock);

	f = get_early_filter_entry(387);
	assert(syscall_number_filter[387] == f.must_handle);
	assert(syscall_needs_fd_rlock[387] == f.fd_rlock);
	assert(syscall_needs_fd_wlock[387] == f.fd_wlock);
	assert(syscall_has_fd_first_arg[387] == f.fd_first_arg);
	assert(syscall_needs_pmem_cwd_rlock[387] == f.cwd_rlock);

#endif
}

static void
config_error(void)
{
	log_write("invalid config");
	fputs("Invalid pmemfile config\n", stderr);
	exit_group_no_intercept(123);
}

static const char *parse_mount_point(struct pool_description *pool,
					const char *conf);
static const char *parse_pool_path(struct pool_description *pool,
					const char *conf);
static void open_mount_point(struct pool_description *pool);

static void open_new_pool(struct pool_description *);

/*
 * establish_mount_points - parse the configuration, which is expected to be a
 * semicolon separated list of path-pairs:
 * mount_point_path:pool_file_path
 * Mount point path is where the application is meant to observe a pmemfile
 * pool mounted -- this should be an actual directory accessoble by the
 * application. The pool file path should point to the path of the actual
 * pmemfile pool.
 */
static void
establish_mount_points(const char *config)
{
	char cwd[0x400];

	/*
	 * The establish_mount_points routine must know about the CWD, to be
	 * aware of the case when the mount point is the same as the CWD.
	 */
	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		perror("getcwd");
		exit_group_no_intercept(124);
	}

	struct stat kernel_cwd_stat;
	if (stat(cwd, &kernel_cwd_stat) != 0) {
		perror("fstat cwd");
		exit_group_no_intercept(124);
	}

	assert(pool_count == 0);

	if (config == NULL || config[0] == '\0') {
		log_write("No mount point");
		return;
	}

	do {
		if ((size_t)pool_count >= ARRAY_SIZE(pools))
			config_error();

		struct pool_description *pool_desc = pools + pool_count;

		/* fetch pool_desc->mount_point */
		config = parse_mount_point(pool_desc, config);

		/* fetch pool_desc->poolfile_path */
		config = parse_pool_path(pool_desc, config);

		/* fetch pool_desc-fd, pool_desc->stat */
		open_mount_point(pool_desc);

		pool_desc->pool = NULL;

		util_mutex_init(&pool_desc->pool_open_lock);

		++pool_count;

		/*
		 * If the current working directory is a mount point, then
		 * the corresponding pmemfile pool must opened at startup.
		 * Normally, a pool is only opened the first time it is
		 * accessed, but without doing this, the first access would
		 * never be noticed.
		 */
		if (pool_desc->stat.st_ino == kernel_cwd_stat.st_ino) {
			open_new_pool(pool_desc);
			if (pool_desc->pool == NULL) {
				perror("opening pmemfile pool");
				exit_group_no_intercept(124);
			}
			cwd_pool = pool_desc;
		}
	} while (config != NULL);
}

static const char *
parse_mount_point(struct pool_description *pool, const char *conf)
{
	if (conf[0] != '/') /* Relative path is not allowed */
		config_error();

	/*
	 * There should be a colon separating the mount path from the pool path.
	 */
	const char *colon = strchr(conf, ':');

	if (colon == NULL || colon == conf)
		config_error();

	if (((size_t)(colon - conf)) >= sizeof(pool->mount_point))
		config_error();

	memcpy(pool->mount_point, conf, (size_t)(colon - conf));
	pool->mount_point[colon - conf] = '\0';

	memcpy(pool->mount_point_parent, conf, (size_t)(colon - conf));
	pool->len_mount_point_parent = (size_t)(colon - conf);

	while (pool->len_mount_point_parent > 1 &&
	    pool->mount_point_parent[pool->len_mount_point_parent] != '/')
		pool->len_mount_point_parent--;

	pool->mount_point_parent[pool->len_mount_point_parent] = '\0';

	/* Return a pointer to the char following the colon */
	return colon + 1;
}

static const char *
parse_pool_path(struct pool_description *pool, const char *conf)
{
	if (conf[0] != '/') /* Relative path is not allowed */
		config_error();

	/*
	 * The path should be followed by either with a null character - in
	 * which case this is the last pool in the conf - or a semicolon.
	 */
	size_t i;
	for (i = 0; conf[i] != ';' && conf[i] != '\0'; ++i) {
		if (i >= sizeof(pool->poolfile_path) - 1)
			config_error();
		pool->poolfile_path[i] = conf[i];
	}

	pool->poolfile_path[i] = '\0';

	/* Return a pointer to the char following the semicolon, or NULL. */
	if (conf[i] == ';')
		return conf + i + 1;
	else
		return NULL;
}

/*
 * open_mount_point - Grab a file descriptor for the mount point, and mark it
 * in the mount_point_fds table.
 */
static void
open_mount_point(struct pool_description *pool)
{
	pool->fd = syscall_no_intercept(SYS_open, pool->mount_point,
					O_DIRECTORY | O_RDONLY, 0);

	if (pool->fd < 0)
		config_error();

	if ((size_t)pool->fd >=
	    sizeof(mount_point_fds) / sizeof(mount_point_fds[0])) {
		log_write("fd too large, sorry mate");
		exit_group_no_intercept(123);
	}

	mount_point_fds[pool->fd] = true;

	if (syscall_no_intercept(SYS_fstat, pool->fd, &pool->stat) != 0)
		config_error();

	if (!S_ISDIR(pool->stat.st_mode))
		config_error();
}

/*
 * Return values expected by libcintercept :
 * A non-zero return value if it should execute the syscall,
 * zero return value if it should not execute the syscall, and
 * use *result value as the syscall's result.
 */
#define NOT_HOOKED 1
#define HOOKED 0

static long hook_openat(struct fd_desc at, long arg0, long arg1, long arg2);
static long hook_linkat(struct fd_desc at0, long arg0,
			struct fd_desc at1, long arg1, long flags);
static long hook_unlinkat(struct fd_desc at, long arg0, long flags);
static long hook_newfstatat(struct fd_desc at, long arg0, long arg1, long arg2);
static long hook_fstat(long fd, long buf_addr);
static long hook_close(long fd);
static long hook_faccessat(struct fd_desc at, long path_arg, long mode);
static long hook_getxattr(long arg0, long arg1, long arg2, long arg3,
			int resolve_last);
static long hook_setxattr(long arg0, long arg1, long arg2, long arg3, long arg4,
			int resolve_last);
static long hook_mkdirat(struct fd_desc at, long path_arg, long mode);

static long hook_write(long fd, const char *buffer, size_t count);
static long hook_read(long fd, char *buffer, size_t count);
static long hook_lseek(long fd, long offset, int whence);
static long hook_pread64(long fd, char *buf, size_t count, off_t pos);
static long hook_pwrite64(long fd, const char *buf, size_t count, off_t pos);
static long hook_getdents(long fd, long dirp, unsigned count);
static long hook_getdents64(long fd, long dirp, unsigned count);

static long hook_chdir(const char *path);
static long hook_fchdir(long fd);
static long hook_getcwd(char *buf, size_t size);

static long hook_flock(long fd, int operation);
static long hook_renameat2(struct fd_desc at_old, const char *path_old,
		struct fd_desc at_new, const char *path_new, unsigned flags);
static long hook_truncate(const char *path, off_t length);
static long hook_ftruncate(long fd, off_t length);
static long hook_symlinkat(const char *target,
				struct fd_desc at, const char *linkpath);
static long hook_fchmod(long fd, mode_t mode);
static long hook_fchmodat(struct fd_desc at, const char *path,
				mode_t mode);
static long hook_fchown(long fd, uid_t owner, gid_t group);
static long hook_fchownat(struct fd_desc at, const char *path,
				uid_t owner, gid_t group, int flags);
static long hook_fallocate(long fd, int mode, off_t offset, off_t len);
static long hook_fcntl(long fd, int cmd, long arg);

static long hook_sendfile(long out_fd, long in_fd, off_t *offset, size_t count);
static long hook_splice(long in_fd, loff_t *off_in, long fd_out,
			loff_t *off_out, size_t len, unsigned flags);
static long hook_readlinkat(struct fd_desc at, const char *path,
				char *buf, size_t bufsiz);

static long nosup_syscall_with_path(long syscall_number,
			long path, int resolve_last,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5);

static long hook_futimesat(struct fd_desc at, const char *path,
				const struct timeval times[2]);
static long hook_name_to_handle_at(struct fd_desc at, const char *path,
		struct file_handle *handle, int *mount_id, int flags);
static long hook_execveat(struct fd_desc at, const char *path,
		char *const argv[], char *const envp[], int flags);
static long hook_copy_file_range(long fd_in, loff_t *off_in, long fd_out,
		loff_t *off_out, size_t len, unsigned flags);
static long hook_mmap(long arg0, long arg1, long arg2,
		long arg3, long fd, long arg5);

static long
dispatch_syscall(long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5)
{
	switch (syscall_number) {

	/* Use pmemfile_openat to implement open, create, openat */
	case SYS_open:
		return hook_openat(cwd_desc(), arg0, arg1, arg2);

	case SYS_creat:
		return hook_openat(cwd_desc(), arg0,
				O_WRONLY | O_CREAT | O_TRUNC, arg1);

	case SYS_openat:
		return hook_openat(fetch_fd(arg0), arg1, arg2, arg3);

	case SYS_rename:
		return hook_renameat2(cwd_desc(), (const char *)arg0,
					cwd_desc(), (const char *)arg1, 0);

	case SYS_renameat:
		return hook_renameat2(fetch_fd(arg0), (const char *)arg1,
					fetch_fd(arg2), (const char *)arg3, 0);

	case SYS_renameat2:
		return hook_renameat2(fetch_fd(arg0), (const char *)arg1,
					fetch_fd(arg2), (const char *)arg3,
					(unsigned)arg4);

	case SYS_link:
		/* Use pmemfile_linkat to implement link */
		return hook_linkat(cwd_desc(), arg0, cwd_desc(), arg1, 0);

	case SYS_linkat:
		return hook_linkat(fetch_fd(arg0), arg1, fetch_fd(arg2), arg3,
		    arg4);

	case SYS_unlink:
		/* Use pmemfile_unlinkat to implement unlink */
		return hook_unlinkat(cwd_desc(), arg0, 0);

	case SYS_unlinkat:
		return hook_unlinkat(fetch_fd(arg0), arg1, arg2);

	case SYS_rmdir:
		/* Use pmemfile_unlinkat to implement rmdir */
		return hook_unlinkat(cwd_desc(), arg0, AT_REMOVEDIR);

	case SYS_mkdir:
		/* Use pmemfile_mkdirat to implement mkdir */
		return hook_mkdirat(cwd_desc(), arg0, arg1);

	case SYS_mkdirat:
		return hook_mkdirat(fetch_fd(arg0), arg1, arg2);

	case SYS_access:
		/* Use pmemfile_faccessat to implement access */
		return hook_faccessat(cwd_desc(), arg0, 0);

	case SYS_faccessat:
		return hook_faccessat(fetch_fd(arg0), arg1, arg2);

	/*
	 * The newfstatat syscall implements both stat and lstat.
	 * Linux calls it: newfstatat ( I guess there was an old one )
	 * POSIX / libc interfaces call it: fstatat
	 * pmemfile calls it: pmemfile_fstatat
	 *
	 * fstat is unique.
	 */
	case SYS_stat:
		return hook_newfstatat(cwd_desc(), arg0, arg1, 0);

	case SYS_lstat:
		return hook_newfstatat(cwd_desc(), arg0, arg1,
		    AT_SYMLINK_NOFOLLOW);

	case SYS_newfstatat:
		return hook_newfstatat(fetch_fd(arg0), arg1, arg2, arg3);

	case SYS_fstat:
		return hook_fstat(arg0, arg1);

	/*
	 * Some simpler ( in terms of argument processing ) syscalls,
	 * which don't require path resolution.
	 */
	case SYS_close:
		return hook_close(arg0);

	case SYS_write:
		return hook_write(arg0, (const char *)arg1, (size_t)arg2);

	case SYS_read:
		return hook_read(arg0, (char *)arg1, (size_t)arg2);

	case SYS_lseek:
		return hook_lseek(arg0, arg1, (int)arg2);

	case SYS_pread64:
		return hook_pread64(arg0, (char *)arg1,
		    (size_t)arg2, (off_t)arg3);

	case SYS_pwrite64:
		return hook_pwrite64(arg0, (const char *)arg1,
		    (size_t)arg2, (off_t)arg3);

	case SYS_getdents:
		return hook_getdents(arg0, arg1, (unsigned)arg2);

	case SYS_getdents64:
		return hook_getdents64(arg0, arg1, (unsigned)arg2);

	case SYS_mmap:
		return hook_mmap(arg0, arg1, arg2, arg3, arg4, arg5);

	/*
	 * NOP implementations for the xattr family. None of these
	 * actually call pmemfile-posix. Some of them do need path resolution,
	 * fgetxattr and fsetxattr don't.
	 */
	case SYS_getxattr:
		return hook_getxattr(arg0, arg1, arg2, arg3,
		    RESOLVE_LAST_SLINK);

	case SYS_lgetxattr:
		return hook_getxattr(arg0, arg1, arg2, arg3,
		    NO_RESOLVE_LAST_SLINK);

	case SYS_setxattr:
		return hook_setxattr(arg0, arg1, arg2, arg3, arg4,
		    RESOLVE_LAST_SLINK);

	case SYS_lsetxattr:
		return hook_setxattr(arg0, arg1, arg2, arg3, arg4,
		    NO_RESOLVE_LAST_SLINK);

	case SYS_fgetxattr:
		return 0;

	case SYS_fsetxattr:
		return check_errno(-ENOTSUP);

	case SYS_fcntl:
		return hook_fcntl(arg0, (int)arg1, arg2);

	case SYS_syncfs:
		return 0;

	case SYS_fdatasync:
		return 0;

	case SYS_fsync:
		return 0;

	case SYS_flock:
		return hook_flock(arg0, (int)arg1);

	case SYS_truncate:
		return hook_truncate((const char *)arg0, arg1);

	case SYS_ftruncate:
		return hook_ftruncate(arg0, arg1);

	case SYS_symlink:
		return hook_symlinkat((const char *)arg0,
					cwd_desc(), (const char *)arg1);

	case SYS_symlinkat:
		return hook_symlinkat((const char *)arg0,
					fetch_fd(arg1), (const char *)arg2);

	case SYS_chmod:
		return hook_fchmodat(cwd_desc(), (const char *)arg0,
					(mode_t)arg1);

	case SYS_fchmod:
		return hook_fchmod(arg0, (mode_t)arg1);

	case SYS_fchmodat:
		return hook_fchmodat(fetch_fd(arg0), (const char *)arg1,
					(mode_t)arg2);

	case SYS_chown:
		return hook_fchownat(cwd_desc(), (const char *)arg0,
					(uid_t)arg1, (gid_t)arg2, 0);

	case SYS_lchown:
		return hook_fchownat(cwd_desc(), (const char *)arg0,
					(uid_t)arg1, (gid_t)arg2,
					AT_SYMLINK_NOFOLLOW);

	case SYS_fchown:
		return hook_fchown(arg0, (uid_t)arg1, (gid_t)arg2);

	case SYS_fchownat:
		return hook_fchownat(fetch_fd(arg0), (const char *)arg1,
					(uid_t)arg2, (gid_t)arg3, (int)arg4);

	case SYS_fallocate:
		return hook_fallocate(arg0, (int)arg1,
					(off_t)arg2, (off_t)arg3);

	case SYS_fadvise64:
		return 0;

	case SYS_readv:
	case SYS_writev:
	case SYS_dup:
	case SYS_dup2:
	case SYS_dup3:
	case SYS_flistxattr:
	case SYS_fremovexattr:
	case SYS_preadv2:
	case SYS_pwritev2:
	case SYS_readahead:
		return check_errno(-ENOTSUP);

	case SYS_sendfile:
		return hook_sendfile(arg0, arg1, (off_t *)arg2, (size_t)arg3);

	/*
	 * Some syscalls that have a path argument, but are not ( yet ) handled
	 * by libpmemfile-posix. The argument of these are not interpreted,
	 * except for the path itself. If the path points to something pmemfile
	 * resident, -ENOTSUP is returned, otherwise, the call is forwarded
	 * to the kernel.
	 */
	case SYS_chroot:
	case SYS_listxattr:
	case SYS_removexattr:
	case SYS_utime:
	case SYS_utimes:
		return nosup_syscall_with_path(syscall_number,
		    arg0, RESOLVE_LAST_SLINK,
		    arg0, arg1, arg2, arg3, arg4, arg5);

	case SYS_llistxattr:
	case SYS_lremovexattr:
		return nosup_syscall_with_path(syscall_number,
		    arg0, NO_RESOLVE_LAST_SLINK,
		    arg0, arg1, arg2, arg3, arg4, arg5);

	case SYS_readlink:
		return hook_readlinkat(cwd_desc(), (const char *)arg0,
		    (char *)arg1, (size_t)arg2);

	case SYS_readlinkat:
		return hook_readlinkat(cwd_desc(), (const char *)arg0,
		    (char *)arg1, (size_t)arg2);

	case SYS_splice:
		return hook_splice(arg0, (loff_t *)arg1,
				arg2, (loff_t *)arg3,
				(size_t)arg4, (unsigned)arg5);

	case SYS_futimesat:
		return hook_futimesat(fetch_fd(arg0), (const char *)arg1,
			(const struct timeval *)arg2);

	case SYS_name_to_handle_at:
		return hook_name_to_handle_at(fetch_fd(arg0),
		    (const char *)arg1, (struct file_handle *)arg2,
		    (int *)arg3, (int)arg4);

	case SYS_execve:
		return hook_execveat(cwd_desc(), (const char *)arg0,
		    (char *const *)arg1, (char *const *)arg2, 0);

	case SYS_execveat:
		return hook_execveat(fetch_fd(arg0), (const char *)arg1,
		    (char *const *)arg2, (char *const *)arg3, (int)arg4);

	case SYS_copy_file_range:
		return hook_copy_file_range(arg0, (loff_t *)arg1,
		    arg2, (loff_t *)arg3, (size_t)arg4, (unsigned)arg5);

	default:
		/* Did we miss something? */
		assert(false);
		return syscall_no_intercept(syscall_number,
		    arg0, arg1, arg2, arg3, arg4, arg5);
	}
}

static int
hook(long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5,
			long *syscall_return_value)
{
	assert(pool_count > 0);

	if (reenter)
		return NOT_HOOKED;

	reenter = true;

	if (syscall_number == SYS_chdir) {
		*syscall_return_value = hook_chdir((const char *)arg0);
		reenter = false;
		return HOOKED;
	}
	if (syscall_number == SYS_fchdir) {
		*syscall_return_value = hook_fchdir(arg0);
		reenter = false;
		return HOOKED;
	}
	if (syscall_number == SYS_getcwd) {
		util_rwlock_rdlock(&pmem_cwd_lock);
		*syscall_return_value = hook_getcwd((char *)arg0, (size_t)arg1);
		util_rwlock_unlock(&pmem_cwd_lock);
		reenter = false;
		return HOOKED;
	}

	/* XXX: move this filtering to the intercepting library */
	if (syscall_number < 0 ||
	    (uint64_t)syscall_number >= ARRAY_SIZE(syscall_number_filter) ||
	    !syscall_number_filter[syscall_number]) {
		reenter = false;
		return NOT_HOOKED;
	}

	int is_hooked;

	if (syscall_needs_pmem_cwd_rlock[syscall_number])
		util_rwlock_rdlock(&pmem_cwd_lock);

	if (syscall_needs_fd_rlock[syscall_number])
		util_rwlock_rdlock(&fd_table_lock);
	else if (syscall_needs_fd_wlock[syscall_number])
		util_rwlock_wrlock(&fd_table_lock);

	if (syscall_has_fd_first_arg[syscall_number] &&
	    !is_fd_in_table(arg0)) {
		/*
		 * shortcut for write, read, and such so this check doesn't
		 * need to be copy-pasted into them
		 */
		is_hooked = NOT_HOOKED;
	} else {
		is_hooked = HOOKED;
		*syscall_return_value = dispatch_syscall(syscall_number,
		    arg0, arg1, arg2, arg3, arg4, arg5);
	}


	if (syscall_needs_fd_rlock[syscall_number] ||
	    syscall_needs_fd_wlock[syscall_number])
		util_rwlock_unlock(&fd_table_lock);

	if (syscall_needs_pmem_cwd_rlock[syscall_number])
		util_rwlock_unlock(&pmem_cwd_lock);

	reenter = false;

	return is_hooked;
}

static long
hook_close(long fd)
{
	(void) syscall_no_intercept(SYS_close, fd);

	pmemfile_close(fd_table[fd].pool->pool, fd_table[fd].file);

	log_write("pmemfile_close(%p, %p) = 0",
	    (void *)fd_table[fd].pool->pool, (void *)fd_table[fd].file);

	fd_table[fd].file = NULL;
	fd_table[fd].pool = NULL;

	return 0;
}

static long
hook_write(long fd, const char *buffer, size_t count)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_write(file->pool->pool, file->file, buffer, count);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_write(%p, %p, %p, %zu) = %ld",
	    (void *)file->pool->pool, (void *)file->file,
	    (void *)buffer, count, r);

	return check_errno(r);
}

static long
hook_read(long fd, char *buffer, size_t count)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_read(file->pool->pool, file->file, buffer, count);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_read(%p, %p, %p, %zu) = %ld",
	    (void *)file->pool, (void *)file->file,
	    (void *)buffer, count, r);

	return check_errno(r);
}

static long
hook_lseek(long fd, long offset, int whence)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_lseek(file->pool->pool, file->file, offset, whence);

	log_write("pmemfile_lseek(%p, %p, %lu, %d) = %ld",
	    (void *)file->pool->pool, (void *)file->file, offset, whence, r);

	if (r < 0)
		r = -errno;

	return check_errno(r);
}

static long
hook_linkat(struct fd_desc at0, long arg0,
		struct fd_desc at1, long arg1, long flags)
{
	struct resolved_path where_old;
	struct resolved_path where_new;

	resolve_path(at0, (const char *)arg0, &where_old, RESOLVE_LAST_SLINK);
	resolve_path(at1, (const char *)arg1, &where_new, RESOLVE_LAST_SLINK);

	if (where_old.error_code != 0)
		return where_old.error_code;

	if (where_new.error_code != 0)
		return where_new.error_code;

	if (where_new.at.pmem_fda.pool != where_old.at.pmem_fda.pool)
		return -EXDEV;

	if (where_new.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_linkat,
		    where_old.at.kernel_fd, where_old.path,
		    where_new.at.kernel_fd, where_new.path, flags);

	int r = pmemfile_linkat(where_old.at.pmem_fda.pool->pool,
		    where_old.at.pmem_fda.file, where_old.path,
		    where_new.at.pmem_fda.file, where_new.path, (int)flags);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_link(%p, \"%s\", \"%s\", %ld) = %d",
	    (void *)where_old.at.pmem_fda.pool->pool,
	    where_old.path, where_new.path, flags, r);

	return check_errno(r);
}

static long
hook_unlinkat(struct fd_desc at, long path_arg, long flags)
{
	struct resolved_path where;

	resolve_path(at, (const char *)path_arg,
	    &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda)) /* Not pmemfile resident path */
		return syscall_no_intercept(SYS_unlinkat,
		    where.at.kernel_fd, where.path, flags);

	int r;
	r = pmemfile_unlinkat(where.at.pmem_fda.pool->pool,
		where.at.pmem_fda.file, where.path, (int)flags);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_unlink(%p, \"%s\") = %d",
	    (void *)where.at.pmem_fda.pool->pool, where.path, r);

	return check_errno(r);
}

static long
hook_chdir(const char *path)
{
	struct resolved_path where;

	long result;

	log_write("%s(\"%s\")", __func__, path);

	util_rwlock_wrlock(&pmem_cwd_lock);

	resolve_path(cwd_desc(), path, &where, RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		result = where.error_code;
	} else if (is_fda_null(&where.at.pmem_fda)) {
		result = syscall_no_intercept(SYS_chdir, where.path);
		if (result == 0)
			cwd_pool = NULL;
	} else {
		if (cwd_pool != where.at.pmem_fda.pool) {
			cwd_pool = where.at.pmem_fda.pool;
			syscall_no_intercept(SYS_chdir, cwd_pool->mount_point);
		}
		if (pmemfile_chdir(cwd_pool->pool, where.path) == 0)
			result = 0;
		else
			result = -errno;
		log_write("pmemfile_chdir(%p, \"%s\") = %ld",
		    cwd_pool->pool, where.path, result);
		check_errno(result);
	}

	util_rwlock_unlock(&pmem_cwd_lock);

	return result;
}

static long
hook_fchdir(long fd)
{
	if (fd == AT_FDCWD)
		return 0;

	long result;

	log_write("%s(\"%ld\")", __func__, fd);

	util_rwlock_wrlock(&pmem_cwd_lock);

	if (is_fd_in_table(fd)) {
		struct fd_association *where = fd_table + fd;
		if (pmemfile_fchdir(where->pool->pool, where->file) == 0) {
			cwd_pool = where->pool;
			result = 0;
		} else {
			result = -errno;
		}
		log_write("pmemfile_fchdir(%p, %p) = %ld",
		    where->pool->pool, where->file, result);
		check_errno(result);
	} else {
		result = syscall_no_intercept(SYS_fchdir, fd);
		if (result == 0)
			cwd_pool = NULL;
	}

	util_rwlock_unlock(&pmem_cwd_lock);

	return result;
}

static long
hook_getcwd(char *buf, size_t size)
{
	if (cwd_pool == NULL)
		return syscall_no_intercept(SYS_getcwd, buf, size);

	size_t mlen = strlen(cwd_pool->mount_point);
	if (mlen >= size)
		return -ERANGE;
	strcpy(buf, cwd_pool->mount_point);
	if (pmemfile_getcwd(cwd_pool->pool, buf + mlen, size - mlen) != NULL)
		return 0;
	else
		return check_errno(-errno);
}

static long log_fd = -1;

static void
log_init(const char *path, const char *trunc)
{
	if (path != NULL) {
		int flags = O_CREAT | O_RDWR | O_APPEND | O_TRUNC;
		if (trunc && trunc[0] == '0')
			flags &= ~O_TRUNC;

		log_fd = syscall_no_intercept(SYS_open, path, flags, 0600);
	}
}

static void
log_write(const char *fmt, ...)
{
	if (log_fd < 0)
		return;

	char buf[0x1000];
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);


	if (len < 1)
		return;

	buf[len++] = '\n';

	syscall_no_intercept(SYS_write, log_fd, buf, len);
}

/*
 * open_new_pool[_under_lock] -- attempts to open a pmemfile_pool
 * Initializes the fields called pool and pmem_stat in a pool_description
 * struct. Does nothing if they are already initialized. The most
 * important part of this initialization is of course calling
 * pmemfile_pool_open.
 *
 * Returns:
 *  On success: the same pointer as the argument
 *  If the pool was already open: the same pointer as the argument
 *  On failure: NULL pointer
 */
static void
open_new_pool_under_lock(struct pool_description *p)
{
	PMEMfilepool *pfp;

	if (p->pool != NULL)
		return; /* already open */

	if ((pfp = pmemfile_pool_open(p->poolfile_path)) == NULL)
		return; /* failed to open */

	if (pmemfile_stat(pfp, "/", &p->pmem_stat) != 0) {
		pmemfile_pool_close(pfp);
		return; /* stat failed */
	}

	__atomic_store_n(&p->pool, pfp, __ATOMIC_RELEASE);
}

static void
open_new_pool(struct pool_description *p)
{
	util_mutex_lock(&p->pool_open_lock);
	open_new_pool_under_lock(p);
	util_mutex_unlock(&p->pool_open_lock);
}

/*
 * With each virtual mount point an inode number is stored, and this
 * function can be used to lookup a mount point by inode number.
 */
struct pool_description *
lookup_pd_by_inode(struct stat *stat)
{
	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;

		/*
		 * Note: p->stat never changes after lib initialization, thus
		 * it is safe to read. If a non-null value is read from p->pool,
		 * the rest of the pool_description struct must be already
		 * initialized -- and never altered thereafter.
		 */
		if (same_inode(&p->stat, stat)) {
			if (__atomic_load_n(&p->pool, __ATOMIC_ACQUIRE) == NULL)
				open_new_pool(p);
			return p;
		}
	}

	return NULL;
}

struct pool_description *
lookup_pd_by_path(const char *path)
{
	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		/*
		 * XXX: first compare the lengths of the two strings to avoid
		 * strcmp calls
		 */
		/*
		 * Note: p->mount_point never changes after lib initialization,
		 * thus it is safe to read. If a non-null value is read from
		 * p->pool, the rest of the pool_description struct must be
		 * already initialized -- and never altered thereafter.
		 */
		if (strcmp(p->mount_point, path) == 0)  {
			if (__atomic_load_n(&p->pool, __ATOMIC_ACQUIRE) == NULL)
				open_new_pool(p);
			return p;
		}
	}

	return NULL;
}

static long
hook_newfstatat(struct fd_desc at, long arg0, long arg1, long arg2)
{
	struct resolved_path where;

	resolve_path(at, (const char *)arg0, &where,
	    (arg2 & AT_SYMLINK_NOFOLLOW)
	    ? NO_RESOLVE_LAST_SLINK : RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda))
		return syscall_no_intercept(SYS_newfstatat,
		    where.at.kernel_fd, where.path, arg1, arg2);

	int r = pmemfile_fstatat(where.at.pmem_fda.pool->pool,
	    where.at.pmem_fda.file,
	    where.path,
		(pmemfile_stat_t *)arg1, (int)arg2);

	if (r != 0)
		r = -errno;

	return check_errno(r);
}

static long
hook_fstat(long fd, long buf_addr)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_fstat(file->pool->pool, file->file,
			(pmemfile_stat_t *)buf_addr);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_fstat(%p, %p, %p) = %ld",
	    (void *)file->pool, (void *)file->file,
	    (void *)buf_addr, r);

	return check_errno(r);
}

static long
hook_pread64(long fd, char *buf, size_t count, off_t pos)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_pread(file->pool->pool, file->file, buf, count, pos);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_pread(%p, %p, %p, %zu, %zu) = %ld",
	    (void *)file->pool, (void *)file->file,
	    (void *)buf, count, pos, r);

	return check_errno(r);
}

static long
hook_pwrite64(long fd, const char *buf, size_t count, off_t pos)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_pwrite(file->pool->pool, file->file, buf, count, pos);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_pwrite(%p, %p, %p, %zu, %zu) = %ld",
	    (void *)file->pool, (void *)file->file,
	    (const void *)buf, count, pos, r);

	return check_errno(r);
}

static long
hook_faccessat(struct fd_desc at, long path_arg, long mode)
{
	struct resolved_path where;

	resolve_path(at, (const char *)path_arg, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda)) {
		return syscall_no_intercept(SYS_faccessat,
		    where.at.kernel_fd, where.path, mode);
	}

	long r = pmemfile_faccessat(where.at.pmem_fda.pool->pool,
	    where.at.pmem_fda.file, where.path, (int)mode, 0);

	log_write("pmemfile_faccessat(%p, %p, \"%s\", %ld, 0) = %ld",
	    (void *)where.at.pmem_fda.pool->pool, where.at.pmem_fda.file,
	    where.path, mode, r);

	if (r == 0)
		return 0;
	else
		return check_errno(-errno);
}

static long
hook_getdents(long fd, long dirp, unsigned count)
{
	struct fd_association *dir = fd_table + fd;
	long r = pmemfile_getdents(dir->pool->pool, dir->file,
	    (struct linux_dirent *)dirp, count);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_getdents(%p, %p, %p, %u) = %ld",
	    (void *)dir->pool->pool, (void *)dir->file,
	    (const void *)dirp, count, r);

	return check_errno(r);
}

static long
hook_getdents64(long fd, long dirp, unsigned count)
{
	struct fd_association *dir = fd_table + fd;
	long r = pmemfile_getdents64(dir->pool->pool, dir->file,
	    (struct linux_dirent64 *)dirp, count);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_getdents64(%p, %p, %p, %u) = %ld",
	    (void *)dir->pool->pool, (void *)dir->file,
	    (const void *)dirp, count, r);

	return check_errno(r);
}

static long
hook_getxattr(long arg0, long arg1, long arg2, long arg3,
		int resolve_last)
{
	struct resolved_path where;

	resolve_path(cwd_desc(), (const char *)arg0, &where, resolve_last);

	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda)) {
		if (where.at.kernel_fd == AT_FDCWD)
			return check_errno(-ENOTSUP); /* XXX */

		return syscall_no_intercept(SYS_getxattr,
		    where.path, arg1, arg2, arg3);
	} else {
		return 0;
	}
}

static long
hook_setxattr(long arg0, long arg1, long arg2, long arg3, long arg4,
		int resolve_last)
{
	struct resolved_path where;

	resolve_path(cwd_desc(), (const char *)arg0, &where, resolve_last);

	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda)) {
		if (where.at.kernel_fd == AT_FDCWD)
			return check_errno(-ENOTSUP); /* XXX */

		return syscall_no_intercept(SYS_setxattr,
		    where.path, arg1, arg2, arg3, arg4);
	} else {
		return check_errno(-ENOTSUP);
	}
}

static long
hook_mkdirat(struct fd_desc at, long path_arg, long mode)
{
	struct resolved_path where;

	resolve_path(at, (const char *)path_arg, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda))
		return syscall_no_intercept(SYS_mkdirat,
		    where.at.kernel_fd, where.path, mode);

	long r = pmemfile_mkdirat(where.at.pmem_fda.pool->pool,
	    where.at.pmem_fda.file, where.path, (mode_t)mode);

	log_write("pmemfile_mkdirat(%p, \"%s\", 0%lo) = %ld",
	    (void *)where.at.pmem_fda.pool->pool, where.path, mode, r);

	if (r == 0)
		return 0;
	else
		return check_errno(-errno);
}

static long
hook_openat(struct fd_desc at, long arg0, long flags, long mode)
{
	struct resolved_path where;
	const char *path_arg = (const char *)arg0;
	int follow_last;

	log_write("%s(\"%s\")", __func__, path_arg);

	if ((flags & O_NOFOLLOW) != 0)
		follow_last = NO_RESOLVE_LAST_SLINK;
	else if ((flags & O_CREAT) != 0)
		follow_last = NO_RESOLVE_LAST_SLINK;
	else
		follow_last = RESOLVE_LAST_SLINK;

	resolve_path(at, path_arg, &where, follow_last);

	if (where.error_code != 0) /* path resolution failed */
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda)) /* Not pmemfile resident path */
		return syscall_no_intercept(SYS_openat,
		    where.at.kernel_fd, where.path, flags, mode);

	/* The fd to represent the pmem resident file for the application */
	long fd = acquire_new_fd(path_arg);

	if (fd < 0) { /* error while trying to allocate a new fd */
		return fd;
	} else {
		PMEMfile *file;

		file = pmemfile_openat(where.at.pmem_fda.pool->pool,
				where.at.pmem_fda.file,
				where.path,
				((int)flags) & ~O_NONBLOCK,
				(mode_t)mode);

		log_write("pmemfile_openat(%p, %p, \"%s\", 0x%x, %u) = %p",
				(void *)where.at.pmem_fda.pool->pool,
				(void *)where.at.pmem_fda.file,
				where.path,
				((int)flags) & ~O_NONBLOCK,
				(mode_t)mode,
				file);

		if (file != NULL) {
			fd_table[fd].pool = where.at.pmem_fda.pool;
			fd_table[fd].file = file;
			return fd;
		} else {
			(void) syscall_no_intercept(SYS_close, fd);
			return check_errno(-errno);
		}
	}
}

static long
hook_fcntl(long fd, int cmd, long arg)
{
	struct fd_association *file = fd_table + fd;
	int r = pmemfile_fcntl(file->pool->pool, file->file, cmd, arg);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_fcntl(%p, %p, 0x%x, 0x%lx) = %d",
	    (void *)file->pool, (void *)file->file, cmd, arg, r);

	return check_errno(r);
}

static long
hook_flock(long fd, int operation)
{
	struct fd_association *file = fd_table + fd;
	int r = pmemfile_flock(file->pool->pool, file->file, operation);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_flock(%p, %p, %d) = %d",
	    (void *)file->pool->pool, (void *)file->file, operation, r);

	return check_errno(r);
}

static long
hook_renameat2(struct fd_desc at_old, const char *path_old,
		struct fd_desc at_new, const char *path_new, unsigned flags)
{
	struct resolved_path where_old;
	struct resolved_path where_new;

	resolve_path(at_old, path_old, &where_old, NO_RESOLVE_LAST_SLINK);
	if (where_old.error_code != 0)
		return where_old.error_code;

	resolve_path(at_new, path_new, &where_new, NO_RESOLVE_LAST_SLINK);
	if (where_new.error_code != 0)
		return where_new.error_code;

	if (where_new.at.pmem_fda.pool != where_old.at.pmem_fda.pool)
		return -EXDEV;

	if (where_new.at.pmem_fda.pool == NULL) {
		if (flags == 0) {
			return syscall_no_intercept(SYS_renameat,
			    where_old.at.kernel_fd, where_old.path,
			    where_new.at.kernel_fd, where_new.path);
		} else {
			return syscall_no_intercept(SYS_renameat2,
			    where_old.at.kernel_fd, where_old.path,
			    where_new.at.kernel_fd, where_new.path, flags);
		}
	}

	int r = pmemfile_renameat2(where_old.at.pmem_fda.pool->pool,
		    where_old.at.pmem_fda.file, where_old.path,
		    where_new.at.pmem_fda.file, where_new.path, flags);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_renameat2(%p, \"%s\", \"%s\", %u) = %d",
	    (void *)where_old.at.pmem_fda.pool->pool,
	    where_old.path, where_new.path, flags, r);

	return check_errno(r);
}

static long
hook_truncate(const char *path, off_t length)
{
	struct resolved_path where;

	resolve_path(cwd_desc(), path, &where, RESOLVE_LAST_SLINK);
	if (where.error_code != 0)
		return where.error_code;

	if (where.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_truncate,
		    where.at.kernel_fd, where.path, length);

	int r = pmemfile_truncate(where.at.pmem_fda.pool->pool,
					where.path, length);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_truncate(%p, \"%s\", %lu) = %d",
	    (void *)where.at.pmem_fda.pool->pool,
	    where.path, length, r);

	return check_errno(r);
}

static long
hook_ftruncate(long fd, off_t length)
{
	struct fd_association *file = fd_table + fd;
	int r = pmemfile_ftruncate(file->pool->pool, file->file, length);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_ftruncate(%p, %p, %lu) = %d",
	    (void *)file->pool->pool, (void *)file->file, length, r);

	return check_errno(r);
}

static long
hook_symlinkat(const char *target, struct fd_desc at, const char *linkpath)
{
	struct resolved_path where;

	resolve_path(at, linkpath, &where, NO_RESOLVE_LAST_SLINK);
	if (where.error_code != 0)
		return where.error_code;

	if (where.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_symlinkat, target,
		    where.at.kernel_fd, where.path);

	int r = pmemfile_symlinkat(where.at.pmem_fda.pool->pool, target,
					where.at.pmem_fda.file, where.path);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_symlinkat(%p, \"%s\", %p, \"%s\") = %d",
	    (void *)where.at.pmem_fda.pool->pool, target,
	    (void *)where.at.pmem_fda.file, where.path, r);

	return check_errno(r);
}

static long
hook_fchmod(long fd, mode_t mode)
{
	struct fd_association *file = fd_table + fd;
	int r = pmemfile_fchmod(file->pool->pool, file->file, mode);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_fchmod(%p, %p, 0%o) = %d",
	    (void *)file->pool->pool, (void *)file->file, mode, r);

	return check_errno(r);
}

static long
hook_fchmodat(struct fd_desc at, const char *path, mode_t mode)
{
	struct resolved_path where;

	resolve_path(at, path, &where, RESOLVE_LAST_SLINK);
	if (where.error_code != 0)
		return where.error_code;

	if (where.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_fchmodat,
		    where.at.kernel_fd, where.path, mode);

	int r = pmemfile_fchmodat(where.at.pmem_fda.pool->pool,
			where.at.pmem_fda.file, where.path, mode, 0);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_fchmodat(%p, %p, \"%s\", 0%o, 0) = %d",
	    (void *)where.at.pmem_fda.pool->pool,
	    (void *)where.at.pmem_fda.file,
	    where.path, mode, r);

	return check_errno(r);
}

static long
hook_fchown(long fd, uid_t owner, gid_t group)
{
	struct fd_association *file = fd_table + fd;
	int r = pmemfile_fchown(file->pool->pool, file->file, owner, group);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_fchown(%p, %p, %d, %d) = %d",
	    (void *)file->pool->pool, (void *)file->file, owner, group, r);

	return check_errno(r);
}

static long
hook_fchownat(struct fd_desc at, const char *path,
				uid_t owner, gid_t group, int flags)
{
	struct resolved_path where;

	resolve_path(at, path, &where,
	    (flags & AT_SYMLINK_NOFOLLOW)
	    ? NO_RESOLVE_LAST_SLINK : RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (where.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_fchownat,
		    where.at.kernel_fd, where.path, owner, group, flags);

	int r = pmemfile_fchownat(where.at.pmem_fda.pool->pool,
			where.at.pmem_fda.file, where.path, owner, group,
			flags);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_fchownat(%p, %p, \"%s\", %d, %d, %d) = %d",
	    (void *)where.at.pmem_fda.pool->pool,
	    (void *)where.at.pmem_fda.file,
	    where.path, owner, group, flags, r);

	return check_errno(r);
}

static long
hook_sendfile(long out_fd, long in_fd, off_t *offset, size_t count)
{
	if (is_fd_in_table(out_fd))
		return check_errno(-ENOTSUP);

	if (is_fd_in_table(in_fd))
		return check_errno(-ENOTSUP);

	return syscall_no_intercept(SYS_sendfile, out_fd, in_fd, offset, count);
}

static long
hook_readlinkat(struct fd_desc at, const char *path,
				char *buf, size_t bufsiz)
{
	struct resolved_path where;

	resolve_path(at, path, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (where.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_readlinkat,
		    where.at.kernel_fd, where.path, buf, bufsiz);

	ssize_t r = pmemfile_readlinkat(where.at.pmem_fda.pool->pool,
			where.at.pmem_fda.file, where.path, buf, bufsiz);

	if (r < 0)
		r = -errno;
	else
		assert(r < INT_MAX);

	log_write("pmemfile_readlinkat(%p, %p, \"%s\", \"%.*s\", %zu) = %zd",
	    (void *)where.at.pmem_fda.pool->pool,
	    (void *)where.at.pmem_fda.file,
	    where.path, r >= 0 ? (int)r : 0, r >= 0 ? buf : "", bufsiz, r);

	return check_errno(r);
}

static long
nosup_syscall_with_path(long syscall_number,
			long path, int resolve_last,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5)
{
	struct resolved_path where;

	resolve_path(cwd_desc(), (const char *)path, &where, resolve_last);

	if (where.error_code != 0)
		return where.error_code;

	/*
	 * XXX only forward these to the kernel, if the path is not relative
	 * to some fd. Normally, the _at version of the syscall would be used
	 * here, i.e.: xxx_at(kernel_fd, path, ...), but some of these syscalls
	 * don't have an _at version. So for now these are only handled, if the
	 * path is relative to AT_FDCWD.
	 */
	if (where.at.pmem_fda.pool == NULL && where.at.kernel_fd == AT_FDCWD)
		return syscall_no_intercept(syscall_number,
		    arg0, arg1, arg2, arg3, arg4, arg5);

	return check_errno(-ENOTSUP);
}

static long
hook_splice(long fd_in, loff_t *off_in, long fd_out,
			loff_t *off_out, size_t len, unsigned flags)
{
	if (is_fd_in_table(fd_out))
		return check_errno(-ENOTSUP);

	if (is_fd_in_table(fd_in))
		return check_errno(-ENOTSUP);

	return syscall_no_intercept(SYS_splice, fd_in, off_in, fd_out, off_out,
	    len, flags);
}

static long
hook_futimesat(struct fd_desc at, const char *path,
				const struct timeval times[2])
{
	struct resolved_path where;

	resolve_path(at, (const char *)path, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (where.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_futimesat,
		    where.at.kernel_fd, where.path, times);

	return check_errno(-ENOTSUP);
}

static long
hook_name_to_handle_at(struct fd_desc at, const char *path,
		struct file_handle *handle, int *mount_id, int flags)
{
	struct resolved_path where;

	resolve_path(at, path, &where,
	    (flags & AT_SYMLINK_FOLLOW)
	    ? RESOLVE_LAST_SLINK : NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (where.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_name_to_handle_at,
		    where.at.kernel_fd, where.path, mount_id, flags);

	return check_errno(-ENOTSUP);
}

static long
hook_execveat(struct fd_desc at, const char *path,
		char *const argv[], char *const envp[], int flags)
{
	struct resolved_path where;

	resolve_path(at, path, &where,
	    (flags & AT_SYMLINK_NOFOLLOW)
	    ? NO_RESOLVE_LAST_SLINK : RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (where.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_execveat,
		    where.at.kernel_fd, where.path, argv, envp, flags);

	/* The expectation is that pmemfile will never support this. */
	return check_errno(-ENOTSUP);
}

static long
hook_copy_file_range(long fd_in, loff_t *off_in, long fd_out,
			loff_t *off_out, size_t len, unsigned flags)
{
	if (is_fd_in_table(fd_out))
		return check_errno(-ENOTSUP);

	if (is_fd_in_table(fd_in))
		return check_errno(-ENOTSUP);

	return syscall_no_intercept(SYS_copy_file_range,
	    fd_in, off_in, fd_out, off_out, len, flags);
}

static long
hook_fallocate(long fd, int mode, off_t offset, off_t len)
{
	struct fd_association *file = fd_table + fd;
	int r = pmemfile_fallocate(file->pool->pool, file->file, mode,
					offset, len);

	if (r < 0)
		r = -errno;

	log_write(
	    "pmemfile_fallocate(%p, %p, %d, %" PRId64 ", %" PRId64 ") = %d",
	    (void *)file->pool->pool, (void *)file->file, mode,
	    (int64_t)offset, (int64_t)len, r);

	return check_errno(r);
}

static long
hook_mmap(long arg0, long arg1, long arg2,
		long arg3, long fd, long arg5)
{
	if (is_fd_in_table(fd))
		return check_errno(-ENOTSUP);

	return syscall_no_intercept(SYS_mmap,
	    arg0, arg1, arg2, arg3, fd, arg5);
}
