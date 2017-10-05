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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <stdio.h>
#include <limits.h>
#include <linux/fs.h>
#include <utime.h>
#include <sys/fsuid.h>
#include <sys/capability.h>
#include <dlfcn.h>

#include <asm-generic/errno.h>

#include "compiler_utils.h"
#include "libsyscall_intercept_hook_point.h"
#include "libpmemfile-posix.h"
#include "sys_util.h"
#include "preload.h"
#include "syscall_early_filter.h"

#include "libpmemfile-posix-fd_first.h"

static long log_fd = -1;
static bool process_switching;

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

void
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

static pthread_mutex_t cwd_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct pool_description pools[0x100];
static int pool_count;

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
 * pool_acquire -- acquires access to pool
 */
void
pool_acquire(struct pool_description *pool)
{
	if (!process_switching)
		return;

	util_mutex_lock(&pool->process_switching_lock);
	pool->ref_cnt++;

	if (pool->ref_cnt == 1 && pool->suspended) {
		if (pmemfile_pool_resume(pool->pool, pool->poolfile_path))
			FATAL("could not restore pmemfile pool");
		pool->suspended = false;
	}

	util_mutex_unlock(&pool->process_switching_lock);
}

/*
 * pool_release -- releases access to pool
 */
void
pool_release(struct pool_description *pool)
{
	if (!process_switching)
		return;

	int oerrno = errno;

	util_mutex_lock(&pool->process_switching_lock);
	pool->ref_cnt--;

	if (pool->ref_cnt == 0 && !pool->suspended) {
		if (pmemfile_pool_suspend(pool->pool))
			FATAL("could not suspend pmemfile pool");
		pool->suspended = true;
	}

	util_mutex_unlock(&pool->process_switching_lock);

	errno = oerrno;
}

/*
 * A separate place to keep track of fds used to hold mount points open, in
 * the previous array. The application should not be aware of these, thus
 * whenever these file descriptors are encountered during interposing, -EBADF
 * must be returned. The contents of this array does not change after startup.
 */
static bool mount_point_fds[PMEMFILE_MAX_FD + 1];

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
hook_linkat(int fd0, long arg0, int fd1, long arg1, long flags)
{
	long ret;
	struct resolved_path where_old;
	struct resolved_path where_new;

	struct vfd_reference at0 = pmemfile_vfd_at_ref(fd0);
	struct vfd_reference at1 = pmemfile_vfd_at_ref(fd1);

	resolve_path(at0, (const char *)arg0, &where_old, RESOLVE_LAST_SLINK);
	resolve_path(at1, (const char *)arg1, &where_new,
			NO_RESOLVE_LAST_SLINK);

	if (where_old.error_code != 0) {
		ret = where_old.error_code;
	} else if (where_new.error_code != 0) {
		ret = where_new.error_code;
	} else if (where_new.at_pool != where_old.at_pool) {
		/* cross-pool link are not possible */
		ret = -EXDEV;
	} else if (where_new.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_linkat,
		    where_old.at_kernel, where_old.path,
		    where_new.at_kernel, where_new.path, flags);
	} else {
		pool_acquire(where_old.at_pool);

		int r = wrapper_pmemfile_linkat(
			    where_old.at_pool->pool,
			    where_old.at_dir, where_old.path,
			    where_new.at_dir, where_new.path,
				(int)flags);

		pool_release(where_old.at_pool);

		ret = check_errno(r, SYS_linkat);
	}

	pmemfile_vfd_unref(at0);
	pmemfile_vfd_unref(at1);

	return ret;
}

static long
hook_unlinkat(int fd, long path_arg, long flags)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, (const char *)path_arg,
	    &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		/* Not pmemfile resident path */
		ret = syscall_no_intercept(SYS_unlinkat,
				where.at_kernel, where.path, flags);
	} else {
		pool_acquire(where.at_pool);

		int r = wrapper_pmemfile_unlinkat(where.at_pool->pool,
			where.at_dir, where.path, (int)flags);

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_unlinkat);
	}

	pmemfile_vfd_unref(at);

	return ret;

}

static long
hook_chdir(const char *path)
{
	util_mutex_lock(&cwd_mutex);
	struct resolved_path where;

	long result;

	log_write("%s(\"%s\")", __func__, path);

	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);
	resolve_path(at, path, &where, RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		result = where.error_code;
	} else if (where.at_pool == NULL) {
		long fd = syscall_no_intercept(SYS_openat,
				where.at_kernel, where.path,
				O_DIRECTORY | O_PATH | O_NOCTTY);

		if (fd >= 0)
			result = pmemfile_vfd_chdir_kernel_fd((int)fd);
		else
			result = fd;
	} else {
		pool_acquire(where.at_pool);

		PMEMfile *file = pmemfile_openat(where.at_pool->pool,
					where.at_dir, where.path,
					O_DIRECTORY | O_PATH | O_NOCTTY);

		if (file == NULL) {
			result = -errno;
		} else {
			result = pmemfile_vfd_chdir_pf(where.at_pool, file);
			if (result != 0)
				pmemfile_close(where.at_pool->pool, file);
		}

		pool_release(where.at_pool);

		result = check_errno(result, SYS_chdir);
	}

	pmemfile_vfd_unref(at);

	util_mutex_unlock(&cwd_mutex);

	return result;
}

static long
hook_pool_getcwd(struct pool_description *pool, char *buf, size_t size)
{
	if (pool == NULL)
		return syscall_no_intercept(SYS_getcwd, buf, size);

	size_t mlen = strlen(pool->mount_point);
	if (mlen >= size)
		return -ERANGE;

	strcpy(buf, pool->mount_point);

	long ret = 0;

	pool_acquire(pool);

	if (pmemfile_getcwd(pool->pool, buf + mlen, size - mlen) == NULL)
		ret = check_errno(-errno, SYS_getcwd);

	pool_release(pool);

	return ret;
}

static long
hook_getcwd(char *buf, size_t size)
{
	util_mutex_lock(&cwd_mutex);
	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);

	long result = hook_pool_getcwd(at.pool, buf, size);

	pmemfile_vfd_unref(at);
	util_mutex_unlock(&cwd_mutex);

	return result;
}

static long
hook_newfstatat(int fd, long arg0, long arg1, long arg2)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, (const char *)arg0, &where,
	    (arg2 & AT_SYMLINK_NOFOLLOW)
	    ? NO_RESOLVE_LAST_SLINK : RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_newfstatat,
		    where.at_kernel, where.path, arg1, arg2);
	} else {
		pool_acquire(where.at_pool);

		int r = wrapper_pmemfile_fstatat(where.at_pool->pool,
			where.at_dir, where.path,
			(pmemfile_stat_t *)arg1, (int)arg2);

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_newfstatat);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_faccessat(int fd, long path_arg, long mode)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, (const char *)path_arg, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_faccessat,
		    where.at_kernel, where.path, mode);
	} else {
		pool_acquire(where.at_pool);

		int r = wrapper_pmemfile_faccessat(where.at_pool->pool,
			where.at_dir, where.path, (int)mode, 0);

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_faccessat);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_getxattr(long arg0, long arg1, long arg2, long arg3,
		int resolve_last)
{
	long result;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);

	resolve_path(at, (const char *)arg0, &where,
			resolve_last | NO_AT_PATH);

	if (where.error_code != 0)
		result = where.error_code;
	else if (where.at_pool != NULL)
		result = check_errno(-ENOTSUP, SYS_getxattr);
	else
		result = syscall_no_intercept(SYS_getxattr, where.path,
						arg1, arg2, arg3);

	pmemfile_vfd_unref(at);

	return result;
}

