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

/* Generated source file, do not edit manually! */

#ifndef LIBPMEMFILE_POSIX_WRAPPERS_H
#define LIBPMEMFILE_POSIX_WRAPPERS_H

#include "libpmemfile-posix.h"
#include "preload.h"
#include <inttypes.h>

static inline unsigned
wrapper_pmemfile_pool_root_count(PMEMfilepool *pfp)
{
	unsigned ret;

	ret = pmemfile_pool_root_count(pfp);

	log_write(
	    "pmemfile_pool_root_count(%p) = %u",
		pfp,
		ret);

	return ret;
}

static inline PMEMfilepool *
wrapper_pmemfile_pool_create(const char *pathname,
		size_t poolsize,
		pmemfile_mode_t mode)
{
	PMEMfilepool *ret;

	ret = pmemfile_pool_create(pathname,
		poolsize,
		mode);

	log_write(
	    "pmemfile_pool_create(\"%s\", %zu, %3jo) = %p",
		pathname,
		poolsize,
		(uintmax_t)mode,
		ret);

	return ret;
}

static inline PMEMfilepool *
wrapper_pmemfile_pool_open(const char *pathname)
{
	PMEMfilepool *ret;

	ret = pmemfile_pool_open(pathname);

	log_write(
	    "pmemfile_pool_open(\"%s\") = %p",
		pathname,
		ret);

	return ret;
}

static inline void
wrapper_pmemfile_pool_close(PMEMfilepool *pfp)
{
	log_write(
	    "pmemfile_pool_close(%p)",
		pfp);

	pmemfile_pool_close(pfp);
}

static inline void
wrapper_pmemfile_pool_set_device(PMEMfilepool *pfp,
		pmemfile_dev_t dev)
{
	log_write(
	    "pmemfile_pool_set_device(%p, %jx)",
		pfp,
		(uintmax_t)dev);

	pmemfile_pool_set_device(pfp,
		dev);
}

static inline PMEMfile *
wrapper_pmemfile_open_root(PMEMfilepool *pfp,
		unsigned index,
		int flags)
{
	PMEMfile *ret;

	ret = pmemfile_open_root(pfp,
		index,
		flags);

	log_write(
	    "pmemfile_open_root(%p, %u, %d) = %p",
		pfp,
		index,
		flags,
		ret);

	return ret;
}

static inline PMEMfile *
wrapper_pmemfile_create(PMEMfilepool *pfp,
		const char *pathname,
		pmemfile_mode_t mode)
{
	PMEMfile *ret;

	ret = pmemfile_create(pfp,
		pathname,
		mode);

	log_write(
	    "pmemfile_create(%p, \"%s\", %3jo) = %p",
		pfp,
		pathname,
		(uintmax_t)mode,
		ret);

	return ret;
}

static inline void
wrapper_pmemfile_close(PMEMfilepool *pfp,
		PMEMfile *file)
{
	log_write(
	    "pmemfile_close(%p, %p)",
		pfp,
		file);

	pmemfile_close(pfp,
		file);
}

