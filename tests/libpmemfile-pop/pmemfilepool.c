/*
 * Copyright 2017, Intel Corporation
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

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/time.h>
#include <syscall.h>
#include <unistd.h>

#include <libpmemfile-posix.h>
#include <valgrind_internal.h>

#include "utils.h"

static bool on_valgrind = false;

struct pmemfilepool {
	char *pool_path;
	size_t pool_size;
};

struct pmemfile_file {
	PMEMfilepool *pfp;
	int fd;
	int flags;
	pmemfile_mode_t mode;
};

__attribute__((constructor)) void Main(void);
bool poolsize_get_set(const char *path, size_t *poolsize, bool set);
void pmemfilepool_init(PMEMfilepool *pfp, const char *path, size_t poolsize);

__attribute__((constructor)) void
Main(void)
{
	setenv("LIBPMEMFILE_POP", "1", 0);
	on_valgrind = RUNNING_ON_VALGRIND;
}

bool
poolsize_get_set(const char *path, size_t *poolsize, bool set)
{
	char *size_filepath = malloc(strlen(path) + 6);
	strcpy(size_filepath, path);
	strcat(size_filepath, ".size");

	int fd = open(size_filepath, O_RDWR | O_CREAT, 0666);

	free(size_filepath);

	if (fd < 0)
		return false;

	ssize_t result;
	if (set)
		result = write(fd, poolsize, sizeof(poolsize));
	else
		result = read(fd, poolsize, sizeof(poolsize));

	close(fd);

	if (result != sizeof(poolsize))
		return false;
	return true;
}

static int
getfiledescriptor(PMEMfile *file)
{
	if (file == PMEMFILE_AT_CWD)
		return AT_FDCWD;
	else if (file)
		return file->fd;
	else
		return 0;
}

static int
sanitize_pointer_arg(const void *p)
{
	/*
	 * Memcheck reports error when calling POSIX function
	 * with NULL-valued parameters, however POSIX function
	 * will set error code and return -1
	 */
	if (on_valgrind && p == NULL) {
		errno = EFAULT;
		return -1;
	}

	return 0;
}

void
pmemfilepool_init(PMEMfilepool *pfp, const char *path, size_t poolsize)
{
	pfp->pool_path = strdup(path);
	pfp->pool_size = poolsize;
}

PMEMfile *
pmemfile_open(PMEMfilepool *pfp, const char *pathname, int flags, ...)
{
	va_list ap;
	va_start(ap, flags);
	mode_t mode = 0;
	if ((flags & PMEMFILE_O_CREAT) || is_tmpfile(flags)) {
		mode = va_arg(ap, mode_t);
	}
	va_end(ap);

	if (pfp == NULL || pathname == NULL) {
		errno = ENOENT;
		return NULL;
	}

	char *full_path = merge_paths(pfp->pool_path, pathname);

	if (strcmp(pfp->pool_path, full_path) == 0) {
		free(full_path);
		errno = ENOENT;
		return NULL;
	}

	int result = open(full_path, flags, mode);

	free(full_path);

	if (result == -1) {
		return NULL;
	}

	PMEMfile *pf = malloc(sizeof(PMEMfile));
	pf->pfp = pfp;
	pf->fd = result;
	pf->flags = flags;
	pf->mode = mode;
	return pf;
}

void
pmemfile_close(PMEMfilepool *pfp, PMEMfile *file)
{
	close(file->fd);
	free(file);
}

PMEMfilepool *
pmemfile_pool_open(const char *pathname)
{
	int chdir_res = chdir(pathname);
	if (chdir_res != 0)
		return NULL;

	size_t poolsize = 0;
	poolsize_get_set(pathname, &poolsize, false);

	PMEMfilepool *ret = malloc(sizeof(PMEMfilepool));
	pmemfilepool_init(ret, pathname, poolsize);
	return ret;
}

void
pmemfile_pool_close(PMEMfilepool *pfp)
{
	free(pfp->pool_path);
	free(pfp);
}