static long
hook_setxattr(long arg0, long arg1, long arg2, long arg3, long arg4,
		int resolve_last)
{
	long result;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);

	resolve_path(at, (const char *)arg0, &where,
			resolve_last | NO_AT_PATH);

	if (where.error_code != 0)
		result = where.error_code;
	else if (where.at_pool != NULL)
		result = check_errno(-ENOTSUP, SYS_setxattr);
	else
		result = syscall_no_intercept(SYS_setxattr, where.path,
						arg1, arg2, arg3, arg4);

	pmemfile_vfd_unref(at);

	return result;
}

static long
hook_mkdirat(int fd, long path_arg, long mode)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, (const char *)path_arg, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_mkdirat,
		    where.at_kernel, where.path, mode);
	} else {
		pool_acquire(where.at_pool);

		long r = wrapper_pmemfile_mkdirat(where.at_pool->pool,
			where.at_dir, where.path, (mode_t)mode);

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_mkdirat);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
openat_helper(struct resolved_path *where, long flags, long mode)
{
	int fd = pmemfile_acquire_new_fd(where->path);
	if (fd < 0)
		return fd;

	pool_acquire(where->at_pool);

	PMEMfile *file = pmemfile_openat(where->at_pool->pool,
					where->at_dir, where->path,
					((int)flags) & ~O_NONBLOCK,
					(mode_t)mode);

	log_write("pmemfile_openat(%p, %p, \"%s\", 0x%x, %lu) = %p",
					(void *)where->at_pool->pool,
					(void *)where->at_dir,
					where->path,
					((int)flags) & ~O_NONBLOCK,
					mode,
					file);
	pool_release(where->at_pool);

	if (file == NULL) {
		syscall_no_intercept(SYS_close, fd);
		return check_errno(-errno, SYS_openat);
	}

	int r = pmemfile_vfd_assign(fd, where->at_pool, file, where->path);

	if (r < 0) {
		pool_acquire(where->at_pool);
		pmemfile_close(where->at_pool->pool, file);
		pool_release(where->at_pool);
		syscall_no_intercept(SYS_close, fd);
	}

	return r;
}

static long
hook_openat(int fd_at, long arg0, long flags, long mode)
{
	long ret = 0;
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

	struct vfd_reference at = pmemfile_vfd_at_ref(fd_at);

	resolve_path(at, path_arg, &where, follow_last);

	if (where.error_code != 0) {
		/* path resolution failed */
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		/* Not pmemfile resident path */
		ret = syscall_no_intercept(SYS_openat,
		    where.at_kernel, where.path, flags, mode);
	} else {
		ret = openat_helper(&where, flags, mode);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_fcntl(struct vfd_reference *file, int cmd, long arg)
{
	assert(!file->pool->suspended);

	int r = 0;

	/*
	 * pmemfile_fcntl only pretends to take a lock. With process switching
	 * enabled this can lead to application data corruption, so to prevent
	 * that we catch it here and return an error. It's better to tell
	 * the application locking failed than mislead it into believing it
	 * succeeded and has exclusive access to the file.
	 */
	if (process_switching) {
		switch (cmd) {
			case F_SETLK:
			case F_SETLKW:
			case F_GETLK:
				r = -ENOTSUP;
				break;
		}
	}

	if (r == 0) {
		r = pmemfile_fcntl(file->pool->pool, file->file, cmd, arg);

		if (r < 0)
			r = -errno;
	}

	log_write("pmemfile_fcntl(%p, %p, 0x%x, 0x%lx) = %d",
		(void *)file->pool->pool, (void *)file->file, cmd, arg, r);

	return r;
}

static long
hook_renameat2(int fd_old, const char *path_old, int fd_new,
		const char *path_new, unsigned flags)
{
	long ret;
	struct resolved_path where_old;
	struct resolved_path where_new;

	struct vfd_reference at_old = pmemfile_vfd_at_ref(fd_old);
	struct vfd_reference at_new = pmemfile_vfd_at_ref(fd_new);

	resolve_path(at_old, path_old, &where_old, NO_RESOLVE_LAST_SLINK);
	resolve_path(at_new, path_new, &where_new, NO_RESOLVE_LAST_SLINK);

	if (where_old.error_code != 0) {
		ret = where_old.error_code;
	} else if (where_new.error_code != 0) {
		ret = where_new.error_code;
	} else if (where_new.at_pool != where_old.at_pool) {
		/* cross-pool renames are not supported */
		ret = -EXDEV;
	} else if (where_new.at_pool == NULL) {
		if (flags == 0) {
			ret = syscall_no_intercept(SYS_renameat,
			    where_old.at_kernel, where_old.path,
			    where_new.at_kernel, where_new.path);
		} else {
			ret = syscall_no_intercept(SYS_renameat2,
			    where_old.at_kernel, where_old.path,
			    where_new.at_kernel, where_new.path, flags);
		}
	} else {
		pool_acquire(where_old.at_pool);

		int r = wrapper_pmemfile_renameat2(
				where_old.at_pool->pool,
				where_old.at_dir, where_old.path,
				where_new.at_dir, where_new.path,
				flags);

		pool_release(where_old.at_pool);

		ret = check_errno(r, SYS_renameat2);
	}

	pmemfile_vfd_unref(at_old);
	pmemfile_vfd_unref(at_new);

	return ret;
}

static long
hook_truncate(const char *path, off_t length)
{
	long result;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);

	resolve_path(at, path, &where, RESOLVE_LAST_SLINK | NO_AT_PATH);

	if (where.error_code != 0) {
		result = where.error_code;
	} else if (where.at_pool == NULL) {
		result = syscall_no_intercept(SYS_truncate, where.path, length);
	} else {
		pool_acquire(where.at_pool);

		int r = wrapper_pmemfile_truncate(where.at_pool->pool,
						where.path, length);

		pool_release(where.at_pool);

		result = check_errno(r, SYS_truncate);
	}

	pmemfile_vfd_unref(at);

	return result;
}

static long
hook_symlinkat(const char *target, int fd, const char *linkpath)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, linkpath, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_symlinkat, target,
		    where.at_kernel, where.path);
	} else {
		pool_acquire(where.at_pool);

		int r = wrapper_pmemfile_symlinkat(where.at_pool->pool,
				target, where.at_dir, where.path);

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_symlinkat);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_fchmodat(int fd, const char *path, mode_t mode)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, path, &where, RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_fchmodat,
		    where.at_kernel, where.path, mode);
	} else {
		pool_acquire(where.at_pool);

		int r = wrapper_pmemfile_fchmodat(where.at_pool->pool,
				where.at_dir, where.path, mode, 0);

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_fchmodat);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_fchownat(int fd, const char *path,
				uid_t owner, gid_t group, int flags)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, path, &where,
	    (flags & AT_SYMLINK_NOFOLLOW)
	    ? NO_RESOLVE_LAST_SLINK : RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_fchownat,
		    where.at_kernel, where.path, owner, group, flags);
	} else {
		pool_acquire(where.at_pool);

		int r = wrapper_pmemfile_fchownat(where.at_pool->pool,
				where.at_dir, where.path, owner,
				group, flags);

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_fchownat);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
	long ret;

	struct vfd_reference in = pmemfile_vfd_at_ref(in_fd);
	struct vfd_reference out = pmemfile_vfd_at_ref(out_fd);

	if (in.pool != NULL || out.pool != NULL)
		ret = check_errno(-ENOTSUP, SYS_sendfile);
	else
		ret = syscall_no_intercept(SYS_sendfile,
						out_fd, in_fd, offset, count);

	pmemfile_vfd_unref(out);
	pmemfile_vfd_unref(in);

	return ret;
}

