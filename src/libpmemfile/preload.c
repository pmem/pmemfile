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
#include <linux/fs.h>
#include <sys/fsuid.h>

#include <asm-generic/errno.h>

#include "compiler_utils.h"
#include "libsyscall_intercept_hook_point.h"
#include "libpmemfile-posix.h"
#include "sys_util.h"
#include "preload.h"
#include "syscall_early_filter.h"

static struct pool_description pools[0x100];
static int pool_count;

static bool is_memfd_syscall_available;

#define PMEMFILE_MAX_FD 0x8000

#ifndef RWF_HIPRI
#define RWF_HIPRI 0x00000001
#endif

#ifndef RWF_DSYNC
#define RWF_DSYNC 0x00000002
#endif

#ifndef RWF_SYNC
#define RWF_SYNC 0x00000004
#endif

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
static long check_errno(long e, long syscall_no)
{
	if (e == -ENOTSUP && exit_on_ENOTSUP) {
		char buf[100];
		sprintf(buf, "syscall %ld not supported by pmemfile, exiting",
				syscall_no);

		exit_with_msg(PMEMFILE_PRELOAD_EXIT_NOT_SUPPORTED, buf);
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

static  pf_printf_like(1, 2) void
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

void
exit_with_msg(int ret, const char *msg)
{
	if (msg && msg[0] == '!') {
		char buf[100];
		char *errstr = strerror_r(errno, buf, sizeof(buf));
		fprintf(stderr, "%s: %d %s\n", msg + 1, errno,
				errstr ? errstr : "unknown");

		log_write("%s: %d %s\n", msg + 1, errno,
				errstr ? errstr : "unknown");
	} else if (msg) {
		fprintf(stderr, "%s\n", msg);

		log_write("%s\n", msg);
	}

	exit(ret);
	__builtin_unreachable();
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

	return check_errno(r, SYS_write);
}

static long
hook_writev(long fd, const struct iovec *iov, int iovcnt)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_writev(file->pool->pool, file->file, iov, iovcnt);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_writev(%p, %p, %p, %d) = %ld",
	    (void *)file->pool->pool, (void *)file->file,
	    (void *)iov, iovcnt, r);

	return check_errno(r, SYS_writev);
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

	return check_errno(r, SYS_read);
}

static long
hook_readv(long fd, const struct iovec *iov, int iovcnt)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_readv(file->pool->pool, file->file, iov, iovcnt);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_readv(%p, %p, %p, %d) = %ld",
	    (void *)file->pool, (void *)file->file,
	    (void *)iov, iovcnt, r);

	return check_errno(r, SYS_readv);
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

	return check_errno(r, SYS_lseek);
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

	/* cross-pool link are not possible */
	if (where_new.at.pmem_fda.pool != where_old.at.pmem_fda.pool)
		return -EXDEV;

	if (is_fda_null(&where_new.at.pmem_fda))
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

	return check_errno(r, SYS_linkat);
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

	int r = pmemfile_unlinkat(where.at.pmem_fda.pool->pool,
		where.at.pmem_fda.file, where.path, (int)flags);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_unlink(%p, \"%s\") = %d",
	    (void *)where.at.pmem_fda.pool->pool, where.path, r);

	return check_errno(r, SYS_unlinkat);
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

		check_errno(result, SYS_chdir);
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

		check_errno(result, SYS_fchdir);
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

	if (pmemfile_getcwd(cwd_pool->pool, buf + mlen, size - mlen) == NULL)
		return check_errno(-errno, SYS_getcwd);

	return 0;
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

	return check_errno(r, SYS_newfstatat);
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

	return check_errno(r, SYS_fstat);
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

	return check_errno(r, SYS_pread64);
}

static long
hook_preadv(long fd, const struct iovec *iov, int iovcnt, off_t pos)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_preadv(file->pool->pool, file->file, iov, iovcnt,
			pos);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_preadv(%p, %p, %p, %d, %zu) = %ld",
	    (void *)file->pool, (void *)file->file,
	    (void *)iov, iovcnt, pos, r);

	return check_errno(r, SYS_preadv);
}

static long
hook_preadv2(long fd, const struct iovec *iov, int iovcnt, off_t pos, int flags)
{
	if (flags & ~(RWF_DSYNC | RWF_HIPRI | RWF_SYNC))
		return -EINVAL;

	return hook_preadv(fd, iov, iovcnt, pos);
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

	return check_errno(r, SYS_pwrite64);
}