PMEMfilepool *
pmemfile_pool_create(const char *pathname, size_t poolsize, mode_t mode)
{
	mode_t previous_umask_value = umask(0);
	/*
	 * pmemfile creates directories as 0777, so we need to replicate that
	 */
	int result = mkdir(pathname, 0777);
	umask(previous_umask_value);
	if (result == 0 || (result != 0 && errno == EEXIST)) {
		int chdir_res = chdir(pathname);
		if (chdir_res != 0)
			return NULL;

		poolsize_get_set(pathname, &poolsize, true);

		PMEMfilepool *ret = malloc(sizeof(PMEMfilepool));
		pmemfilepool_init(ret, pathname, poolsize);

		return ret;
	}
	return NULL;
}

int
pmemfile_getdents64(PMEMfilepool *pfp, PMEMfile *file,
			struct linux_dirent64 *dirp, unsigned count)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	return (int)syscall(SYS_getdents64, file->fd, dirp, count);
}

int
pmemfile_fstatat(PMEMfilepool *pfp, PMEMfile *dir, const char *path,
		struct stat *buf, int flags)
{
	if (pfp == NULL || dir == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param fstatat(file_name) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(path) != 0 || sanitize_pointer_arg(buf) != 0)
		return -1;

	int result = fstatat(getfiledescriptor(dir), path, buf, flags);

	/*
	 * Symlink size is length of path it points to, so we need
	 * to subtract length of pool path from it
	 * hack for tests/posix/stat/stat.cpp - fstatat test
	 */
	if (result != -1 && S_ISLNK(buf->st_mode)) {
		buf->st_size -= (ssize_t)strlen(pfp->pool_path);
		if (buf->st_size < 0)
			buf->st_size = 0;
	}

	return result;
}

int
pmemfile_unlink(PMEMfilepool *pfp, const char *pathname)
{
	if (pfp == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (pathname == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, pathname);
	int result = unlink(full_path);

	free(full_path);

	return result;
}

int
pmemfile_link(PMEMfilepool *pfp, const char *oldpath, const char *newpath)
{
	if (pfp == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (oldpath == NULL || newpath == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_old_path = merge_paths(pfp->pool_path, oldpath);
	char *full_new_path = merge_paths(pfp->pool_path, newpath);

	int result = link(full_old_path, full_new_path);

	free(full_old_path);
	free(full_new_path);

	return result;
}

void
pmemfile_stats(PMEMfilepool *pfp, struct pmemfile_stats *stats)
{
	stats->blocks = 0;
	stats->block_arrays = 0;
	stats->dirs = 0;
	stats->inodes = 0;
	stats->inode_arrays = 0;
}

int
pmemfile_mkdir(PMEMfilepool *pfp, const char *path, mode_t mode)
{
	if (pfp == NULL || path == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, path);
	int result = mkdir(full_path, mode);
	free(full_path);

	return result;
}

int
pmemfile_rmdir(PMEMfilepool *pfp, const char *path)
{
	if (pfp == NULL || path == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, path);
	int result = rmdir(full_path);
	free(full_path);

	return result;
}

ssize_t
pmemfile_write(PMEMfilepool *pfp, PMEMfile *file, const void *buf, size_t count)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	/*
	 * Do not alow to write files bigger than poolsize
	 * hack for tests/posix/rw.cpp - failed_write test
	 */
	if (count > pfp->pool_size) {
		errno = ENOSPC;
		return -1;
	}

	return write(file->fd, buf, count);
}

ssize_t
pmemfile_read(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	return read(file->fd, buf, count);
}

int
pmemfile_getdents(PMEMfilepool *pfp, PMEMfile *file, struct linux_dirent *dirp,
		unsigned count)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param getdents(dirp) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(dirp) != 0)
		return -1;

	return (int)syscall(SYS_getdents, file->fd, dirp, count);
}

pmemfile_off_t
pmemfile_lseek(PMEMfilepool *pfp, PMEMfile *file, pmemfile_off_t offset,
		int whence)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	return lseek64(file->fd, offset, whence);
}

int
pmemfile_truncate(PMEMfilepool *pfp, const char *path, pmemfile_off_t length)
{
	if (pfp == NULL || path == NULL) {
		errno = EFAULT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, path);
	int result = truncate(full_path, length);
	free(full_path);

	return result;
}