static long
hook_readlinkat(int fd, const char *path, char *buf, size_t bufsiz)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, path, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_readlinkat,
		    where.at_kernel, where.path, buf, bufsiz);
	} else {
		pool_acquire(where.at_pool);

		ssize_t r = wrapper_pmemfile_readlinkat(
				where.at_pool->pool,
				where.at_dir, where.path, buf,
				bufsiz);

		pool_release(where.at_pool);

		assert(r < INT_MAX);

		ret = check_errno(r, SYS_readlinkat);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
nosup_syscall_with_path(long syscall_number, const char *path, int resolve_last,
			long arg1, long arg2, long arg3, long arg4, long arg5)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);

	resolve_path(at, path, &where, resolve_last | NO_AT_PATH);

	if (where.error_code != 0)
		ret = where.error_code;
	else if (where.at_pool != NULL)
		ret = check_errno(-ENOTSUP, syscall_number);
	else
		ret = syscall_no_intercept(syscall_number, where.path,
				arg1, arg2, arg3, arg4, arg5);

	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_splice(int fd_in, loff_t *off_in, int fd_out,
			loff_t *off_out, size_t len, unsigned flags)
{
	/*
	 * XXX -- this is eerie similar to hook_sendfile and to
	 * hook_copy_file_range, perhaps these could merged.
	 */
	long ret;

	struct vfd_reference in = pmemfile_vfd_at_ref(fd_in);
	struct vfd_reference out = pmemfile_vfd_at_ref(fd_out);

	if (in.pool != NULL || out.pool != NULL)
		ret = check_errno(-ENOTSUP, SYS_splice);
	else
		ret = syscall_no_intercept(SYS_splice,
				fd_in, off_in, fd_out, off_out, len, flags);

	pmemfile_vfd_unref(out);
	pmemfile_vfd_unref(in);

	return ret;
}

static long
hook_futimesat(int fd, const char *path, const struct timeval times[2])
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, (const char *)path, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_futimesat,
				where.at_kernel, where.path, times);
	} else {
		pool_acquire(where.at_pool);

		int r = pmemfile_futimesat(where.at_pool->pool,
				where.at_dir, where.path, times);

		if (r != 0)
			r = -errno;

		if (times) {
			log_write(
			    "pmemfile_futimesat(%p, %p, \"%s\", [%ld,%ld,%ld,%ld]) = %d",
			    where.at_pool->pool,
			    where.at_dir, where.path,
			    times[0].tv_sec, times[0].tv_usec, times[1].tv_sec,
			    times[1].tv_usec, r);
		} else {
			log_write(
			    "pmemfile_futimesat(%p, %p, \"%s\", NULL) = %d",
			    where.at_pool->pool,
			    where.at_dir, where.path, r);
		}

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_futimesat);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
utimensat_helper(int sc, struct vfd_reference at, const char *path,
		const struct timespec times[2], int flags)
{
	struct resolved_path where;


	/*
	 * Handle non-pmem file descriptor with NULL path earlier. resolve_path
	 * does not handle empty paths in a way we want here.
	 */
	if (at.pool == NULL && path == NULL) {
		return syscall_no_intercept(SYS_utimensat, at.kernel_fd,
				NULL, times, flags);
	}

	int follow = (flags & AT_SYMLINK_NOFOLLOW) ?
			NO_RESOLVE_LAST_SLINK : RESOLVE_LAST_SLINK;
	resolve_path(at, path, &where, follow);

	if (where.error_code != 0)
		return where.error_code;

	if (where.at_pool == NULL)
		return syscall_no_intercept(SYS_utimensat,
				where.at_kernel, where.path, times, flags);

	if (path == NULL && flags & ~AT_SYMLINK_NOFOLLOW) {
		/*
		 * Currently the only defined flag for utimensat is
		 * AT_SYMLINK_NOFOLLOW. We have to detect any other flag
		 * set and return error just in case future kernel
		 * will accept some new flag.
		 */
		return -EINVAL;
	}

	int r;

	if (path == NULL) {
		/*
		 * Linux nonstandard syscall-level feature. Glibc
		 * behaves differently, but we have to emulate kernel
		 * behavior because futimens at glibc level is
		 * implemented using utimensat with NULL pathname.
		 * See "C library/ kernel ABI differences"
		 * section in man utimensat.
		 */
		r = pmemfile_futimens(where.at_pool->pool,
					where.at_dir, times);

		if (r != 0)
			r = -errno;

		if (times) {
			log_write(
			    "pmemfile_futimens(%p, %p, [%ld,%ld,%ld,%ld]) = %d",
			    where.at_pool->pool, where.at_dir,
			    times[0].tv_sec, times[0].tv_nsec,
			    times[1].tv_sec, times[1].tv_nsec, r);
		} else {
			log_write("pmemfile_futimens(%p, %p, NULL) = %d",
			    where.at_pool->pool, where.at_dir, r);

		}
	} else {
		pool_acquire(where.at_pool);

		r = pmemfile_utimensat(where.at_pool->pool,
		    where.at_dir, where.path, times,
		    flags);

		if (r != 0)
			r = -errno;

		if (times) {
			log_write(
			    "pmemfile_utimensat(%p, %p, \"%s\", [%ld,%ld,%ld,%ld], %d) = %d",
			    where.at_pool->pool, where.at_dir, where.path,
			    times[0].tv_sec, times[0].tv_nsec,
			    times[1].tv_sec, times[1].tv_nsec, flags, r);
		} else {
			log_write(
			    "pmemfile_utimensat(%p, %p, \"%s\", NULL, %d) = %d",
			    where.at_pool->pool, where.at_dir,
			    where.path, flags, r);
		}

		pool_release(where.at_pool);
	}

	return check_errno(r, sc);
}

static long
hook_utime(const char *path, const struct utimbuf *times)
{
	struct timespec timespec[2];
	struct timespec *timespec_arg = NULL;

	if (path == NULL)
		return -EFAULT;

	if (times) {
		timespec[0].tv_sec = times->actime;
		timespec[0].tv_nsec = 0;
		timespec[1].tv_sec = times->modtime;
		timespec[1].tv_nsec = 0;
		timespec_arg = timespec;
	}

	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);
	long ret = utimensat_helper(SYS_utime, at, path, timespec_arg, 0);
	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_utimes(const char *path, const struct timeval times[2])
{
	struct timespec timespec[2];
	struct timespec *timespec_arg = NULL;

	if (path == NULL)
		return -EFAULT;

	if (times) {
		timespec[0].tv_sec = times[0].tv_sec;
		timespec[0].tv_nsec = times[0].tv_usec * 1000;
		timespec[1].tv_sec = times[1].tv_sec;
		timespec[1].tv_nsec = times[1].tv_usec * 1000;
		timespec_arg = timespec;
	}

	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);
	long ret = utimensat_helper(SYS_utimes, at, path, timespec_arg, 0);
	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_utimensat(int fd, const char *path,
		const struct timespec times[2], int flags)
{
	struct vfd_reference at = pmemfile_vfd_at_ref(fd);
	long ret = utimensat_helper(SYS_utimensat, at, path, times, flags);
	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_name_to_handle_at(int fd, const char *path,
		struct file_handle *handle, int *mount_id, int flags)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, path, &where,
	    (flags & AT_SYMLINK_FOLLOW)
	    ? RESOLVE_LAST_SLINK : NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0)
		ret = where.error_code;
	else if (where.at_pool == NULL)
		ret = syscall_no_intercept(SYS_name_to_handle_at,
		    where.at_kernel, where.path, mount_id, flags);
	else
		ret = check_errno(-ENOTSUP, SYS_name_to_handle_at);

	pmemfile_vfd_unref(at);

	return ret;
}