static long
hook_pwritev(long fd, const struct iovec *iov, int iovcnt, off_t pos)
{
	struct fd_association *file = fd_table + fd;
	long r = pmemfile_pwritev(file->pool->pool, file->file, iov, iovcnt,
			pos);

	if (r < 0)
		r = -errno;

	log_write("pmemfile_pwritev(%p, %p, %p, %d, %zu) = %ld",
	    (void *)file->pool, (void *)file->file,
	    (const void *)iov, iovcnt, pos, r);

	return check_errno(r, SYS_pwritev);
}

static long
hook_pwritev2(long fd, const struct iovec *iov, int iovcnt, off_t pos,
		int flags)
{
	if (flags & ~(RWF_DSYNC | RWF_HIPRI | RWF_SYNC))
		return -EINVAL;

	return hook_pwritev(fd, iov, iovcnt, pos);
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

	if (r)
		return check_errno(-errno, SYS_faccessat);

	return 0;
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

	return check_errno(r, SYS_getdents);
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

	return check_errno(r, SYS_getdents64);
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
			return check_errno(-ENOTSUP, SYS_getxattr); /* XXX */

		return syscall_no_intercept(SYS_getxattr,
		    where.path, arg1, arg2, arg3);
	}

	return 0;
}

static long
hook_setxattr(long arg0, long arg1, long arg2, long arg3, long arg4,
		int resolve_last)
{
	struct resolved_path where;

	resolve_path(cwd_desc(), (const char *)arg0, &where, resolve_last);

	if (where.error_code != 0)
		return where.error_code;

	if (!is_fda_null(&where.at.pmem_fda))
		return check_errno(-ENOTSUP, SYS_setxattr);

	if (where.at.kernel_fd == AT_FDCWD)
		return check_errno(-ENOTSUP, SYS_setxattr); /* XXX */

	return syscall_no_intercept(SYS_setxattr, where.path, arg1, arg2, arg3,
			arg4);
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

	if (r)
		return check_errno(-errno, SYS_mkdirat);

	return 0;
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

	if (fd < 0) /* error while trying to allocate a new fd */
		return fd;

	PMEMfile *file = pmemfile_openat(where.at.pmem_fda.pool->pool,
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

	if (file == NULL) {
		(void) syscall_no_intercept(SYS_close, fd);
		return check_errno(-errno, SYS_openat);
	}

	fd_table[fd].pool = where.at.pmem_fda.pool;
	fd_table[fd].file = file;

	return fd;
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

	return check_errno(r, SYS_fcntl);
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

	return check_errno(r, SYS_flock);
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

	/* cross-pool renames are not supported */
	if (where_new.at.pmem_fda.pool != where_old.at.pmem_fda.pool)
		return -EXDEV;

	if (is_fda_null(&where_new.at.pmem_fda)) {
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

	return check_errno(r, SYS_renameat2);
}

static long
hook_truncate(const char *path, off_t length)
{
	struct resolved_path where;

	resolve_path(cwd_desc(), path, &where, RESOLVE_LAST_SLINK);
	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda))
		return syscall_no_intercept(SYS_truncate,
		    where.at.kernel_fd, where.path, length);

	int r = pmemfile_truncate(where.at.pmem_fda.pool->pool,
					where.path, length);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_truncate(%p, \"%s\", %lu) = %d",
	    (void *)where.at.pmem_fda.pool->pool,
	    where.path, length, r);

	return check_errno(r, SYS_truncate);
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

	return check_errno(r, SYS_ftruncate);
}

static long
hook_symlinkat(const char *target, struct fd_desc at, const char *linkpath)
{
	struct resolved_path where;

	resolve_path(at, linkpath, &where, NO_RESOLVE_LAST_SLINK);
	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda))
		return syscall_no_intercept(SYS_symlinkat, target,
		    where.at.kernel_fd, where.path);

	int r = pmemfile_symlinkat(where.at.pmem_fda.pool->pool, target,
					where.at.pmem_fda.file, where.path);

	if (r != 0)
		r = -errno;

	log_write("pmemfile_symlinkat(%p, \"%s\", %p, \"%s\") = %d",
	    (void *)where.at.pmem_fda.pool->pool, target,
	    (void *)where.at.pmem_fda.file, where.path, r);

	return check_errno(r, SYS_symlinkat);
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

	return check_errno(r, SYS_fchmod);
}