int
pmemfile_ftruncate(PMEMfilepool *pfp, PMEMfile *file, pmemfile_off_t length)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	return ftruncate(file->fd, length);
}

int
pmemfile_fallocate(PMEMfilepool *pfp, PMEMfile *file, int mode,
		pmemfile_off_t offset, pmemfile_off_t length)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	return fallocate(file->fd, mode, offset, length);
}

int
pmemfile_posix_fallocate(PMEMfilepool *pfp, PMEMfile *file,
			pmemfile_off_t offset, pmemfile_off_t length)
{
	return posix_fallocate(file->fd, offset, length);
}

pmemfile_ssize_t
pmemfile_pwrite(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
		size_t count, pmemfile_off_t offset)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param pwrite64(buf) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(buf) != 0)
		return -1;

	/* Syscall param pwrite64(buf) points to uninitialised byte(s) */
	if (on_valgrind && offset == -1) {
		errno = EINVAL;
		return -1;
	}

	return pwrite(file->fd, buf, count, offset);
}

pmemfile_ssize_t
pmemfile_pread(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count,
		pmemfile_off_t offset)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param pread64(buf) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(buf) != 0)
		return -1;

	return pread(file->fd, buf, count, offset);
}

pmemfile_ssize_t
pmemfile_readv(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param readv(vector) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(iov) != 0)
		return -1;

	return readv(file->fd, iov, iovcnt);
}

pmemfile_ssize_t
pmemfile_preadv(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt, pmemfile_off_t offset)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param preadv(vector) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(iov) != 0)
		return -1;

	return preadv(file->fd, iov, iovcnt, offset);
}

pmemfile_ssize_t
pmemfile_writev(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param writev(vector) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(iov) != 0)
		return -1;

	return writev(file->fd, iov, iovcnt);
}

pmemfile_ssize_t
pmemfile_pwritev(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt, pmemfile_off_t offset)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param pwritev(vector) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(iov) != 0)
		return -1;

	return pwritev(file->fd, iov, iovcnt, offset);
}

int
pmemfile_stat(PMEMfilepool *pfp, const char *path, pmemfile_stat_t *buf)
{
	if (pfp == NULL || path == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param stat(buf) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(buf) != 0)
		return -1;

	char *full_path = merge_paths(pfp->pool_path, path);
	int result = stat(full_path, buf);
	free(full_path);

	return result;
}

int
pmemfile_fstat(PMEMfilepool *pfp, PMEMfile *file, pmemfile_stat_t *buf)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Syscall param fstat(buf) points to unaddressable byte(s) */
	if (sanitize_pointer_arg(buf) != 0)
		return -1;

	return fstat(file->fd, buf);
}

PMEMfile *
pmemfile_openat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int flags, ...)
{
	if (!pfp) {
		errno = EFAULT;
		return NULL;
	}

	if (!pathname) {
		errno = ENOENT;
		return NULL;
	}

	if (pathname[0] != '/' && !dir) {
		errno = EFAULT;
		return NULL;
	}

	va_list ap;
	va_start(ap, flags);
	pmemfile_mode_t mode = 0;
	if ((flags & PMEMFILE_O_CREAT) || is_tmpfile(flags))
		mode = va_arg(ap, pmemfile_mode_t);
	va_end(ap);

	int fd = getfiledescriptor(dir);

	int result = -1;

	char *full_path = (char *)pathname;

	if (pathname[0] == '/')
		full_path = merge_paths(pfp->pool_path, pathname);

	result = openat(fd, full_path, flags, mode);

	if (pathname[0] == '/')
		free(full_path);

	if (result == -1)
		return NULL;

	PMEMfile *pf = malloc(sizeof(PMEMfile));
	pf->pfp = pfp;
	pf->fd = result;
	pf->flags = flags;
	pf->mode = mode;
	return pf;
}