struct execvat_desc {
	char **new_envp;
	char *pmemfile_cd;
	char *const *argv;
	char *const *envp;
	char *cwd;
	int flags;
};

static long
hook_execveat_vfdref(struct pool_description *cwd_pool,
			struct execvat_desc *desc)
{
	static const char prefix[] = "PMEMFILE_CD=";

	unsigned env_idx = 0;

	unsigned envs = 0;
	while (desc->envp[envs] != NULL)
		envs++;

	desc->new_envp = malloc((envs + 2) * sizeof(char *));
	if (!desc->new_envp)
		return -errno;

	/* Copy all environment variables, but skip PMEMFILE_CD. */
	for (unsigned i = 0; i < envs; ++i) {
		if (strncmp(desc->envp[i], prefix, strlen(prefix)) == 0)
			continue;
		desc->new_envp[env_idx++] = desc->envp[i];
	}

	pool_acquire(cwd_pool);
	desc->cwd = pmemfile_getcwd(cwd_pool->pool, NULL, 0);
	pool_release(cwd_pool);

	if (!desc->cwd)
		return -errno;

	if (asprintf(&desc->pmemfile_cd, "%s%s/%s",
			prefix, cwd_pool->mount_point, desc->cwd) == -1)
		return -errno;

	desc->new_envp[env_idx++] = desc->pmemfile_cd;
	desc->new_envp[env_idx++] = NULL;
	desc->envp = desc->new_envp;

	return 0;
}

static long
hook_execveat(int fd, const char *path, char *const argv[],
		char *const envp[], int flags)
{
	long ret = 0;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);
	struct resolved_path where;

	resolve_path(at, path, &where,
	    (flags & AT_SYMLINK_NOFOLLOW)
	    ? NO_RESOLVE_LAST_SLINK : RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool != NULL) {
		/* The expectation is that pmemfile will never support this. */
		ret = check_errno(-ENOTSUP, SYS_execveat);
	} else {
		struct execvat_desc desc = {
		    .argv = argv, .envp = envp, .flags = flags, };

		util_mutex_lock(&cwd_mutex);
		struct vfd_reference cwd = pmemfile_vfd_at_ref(AT_FDCWD);

		if (process_switching && cwd.pool != NULL)
			ret = hook_execveat_vfdref(cwd.pool, &desc);

		pmemfile_vfd_unref(cwd);
		util_mutex_unlock(&cwd_mutex);

		if (ret == 0)
			ret = syscall_no_intercept(SYS_execveat,
				where.at_kernel, where.path,
				desc.argv, desc.envp, desc.flags);

		free(desc.pmemfile_cd);
		free(desc.new_envp);
		free(desc.cwd);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_copy_file_range(int fd_in, loff_t *off_in, int fd_out,
			loff_t *off_out, size_t len, unsigned flags)
{
	long ret;

	struct vfd_reference in = pmemfile_vfd_at_ref(fd_in);
	struct vfd_reference out = pmemfile_vfd_at_ref(fd_out);

	if (in.pool != NULL || out.pool != NULL)
		ret = check_errno(-ENOTSUP, SYS_copy_file_range);
	else
		ret = syscall_no_intercept(SYS_copy_file_range,
				fd_in, off_in, fd_out, off_out, len, flags);

	pmemfile_vfd_unref(out);
	pmemfile_vfd_unref(in);

	return ret;
}

static long
hook_mmap(long arg0, long arg1, long arg2,
		long arg3, int fd, long arg5)
{
	long ret;

	struct vfd_reference file = pmemfile_vfd_ref(fd);

	if (file.pool != NULL)
		ret = check_errno(-ENOTSUP, SYS_mmap);
	else
		ret = syscall_no_intercept(SYS_mmap,
			arg0, arg1, arg2, arg3, file.kernel_fd, arg5);

	pmemfile_vfd_unref(file);

	return ret;
}

static long
hook_mknodat(int fd, const char *path, mode_t mode, dev_t dev)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(fd);

	resolve_path(at, path, &where, NO_RESOLVE_LAST_SLINK);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_mknodat,
		    where.at_kernel, where.path, mode, dev);
	} else {
		pool_acquire(where.at_pool);

		long r = wrapper_pmemfile_mknodat(where.at_pool->pool,
		    where.at_dir, where.path, (mode_t) mode,
		    (dev_t) dev);

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_mknodat);
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static void
update_capabilities(PMEMfilepool *pfp)
{
	cap_t caps = cap_get_proc();
	if (!caps)
		FATAL("!cap_get_proc");

	cap_flag_value_t cap_value;
	if (cap_get_flag(caps, CAP_CHOWN, CAP_EFFECTIVE, &cap_value))
		FATAL("!cap_get_flag failed");
	if (cap_value)
		pmemfile_setcap(pfp, PMEMFILE_CAP_CHOWN);
	else
		pmemfile_clrcap(pfp, PMEMFILE_CAP_CHOWN);

	if (cap_get_flag(caps, CAP_FOWNER, CAP_EFFECTIVE, &cap_value))
		FATAL("!cap_get_flag failed");
	if (cap_value)
		pmemfile_setcap(pfp, PMEMFILE_CAP_FOWNER);
	else
		pmemfile_clrcap(pfp, PMEMFILE_CAP_FOWNER);

	if (cap_get_flag(caps, CAP_FSETID, CAP_EFFECTIVE, &cap_value))
		FATAL("!cap_get_flag failed");
	if (cap_value)
		pmemfile_setcap(pfp, PMEMFILE_CAP_FSETID);
	else
		pmemfile_clrcap(pfp, PMEMFILE_CAP_FSETID);

	cap_free(caps);
}

static long
hook_setfsuid(uid_t fsuid)
{
	long old = syscall_no_intercept(SYS_setfsuid, fsuid);

	/*
	 * There's no way to determine if setfsuid succeeded just by looking at
	 * its return value. We have to invoke it again with an invalid argument
	 * and verify that previous fsuid matches what we passed initially.
	 */
	if (syscall_no_intercept(SYS_setfsuid, -1) != fsuid)
		return old;

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_setfsuid(p->pool, fsuid) != old)
			FATAL("inconsistent fsuid state");
		update_capabilities(p->pool);
	}

	return old;
}

static long
hook_setfsgid(gid_t fsgid)
{
	long old = syscall_no_intercept(SYS_setfsgid, fsgid);

	/* See hook_setfsuid. */
	if (syscall_no_intercept(SYS_setfsgid, -1) != fsgid)
		return old;

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_setfsgid(p->pool, fsgid) != old)
			FATAL("inconsistent fsgid state");
		update_capabilities(p->pool);
	}

	return old;
}