static long
hook_fchmodat(struct fd_desc at, const char *path, mode_t mode)
{
	struct resolved_path where;

	resolve_path(at, path, &where, RESOLVE_LAST_SLINK);
	if (where.error_code != 0)
		return where.error_code;

	if (is_fda_null(&where.at.pmem_fda))
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

	return check_errno(r, SYS_fchmodat);
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

	return check_errno(r, SYS_fchown);
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

	if (is_fda_null(&where.at.pmem_fda))
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

	return check_errno(r, SYS_fchownat);
}

static long
hook_sendfile(long out_fd, long in_fd, off_t *offset, size_t count)
{
	if (is_fd_in_table(out_fd))
		return check_errno(-ENOTSUP, SYS_sendfile);

	if (is_fd_in_table(in_fd))
		return check_errno(-ENOTSUP, SYS_sendfile);

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

	if (is_fda_null(&where.at.pmem_fda))
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

	return check_errno(r, SYS_readlinkat);
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
	if (is_fda_null(&where.at.pmem_fda) && where.at.kernel_fd == AT_FDCWD)
		return syscall_no_intercept(syscall_number,
		    arg0, arg1, arg2, arg3, arg4, arg5);

	return check_errno(-ENOTSUP, syscall_number);
}

static long
hook_splice(long fd_in, loff_t *off_in, long fd_out,
			loff_t *off_out, size_t len, unsigned flags)
{
	if (is_fd_in_table(fd_out))
		return check_errno(-ENOTSUP, SYS_splice);

	if (is_fd_in_table(fd_in))
		return check_errno(-ENOTSUP, SYS_splice);

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

	if (is_fda_null(&where.at.pmem_fda))
		return syscall_no_intercept(SYS_futimesat,
		    where.at.kernel_fd, where.path, times);

	return check_errno(-ENOTSUP, SYS_futimesat);
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

	return check_errno(-ENOTSUP, SYS_name_to_handle_at);
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

	if (is_fda_null(&where.at.pmem_fda))
		return syscall_no_intercept(SYS_execveat,
		    where.at.kernel_fd, where.path, argv, envp, flags);

	/* The expectation is that pmemfile will never support this. */
	return check_errno(-ENOTSUP, SYS_execveat);
}

static long
hook_copy_file_range(long fd_in, loff_t *off_in, long fd_out,
			loff_t *off_out, size_t len, unsigned flags)
{
	if (is_fd_in_table(fd_out))
		return check_errno(-ENOTSUP, SYS_copy_file_range);

	if (is_fd_in_table(fd_in))
		return check_errno(-ENOTSUP, SYS_copy_file_range);

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

	return check_errno(r, SYS_fallocate);
}