PMEMfile *
pmemfile_create(PMEMfilepool *pfp, const char *pathname, pmemfile_mode_t mode)
{
	if (pfp == NULL || pathname == NULL) {
		errno = ENOENT;
		return NULL;
	}

	char *full_path = merge_paths(pfp->pool_path, pathname);
	int result = creat(full_path, mode);
	free(full_path);

	if (result == -1)
		return NULL;
	PMEMfile *pf = malloc(sizeof(*pf));
	pf->pfp = pfp;
	pf->fd = result;
	pf->flags = 0;
	pf->mode = mode;

	return pf;
}

int
pmemfile_linkat(PMEMfilepool *pfp, PMEMfile *olddir, const char *oldpath,
		PMEMfile *newdir, const char *newpath, int flags)
{
	char *full_old_path = (char *)oldpath;
	char *full_new_path = (char *)newpath;

	if (oldpath[0] == '/')
		full_old_path = merge_paths(pfp->pool_path, oldpath);
	if (newpath[0] == '/')
		full_new_path = merge_paths(pfp->pool_path, newpath);

	int result = linkat(getfiledescriptor(olddir), full_old_path,
			    getfiledescriptor(newdir), full_new_path, flags);

	if (oldpath[0] == '/')
		free(full_old_path);
	if (newpath[0] == '/')
		free(full_new_path);

	return result;
}

int
pmemfile_unlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int flags)
{
	char *full_path = (char *)pathname;

	if (pathname[0] == '/')
		full_path = merge_paths(pfp->pool_path, pathname);

	int result = unlinkat(getfiledescriptor(dir), full_path, flags);

	if (pathname[0] == '/')
		free(full_path);

	return result;
}