static long
hook_setgid(gid_t gid)
{
	long ret = syscall_no_intercept(SYS_setgid, gid);
	if (ret)
		return ret;

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_setgid(p->pool, gid))
			FATAL("inconsistent gid state");
		update_capabilities(p->pool);
	}

	return 0;
}

static long
hook_setgroups(size_t size, const gid_t *list)
{
	long ret = syscall_no_intercept(SYS_setgroups, size, list);
	if (ret)
		return ret;

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_setgroups(p->pool, size, list))
			FATAL("inconsistent groups state");
		update_capabilities(p->pool);
	}

	return 0;
}

static long
hook_setregid(gid_t rgid, gid_t egid)
{
	long ret = syscall_no_intercept(SYS_setregid, rgid, egid);
	if (ret)
		return ret;

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_setregid(p->pool, rgid, egid))
			FATAL("inconsistent regid state");
		update_capabilities(p->pool);
	}

	return 0;
}

static long
hook_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	long ret = syscall_no_intercept(SYS_setresgid, rgid, egid, sgid);
	if (ret)
		return ret;

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_setregid(p->pool, rgid, egid))
			FATAL("inconsistent resgid state");
		update_capabilities(p->pool);
	}

	return 0;

}

static long
hook_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	long ret = syscall_no_intercept(SYS_setresuid, ruid, euid, suid);
	if (ret)
		return ret;

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_setreuid(p->pool, ruid, euid))
			FATAL("inconsistent resuid state");
		update_capabilities(p->pool);
	}

	return 0;
}

static long
hook_setreuid(uid_t ruid, uid_t euid)
{
	long ret = syscall_no_intercept(SYS_setreuid, ruid, euid);
	if (ret)
		return ret;

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_setreuid(p->pool, ruid, euid))
			FATAL("inconsistent reuid state");
		update_capabilities(p->pool);
	}

	return 0;
}

static long
hook_setuid(uid_t uid)
{
	long ret = syscall_no_intercept(SYS_setuid, uid);
	if (ret)
		return ret;

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_setuid(p->pool, uid))
			FATAL("inconsistent uid state");
		update_capabilities(p->pool);
	}

	return 0;
}

static long
hook_umask(mode_t mask)
{
	long ret = syscall_no_intercept(SYS_umask, mask);

	for (int i = 0; i < pool_count; ++i) {
		struct pool_description *p = pools + i;
		if (!p->pool)
			continue;
		if (pmemfile_umask(p->pool, mask) != ret)
			FATAL("inconsistent umask state");
	}

	return ret;
}

static long
hook_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	if (addr->sa_family != AF_UNIX ||
			addrlen < sizeof(struct sockaddr_un)) {
		return syscall_no_intercept(SYS_bind, sockfd, addr, addrlen);
	}

	const struct sockaddr_un *uaddr = (struct sockaddr_un *)addr;

	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);

	resolve_path(at, uaddr->sun_path, &where,
			NO_RESOLVE_LAST_SLINK | NO_AT_PATH);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool != NULL) {
		ret = check_errno(-ENOTSUP, SYS_bind);
	} else {
		struct sockaddr_un tmp_uaddr;

		tmp_uaddr.sun_family = AF_UNIX;

		size_t len = strlen(where.path);

		if (len >= sizeof(tmp_uaddr.sun_path)) {
			ret = -ENAMETOOLONG;
		} else {
			strncpy(tmp_uaddr.sun_path, where.path, len);
			tmp_uaddr.sun_path[len] = 0;

			ret = syscall_no_intercept(SYS_bind, sockfd, &tmp_uaddr,
					sizeof(tmp_uaddr));
		}
	}

	pmemfile_vfd_unref(at);

	return ret;
}