static long
hook_mmap(long arg0, long arg1, long arg2,
		long arg3, long fd, long arg5)
{
	if (is_fd_in_table(fd))
		return check_errno(-ENOTSUP, SYS_mmap);

	return syscall_no_intercept(SYS_mmap,
	    arg0, arg1, arg2, arg3, fd, arg5);
}

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

	case SYS_writev:
		return hook_writev(arg0, (struct iovec *)arg1, (int)arg2);

	case SYS_read:
		return hook_read(arg0, (char *)arg1, (size_t)arg2);

	case SYS_readv:
		return hook_readv(arg0, (struct iovec *)arg1, (int)arg2);

	case SYS_lseek:
		return hook_lseek(arg0, arg1, (int)arg2);

	case SYS_pread64:
		return hook_pread64(arg0, (char *)arg1, (size_t)arg2,
				(off_t)arg3);

	case SYS_preadv:
		return hook_preadv(arg0, (struct iovec *)arg1, (int)arg2,
				(off_t)arg3);

	case SYS_preadv2:
		return hook_preadv2(arg0, (struct iovec *)arg1, (int)arg2,
				(off_t)arg3, (int)arg4);

	case SYS_pwrite64:
		return hook_pwrite64(arg0, (const char *)arg1, (size_t)arg2,
				(off_t)arg3);

	case SYS_pwritev:
		return hook_pwritev(arg0, (struct iovec *)arg1, (int)arg2,
				(off_t)arg3);

	case SYS_pwritev2:
		return hook_pwritev2(arg0, (struct iovec *)arg1, (int)arg2,
				(off_t)arg3, (int)arg4);

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

	case SYS_fcntl:
		return hook_fcntl(arg0, (int)arg1, arg2);

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
	int oerrno;

	if (p->pool != NULL)
		return; /* already open */

	if ((pfp = pmemfile_pool_open(p->poolfile_path)) == NULL)
		return; /* failed to open */

	if (pmemfile_setreuid(pfp, getuid(), geteuid()))
		goto err;

	uid_t fsuid = (uid_t)setfsuid(geteuid());
	setfsuid(fsuid);
	if (pmemfile_setfsuid(pfp, fsuid) < 0)
		goto err;

	if (pmemfile_setregid(pfp, getgid(), getegid()))
		goto err;

	gid_t fsgid = (gid_t)setfsgid(getegid());
	setfsgid(fsgid);
	if (pmemfile_setfsgid(pfp, fsgid) < 0)
		goto err;

	int gnum = getgroups(0, NULL);
	if (gnum > 0) {
		gid_t *groups = malloc((unsigned)gnum * sizeof(*groups));
		if (!groups)
			goto err;

		if (getgroups(gnum, groups) != gnum) {
			free(groups);
			goto err;
		}

		if (pmemfile_setgroups(pfp, (unsigned)gnum, groups)) {
			free(groups);
			goto err;
		}

		free(groups);
	} else if (gnum < 0) {
		goto err;
	}

	mode_t um = umask(0);
	umask(um);
	pmemfile_umask(pfp, um);

	if (pmemfile_stat(pfp, "/", &p->pmem_stat) != 0)
		goto err;

	__atomic_store_n(&p->pool, pfp, __ATOMIC_RELEASE);
	return;

err:
	oerrno = errno;
	pmemfile_pool_close(pfp);
	errno = oerrno;
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

/*
 * Return values expected by libcintercept :
 * A non-zero return value if it should execute the syscall,
 * zero return value if it should not execute the syscall, and
 * use *result value as the syscall's result.
 */
#define NOT_HOOKED 1
#define HOOKED 0

static int
hook(long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5,
			long *syscall_return_value)
{
	assert(pool_count > 0);

	if (syscall_number == SYS_chdir) {
		*syscall_return_value = hook_chdir((const char *)arg0);
		return HOOKED;
	}

	if (syscall_number == SYS_fchdir) {
		*syscall_return_value = hook_fchdir(arg0);
		return HOOKED;
	}

	if (syscall_number == SYS_getcwd) {
		util_rwlock_rdlock(&pmem_cwd_lock);
		*syscall_return_value = hook_getcwd((char *)arg0, (size_t)arg1);
		util_rwlock_unlock(&pmem_cwd_lock);
		return HOOKED;
	}

	struct syscall_early_filter_entry filter_entry;
	filter_entry = get_early_filter_entry(syscall_number);

	if (!filter_entry.must_handle)
		return NOT_HOOKED;

	int is_hooked;

	if (filter_entry.cwd_rlock)
		util_rwlock_rdlock(&pmem_cwd_lock);

	if (filter_entry.fd_rlock)
		util_rwlock_rdlock(&fd_table_lock);
	else if (filter_entry.fd_wlock)
		util_rwlock_wrlock(&fd_table_lock);

	if (filter_entry.fd_first_arg && !is_fd_in_table(arg0)) {
		/*
		 * shortcut for write, read, and such so this check doesn't
		 * need to be copy-pasted into them
		 */
		is_hooked = NOT_HOOKED;
	} else {
		is_hooked = HOOKED;
		if (filter_entry.returns_zero)
			*syscall_return_value = 0;
		else if (filter_entry.returns_ENOTSUP)
			*syscall_return_value = check_errno(-ENOTSUP,
					syscall_number);
		else
			*syscall_return_value = dispatch_syscall(syscall_number,
			    arg0, arg1, arg2, arg3, arg4, arg5);
	}

	if (filter_entry.fd_rlock || filter_entry.fd_wlock)
		util_rwlock_unlock(&fd_table_lock);

	if (filter_entry.cwd_rlock)
		util_rwlock_unlock(&pmem_cwd_lock);

	return is_hooked;
}