int
pmemfile_rename(PMEMfilepool *pfp, const char *old_path, const char *new_path)
{
	if (pfp == NULL || old_path == NULL || new_path == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_old_path = merge_paths(pfp->pool_path, old_path);
	char *full_new_path = merge_paths(pfp->pool_path, new_path);

	int result = rename(full_old_path, full_new_path);

	free(full_old_path);
	free(full_new_path);
	return result;
}

int
pmemfile_renameat(PMEMfilepool *pfp, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path)
{
	char *full_old_path = (char *)old_path;
	char *full_new_path = (char *)new_path;

	if (old_path[0] == '/')
		full_old_path = merge_paths(pfp->pool_path, old_path);
	if (new_path[0] == '/')
		full_new_path = merge_paths(pfp->pool_path, new_path);

	int result = renameat(getfiledescriptor(old_at), old_path,
			    getfiledescriptor(new_at), new_path);

	if (old_path[0] == '/')
		free(full_old_path);
	if (new_path[0] == '/')
		free(full_new_path);

	return result;
}

int
pmemfile_renameat2(PMEMfilepool *pfp, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path, unsigned flags)
{
	char *full_old_path = (char *)old_path;
	char *full_new_path = (char *)new_path;

	if (old_path[0] == '/')
		full_old_path = merge_paths(pfp->pool_path, old_path);
	if (new_path[0] == '/')
		full_new_path = merge_paths(pfp->pool_path, new_path);

	int result = (int)syscall(SYS_renameat2, getfiledescriptor(old_at),
				    full_old_path, getfiledescriptor(new_at),
				    full_new_path, flags);

	if (old_path[0] == '/')
		free(full_old_path);
	if (new_path[0] == '/')
		free(full_new_path);

	return result;
}

int
pmemfile_lstat(PMEMfilepool *pfp, const char *path, pmemfile_stat_t *buf)
{
	if (pfp == NULL || path == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, path);
	int result = lstat(full_path, buf);
	free(full_path);

	return result;
}

int
pmemfile_mkdirat(PMEMfilepool *pfp, PMEMfile *dir, const char *path,
		pmemfile_mode_t mode)
{
	char *full_path = (char *)path;

	if (path[0] == '/')
		full_path = merge_paths(pfp->pool_path, path);

	int result = mkdirat(getfiledescriptor(dir), path, mode);

	if (path[0] == '/')
		free(full_path);

	return result;
}

int
pmemfile_chdir(PMEMfilepool *pfp, const char *path)
{
	if (pfp == NULL || path == NULL) {
		errno = ENOENT;
		return -1;
	}

	if (path[0] == '/') {
		char *full_path = malloc(strlen(path)
					+ strlen(pfp->pool_path)
					+ 1);
		full_path[0] = '\0';
		strcat(full_path, pfp->pool_path);
		strcat(full_path, path);
		int result = chdir(full_path);
		free(full_path);
		return result;
	}

	return chdir(path);
}

int
pmemfile_fchdir(PMEMfilepool *pfp, PMEMfile *dir)
{
	return fchdir(dir->fd);
}

char *
pmemfile_getcwd(PMEMfilepool *pfp, char *buf, size_t size)
{
	return getcwd(buf, size);
}

int
pmemfile_fcntl(PMEMfilepool *pfp, PMEMfile *file, int cmd, ...)
{
	va_list ap;
	va_start(ap, cmd);

	int result = fcntl(file->fd, cmd, ap);

	va_end(ap);

	return result;
}

int
pmemfile_symlink(PMEMfilepool *pfp, const char *path1, const char *path2)
{
	if (pfp == NULL || path1 == NULL || path2 == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_old_path = (char *)path1;
	char *full_new_path = (char *)path2;

	if (path1[0] == '.' || path1[0] == '/')
		full_old_path = merge_paths(pfp->pool_path, path1);
	if (path2[0] == '.' || path2[0] == '/')
		full_new_path = merge_paths(pfp->pool_path, path2);

	int result = symlink(full_old_path, full_new_path);

	if (full_old_path != path1)
		free(full_old_path);
	if (full_new_path != path2)
		free(full_new_path);

	return result;
}

int
pmemfile_symlinkat(PMEMfilepool *pfp, const char *path1, PMEMfile *at,
		const char *path2)
{
	if (pfp == NULL || path1 == NULL || path2 == NULL) {
		errno = EFAULT;
		return -1;
	}

	char *full_old_path = merge_paths(pfp->pool_path, path1);
	char *full_new_path = (char *)path2;

	if (path2[0] == '/')
		full_new_path = merge_paths(pfp->pool_path, path2);

	int result = symlinkat(full_old_path, getfiledescriptor(at),
				full_new_path);

	free(full_old_path);

	if (path2[0] == '/')
		free(full_new_path);

	return result;
}

pmemfile_ssize_t
pmemfile_readlink(PMEMfilepool *pfp, const char *path, char *buf,
		size_t buf_len)
{
	if (pfp == NULL || path == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, path);
	char *readlink_buf = malloc(buf_len + 1);
	readlink_buf[0] = '\0';

	pmemfile_ssize_t result = readlink(full_path, readlink_buf, buf_len);
	free(full_path);

	if (result <= 0) {
		free(readlink_buf);
		return result;
	}

	readlink_buf[result] = '\0';

	char *tmp_buf = replace(readlink_buf, pfp->pool_path, "");
	strcpy(buf, tmp_buf);

	free(readlink_buf);
	free(tmp_buf);

	return result;
}

pmemfile_ssize_t
pmemfile_readlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
			char *buf, size_t bufsiz)
{
	char *readlink_buf = malloc(bufsiz + 1);
	readlink_buf[0] = '\0';

	char *full_path = (char *)pathname;

	if (pathname[0] == '/')
		full_path = merge_paths(pfp->pool_path, pathname);

	pmemfile_ssize_t result = readlinkat(getfiledescriptor(dir), full_path,
					    readlink_buf, bufsiz);

	if (pathname[0] == '/')
		free(full_path);

	if (result <= 0) {
		free(readlink_buf);
		return result;
	}

	readlink_buf[result] = '\0';

	char *tmp_buf = replace(readlink_buf, pfp->pool_path, "");
	strcpy(buf, tmp_buf);
	free(tmp_buf);
	free(readlink_buf);

	return result;
}

int
pmemfile_chmod(PMEMfilepool *pfp, const char *path, pmemfile_mode_t mode)
{
	if (pfp == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (path == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, path);
	int result = chmod(full_path, mode);
	free(full_path);

	return result;
}

int
pmemfile_fchmod(PMEMfilepool *pfp, PMEMfile *file, pmemfile_mode_t mode)
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	return fchmod(file->fd, mode);
}

int
pmemfile_fchmodat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		pmemfile_mode_t mode, int flags)
{
	if (pfp == NULL || dir == NULL)	{
		errno = EFAULT;
		return -1;
	}

	if (pathname == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = (char *)pathname;

	if (pathname[0] == '/')
		full_path = merge_paths(pfp->pool_path, pathname);

	int result = fchmodat(getfiledescriptor(dir), full_path, mode, flags);

	if (pathname[0] == '/')
		free(full_path);

	return result;
}

int
pmemfile_setreuid(PMEMfilepool *pfp, pmemfile_uid_t ruid, pmemfile_uid_t euid)
{
	return setreuid(ruid, euid);
}

int
pmemfile_setregid(PMEMfilepool *pfp, pmemfile_gid_t rgid, pmemfile_gid_t egid)
{
	return setregid(rgid, egid);
}

int
pmemfile_setuid(PMEMfilepool *pfp, pmemfile_uid_t uid)
{
	return setuid(uid);
}

int
pmemfile_setgid(PMEMfilepool *pfp, pmemfile_gid_t gid)
{
	return setgid(gid);
}

pmemfile_uid_t
pmemfile_getuid(PMEMfilepool *pfp)
{
	return getuid();
}

pmemfile_gid_t
pmemfile_getgid(PMEMfilepool *pfp)
{
	return getgid();
}

int
pmemfile_seteuid(PMEMfilepool *pfp, pmemfile_uid_t uid)
{
	return seteuid(uid);
}

int
pmemfile_setegid(PMEMfilepool *pfp, pmemfile_gid_t gid)
{
	return setegid(gid);
}

pmemfile_uid_t
pmemfile_geteuid(PMEMfilepool *pfp)
{
	return geteuid();
}

pmemfile_gid_t
pmemfile_getegid(PMEMfilepool *pfp)
{
	return getegid();
}

int
pmemfile_setfsuid(PMEMfilepool *pfp, pmemfile_uid_t fsuid)
{
	return setfsuid(fsuid);
}

int
pmemfile_setfsgid(PMEMfilepool *pfp, pmemfile_uid_t fsgid)
{
	return setfsgid(fsgid);
}

int
pmemfile_getgroups(PMEMfilepool *pfp, int size, pmemfile_gid_t list[])
{
	return getgroups(size, list);
}

int
pmemfile_setgroups(PMEMfilepool *pfp, size_t size, const pmemfile_gid_t *list)
{
	return setgroups(size, list);
}

int
pmemfile_chown(PMEMfilepool *pfp, const char *pathname, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	if (pfp == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (pathname == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, pathname);
	int result = chown(full_path, owner, group);
	free(full_path);

	return result;
}

int
pmemfile_fchown(PMEMfilepool *pfp, PMEMfile *file, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	return fchown(file->fd, owner, group);
}

int
pmemfile_lchown(PMEMfilepool *pfp, const char *pathname, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	if (pfp == NULL || pathname == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, pathname);
	int result = lchown(full_path, owner, group);
	free(full_path);

	return result;
}

int
pmemfile_fchownat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		pmemfile_uid_t owner, pmemfile_gid_t group, int flags)
{
	char *full_path = (char *)pathname;

	if (pathname[0] == '/')
		full_path = merge_paths(pfp->pool_path, pathname);

	int result = fchownat(getfiledescriptor(dir), full_path, owner, group,
				flags);

	if (pathname[0] == '/')
		free(full_path);

	return result;
}

int
pmemfile_access(PMEMfilepool *pfp, const char *path, int mode)
{
	if (pfp == NULL || path == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, path);
	int result = access(full_path, mode);
	free(full_path);

	return result;
}

int
pmemfile_euidaccess(PMEMfilepool *pfp, const char *pathname, int mode)
{
	if (pfp == NULL || pathname == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, pathname);
	int result = euidaccess(full_path, mode);
	free(full_path);

	return result;
}

int
pmemfile_faccessat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int mode, int flags)
{
	char *full_path = (char *)pathname;

	if (pathname[0] == '/')
		full_path = merge_paths(pfp->pool_path, pathname);

	int result = faccessat(getfiledescriptor(dir), full_path, mode, flags);

	if (pathname[0] == '/')
		free(full_path);

	return result;
}