static long
hook_statfs(const char *path, struct statfs *buf)
{
	long ret;
	struct resolved_path where;

	struct vfd_reference at = pmemfile_vfd_at_ref(AT_FDCWD);

	resolve_path(at, path, &where, RESOLVE_LAST_SLINK | NO_AT_PATH);

	if (where.error_code != 0) {
		ret = where.error_code;
	} else if (where.at_pool == NULL) {
		ret = syscall_no_intercept(SYS_statfs, where.path, buf);
	} else {
		pool_acquire(where.at_pool);

		int r = wrapper_pmemfile_statfs(where.at_pool->pool, buf);

		pool_release(where.at_pool);

		ret = check_errno(r, SYS_statfs);
	}

	pmemfile_vfd_unref(at);

	return ret;
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
		return hook_openat(AT_FDCWD, arg0, arg1, arg2);

	case SYS_creat:
		return hook_openat(AT_FDCWD, arg0,
			O_WRONLY | O_CREAT | O_TRUNC, arg1);

	case SYS_openat:
		return hook_openat((int)arg0, arg1, arg2, arg3);

	case SYS_rename:
		return hook_renameat2(AT_FDCWD, (const char *)arg0,
			AT_FDCWD, (const char *)arg1, 0);

	case SYS_renameat:
		return hook_renameat2((int)arg0, (const char *)arg1, (int)arg2,
			(const char *)arg3, 0);

	case SYS_renameat2:
		return hook_renameat2((int)arg0, (const char *)arg1, (int)arg2,
			(const char *)arg3, (unsigned)arg4);

	case SYS_link:
		/* Use pmemfile_linkat to implement link */
		return hook_linkat(AT_FDCWD, arg0, AT_FDCWD, arg1, 0);

	case SYS_linkat:
		return hook_linkat((int)arg0, arg1, (int)arg2, arg3, arg4);

	case SYS_unlink:
		/* Use pmemfile_unlinkat to implement unlink */
		return hook_unlinkat(AT_FDCWD, arg0, 0);

	case SYS_unlinkat:
		return hook_unlinkat((int)arg0, arg1, arg2);

	case SYS_rmdir:
		/* Use pmemfile_unlinkat to implement rmdir */
		return hook_unlinkat(AT_FDCWD, arg0, AT_REMOVEDIR);

	case SYS_mkdir:
		/* Use pmemfile_mkdirat to implement mkdir */
		return hook_mkdirat(AT_FDCWD, arg0, arg1);

	case SYS_mkdirat:
		return hook_mkdirat((int)arg0, arg1, arg2);

	case SYS_access:
		/* Use pmemfile_faccessat to implement access */
		return hook_faccessat(AT_FDCWD, arg0, arg1);

	case SYS_faccessat:
		return hook_faccessat((int)arg0, arg1, arg2);

	/*
	 * The newfstatat syscall implements both stat and lstat.
	 * Linux calls it: newfstatat ( I guess there was an old one )
	 * POSIX / libc interfaces call it: fstatat
	 * pmemfile calls it: pmemfile_fstatat
	 *
	 * fstat is unique.
	 */
	case SYS_stat:
		return hook_newfstatat(AT_FDCWD, arg0, arg1, 0);

	case SYS_lstat:
		return hook_newfstatat(AT_FDCWD, arg0, arg1,
		    AT_SYMLINK_NOFOLLOW);

	case SYS_newfstatat:
		return hook_newfstatat((int)arg0, arg1, arg2, arg3);

	case SYS_close:
		return pmemfile_vfd_close((int)arg0);

	case SYS_mmap:
		return hook_mmap(arg0, arg1, arg2, arg3, (int)arg4, arg5);

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

	case SYS_truncate:
		return hook_truncate((const char *)arg0, arg1);

	case SYS_symlink:
		return hook_symlinkat((const char *)arg0,
			AT_FDCWD, (const char *)arg1);

	case SYS_symlinkat:
		return hook_symlinkat((const char *)arg0, (int)arg1,
			(const char *)arg2);

	case SYS_chmod:
		return hook_fchmodat(AT_FDCWD, (const char *)arg0,
			(mode_t)arg1);

	case SYS_fchmodat:
		return hook_fchmodat((int)arg0, (const char *)arg1,
			(mode_t)arg2);

	case SYS_chown:
		return hook_fchownat(AT_FDCWD, (const char *)arg0,
			(uid_t)arg1, (gid_t)arg2, 0);

	case SYS_lchown:
		return hook_fchownat(AT_FDCWD, (const char *)arg0,
			(uid_t)arg1, (gid_t)arg2, AT_SYMLINK_NOFOLLOW);

	case SYS_fchownat:
		return hook_fchownat((int)arg0, (const char *)arg1,
			(uid_t)arg2, (gid_t)arg3, (int)arg4);

	case SYS_sendfile:
		return hook_sendfile((int)arg0, (int)arg1,
					(off_t *)arg2, (size_t)arg3);

	case SYS_mknod:
		return hook_mknodat(AT_FDCWD, (const char *)arg0,
				(mode_t)arg1, (dev_t)arg2);

	case SYS_mknodat:
		return hook_mknodat((int)arg0, (const char *)arg1,
				(mode_t)arg2, (dev_t)arg3);

	case SYS_setfsuid:
		return hook_setfsuid((uid_t)arg0);

	case SYS_setfsgid:
		return hook_setfsgid((gid_t)arg0);

	case SYS_setgid:
		return hook_setgid((gid_t)arg0);

	case SYS_setgroups:
		return hook_setgroups((size_t)arg0, (const gid_t *)arg1);

	case SYS_setregid:
		return hook_setregid((gid_t)arg0, (gid_t)arg1);

	case SYS_setresgid:
		return hook_setresgid((gid_t)arg0, (gid_t)arg1, (gid_t)arg2);

	case SYS_setresuid:
		return hook_setresuid((uid_t)arg0, (uid_t)arg1, (uid_t)arg2);

	case SYS_setreuid:
		return hook_setreuid((uid_t)arg0, (uid_t)arg1);

	case SYS_setuid:
		return hook_setuid((uid_t)arg0);

	case SYS_umask:
		return hook_umask((mode_t)arg0);

	/*
	 * Some syscalls that have a path argument, but are not (yet) handled
	 * by libpmemfile-posix. The argument of these are not interpreted,
	 * except for the path itself. If the path points to something pmemfile
	 * resident, -ENOTSUP is returned, otherwise, the call is forwarded
	 * to the kernel.
	 */
	case SYS_chroot:
	case SYS_listxattr:
	case SYS_removexattr:
		return nosup_syscall_with_path(syscall_number,
		    (const char *)arg0, RESOLVE_LAST_SLINK,
		    arg1, arg2, arg3, arg4, arg5);

	case SYS_llistxattr:
	case SYS_lremovexattr:
		return nosup_syscall_with_path(syscall_number,
		    (const char *)arg0, NO_RESOLVE_LAST_SLINK,
		    arg1, arg2, arg3, arg4, arg5);

	case SYS_readlink:
		return hook_readlinkat(AT_FDCWD, (const char *)arg0,
		    (char *)arg1, (size_t)arg2);

	case SYS_readlinkat:
		return hook_readlinkat((int)arg0, (const char *)arg1,
		    (char *)arg2, (size_t)arg3);

	case SYS_splice:
		return hook_splice((int)arg0, (loff_t *)arg1, (int)arg2,
			(loff_t *)arg3, (size_t)arg4, (unsigned)arg5);

	case SYS_futimesat:
		return hook_futimesat((int)arg0, (const char *)arg1,
			(const struct timeval *)arg2);

	case SYS_utime:
		return hook_utime((const char *)arg0,
				(const struct utimbuf *)arg1);

	case SYS_utimes:
		return hook_utimes((const char *)arg0,
				(const struct timeval *)arg1);

	case SYS_utimensat:
		return hook_utimensat((int)arg0, (const char *)arg1,
				(const struct timespec *)arg2, (int)arg3);

	case SYS_name_to_handle_at:
		return hook_name_to_handle_at((int)arg0, (const char *)arg1,
			(struct file_handle *)arg2, (int *)arg3, (int)arg4);

	case SYS_execve:
		return hook_execveat(AT_FDCWD, (const char *)arg0,
		    (char *const *)arg1, (char *const *)arg2, 0);

	case SYS_execveat:
		return hook_execveat((int)arg0, (const char *)arg1,
			(char *const *)arg2, (char *const *)arg3, (int)arg4);

	case SYS_copy_file_range:
		return hook_copy_file_range((int)arg0, (loff_t *)arg1,
		    (int)arg2, (loff_t *)arg3, (size_t)arg4, (unsigned)arg5);

	case SYS_bind:
		return hook_bind((int)arg0, (const struct sockaddr *)arg1,
				(socklen_t)arg2);

	case SYS_dup:
		return pmemfile_vfd_dup((int)arg0);

	case SYS_dup2:
		return pmemfile_vfd_dup2((int)arg0, (int)arg1);

	case SYS_dup3:
		return pmemfile_vfd_dup3((int)arg0, (int)arg1, (int)arg2);

	case SYS_statfs:
		return hook_statfs((const char *)arg0, (struct statfs *)arg1);

	default:
		/* Did we miss something? */
		assert(false);
		return syscall_no_intercept(syscall_number,
		    arg0, arg1, arg2, arg3, arg4, arg5);
	}
}