/*
 * hook_reentrance_guard_wrapper -- a wrapper which can notice reentrance.
 *
 * The guard_flag flag allows pmemfile to prevent the hooking of its own
 * syscalls. E.g. while handling an open syscall, libpmemfile might
 * call pmemfile_pool_open, which in turn uses an open syscall internally.
 * This internally used open syscall is once again forwarded to libpmemfile,
 * but using this flag libpmemfile can notice this case of reentering itself.
 *
 * XXX This approach still contains a very significant bug, as libpmemfile being
 * called inside a signal handler might easily forward a mock fd to the kernel.
 */
static int
hook_reentrance_guard_wrapper(long syscall_number,
				long arg0, long arg1,
				long arg2, long arg3,
				long arg4, long arg5,
				long *syscall_return_value)
{
	static __thread bool guard_flag = false;

	if (guard_flag)
		return NOT_HOOKED;

	int is_hooked;

	guard_flag = true;
	is_hooked = hook(syscall_number, arg0, arg1, arg2, arg3, arg4, arg5,
				syscall_return_value);
	guard_flag = false;

	return is_hooked;
}

static void
init_hooking(void)
{
	/*
	 * Install the callback to be calleb by the syscall intercepting library
	 */
	intercept_hook_point = &hook_reentrance_guard_wrapper;
}

static void
config_error(const char *msg)
{
	exit_with_msg(PMEMFILE_PRELOAD_EXIT_CONFIG_ERROR, msg);
}

static const char *
parse_mount_point(struct pool_description *pool, const char *conf)
{
	if (conf[0] != '/') { /* Relative path is not allowed */
		config_error(
			"invalid pmemfile config: relative path is not allowed");
	}

	/*
	 * There should be a colon separating the mount path from the pool path.
	 */
	const char *colon = strchr(conf, ':');

	if (colon == NULL || colon == conf)
		config_error("invalid pmemfile config: no colon");

	if (((size_t)(colon - conf)) >= sizeof(pool->mount_point)) {
		config_error(
			"invalid pmemfile config: too long mount point path");
	}

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
	if (conf[0] != '/') { /* Relative path is not allowed */
		config_error(
			"invalid pmemfile config: relative path is not allowed");
	}

	/*
	 * The path should be followed by either with a null character - in
	 * which case this is the last pool in the conf - or a semicolon.
	 */
	size_t i;
	for (i = 0; conf[i] != ';' && conf[i] != '\0'; ++i) {
		if (i >= sizeof(pool->poolfile_path) - 1) {
			config_error(
				"invalid pmemfile config: too long pool path");
		}
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

	if (pool->fd < 0) {
		config_error(
			"invalid pmemfile config: cannot open mount point");
	}

	if ((size_t)pool->fd >= ARRAY_SIZE(mount_point_fds)) {
		exit_with_msg(PMEMFILE_PRELOAD_EXIT_TOO_MANY_FDS,
				"mount point fd too large");
	}

	mount_point_fds[pool->fd] = true;

	if (syscall_no_intercept(SYS_fstat, pool->fd, &pool->stat) != 0) {
		config_error(
			"invalid pmemfile config: cannot fstat mount point");
	}

	if (!S_ISDIR(pool->stat.st_mode)) {
		config_error(
			"invalid pmemfile config: mount point is not a directory");
	}
}

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
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		exit_with_msg(PMEMFILE_PRELOAD_EXIT_GETCWD_FAILED, "!getcwd");

	struct stat kernel_cwd_stat;
	if (stat(cwd, &kernel_cwd_stat) != 0) {
		exit_with_msg(PMEMFILE_PRELOAD_EXIT_CWD_STAT_FAILED,
				"!fstat cwd");
	}

	assert(pool_count == 0);

	if (config == NULL || config[0] == '\0') {
		log_write("No mount point");
		return;
	}

	do {
		if ((size_t)pool_count >= ARRAY_SIZE(pools))
			config_error("invalid pmemfile config: too many pools");

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
		if (same_inode(&pool_desc->stat, &kernel_cwd_stat)) {
			open_new_pool(pool_desc);
			if (pool_desc->pool == NULL) {
				exit_with_msg(
					PMEMFILE_PRELOAD_EXIT_POOL_OPEN_FAILED,
					"!opening pmemfile_pool");
			}
			cwd_pool = pool_desc;
		}
	} while (config != NULL);
}

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