int
pmemfile_setcap(PMEMfilepool *pfp, int cap)
{
	printf("pmemfile_setcap() - not implemented\n");
	return -1;
}

int
pmemfile_clrcap(PMEMfilepool *pfp, int cap)
{
	printf("pmemfile_clrcap() - not implemented\n");
	return -1;
}

char *
pmemfile_get_dir_path(PMEMfilepool *pfp, PMEMfile *dir, char *buf, size_t size)
{
	if (!pfp || !dir)
		return NULL;
	char *fd_path = malloc(PATH_MAX + 1);
	fd_path[0] = '\0';
	strcat(fd_path, "/proc/self/fd/");
	char *fd_number = malloc(10);
	sprintf(fd_number, "%d", dir->fd);
	strcat(fd_path, fd_number);

	ssize_t linklength = readlink(fd_path, buf, size);

	free(fd_path);
	free(fd_number);

	if (linklength != -1) {
		char *tmp_result = replace(buf, pfp->pool_path, "");
		strcpy(buf, tmp_result);
		free(tmp_result);
		return buf;
	}
	return NULL;
}

PMEMfile *
pmemfile_open_parent(PMEMfilepool *pfp, PMEMfile *at, char *path,
		size_t path_size, int flags)
{
	printf("pmemfile_open_parent() - not implemented\n");
	return NULL;
}