static long
dispatch_syscall_fd_first(long syscall_number,
			struct vfd_reference *arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5)
{
	switch (syscall_number) {

	case SYS_write:
		return fd_first_pmemfile_write(arg0, arg1, arg2);

	case SYS_writev:
		return fd_first_pmemfile_writev(arg0, arg1, arg2);

	case SYS_read:
		return fd_first_pmemfile_read(arg0, arg1, arg2);

	case SYS_readv:
		return fd_first_pmemfile_readv(arg0, arg1, arg2);

	case SYS_lseek:
		return fd_first_pmemfile_lseek(arg0, arg1, arg2);

	case SYS_pread64:
		return fd_first_pmemfile_pread(arg0, arg1, arg2, arg3);

	case SYS_pwrite64:
		return fd_first_pmemfile_pwrite(arg0, arg1, arg2, arg3);

	case SYS_preadv2:
		if (arg4 & ~(RWF_DSYNC | RWF_HIPRI | RWF_SYNC))
			return -EINVAL;
		/* fallthrough */
	case SYS_preadv:
		return fd_first_pmemfile_preadv(arg0, arg1, arg2, arg3);

	case SYS_pwritev2:
		if (arg4 & ~(RWF_DSYNC | RWF_HIPRI | RWF_SYNC))
			return -EINVAL;
		/* fallthrough */
	case SYS_pwritev:
		return fd_first_pmemfile_pwritev(arg0, arg1, arg2, arg3);

	case SYS_getdents:
		return fd_first_pmemfile_getdents(arg0, arg1, arg2);

	case SYS_getdents64:
		return fd_first_pmemfile_getdents64(arg0, arg1, arg2);

	case SYS_fcntl:
		return hook_fcntl(arg0, (int)arg1, arg2);

	case SYS_flock:
		return fd_first_pmemfile_flock(arg0, arg1);

	case SYS_ftruncate:
		return fd_first_pmemfile_ftruncate(arg0, arg1);

	case SYS_fchmod:
		return fd_first_pmemfile_fchmod(arg0, arg1);

	case SYS_fchown:
		return fd_first_pmemfile_fchown(arg0, arg1, arg2);

	case SYS_fallocate:
		return fd_first_pmemfile_fallocate(arg0, arg1, arg2, arg3);

	case SYS_fstat:
		return fd_first_pmemfile_fstat(arg0, arg1);

	case SYS_fstatfs:
		return wrapper_pmemfile_statfs(arg0->pool->pool,
				(pmemfile_statfs_t *)arg1);

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

	do {
		pfp = pmemfile_pool_open(p->poolfile_path);
	} while (pfp == NULL && process_switching && errno == EAGAIN);

	if (pfp == NULL)
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

	update_capabilities(pfp);

	pmemfile_pool_set_device(pfp, p->stat.st_dev);

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
 * Return values expected by libsyscall_intercept:
 * A non-zero return value if it should execute the syscall,
 * zero return value if it should not execute the syscall, and
 * use *result value as the syscall's result.
 */
#define NOT_HOOKED 1
#define HOOKED 0

static int
hook(const struct syscall_early_filter_entry *filter_entry, long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5,
			long *syscall_return_value)
{
	if (syscall_number == SYS_chdir) {
		*syscall_return_value = hook_chdir((const char *)arg0);
		return HOOKED;
	}

	if (syscall_number == SYS_fchdir) {
		util_mutex_unlock(&cwd_mutex);
		*syscall_return_value = pmemfile_vfd_fchdir((int)arg0);
		util_mutex_unlock(&cwd_mutex);
		return HOOKED;
	}

	if (syscall_number == SYS_getcwd) {
		*syscall_return_value = hook_getcwd((char *)arg0, (size_t)arg1);
		return HOOKED;
	}

	if (syscall_number == SYS_fcntl &&
	    ((int)arg1 == F_DUPFD || (int)arg1 == F_DUPFD_CLOEXEC)) {
		/*
		 * Other fcntl commands on pmemfile resident files are handled
		 * via dispatch_syscall_fd_first.
		 *
		 * XXX: close-on-exec flag is not handled correctly yet.
		 */
		*syscall_return_value =
		    pmemfile_vfd_fcntl_dup((int)arg0, (int)arg2);
		return HOOKED;
	}

	int is_hooked;

	is_hooked = HOOKED;

	if (filter_entry->fd_first_arg) {
		struct vfd_reference file = pmemfile_vfd_ref((int)arg0);

		if (file.pool == NULL) {
			is_hooked = NOT_HOOKED;
		} else if (filter_entry->returns_zero) {
			*syscall_return_value = 0;
		} else if (filter_entry->returns_ENOTSUP) {
			*syscall_return_value =
			    check_errno(-ENOTSUP, syscall_number);
		} else {
			pool_acquire(file.pool);

			*syscall_return_value =
			    dispatch_syscall_fd_first(syscall_number,
			    &file, arg1, arg2, arg3, arg4, arg5);

			*syscall_return_value =
			    check_errno(*syscall_return_value, syscall_number);

			pool_release(file.pool);
		}

		pmemfile_vfd_unref(file);
	} else {
		*syscall_return_value = dispatch_syscall(syscall_number,
			arg0, arg1, arg2, arg3, arg4, arg5);
	}

	return is_hooked;
}

static __thread bool guard_flag;

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
	if (guard_flag)
		return NOT_HOOKED;

	assert(pool_count > 0);

	struct syscall_early_filter_entry filter_entry;
	filter_entry = get_early_filter_entry(syscall_number);

	if (!filter_entry.must_handle)
		return NOT_HOOKED;

	int is_hooked;

	guard_flag = true;
	int oerrno = errno;
	is_hooked = hook(&filter_entry, syscall_number, arg0, arg1, arg2, arg3,
			arg4, arg5, syscall_return_value);
	errno = oerrno;
	guard_flag = false;

	return is_hooked;
}

static void
init_hooking(void)
{
	/*
	 * Install the callback to be called by the syscall intercepting library
	 */
	intercept_hook_point = &hook_reentrance_guard_wrapper;
}

static void
config_error(const char *msg)
{
	exit_with_msg(PMEMFILE_PRELOAD_EXIT_CONFIG_ERROR, msg);
}

static void
set_mount_point(struct pool_description *pool, const char *path, size_t len)
{
	memcpy(pool->mount_point, path, len);
	pool->mount_point[len] = '\0';

	memcpy(pool->mount_point_parent, path, len);
	pool->len_mount_point_parent = len;

	while (pool->len_mount_point_parent > 1 &&
	    pool->mount_point_parent[pool->len_mount_point_parent] != '/')
		pool->len_mount_point_parent--;

	pool->mount_point_parent[pool->len_mount_point_parent] = '\0';

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

	set_mount_point(pool, conf, (size_t)(colon - conf));

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

static void
stat_cwd(struct stat *kernel_cwd_stat)
{
	char cwd[0x400];

	/*
	 * The establish_mount_points routine must know about the CWD, to be
	 * aware of the case when the mount point is the same as the CWD.
	 */
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		exit_with_msg(PMEMFILE_PRELOAD_EXIT_GETCWD_FAILED, "!getcwd");

	if (stat(cwd, kernel_cwd_stat) != 0) {
		exit_with_msg(PMEMFILE_PRELOAD_EXIT_CWD_STAT_FAILED,
				"!fstat cwd");
	}
}

/*
 * open_pool_at_startup - open a pool at startup, if cwd points to it
 *
 * If the current working directory is a mount point, then
 * the corresponding pmemfile pool must opened at startup.
 * Normally, a pool is only opened the first time it is
 * accessed, but without doing this, the first access would
 * never be noticed.
 */
static void
open_pool_at_startup(struct pool_description *pool_desc)
{
	open_new_pool(pool_desc);

	if (pool_desc->pool == NULL) {
		exit_with_msg(PMEMFILE_PRELOAD_EXIT_POOL_OPEN_FAILED,
				"!opening pmemfile_pool");
	}

	PMEMfile *file = pmemfile_open(pool_desc->pool, ".",
				O_DIRECTORY | O_PATH | O_NOCTTY);

	if (file == NULL)
		exit_with_msg(PMEMFILE_PRELOAD_EXIT_POOL_OPEN_FAILED,
				"!opening cwd pmemfile_pool");

	if (pmemfile_vfd_chdir_pf(pool_desc, file) != 0)
		exit_with_msg(PMEMFILE_PRELOAD_EXIT_POOL_OPEN_FAILED,
				"!chdir into pmemfile_pool");

	if (process_switching) {
		/*
		 * Give up access to the pool. We have to acquire it
		 * first because ref_cnt == 0 and suspended == false.
		 */
		pool_acquire(pool_desc);
		pool_release(pool_desc);
	}
}

static void
init_pool(struct pool_description *pool_desc, struct stat *kernel_cwd_stat)
{
	/* fetch pool_desc-fd, pool_desc->stat */
	open_mount_point(pool_desc);

	pool_desc->pool = NULL;

	util_mutex_init(&pool_desc->pool_open_lock);
	util_mutex_init(&pool_desc->process_switching_lock);

	++pool_count;

	/*
	 * If the current working directory is a mount point, then
	 * the corresponding pmemfile pool must opened at startup.
	 * Normally, a pool is only opened the first time it is
	 * accessed, but without doing this, the first access would
	 * never be noticed.
	 */
	if (same_inode(&pool_desc->stat, kernel_cwd_stat))
		open_pool_at_startup(pool_desc);
}

static void
detect_mount_points(struct stat *kernel_cwd_stat)
{
	FILE *file = fopen("/proc/self/mountinfo", "r");

	if (!file)
		return;

	unsigned mount_id, parent_id, major, minor;
	char root[PATH_MAX];
	char mount_point[PATH_MAX];
	char mount_options[4096];
	char f[9][4096];
	int matched = 0;
	size_t len = PATH_MAX;
	char *line = malloc(len);
	if (!line) {
		fclose(file);
		return;
	}

	while (getline(&line, &len, file) > 0) {
		matched = sscanf(line,
			"%u %u %u:%u %s %s %s %[^\n ] %[^\n ] %[^\n ] %[^\n ] %[^\n ] %[^\n ] %[^\n ] %[^\n ] %[^\n ]",
			&mount_id, &parent_id, &major, &minor, root,
			mount_point, mount_options, f[0], f[1], f[2], f[3],
			f[4], f[5], f[6], f[7], f[8]);

		if (matched <= 0)
			break;

		int i;
		for (i = 7; i < matched; ++i) {
			if (strcmp(f[i - 7], "-") == 0) {
				i++;
				break;
			}
		}
		if (i == matched)
			continue;
		const char *fstype = f[i - 7];
		const char *mount_source = f[i - 7 + 1];
		static const char prefix[] = "pmemfile:";

		if (strcmp(fstype, "tmpfs") != 0)
			continue;
		if (strncmp(mount_source, prefix, strlen(prefix)) != 0)
			continue;

		log_write(
			"matched:%d mount_id:%u parent_id:%u major:%u minor:%u root:%s mount_point:%s mount_options:%s",
			matched, mount_id, parent_id, major, minor, root,
			mount_point, mount_options);
		for (int i = 7; i < matched; ++i)
			log_write("f[%d]:%s", i - 7, f[i - 7]);
		log_write("EOR");

		size_t ret = strlen(mount_source + strlen(prefix));
		strcpy(line, mount_source + strlen(prefix));

		log_write("Using pool from '%s' to mount at '%s'.", line,
				mount_point);

		struct pool_description *pool = pools + pool_count;

		set_mount_point(pool, mount_point, strlen(mount_point));

		memcpy(pool->poolfile_path, line, (size_t)ret);
		pool->poolfile_path[ret] = 0;

		init_pool(pool, kernel_cwd_stat);
	}

	free(line);

	fclose(file);
}

/*
 * establish_mount_points - parse the configuration, which is expected to be a
 * semicolon separated list of path-pairs:
 * mount_point_path:pool_file_path
 * Mount point path is where the application is meant to observe a pmemfile
 * pool mounted -- this should be an actual directory accessible by the
 * application. The pool file path should point to the path of the actual
 * pmemfile pool.
 */
static void
establish_mount_points(const char *config, struct stat *kernel_cwd_stat)
{
	if (config == NULL || config[0] == '\0') {
		log_write("No mount information in PMEMFILE_POOLS.");
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

		init_pool(pool_desc, kernel_cwd_stat);
	} while (config != NULL);
}

static int (*libc__xpg_strerror_r)(int __errnum, char *__buf, size_t __buflen);
static char *(*libc_strerror_r)(int __errnum, char *__buf, size_t __buflen);
static char *(*libc_strerror)(int __errnum);

int __xpg_strerror_r(int __errnum, char *__buf, size_t __buflen);

/*
 * XSI-compliant version of strerror_r. We have to override it to handle
 * possible deadlock/infinite recursion when pmemfile is called from inside of
 * strerror_r implementation and we call back into libc because of some failure
 * (notably: pool opening failed when process switching is enabled).
 */
int
__xpg_strerror_r(int __errnum, char *__buf, size_t __buflen)
{
	if (!guard_flag && libc__xpg_strerror_r)
		return libc__xpg_strerror_r(__errnum, __buf, __buflen);

	if (__errnum == EAGAIN) {
		const char *str =
			"Resource temporary unavailable (pmemfile wrapper)";
		if (__buflen < strlen(str) + 1)
			return ERANGE;
		strcpy(__buf, str);
		return 0;
	}

	const char *str = "Error code %d (pmemfile wrapper)";
	if (__buflen < strlen(str) + 10)
		return ERANGE;
	sprintf(__buf, str, __errnum);

	return 0;
}

/*
 * GNU-compliant version of strerror_r. See __xpg_strerror_r description.
 */
char *
strerror_r(int __errnum, char *__buf, size_t __buflen)
{
	if (!guard_flag && libc_strerror_r)
		return libc_strerror_r(__errnum, __buf, __buflen);

	const char *str = "Error code %d (pmemfile wrapper)";
	if (__buflen < strlen(str) + 10)
		return NULL;

	sprintf(__buf, str, __errnum);
	return __buf;
}

/*
 * See __xpg_strerror_r description.
 */
char *
strerror(int __errnum)
{
	static char buf[100];
	if (!guard_flag && libc_strerror)
		return libc_strerror(__errnum);

	sprintf(buf, "Error code %d (pmemfile wrapper)", __errnum);
	return buf;
}

static volatile int pause_at_start;

pf_constructor void
pmemfile_preload_constructor(void)
{
	if (!syscall_hook_in_process_allowed())
		return;

	pmemfile_vfd_table_init();

	log_init(getenv("PMEMFILE_PRELOAD_LOG"),
			getenv("PMEMFILE_PRELOAD_LOG_TRUNC"));

	const char *env_str = getenv("PMEMFILE_EXIT_ON_NOT_SUPPORTED");
	if (env_str)
		exit_on_ENOTSUP = env_str[0] == '1';

	env_str = getenv("PMEMFILE_PRELOAD_PROCESS_SWITCHING");
	if (env_str)
		process_switching = env_str[0] == '1';

	if (getenv("PMEMFILE_PRELOAD_PAUSE_AT_START")) {
		pause_at_start = 1;
		while (pause_at_start)
			;
	}

	assert(pool_count == 0);
	struct stat kernel_cwd_stat;
	stat_cwd(&kernel_cwd_stat);

	detect_mount_points(&kernel_cwd_stat);
	establish_mount_points(getenv("PMEMFILE_POOLS"), &kernel_cwd_stat);

	if (pool_count == 0)
		/* No pools mounted. XXX prevent syscall interception */
		return;

	libc__xpg_strerror_r = dlsym(RTLD_NEXT, "__xpg_strerror_r");
	if (!libc__xpg_strerror_r)
		FATAL("!can't find __xpg_strerror_r");

	libc_strerror_r = dlsym(RTLD_NEXT, "strerror_r");
	if (!libc_strerror_r)
		FATAL("!can't find strerror_r");

	libc_strerror = dlsym(RTLD_NEXT, "strerror");
	if (!libc_strerror)
		FATAL("!can't find strerror");

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

pf_destructor void
pmemfile_preload_destructor(void)
{
	/*
	 * Flush all streams, before library state is destructed.
	 * Fixes an issue when application forgets to flush or close a file
	 * it written to and libc destructor calls fflush when pmemfile
	 * and pmemobj state doesn't exist anymore.
	 */
	fflush(NULL);
}
