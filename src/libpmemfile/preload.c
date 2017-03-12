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

#include "libsyscall_intercept_hook_point.h"
#include "libpmemfile-posix.h"
#include "util.h"

/*
 * Some syscalls that are missing on older kernels.
 */
#ifndef SYS_renameat2
#define SYS_renameat2 316
#endif

#ifndef SYS_execveat
#define SYS_execveat 322
#endif

#ifndef SYS_copy_file_range
#define SYS_copy_file_range 326
#endif

#ifndef SYS_preadv2
#define SYS_preadv2 327
#endif

#ifndef SYS_pwritev2
#define SYS_pwritev2 328
#endif

static pf_noreturn void
exit_group_no_intercept(int ret)
{
	syscall_no_intercept(SYS_exit_group, ret);
	__builtin_unreachable();
}

static inline void
FATAL(const char *str)
{
	syscall_no_intercept(SYS_write, 2, str, strlen(str));
	exit_group_no_intercept(128 + 7);
}

#include "sys_util.h"

#include "fd_pool.h"

#include "preload.h"

static int hook(long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5,
			long *result);

static bool syscall_number_filter[0x200];
static bool syscall_needs_fd_rlock[0x200];
static bool syscall_needs_fd_wlock[0x200];
static bool syscall_needs_pmem_cwd_rlock[0x200];
static bool syscall_has_fd_first_arg[0x200];

static pthread_rwlock_t fd_lock = PTHREAD_RWLOCK_INITIALIZER;

static struct pool_description pools[0x100];
static int pool_count;

/*
 * A separate place to keep track of fds used to hold mount points open, in
 * the previous array. The application should not be aware of these, thus
 * whenever these file descriptors are encountered during interposing, -EBADF
 * must be returned. The contents of this array does not change after startup.
 */
static bool mount_point_fds[PMEMFILE_MAX_FD + 1];

static struct fd_association fd_table[PMEMFILE_MAX_FD + 1];

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
cwd_desc()
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
	} else if (fd_pool_has_allocated(fd)) {
		result.pmem_fda = fd_table[fd];
	} else {
		result.pmem_fda.pool = NULL;
		result.pmem_fda.file = NULL;
	}

	return result;
}

static __thread bool reenter = false;
/*
 * This way the default can be overridden from the command line during
 * a build, without altering the source. As a second option, it can be
 * overridden using a environment variable at runtime.
 */
#ifndef PMEMFILE_DEFAULT_USE_SYMLINK_STRICT
#define PMEMFILE_DEFAULT_USE_SYMLINK_STRICT false
#endif

static bool use_stricter_symlink_resolver = PMEMFILE_DEFAULT_USE_SYMLINK_STRICT;

static void log_init(const char *path, const char *trunc);
static void log_write(const char *fmt, ...) pf_printf_like(1, 2);

static void establish_mount_points(const char *);
static void setup_strict_symlink_flag(const char *);
static void init_hooking(void);

pf_constructor void
pmemfile_preload_constructor(void)
{
	if (!libc_hook_in_process_allowed())
		return;

	log_init(getenv("PMEMFILE_PRELOAD_LOG"),
			getenv("PMEMFILE_PRELOAD_LOG_TRUNC"));

	const char *env_str = getenv("PMEMFILE_EXIT_ON_NOT_SUPPORTED");
	exit_on_ENOTSUP = env_str ? env_str[0] == '1' : 0;

	// establish_mount_points already needs to have the flag set up
	setup_strict_symlink_flag(getenv("PMEMFILE_USE_SYMLINK_STRICT"));

	establish_mount_points(getenv("PMEMFILE_POOLS"));

	if (pool_count == 0)
		return; // No pools mounted. TODO: prevent syscall interception

	/*
	 * Must be the last step, the callback can be called anytime
	 * after the call to init_hooking()
	 */
	init_hooking();
}