const char *
pmemfile_errormsg(void)
{
	return strerror(errno);
}

int
pmemfile_utime(PMEMfilepool *pfp, const char *filename,
		const pmemfile_utimbuf_t *times)
{
	if (pfp == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (filename == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, filename);
	int result = utime(full_path, times);
	free(full_path);

	return result;
}

int
pmemfile_utimes(PMEMfilepool *pfp, const char *filename,
		const pmemfile_timeval_t times[2])
{
	if (pfp == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (filename == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, filename);
	int result = utimes(full_path, times);
	free(full_path);

	return result;
}

int
pmemfile_futimes(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_timeval_t tv[2])
{
	if (pfp == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (file == NULL) {
		errno = EFAULT;
		return -1;
	}

	return futimes(file->fd, tv);
}

int
pmemfile_futimesat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		const pmemfile_timeval_t tv[2])
{
	char *full_path = (char *)pathname;

	if (pathname[0] == '/')
		full_path = merge_paths(pfp->pool_path, pathname);

	int result = futimesat(getfiledescriptor(dir), full_path, tv);

	if (pathname[0] == '/')
		free(full_path);

	return result;
}

int
pmemfile_lutimes(PMEMfilepool *pfp, const char *filename,
		const pmemfile_timeval_t tv[2])
{
	if (pfp == NULL || filename == NULL) {
		errno = ENOENT;
		return -1;
	}

	char *full_path = merge_paths(pfp->pool_path, filename);
	int result = lutimes(full_path, tv);
	free(full_path);

	return result;
}

int
pmemfile_utimensat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		const pmemfile_timespec_t times[2], int flags)
{
	if (pathname == NULL) {
		errno = ENOENT;
		return -1;
	}

	if (pfp == NULL || dir == NULL)	{
		errno = EFAULT;
		return -1;
	}

	char *full_path = (char *)pathname;

	if (pathname[0] == '/')
		full_path = merge_paths(pfp->pool_path, pathname);

	int result = utimensat(getfiledescriptor(dir), full_path, times, flags);

	if (pathname[0] == '/')
		free(full_path);

	return result;
}

int
pmemfile_futimens(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_timespec_t times[2])
{
	if (pfp == NULL || file == NULL) {
		errno = EFAULT;
		return -1;
	}

	return futimens(file->fd, times);
}

pmemfile_mode_t
pmemfile_umask(PMEMfilepool *pfp, pmemfile_mode_t mask)
{
	return umask(mask);
}