static inline int
wrapper_pmemfile_link(PMEMfilepool *pfp,
		const char *oldpath,
		const char *newpath)
{
	int ret;

	ret = pmemfile_link(pfp,
		oldpath,
		newpath);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_link(%p, \"%s\", \"%s\") = %d",
		pfp,
		oldpath,
		newpath,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_linkat(PMEMfilepool *pfp,
		PMEMfile *olddir,
		const char *oldpath,
		PMEMfile *newdir,
		const char *newpath,
		int flags)
{
	int ret;

	ret = pmemfile_linkat(pfp,
		olddir,
		oldpath,
		newdir,
		newpath,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_linkat(%p, %p, \"%s\", %p, \"%s\", %d) = %d",
		pfp,
		olddir,
		oldpath,
		newdir,
		newpath,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_unlink(PMEMfilepool *pfp,
		const char *pathname)
{
	int ret;

	ret = pmemfile_unlink(pfp,
		pathname);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_unlink(%p, \"%s\") = %d",
		pfp,
		pathname,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_unlinkat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *pathname,
		int flags)
{
	int ret;

	ret = pmemfile_unlinkat(pfp,
		dir,
		pathname,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_unlinkat(%p, %p, \"%s\", %d) = %d",
		pfp,
		dir,
		pathname,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_rename(PMEMfilepool *pfp,
		const char *old_path,
		const char *new_path)
{
	int ret;

	ret = pmemfile_rename(pfp,
		old_path,
		new_path);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_rename(%p, \"%s\", \"%s\") = %d",
		pfp,
		old_path,
		new_path,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_renameat(PMEMfilepool *pfp,
		PMEMfile *old_at,
		const char *old_path,
		PMEMfile *new_at,
		const char *new_path)
{
	int ret;

	ret = pmemfile_renameat(pfp,
		old_at,
		old_path,
		new_at,
		new_path);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_renameat(%p, %p, \"%s\", %p, \"%s\") = %d",
		pfp,
		old_at,
		old_path,
		new_at,
		new_path,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_renameat2(PMEMfilepool *pfp,
		PMEMfile *old_at,
		const char *old_path,
		PMEMfile *new_at,
		const char *new_path,
		unsigned flags)
{
	int ret;

	ret = pmemfile_renameat2(pfp,
		old_at,
		old_path,
		new_at,
		new_path,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_renameat2(%p, %p, \"%s\", %p, \"%s\", %u) = %d",
		pfp,
		old_at,
		old_path,
		new_at,
		new_path,
		flags,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_read(PMEMfilepool *pfp,
		PMEMfile *file,
		void *buf,
		size_t count)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_read(pfp,
		file,
		buf,
		count);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_read(%p, %p, %p, %zu) = %zd",
		pfp,
		file,
		buf,
		count,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_pread(PMEMfilepool *pfp,
		PMEMfile *file,
		void *buf,
		size_t count,
		pmemfile_off_t offset)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_pread(pfp,
		file,
		buf,
		count,
		offset);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_pread(%p, %p, %p, %zu, %jx) = %zd",
		pfp,
		file,
		buf,
		count,
		(uintmax_t)offset,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_readv(PMEMfilepool *pfp,
		PMEMfile *file,
		const pmemfile_iovec_t *iov,
		int iovcnt)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_readv(pfp,
		file,
		iov,
		iovcnt);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_readv(%p, %p, %p, %d) = %zd",
		pfp,
		file,
		iov,
		iovcnt,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_preadv(PMEMfilepool *pfp,
		PMEMfile *file,
		const pmemfile_iovec_t *iov,
		int iovcnt,
		pmemfile_off_t offset)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_preadv(pfp,
		file,
		iov,
		iovcnt,
		offset);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_preadv(%p, %p, %p, %d, %jx) = %zd",
		pfp,
		file,
		iov,
		iovcnt,
		(uintmax_t)offset,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_write(PMEMfilepool *pfp,
		PMEMfile *file,
		const void *buf,
		size_t count)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_write(pfp,
		file,
		buf,
		count);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_write(%p, %p, %p, %zu) = %zd",
		pfp,
		file,
		buf,
		count,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_pwrite(PMEMfilepool *pfp,
		PMEMfile *file,
		const void *buf,
		size_t count,
		pmemfile_off_t offset)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_pwrite(pfp,
		file,
		buf,
		count,
		offset);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_pwrite(%p, %p, %p, %zu, %jx) = %zd",
		pfp,
		file,
		buf,
		count,
		(uintmax_t)offset,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_writev(PMEMfilepool *pfp,
		PMEMfile *file,
		const pmemfile_iovec_t *iov,
		int iovcnt)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_writev(pfp,
		file,
		iov,
		iovcnt);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_writev(%p, %p, %p, %d) = %zd",
		pfp,
		file,
		iov,
		iovcnt,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_pwritev(PMEMfilepool *pfp,
		PMEMfile *file,
		const pmemfile_iovec_t *iov,
		int iovcnt,
		pmemfile_off_t offset)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_pwritev(pfp,
		file,
		iov,
		iovcnt,
		offset);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_pwritev(%p, %p, %p, %d, %jx) = %zd",
		pfp,
		file,
		iov,
		iovcnt,
		(uintmax_t)offset,
		ret);

	return ret;
}

static inline pmemfile_off_t
wrapper_pmemfile_lseek(PMEMfilepool *pfp,
		PMEMfile *file,
		pmemfile_off_t offset,
		int whence)
{
	pmemfile_off_t ret;

	ret = pmemfile_lseek(pfp,
		file,
		offset,
		whence);

	log_write(
	    "pmemfile_lseek(%p, %p, %jx, %d) = %jx",
		pfp,
		file,
		(uintmax_t)offset,
		whence,
		(uintmax_t)ret);

	return ret;
}

static inline int
wrapper_pmemfile_stat(PMEMfilepool *pfp,
		const char *path,
		pmemfile_stat_t *buf)
{
	int ret;

	ret = pmemfile_stat(pfp,
		path,
		buf);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_stat(%p, \"%s\", %p) = %d",
		pfp,
		path,
		buf,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_lstat(PMEMfilepool *pfp,
		const char *path,
		pmemfile_stat_t *buf)
{
	int ret;

	ret = pmemfile_lstat(pfp,
		path,
		buf);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_lstat(%p, \"%s\", %p) = %d",
		pfp,
		path,
		buf,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_fstat(PMEMfilepool *pfp,
		PMEMfile *file,
		pmemfile_stat_t *buf)
{
	int ret;

	ret = pmemfile_fstat(pfp,
		file,
		buf);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_fstat(%p, %p, %p) = %d",
		pfp,
		file,
		buf,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_fstatat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *path,
		pmemfile_stat_t *buf,
		int flags)
{
	int ret;

	ret = pmemfile_fstatat(pfp,
		dir,
		path,
		buf,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_fstatat(%p, %p, \"%s\", %p, %d) = %d",
		pfp,
		dir,
		path,
		buf,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_getdents(PMEMfilepool *pfp,
		PMEMfile *file,
		struct linux_dirent *dirp,
		unsigned count)
{
	int ret;

	ret = pmemfile_getdents(pfp,
		file,
		dirp,
		count);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_getdents(%p, %p, %p, %u) = %d",
		pfp,
		file,
		dirp,
		count,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_getdents64(PMEMfilepool *pfp,
		PMEMfile *file,
		struct linux_dirent64 *dirp,
		unsigned count)
{
	int ret;

	ret = pmemfile_getdents64(pfp,
		file,
		dirp,
		count);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_getdents64(%p, %p, %p, %u) = %d",
		pfp,
		file,
		dirp,
		count,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_mkdir(PMEMfilepool *pfp,
		const char *path,
		pmemfile_mode_t mode)
{
	int ret;

	ret = pmemfile_mkdir(pfp,
		path,
		mode);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_mkdir(%p, \"%s\", %3jo) = %d",
		pfp,
		path,
		(uintmax_t)mode,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_mkdirat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *path,
		pmemfile_mode_t mode)
{
	int ret;

	ret = pmemfile_mkdirat(pfp,
		dir,
		path,
		mode);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_mkdirat(%p, %p, \"%s\", %3jo) = %d",
		pfp,
		dir,
		path,
		(uintmax_t)mode,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_rmdir(PMEMfilepool *pfp,
		const char *path)
{
	int ret;

	ret = pmemfile_rmdir(pfp,
		path);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_rmdir(%p, \"%s\") = %d",
		pfp,
		path,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_chdir(PMEMfilepool *pfp,
		const char *path)
{
	int ret;

	ret = pmemfile_chdir(pfp,
		path);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_chdir(%p, \"%s\") = %d",
		pfp,
		path,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_fchdir(PMEMfilepool *pfp,
		PMEMfile *dir)
{
	int ret;

	ret = pmemfile_fchdir(pfp,
		dir);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_fchdir(%p, %p) = %d",
		pfp,
		dir,
		ret);

	return ret;
}

static inline char *
wrapper_pmemfile_getcwd(PMEMfilepool *pfp,
		char *buf,
		size_t size)
{
	char *ret;

	ret = pmemfile_getcwd(pfp,
		buf,
		size);

	log_write(
	    "pmemfile_getcwd(%p, %p, %zu) = %p",
		pfp,
		buf,
		size,
		ret);

	return ret;
}

static inline pmemfile_mode_t
wrapper_pmemfile_umask(PMEMfilepool *pfp,
		pmemfile_mode_t mask)
{
	pmemfile_mode_t ret;

	ret = pmemfile_umask(pfp,
		mask);

	log_write(
	    "pmemfile_umask(%p, %3jo) = %3jo",
		pfp,
		(uintmax_t)mask,
		(uintmax_t)ret);

	return ret;
}

static inline int
wrapper_pmemfile_symlink(PMEMfilepool *pfp,
		const char *path1,
		const char *path2)
{
	int ret;

	ret = pmemfile_symlink(pfp,
		path1,
		path2);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_symlink(%p, %p, %p) = %d",
		pfp,
		path1,
		path2,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_symlinkat(PMEMfilepool *pfp,
		const char *path1,
		PMEMfile *at,
		const char *path2)
{
	int ret;

	ret = pmemfile_symlinkat(pfp,
		path1,
		at,
		path2);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_symlinkat(%p, %p, %p, %p) = %d",
		pfp,
		path1,
		at,
		path2,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_readlink(PMEMfilepool *pfp,
		const char *path,
		char *buf,
		size_t buf_len)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_readlink(pfp,
		path,
		buf,
		buf_len);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_readlink(%p, \"%s\", %p, %zu) = %zd",
		pfp,
		path,
		buf,
		buf_len,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_readlinkat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *pathname,
		char *buf,
		size_t bufsiz)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_readlinkat(pfp,
		dir,
		pathname,
		buf,
		bufsiz);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_readlinkat(%p, %p, \"%s\", %p, %zu) = %zd",
		pfp,
		dir,
		pathname,
		buf,
		bufsiz,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_chmod(PMEMfilepool *pfp,
		const char *path,
		pmemfile_mode_t mode)
{
	int ret;

	ret = pmemfile_chmod(pfp,
		path,
		mode);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_chmod(%p, \"%s\", %3jo) = %d",
		pfp,
		path,
		(uintmax_t)mode,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_fchmod(PMEMfilepool *pfp,
		PMEMfile *file,
		pmemfile_mode_t mode)
{
	int ret;

	ret = pmemfile_fchmod(pfp,
		file,
		mode);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_fchmod(%p, %p, %3jo) = %d",
		pfp,
		file,
		(uintmax_t)mode,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_fchmodat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *pathname,
		pmemfile_mode_t mode,
		int flags)
{
	int ret;

	ret = pmemfile_fchmodat(pfp,
		dir,
		pathname,
		mode,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_fchmodat(%p, %p, \"%s\", %3jo, %d) = %d",
		pfp,
		dir,
		pathname,
		(uintmax_t)mode,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_setreuid(PMEMfilepool *pfp,
		pmemfile_uid_t ruid,
		pmemfile_uid_t euid)
{
	int ret;

	ret = pmemfile_setreuid(pfp,
		ruid,
		euid);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_setreuid(%p, %jx, %jx) = %d",
		pfp,
		(uintmax_t)ruid,
		(uintmax_t)euid,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_setregid(PMEMfilepool *pfp,
		pmemfile_gid_t rgid,
		pmemfile_gid_t egid)
{
	int ret;

	ret = pmemfile_setregid(pfp,
		rgid,
		egid);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_setregid(%p, %jx, %jx) = %d",
		pfp,
		(uintmax_t)rgid,
		(uintmax_t)egid,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_setuid(PMEMfilepool *pfp,
		pmemfile_uid_t uid)
{
	int ret;

	ret = pmemfile_setuid(pfp,
		uid);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_setuid(%p, %jx) = %d",
		pfp,
		(uintmax_t)uid,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_setgid(PMEMfilepool *pfp,
		pmemfile_gid_t gid)
{
	int ret;

	ret = pmemfile_setgid(pfp,
		gid);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_setgid(%p, %jx) = %d",
		pfp,
		(uintmax_t)gid,
		ret);

	return ret;
}

static inline pmemfile_uid_t
wrapper_pmemfile_getuid(PMEMfilepool *pfp)
{
	pmemfile_uid_t ret;

	ret = pmemfile_getuid(pfp);

	log_write(
	    "pmemfile_getuid(%p) = %jx",
		pfp,
		(uintmax_t)ret);

	return ret;
}

static inline pmemfile_gid_t
wrapper_pmemfile_getgid(PMEMfilepool *pfp)
{
	pmemfile_gid_t ret;

	ret = pmemfile_getgid(pfp);

	log_write(
	    "pmemfile_getgid(%p) = %jx",
		pfp,
		(uintmax_t)ret);

	return ret;
}

static inline int
wrapper_pmemfile_seteuid(PMEMfilepool *pfp,
		pmemfile_uid_t uid)
{
	int ret;

	ret = pmemfile_seteuid(pfp,
		uid);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_seteuid(%p, %jx) = %d",
		pfp,
		(uintmax_t)uid,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_setegid(PMEMfilepool *pfp,
		pmemfile_gid_t gid)
{
	int ret;

	ret = pmemfile_setegid(pfp,
		gid);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_setegid(%p, %jx) = %d",
		pfp,
		(uintmax_t)gid,
		ret);

	return ret;
}

static inline pmemfile_uid_t
wrapper_pmemfile_geteuid(PMEMfilepool *pfp)
{
	pmemfile_uid_t ret;

	ret = pmemfile_geteuid(pfp);

	log_write(
	    "pmemfile_geteuid(%p) = %jx",
		pfp,
		(uintmax_t)ret);

	return ret;
}

static inline pmemfile_gid_t
wrapper_pmemfile_getegid(PMEMfilepool *pfp)
{
	pmemfile_gid_t ret;

	ret = pmemfile_getegid(pfp);

	log_write(
	    "pmemfile_getegid(%p) = %jx",
		pfp,
		(uintmax_t)ret);

	return ret;
}

static inline int
wrapper_pmemfile_setfsuid(PMEMfilepool *pfp,
		pmemfile_uid_t fsuid)
{
	int ret;

	ret = pmemfile_setfsuid(pfp,
		fsuid);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_setfsuid(%p, %jx) = %d",
		pfp,
		(uintmax_t)fsuid,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_setfsgid(PMEMfilepool *pfp,
		pmemfile_uid_t fsgid)
{
	int ret;

	ret = pmemfile_setfsgid(pfp,
		fsgid);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_setfsgid(%p, %jx) = %d",
		pfp,
		(uintmax_t)fsgid,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_getgroups(PMEMfilepool *pfp,
		int size,
		pmemfile_gid_t *list)
{
	int ret;

	ret = pmemfile_getgroups(pfp,
		size,
		list);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_getgroups(%p, %d, %p) = %d",
		pfp,
		size,
		list,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_setgroups(PMEMfilepool *pfp,
		size_t size,
		const pmemfile_gid_t *list)
{
	int ret;

	ret = pmemfile_setgroups(pfp,
		size,
		list);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_setgroups(%p, %zu, %p) = %d",
		pfp,
		size,
		list,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_chown(PMEMfilepool *pfp,
		const char *pathname,
		pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	int ret;

	ret = pmemfile_chown(pfp,
		pathname,
		owner,
		group);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_chown(%p, \"%s\", %jx, %jx) = %d",
		pfp,
		pathname,
		(uintmax_t)owner,
		(uintmax_t)group,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_fchown(PMEMfilepool *pfp,
		PMEMfile *file,
		pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	int ret;

	ret = pmemfile_fchown(pfp,
		file,
		owner,
		group);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_fchown(%p, %p, %jx, %jx) = %d",
		pfp,
		file,
		(uintmax_t)owner,
		(uintmax_t)group,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_lchown(PMEMfilepool *pfp,
		const char *pathname,
		pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	int ret;

	ret = pmemfile_lchown(pfp,
		pathname,
		owner,
		group);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_lchown(%p, \"%s\", %jx, %jx) = %d",
		pfp,
		pathname,
		(uintmax_t)owner,
		(uintmax_t)group,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_fchownat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *pathname,
		pmemfile_uid_t owner,
		pmemfile_gid_t group,
		int flags)
{
	int ret;

	ret = pmemfile_fchownat(pfp,
		dir,
		pathname,
		owner,
		group,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_fchownat(%p, %p, \"%s\", %jx, %jx, %d) = %d",
		pfp,
		dir,
		pathname,
		(uintmax_t)owner,
		(uintmax_t)group,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_access(PMEMfilepool *pfp,
		const char *path,
		int mode)
{
	int ret;

	ret = pmemfile_access(pfp,
		path,
		mode);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_access(%p, \"%s\", %d) = %d",
		pfp,
		path,
		mode,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_euidaccess(PMEMfilepool *pfp,
		const char *pathname,
		int mode)
{
	int ret;

	ret = pmemfile_euidaccess(pfp,
		pathname,
		mode);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_euidaccess(%p, \"%s\", %d) = %d",
		pfp,
		pathname,
		mode,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_faccessat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *pathname,
		int mode,
		int flags)
{
	int ret;

	ret = pmemfile_faccessat(pfp,
		dir,
		pathname,
		mode,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_faccessat(%p, %p, \"%s\", %d, %d) = %d",
		pfp,
		dir,
		pathname,
		mode,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_utime(PMEMfilepool *pfp,
		const char *filename,
		const pmemfile_utimbuf_t *times)
{
	int ret;

	ret = pmemfile_utime(pfp,
		filename,
		times);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_utime(%p, %p, %p) = %d",
		pfp,
		filename,
		times,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_utimes(PMEMfilepool *pfp,
		const char *filename,
		const pmemfile_timeval_t *times)
{
	int ret;

	ret = pmemfile_utimes(pfp,
		filename,
		times);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_utimes(%p, %p, %p) = %d",
		pfp,
		filename,
		times,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_futimes(PMEMfilepool *pfp,
		PMEMfile *file,
		const pmemfile_timeval_t *tv)
{
	int ret;

	ret = pmemfile_futimes(pfp,
		file,
		tv);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_futimes(%p, %p, %p) = %d",
		pfp,
		file,
		tv,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_futimesat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *pathname,
		const pmemfile_timeval_t *tv)
{
	int ret;

	ret = pmemfile_futimesat(pfp,
		dir,
		pathname,
		tv);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_futimesat(%p, %p, \"%s\", %p) = %d",
		pfp,
		dir,
		pathname,
		tv,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_lutimes(PMEMfilepool *pfp,
		const char *filename,
		const pmemfile_timeval_t *tv)
{
	int ret;

	ret = pmemfile_lutimes(pfp,
		filename,
		tv);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_lutimes(%p, %p, %p) = %d",
		pfp,
		filename,
		tv,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_utimensat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *pathname,
		const pmemfile_timespec_t *times,
		int flags)
{
	int ret;

	ret = pmemfile_utimensat(pfp,
		dir,
		pathname,
		times,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_utimensat(%p, %p, \"%s\", %p, %d) = %d",
		pfp,
		dir,
		pathname,
		times,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_futimens(PMEMfilepool *pfp,
		PMEMfile *file,
		const pmemfile_timespec_t *times)
{
	int ret;

	ret = pmemfile_futimens(pfp,
		file,
		times);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_futimens(%p, %p, %p) = %d",
		pfp,
		file,
		times,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_setcap(PMEMfilepool *pfp,
		int cap)
{
	int ret;

	ret = pmemfile_setcap(pfp,
		cap);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_setcap(%p, %d) = %d",
		pfp,
		cap,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_clrcap(PMEMfilepool *pfp,
		int cap)
{
	int ret;

	ret = pmemfile_clrcap(pfp,
		cap);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_clrcap(%p, %d) = %d",
		pfp,
		cap,
		ret);

	return ret;
}

static inline void
wrapper_pmemfile_stats(PMEMfilepool *pfp,
		struct pmemfile_stats *stats)
{
	log_write(
	    "pmemfile_stats(%p, %p)",
		pfp,
		stats);

	pmemfile_stats(pfp,
		stats);
}

static inline int
wrapper_pmemfile_statfs(PMEMfilepool *pfp,
		pmemfile_statfs_t *buf)
{
	int ret;

	ret = pmemfile_statfs(pfp,
		buf);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_statfs(%p, %p) = %d",
		pfp,
		buf,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_truncate(PMEMfilepool *pfp,
		const char *path,
		pmemfile_off_t length)
{
	int ret;

	ret = pmemfile_truncate(pfp,
		path,
		length);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_truncate(%p, \"%s\", %jx) = %d",
		pfp,
		path,
		(uintmax_t)length,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_ftruncate(PMEMfilepool *pfp,
		PMEMfile *file,
		pmemfile_off_t length)
{
	int ret;

	ret = pmemfile_ftruncate(pfp,
		file,
		length);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_ftruncate(%p, %p, %jx) = %d",
		pfp,
		file,
		(uintmax_t)length,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_fallocate(PMEMfilepool *pfp,
		PMEMfile *file,
		int mode,
		pmemfile_off_t offset,
		pmemfile_off_t length)
{
	int ret;

	ret = pmemfile_fallocate(pfp,
		file,
		mode,
		offset,
		length);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_fallocate(%p, %p, %d, %jx, %jx) = %d",
		pfp,
		file,
		mode,
		(uintmax_t)offset,
		(uintmax_t)length,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_posix_fallocate(PMEMfilepool *pfp,
		PMEMfile *file,
		pmemfile_off_t offset,
		pmemfile_off_t length)
{
	int ret;

	ret = pmemfile_posix_fallocate(pfp,
		file,
		offset,
		length);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_posix_fallocate(%p, %p, %jx, %jx) = %d",
		pfp,
		file,
		(uintmax_t)offset,
		(uintmax_t)length,
		ret);

	return ret;
}

static inline char *
wrapper_pmemfile_get_dir_path(PMEMfilepool *pfp,
		PMEMfile *dir,
		char *buf,
		size_t size)
{
	char *ret;

	ret = pmemfile_get_dir_path(pfp,
		dir,
		buf,
		size);

	log_write(
	    "pmemfile_get_dir_path(%p, %p, %p, %zu) = %p",
		pfp,
		dir,
		buf,
		size,
		ret);

	return ret;
}

static inline PMEMfile *
wrapper_pmemfile_open_parent(PMEMfilepool *pfp,
		PMEMfile *at,
		char *path,
		size_t path_size,
		int flags)
{
	PMEMfile *ret;

	ret = pmemfile_open_parent(pfp,
		at,
		path,
		path_size,
		flags);

	log_write(
	    "pmemfile_open_parent(%p, %p, %p, %zu, %d) = %p",
		pfp,
		at,
		path,
		path_size,
		flags,
		ret);

	return ret;
}

static inline const char *
wrapper_pmemfile_errormsg(void)
{
	const char *ret;

	ret = pmemfile_errormsg();

	log_write(
	    "pmemfile_errormsg() = %p",
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_pool_resume(PMEMfilepool *pfp,
		const char *pool_path,
		unsigned at_root,
		const char *const *paths,
		int flags)
{
	int ret;

	ret = pmemfile_pool_resume(pfp,
		pool_path,
		at_root,
		paths,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_pool_resume(%p, %p, %u, %p, %d) = %d",
		pfp,
		pool_path,
		at_root,
		paths,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_pool_suspend(PMEMfilepool *pfp,
		unsigned at_root,
		const char *const *paths,
		int flags)
{
	int ret;

	ret = pmemfile_pool_suspend(pfp,
		at_root,
		paths,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_pool_suspend(%p, %u, %p, %d) = %d",
		pfp,
		at_root,
		paths,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_flock(PMEMfilepool *pfp,
		PMEMfile *file,
		int operation)
{
	int ret;

	ret = pmemfile_flock(pfp,
		file,
		operation);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_flock(%p, %p, %d) = %d",
		pfp,
		file,
		operation,
		ret);

	return ret;
}

static inline void *
wrapper_pmemfile_mmap(PMEMfilepool *pfp,
		void *addr,
		size_t len,
		int prot,
		int flags,
		PMEMfile *file,
		pmemfile_off_t off)
{
	void *ret;

	ret = pmemfile_mmap(pfp,
		addr,
		len,
		prot,
		flags,
		file,
		off);

	log_write(
	    "pmemfile_mmap(%p, %p, %zu, %d, %d, %p, %jx) = %p",
		pfp,
		addr,
		len,
		prot,
		flags,
		file,
		(uintmax_t)off,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_munmap(PMEMfilepool *pfp,
		void *addr,
		size_t len)
{
	int ret;

	ret = pmemfile_munmap(pfp,
		addr,
		len);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_munmap(%p, %p, %zu) = %d",
		pfp,
		addr,
		len,
		ret);

	return ret;
}

static inline void *
wrapper_pmemfile_mremap(PMEMfilepool *pfp,
		void *old_addr,
		size_t old_size,
		size_t new_size,
		int flags,
		void *new_addr)
{
	void *ret;

	ret = pmemfile_mremap(pfp,
		old_addr,
		old_size,
		new_size,
		flags,
		new_addr);

	log_write(
	    "pmemfile_mremap(%p, %p, %zu, %zu, %d, %p) = %p",
		pfp,
		old_addr,
		old_size,
		new_size,
		flags,
		new_addr,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_msync(PMEMfilepool *pfp,
		void *addr,
		size_t len,
		int flags)
{
	int ret;

	ret = pmemfile_msync(pfp,
		addr,
		len,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_msync(%p, %p, %zu, %d) = %d",
		pfp,
		addr,
		len,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_mprotect(PMEMfilepool *pfp,
		void *addr,
		size_t len,
		int prot)
{
	int ret;

	ret = pmemfile_mprotect(pfp,
		addr,
		len,
		prot);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_mprotect(%p, %p, %zu, %d) = %d",
		pfp,
		addr,
		len,
		prot,
		ret);

	return ret;
}

static inline pmemfile_ssize_t
wrapper_pmemfile_copy_file_range(PMEMfilepool *pfp,
		PMEMfile *file_in,
		pmemfile_off_t *off_in,
		PMEMfile *file_out,
		pmemfile_off_t *off_out,
		size_t len,
		unsigned flags)
{
	pmemfile_ssize_t ret;

	ret = pmemfile_copy_file_range(pfp,
		file_in,
		off_in,
		file_out,
		off_out,
		len,
		flags);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_copy_file_range(%p, %p, %p, %p, %p, %zu, %u) = %zd",
		pfp,
		file_in,
		off_in,
		file_out,
		off_out,
		len,
		flags,
		ret);

	return ret;
}

static inline int
wrapper_pmemfile_mknodat(PMEMfilepool *pfp,
		PMEMfile *dir,
		const char *path,
		pmemfile_mode_t mode,
		pmemfile_dev_t dev)
{
	int ret;

	ret = pmemfile_mknodat(pfp,
		dir,
		path,
		mode,
		dev);
	if (ret < 0)
		ret = -errno;

	log_write(
	    "pmemfile_mknodat(%p, %p, \"%s\", %3jo, %jx) = %d",
		pfp,
		dir,
		path,
		(uintmax_t)mode,
		(uintmax_t)dev,
		ret);

	return ret;
}

#endif