static void
setup_strict_symlink_flag(const char *e)
{
	// Only overwrite the default when explicitly requested by an env var.
	if (e != NULL)
		use_stricter_symlink_resolver = (e[0] != '0');
}

static void
init_hooking(void)
{
	// todo: move this filtering to the intercepting library
	syscall_number_filter[SYS_access] = true;
	syscall_number_filter[SYS_chmod] = true;
	syscall_number_filter[SYS_chown] = true;
	syscall_number_filter[SYS_close] = true;
	syscall_number_filter[SYS_faccessat] = true;
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
	syscall_number_filter[SYS_fadvise64] = true;
	syscall_number_filter[SYS_fallocate] = true;
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
	syscall_number_filter[SYS_readlinkat] = true;
	syscall_number_filter[SYS_readlink] = true;
	syscall_number_filter[SYS_readv] = true;
	syscall_number_filter[SYS_removexattr] = true;
	syscall_number_filter[SYS_sendfile] = true;
	syscall_number_filter[SYS_splice] = true;
	syscall_number_filter[SYS_utimensat] = true;
	syscall_number_filter[SYS_utimes] = true;
	syscall_number_filter[SYS_utime] = true;
	syscall_number_filter[SYS_writev] = true;

	syscall_needs_fd_rlock[SYS_faccessat] = true;
	syscall_needs_fd_rlock[SYS_fchmodat] = true;
	syscall_needs_fd_rlock[SYS_fchmod] = true;
	syscall_needs_fd_rlock[SYS_fchownat] = true;
	syscall_needs_fd_rlock[SYS_fchown] = true;
	syscall_needs_fd_rlock[SYS_fcntl] = true;
	syscall_needs_fd_rlock[SYS_fdatasync] = true;
	syscall_needs_fd_rlock[SYS_fgetxattr] = true;
	syscall_needs_fd_rlock[SYS_flock] = true;
	syscall_needs_fd_rlock[SYS_fsetxattr] = true;
	syscall_needs_fd_rlock[SYS_fstat] = true;
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
	syscall_needs_fd_rlock[SYS_pwrite64] = true;
	syscall_needs_fd_rlock[SYS_readlinkat] = true;
	syscall_needs_fd_rlock[SYS_read] = true;
	syscall_needs_fd_rlock[SYS_renameat2] = true;
	syscall_needs_fd_rlock[SYS_renameat] = true;
	syscall_needs_fd_rlock[SYS_sendfile] = true;
	syscall_needs_fd_rlock[SYS_symlinkat] = true;
	syscall_needs_fd_rlock[SYS_syncfs] = true;
	syscall_needs_fd_rlock[SYS_unlinkat] = true;
	syscall_needs_fd_rlock[SYS_write] = true;

	syscall_needs_fd_wlock[SYS_close] = true;
	syscall_needs_fd_wlock[SYS_openat] = true;
	syscall_needs_fd_wlock[SYS_open] = true;

	syscall_needs_pmem_cwd_rlock[SYS_access] = true;
	syscall_needs_pmem_cwd_rlock[SYS_chmod] = true;
	syscall_needs_pmem_cwd_rlock[SYS_chown] = true;
	syscall_needs_pmem_cwd_rlock[SYS_faccessat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_fchmodat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_fchownat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_getxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_lchown] = true;
	syscall_needs_pmem_cwd_rlock[SYS_lgetxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_linkat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_link] = true;
	syscall_needs_pmem_cwd_rlock[SYS_lsetxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_lstat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_mkdirat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_mkdir] = true;
	syscall_needs_pmem_cwd_rlock[SYS_newfstatat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_openat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_open] = true;
	syscall_needs_pmem_cwd_rlock[SYS_renameat2] = true;
	syscall_needs_pmem_cwd_rlock[SYS_renameat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_rename] = true;
	syscall_needs_pmem_cwd_rlock[SYS_rmdir] = true;
	syscall_needs_pmem_cwd_rlock[SYS_setxattr] = true;
	syscall_needs_pmem_cwd_rlock[SYS_stat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_symlinkat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_symlink] = true;
	syscall_needs_pmem_cwd_rlock[SYS_truncate] = true;
	syscall_needs_pmem_cwd_rlock[SYS_unlinkat] = true;
	syscall_needs_pmem_cwd_rlock[SYS_unlink] = true;

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
	syscall_has_fd_first_arg[SYS_ftruncate] = true;
	syscall_has_fd_first_arg[SYS_getdents64] = true;
	syscall_has_fd_first_arg[SYS_getdents] = true;
	syscall_has_fd_first_arg[SYS_lseek] = true;
	syscall_has_fd_first_arg[SYS_mmap] = true;
	syscall_has_fd_first_arg[SYS_pread64] = true;
	syscall_has_fd_first_arg[SYS_preadv2] = true;
	syscall_has_fd_first_arg[SYS_pwrite64] = true;
	syscall_has_fd_first_arg[SYS_pwritev2] = true;
	syscall_has_fd_first_arg[SYS_readahead] = true;
	syscall_has_fd_first_arg[SYS_readv] = true;
	syscall_has_fd_first_arg[SYS_read] = true;
	syscall_has_fd_first_arg[SYS_writev] = true;
	syscall_has_fd_first_arg[SYS_write] = true;

	// Install the callback to be calleb by the syscall intercepting library
	intercept_hook_point = &hook;
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

	if (config == NULL || config[0] == 0) {
		log_write("No mount point");
		return;
	}

	do {
		if ((size_t)pool_count >= sizeof(pools) / sizeof(pools[0]))
			config_error();

		struct pool_description *pool_desc = pools + pool_count;

		// fetch pool_desc->mount_point
		config = parse_mount_point(pool_desc, config);

		// fetch pool_desc->poolfile_path
		config = parse_pool_path(pool_desc, config);

		// fetch pool_desc-fd, pool_desc->stat
		open_mount_point(pool_desc);

		pool_desc->pool = NULL;

		++pool_count;

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
	if (conf[0] != '/') // Relative path is not allowed
		config_error();

	// There should be a colon separating the mount path from the pool path
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

	// Return a pointer to the char following the colon
	return colon + 1;
}

static const char *
parse_pool_path(struct pool_description *pool, const char *conf)
{
	if (conf[0] != '/') // Relative path is not allowed
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

	// Return a pointer to the char following the semicolon, or NULL.
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
static long hook_faccessat(struct fd_desc at, long path_arg,
				long mode, long flags);
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
				mode_t mode, int flags);
static long hook_fchown(long fd, uid_t owner, gid_t group);
static long hook_fchownat(struct fd_desc at, const char *path,
				uid_t owner, gid_t group, int flags);
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

static long
dispatch_syscall(long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5)
{
	// Use pmemfile_openat to implement open, create, openat
	if (syscall_number == SYS_open)
		return hook_openat(cwd_desc(), arg0, arg1, arg2);

	if (syscall_number == SYS_creat)
		return hook_openat(cwd_desc(), arg0,
				O_WRONLY | O_CREAT | O_TRUNC, arg1);

	if (syscall_number == SYS_openat)
		return hook_openat(fetch_fd(arg0), arg1, arg2, arg3);

	if (syscall_number == SYS_rename)
		return hook_renameat2(cwd_desc(), (const char *)arg0,
					cwd_desc(), (const char *)arg1, 0);

	if (syscall_number == SYS_renameat)
		return hook_renameat2(fetch_fd(arg0), (const char *)arg1,
					fetch_fd(arg2), (const char *)arg3, 0);

	if (syscall_number == SYS_renameat2)
		return hook_renameat2(fetch_fd(arg0), (const char *)arg1,
					fetch_fd(arg2), (const char *)arg3,
					(unsigned)arg4);

	// Use pmemfile_linkat to implement link, linkat
	if (syscall_number == SYS_link)
		return hook_linkat(cwd_desc(), arg0, cwd_desc(), arg1, 0);

	if (syscall_number == SYS_linkat)
		return hook_linkat(fetch_fd(arg0), arg1, fetch_fd(arg2), arg3,
		    arg4);

	// Use pmemfile_unlinkat to implement unlink, unlinkat, rmdir
	if (syscall_number == SYS_unlink)
		return hook_unlinkat(cwd_desc(), arg0, 0);

	if (syscall_number == SYS_unlinkat)
		return hook_unlinkat(fetch_fd(arg0), arg1, arg2);

	if (syscall_number == SYS_rmdir)
		return hook_unlinkat(cwd_desc(), arg0, AT_REMOVEDIR);

	// Use pmemfile_mkdirat to implement mkdir, mkdirat
	if (syscall_number == SYS_mkdir)
		return hook_mkdirat(cwd_desc(), arg0, arg1);

	if (syscall_number == SYS_mkdirat)
		return hook_mkdirat(fetch_fd(arg0), arg1, arg2);

	// Use pmemfile_faccessat to implement access, faccessat
	if (syscall_number == SYS_access)
		return hook_faccessat(cwd_desc(), arg0, 0, 0);

	if (syscall_number == SYS_faccessat)
		return hook_faccessat(fetch_fd(arg0), arg1, arg2, arg3);

	/*
	 * The newfstatat syscall implements both stat and lstat.
	 * Linux calls it: newfstatat ( I guess there was an old one )
	 * POSIX / libc interfaces call it: fstatat
	 * pmemfile calls it: pmemfile_fstatat
	 *
	 * fstat is unique.
	 */
	if (syscall_number == SYS_stat)
		return hook_newfstatat(cwd_desc(), arg0, arg1, 0);

	if (syscall_number == SYS_lstat)
		return hook_newfstatat(cwd_desc(), arg0, arg1,
		    AT_SYMLINK_NOFOLLOW);

	if (syscall_number == SYS_newfstatat)
		return hook_newfstatat(fetch_fd(arg0), arg1, arg2, arg3);

	if (syscall_number == SYS_fstat)
		return hook_fstat(arg0, arg1);

	/*
	 * Some simpler ( in terms of argument processing ) syscalls,
	 * which don't require path resolution.
	 */
	if (syscall_number == SYS_close)
		return hook_close(arg0);

	if (syscall_number == SYS_write)
		return hook_write(arg0, (const char *)arg1, (size_t)arg2);

	if (syscall_number == SYS_read)
		return hook_read(arg0, (char *)arg1, (size_t)arg2);

	if (syscall_number == SYS_lseek)
		return hook_lseek(arg0, arg1, (int)arg2);

	if (syscall_number == SYS_pread64)
		return hook_pread64(arg0, (char *)arg1,
		    (size_t)arg2, (off_t)arg3);

	if (syscall_number == SYS_pwrite64)
		return hook_pwrite64(arg0, (const char *)arg1,
		    (size_t)arg2, (off_t)arg3);

	if (syscall_number == SYS_getdents)
		return hook_getdents(arg0, arg1, (unsigned)arg2);

	if (syscall_number == SYS_getdents64)
		return hook_getdents64(arg0, arg1, (unsigned)arg2);

	/*
	 * NOP implementations for the xattr family. None of these
	 * actually call pmemfile-posix. Some of them do need path resolution,
	 * fgetxattr and fsetxattr don't.
	 */
	if (syscall_number == SYS_getxattr)
		return hook_getxattr(arg0, arg1, arg2, arg3,
		    RESOLVE_LAST_SLINK);

	if (syscall_number == SYS_lgetxattr)
		return hook_getxattr(arg0, arg1, arg2, arg3,
		    NO_RESOLVE_LAST_SLINK);

	if (syscall_number == SYS_setxattr)
		return hook_setxattr(arg0, arg1, arg2, arg3, arg4,
		    RESOLVE_LAST_SLINK);

	if (syscall_number == SYS_lsetxattr)
		return hook_setxattr(arg0, arg1, arg2, arg3, arg4,
		    NO_RESOLVE_LAST_SLINK);

	if (syscall_number == SYS_fgetxattr)
		return 0;

	if (syscall_number == SYS_fsetxattr)
		return check_errno(-ENOTSUP);

	if (syscall_number == SYS_fcntl)
		return hook_fcntl(arg0, (int)arg1, arg2);

	if (syscall_number == SYS_syncfs)
		return 0;

	if (syscall_number == SYS_fdatasync)
		return 0;

	if (syscall_number == SYS_flock)
		return hook_flock(arg0, (int)arg1);

	if (syscall_number == SYS_truncate)
		return hook_truncate((const char *)arg0, arg1);

	if (syscall_number == SYS_ftruncate)
		return hook_ftruncate(arg0, arg1);

	if (syscall_number == SYS_symlink)
		return hook_symlinkat((const char *)arg0,
					cwd_desc(), (const char *)arg1);

	if (syscall_number == SYS_symlinkat)
		return hook_symlinkat((const char *)arg0,
					fetch_fd(arg1), (const char *)arg2);

	if (syscall_number == SYS_chmod)
		return hook_fchmodat(cwd_desc(), (const char *)arg0,
					(mode_t)arg1, 0);

	if (syscall_number == SYS_fchmod)
		return hook_fchmod(arg0, (mode_t)arg1);

	if (syscall_number == SYS_fchmodat)
		return hook_fchmodat(fetch_fd(arg0), (const char *)arg1,
					(mode_t)arg2, (int)arg3);

	if (syscall_number == SYS_chown)
		return hook_fchownat(cwd_desc(), (const char *)arg0,
					(uid_t)arg1, (gid_t)arg2, 0);

	if (syscall_number == SYS_lchown)
		return hook_fchownat(cwd_desc(), (const char *)arg0,
					(uid_t)arg1, (gid_t)arg2,
					AT_SYMLINK_NOFOLLOW);

	if (syscall_number == SYS_fchown)
		return hook_fchown(arg0, (uid_t)arg1, (gid_t)arg2);

	if (syscall_number == SYS_fchownat)
		return hook_fchownat(fetch_fd(arg0), (const char *)arg1,
					(uid_t)arg2, (gid_t)arg3, (int)arg4);

	if (syscall_number == SYS_mmap ||
	    syscall_number == SYS_readv ||
	    syscall_number == SYS_writev ||
	    syscall_number == SYS_dup ||
	    syscall_number == SYS_dup2 ||
	    syscall_number == SYS_dup3 ||
	    syscall_number == SYS_flistxattr ||
	    syscall_number == SYS_fremovexattr ||
	    syscall_number == SYS_fadvise64 ||
	    syscall_number == SYS_fallocate ||
	    syscall_number == SYS_preadv2 ||
	    syscall_number == SYS_pwritev2 ||
	    syscall_number == SYS_readahead)
		return check_errno(-ENOTSUP);

	if (syscall_number == SYS_sendfile)
		return hook_sendfile(arg0, arg1, (off_t *)arg2, (size_t)arg3);

	/*
	 * Some syscalls that have a path argument, but are not ( yet ) handled
	 * by libpmemfile-posix. The argument of these are not interpreted,
	 * except for the path itself. If the path points to something pmemfile
	 * resident, -ENOTSUP is returned, otherwise, the call is forwarded
	 * to the kernel.
	 */
	if (syscall_number == SYS_chroot ||
	    syscall_number == SYS_listxattr ||
	    syscall_number == SYS_removexattr ||
	    syscall_number == SYS_utime ||
	    syscall_number == SYS_utimes)
		return nosup_syscall_with_path(syscall_number,
		    arg0, RESOLVE_LAST_SLINK,
		    arg0, arg1, arg2, arg3, arg4, arg5);

	if (syscall_number == SYS_llistxattr ||
	    syscall_number == SYS_lremovexattr)
		return nosup_syscall_with_path(syscall_number,
		    arg0, NO_RESOLVE_LAST_SLINK,
		    arg0, arg1, arg2, arg3, arg4, arg5);

	if (syscall_number == SYS_readlink)
		return hook_readlinkat(cwd_desc(), (const char *)arg0,
		    (char *)arg1, (size_t)arg2);

	if (syscall_number == SYS_readlinkat)
		return hook_readlinkat(cwd_desc(), (const char *)arg0,
		    (char *)arg1, (size_t)arg2);

	if (syscall_number == SYS_splice)
		return hook_splice(arg0, (loff_t *)arg1,
				arg2, (loff_t *)arg3,
				(size_t)arg4, (unsigned)arg5);

	if (syscall_number == SYS_futimesat)
		return hook_futimesat(fetch_fd(arg0), (const char *)arg1,
			(const struct timeval *)arg2);

	if (syscall_number == SYS_name_to_handle_at)
		return hook_name_to_handle_at(fetch_fd(arg0),
		    (const char *)arg1, (struct file_handle *)arg2,
		    (int *)arg3, (int)arg4);

	if (syscall_number == SYS_execve)
		return hook_execveat(cwd_desc(), (const char *)arg0,
		    (char *const *)arg1, (char *const *)arg2, 0);

	if (syscall_number == SYS_execveat)
		return hook_execveat(fetch_fd(arg0), (const char *)arg1,
		    (char *const *)arg2, (char *const *)arg3, (int)arg4);

	if (syscall_number == SYS_copy_file_range)
		return hook_copy_file_range(arg0, (loff_t *)arg1,
		    arg2, (loff_t *)arg3, (size_t)arg4, (unsigned)arg5);

	// Did we miss something?
	assert(false);
	return syscall_no_intercept(syscall_number,
	    arg0, arg1, arg2, arg3, arg4, arg5);
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

	// todo: move this filtering to the intercepting library
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
		util_rwlock_rdlock(&fd_lock);
	else if (syscall_needs_fd_wlock[syscall_number])
		util_rwlock_wrlock(&fd_lock);

	if (syscall_has_fd_first_arg[syscall_number] &&
	    !fd_pool_has_allocated(arg0)) {
		// shortcut for write, read, and such
		// so this check doesn't need to be copy-pasted into them
		is_hooked = NOT_HOOKED;
	} else {
		is_hooked = HOOKED;
		*syscall_return_value = dispatch_syscall(syscall_number,
		    arg0, arg1, arg2, arg3, arg4, arg5);
	}


	if (syscall_needs_fd_rlock[syscall_number] ||
	    syscall_needs_fd_wlock[syscall_number])
		util_rwlock_unlock(&fd_lock);

	if (syscall_needs_pmem_cwd_rlock[syscall_number])
		util_rwlock_unlock(&pmem_cwd_lock);

	reenter = false;

	return is_hooked;
}

static long
hook_close(long fd)
{
	fd_pool_release_fd(fd);

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

	if (is_fda_null(&where.at.pmem_fda)) // Not pmemfile resident path
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

	if (fd_pool_has_allocated(fd)) {
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

static void
open_new_pool(struct pool_description *p)
{
	if (p->pool == NULL) {
		PMEMfilepool *pfp = pmemfile_pool_open(p->poolfile_path);
		__atomic_store_n(&p->pool, pfp, __ATOMIC_SEQ_CST);
	}
}

/*
 * With each virtual mount point an inode number is stored, and this
 * function can be used to lookup a mount point by inode number.
 */
struct pool_description *
lookup_pd_by_inode(__ino_t inode)
{
	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (p->stat.st_ino == inode)  {
			PMEMfilepool *pfp;

			pfp = __atomic_load_n(&p->pool, __ATOMIC_SEQ_CST);
			if (pfp == NULL)
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
		// TODO: first compare the lengths of the two strings to
		// avoid strcmp calls
		if (strcmp(p->mount_point, path) == 0)  {
			PMEMfilepool *pfp;

			pfp = __atomic_load_n(&p->pool, __ATOMIC_SEQ_CST);
			if (pfp == NULL)
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
		(struct pmemfile_stat *)arg1, (int)arg2);

	if (r != 0)
		r = -errno;

	return check_errno(r);
}

static long
hook_fstat(long fd, long buf_addr)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_fstat(file->pool->pool, file->file,
			(struct pmemfile_stat *)buf_addr);

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
hook_faccessat(struct fd_desc at, long path_arg, long mode, long flags)
{
	struct resolved_path where;

	resolve_path(at, (const char *)path_arg, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda)) {
		return syscall_no_intercept(SYS_faccessat,
		    where.at.kernel_fd, where.path, mode, flags);
	}

	struct pmemfile_stat stat;
	/* XXX implement for real! */
	if (pmemfile_fstatat(where.at.pmem_fda.pool->pool,
			where.at.pmem_fda.file, where.path, &stat, 0))
		return check_errno(-errno);
	return 0;

	/*
	 *
	 * TODO
	 *
	 * long r = pmemfile_faccessat(where.pool->pool,
	 *		where.path, (mode_t)arg1);
	 *
	 * log_write("pmemfile_lstat(%p, \"%s\", %ld) = %ld",
	 *     (void *)where.pool->pool, where.path, arg1, r);
	 *
	 * if (r < 0)
	 *     r = -errno;
	 *
	 * return r;
	 *
	 */
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
			return check_errno(-ENOTSUP); // todo...

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
			return check_errno(-ENOTSUP); // todo...

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
		    where.at.kernel_fd, path_arg, mode);

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

	if (where.error_code != 0) // path resolution failed
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda)) // Not pmemfile resident path
		return syscall_no_intercept(SYS_openat,
		    where.at.kernel_fd, arg0, flags, mode);

	// The fd to represent the pmem resident file for the application
	long fd = fd_pool_fetch_new_fd();

	if (fd < 0) { // error while trying to allocate a new fd
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
			fd_pool_release_fd(fd);
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
hook_fchmodat(struct fd_desc at, const char *path,
				mode_t mode, int flags)
{
	struct resolved_path where;

	resolve_path(at, path, &where, RESOLVE_LAST_SLINK);
	if (where.error_code != 0)
		return where.error_code;

	if (where.at.pmem_fda.pool == NULL)
		return syscall_no_intercept(SYS_fchmodat,
		    where.at.kernel_fd, where.path, mode, flags);

	int r = pmemfile_fchmodat(where.at.pmem_fda.pool->pool,
			where.at.pmem_fda.file, where.path, mode, flags);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_fchmodat(%p, %p, \"%s\", 0%o, %d) = %d",
	    (void *)where.at.pmem_fda.pool->pool,
	    (void *)where.at.pmem_fda.file,
	    where.path, mode, flags, r);

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
	if (fd_pool_has_allocated(out_fd))
		return check_errno(-ENOTSUP);

	if (fd_pool_has_allocated(in_fd))
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
	if (fd_pool_has_allocated(fd_out))
		return check_errno(-ENOTSUP);

	if (fd_pool_has_allocated(fd_in))
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
		    where.at.kernel_fd, path, times);

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
		    where.at.kernel_fd, path, mount_id, flags);

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
		    where.at.kernel_fd, path, argv, envp, flags);

	/* The expectation is that pmemfile will never support this. */
	return check_errno(-ENOTSUP);
}

static long
hook_copy_file_range(long fd_in, loff_t *off_in, long fd_out,
			loff_t *off_out, size_t len, unsigned flags)
{
	if (fd_pool_has_allocated(fd_out))
		return check_errno(-ENOTSUP);

	if (fd_pool_has_allocated(fd_in))
		return check_errno(-ENOTSUP);

	return syscall_no_intercept(SYS_copy_file_range,
	    fd_in, off_in, fd_out, off_out, len, flags);
}
