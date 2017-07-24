/* Generated source file, do not edit manually! */

#ifndef LIBPMEMFILE_POSIX_WRAPPERS_H
#define LIBPMEMFILE_POSIX_WRAPPERS_H

#include "libpmemfile-posix.h"
#include "preload.h"
#include <stdint.h>

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
		(uintmax_t)mode, (void *)ret);


	return ret;
}

static inline PMEMfilepool *
wrapper_pmemfile_pool_open(const char *pathname)
{
	PMEMfilepool *ret;

	ret = pmemfile_pool_open(pathname);

	log_write(
	    "pmemfile_pool_open(\"%s\") = %p",
		pathname, (void *)ret);


	return ret;
}

static inline void
wrapper_pmemfile_pool_close(PMEMfilepool *pfp)
{
	pmemfile_pool_close(pfp);

	log_write(
	    "pmemfile_pool_close(%p)",
		(void *)pfp);

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
		(void *)pfp,
		pathname,
		(uintmax_t)mode, (void *)ret);


	return ret;
}

static inline void
wrapper_pmemfile_close(PMEMfilepool *pfp,
		PMEMfile *file)
{
	pmemfile_close(pfp,
		file);

	log_write(
	    "pmemfile_close(%p, %p)",
		(void *)pfp,
		(void *)file);

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
	    "pmemfile_link(%p, \"%s\", \"%s\") = %jd",
		(void *)pfp,
		oldpath,
		newpath, (intmax_t)ret);


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
	    "pmemfile_linkat(%p, %p, \"%s\", %p, \"%s\", %jd) = %jd",
		(void *)pfp,
		(void *)olddir,
		oldpath,
		(void *)newdir,
		newpath,
		(intmax_t)flags, (intmax_t)ret);


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
	    "pmemfile_unlink(%p, \"%s\") = %jd",
		(void *)pfp,
		pathname, (intmax_t)ret);


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
	    "pmemfile_unlinkat(%p, %p, \"%s\", %jd) = %jd",
		(void *)pfp,
		(void *)dir,
		pathname,
		(intmax_t)flags, (intmax_t)ret);


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
	    "pmemfile_rename(%p, \"%s\", \"%s\") = %jd",
		(void *)pfp,
		old_path,
		new_path, (intmax_t)ret);


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
	    "pmemfile_renameat(%p, %p, \"%s\", %p, \"%s\") = %jd",
		(void *)pfp,
		(void *)old_at,
		old_path,
		(void *)new_at,
		new_path, (intmax_t)ret);


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
	    "pmemfile_renameat2(%p, %p, \"%s\", %p, \"%s\", %jx) = %jd",
		(void *)pfp,
		(void *)old_at,
		old_path,
		(void *)new_at,
		new_path,
		(uintmax_t)flags, (intmax_t)ret);


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
		(void *)pfp,
		(void *)file,
		(void *)buf,
		count, ret);


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
		(void *)pfp,
		(void *)file,
		(void *)buf,
		count,
		(uintmax_t)offset, ret);


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
	    "pmemfile_readv(%p, %p, %p, %jd) = %zd",
		(void *)pfp,
		(void *)file,
		(const void *)iov,
		(intmax_t)iovcnt, ret);


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
	    "pmemfile_preadv(%p, %p, %p, %jd, %jx) = %zd",
		(void *)pfp,
		(void *)file,
		(const void *)iov,
		(intmax_t)iovcnt,
		(uintmax_t)offset, ret);


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
		(void *)pfp,
		(void *)file,
		(const void *)buf,
		count, ret);


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
		(void *)pfp,
		(void *)file,
		(const void *)buf,
		count,
		(uintmax_t)offset, ret);


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
	    "pmemfile_writev(%p, %p, %p, %jd) = %zd",
		(void *)pfp,
		(void *)file,
		(const void *)iov,
		(intmax_t)iovcnt, ret);


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
	    "pmemfile_pwritev(%p, %p, %p, %jd, %jx) = %zd",
		(void *)pfp,
		(void *)file,
		(const void *)iov,
		(intmax_t)iovcnt,
		(uintmax_t)offset, ret);


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
	    "pmemfile_lseek(%p, %p, %jx, %jd) = %jx",
		(void *)pfp,
		(void *)file,
		(uintmax_t)offset,
		(intmax_t)whence, (uintmax_t)ret);


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
	    "pmemfile_stat(%p, \"%s\", %p) = %jd",
		(void *)pfp,
		path,
		(void *)buf, (intmax_t)ret);


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
	    "pmemfile_lstat(%p, \"%s\", %p) = %jd",
		(void *)pfp,
		path,
		(void *)buf, (intmax_t)ret);


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
	    "pmemfile_fstat(%p, %p, %p) = %jd",
		(void *)pfp,
		(void *)file,
		(void *)buf, (intmax_t)ret);


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
	    "pmemfile_fstatat(%p, %p, \"%s\", %p, %jd) = %jd",
		(void *)pfp,
		(void *)dir,
		path,
		(void *)buf,
		(intmax_t)flags, (intmax_t)ret);


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
	    "pmemfile_getdents(%p, %p, %p, %jx) = %jd",
		(void *)pfp,
		(void *)file,
		(void *)dirp,
		(uintmax_t)count, (intmax_t)ret);


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
	    "pmemfile_getdents64(%p, %p, %p, %jx) = %jd",
		(void *)pfp,
		(void *)file,
		(void *)dirp,
		(uintmax_t)count, (intmax_t)ret);


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
	    "pmemfile_mkdir(%p, \"%s\", %3jo) = %jd",
		(void *)pfp,
		path,
		(uintmax_t)mode, (intmax_t)ret);


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
	    "pmemfile_mkdirat(%p, %p, \"%s\", %3jo) = %jd",
		(void *)pfp,
		(void *)dir,
		path,
		(uintmax_t)mode, (intmax_t)ret);


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
	    "pmemfile_rmdir(%p, \"%s\") = %jd",
		(void *)pfp,
		path, (intmax_t)ret);


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
	    "pmemfile_chdir(%p, \"%s\") = %jd",
		(void *)pfp,
		path, (intmax_t)ret);


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
	    "pmemfile_fchdir(%p, %p) = %jd",
		(void *)pfp,
		(void *)dir, (intmax_t)ret);


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
		(void *)pfp,
		(void *)buf,
		size, (void *)ret);


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
		(void *)pfp,
		(uintmax_t)mask, (uintmax_t)ret);


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
	    "pmemfile_symlink(%p, %p, %p) = %jd",
		(void *)pfp,
		(const void *)path1,
		(const void *)path2, (intmax_t)ret);


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
	    "pmemfile_symlinkat(%p, %p, %p, %p) = %jd",
		(void *)pfp,
		(const void *)path1,
		(void *)at,
		(const void *)path2, (intmax_t)ret);


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
		(void *)pfp,
		path,
		(void *)buf,
		buf_len, ret);


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
		(void *)pfp,
		(void *)dir,
		pathname,
		(void *)buf,
		bufsiz, ret);


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
	    "pmemfile_chmod(%p, \"%s\", %3jo) = %jd",
		(void *)pfp,
		path,
		(uintmax_t)mode, (intmax_t)ret);


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
	    "pmemfile_fchmod(%p, %p, %3jo) = %jd",
		(void *)pfp,
		(void *)file,
		(uintmax_t)mode, (intmax_t)ret);


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
	    "pmemfile_fchmodat(%p, %p, \"%s\", %3jo, %jd) = %jd",
		(void *)pfp,
		(void *)dir,
		pathname,
		(uintmax_t)mode,
		(intmax_t)flags, (intmax_t)ret);


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
	    "pmemfile_setreuid(%p, %jx, %jx) = %jd",
		(void *)pfp,
		(uintmax_t)ruid,
		(uintmax_t)euid, (intmax_t)ret);


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
	    "pmemfile_setregid(%p, %jx, %jx) = %jd",
		(void *)pfp,
		(uintmax_t)rgid,
		(uintmax_t)egid, (intmax_t)ret);


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
	    "pmemfile_setuid(%p, %jx) = %jd",
		(void *)pfp,
		(uintmax_t)uid, (intmax_t)ret);


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
	    "pmemfile_setgid(%p, %jx) = %jd",
		(void *)pfp,
		(uintmax_t)gid, (intmax_t)ret);


	return ret;
}

static inline pmemfile_uid_t
wrapper_pmemfile_getuid(PMEMfilepool *pfp)
{
	pmemfile_uid_t ret;

	ret = pmemfile_getuid(pfp);

	log_write(
	    "pmemfile_getuid(%p) = %jx",
		(void *)pfp, (uintmax_t)ret);


	return ret;
}

static inline pmemfile_gid_t
wrapper_pmemfile_getgid(PMEMfilepool *pfp)
{
	pmemfile_gid_t ret;

	ret = pmemfile_getgid(pfp);

	log_write(
	    "pmemfile_getgid(%p) = %jx",
		(void *)pfp, (uintmax_t)ret);


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
	    "pmemfile_seteuid(%p, %jx) = %jd",
		(void *)pfp,
		(uintmax_t)uid, (intmax_t)ret);


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
	    "pmemfile_setegid(%p, %jx) = %jd",
		(void *)pfp,
		(uintmax_t)gid, (intmax_t)ret);


	return ret;
}

static inline pmemfile_uid_t
wrapper_pmemfile_geteuid(PMEMfilepool *pfp)
{
	pmemfile_uid_t ret;

	ret = pmemfile_geteuid(pfp);

	log_write(
	    "pmemfile_geteuid(%p) = %jx",
		(void *)pfp, (uintmax_t)ret);


	return ret;
}

static inline pmemfile_gid_t
wrapper_pmemfile_getegid(PMEMfilepool *pfp)
{
	pmemfile_gid_t ret;

	ret = pmemfile_getegid(pfp);

	log_write(
	    "pmemfile_getegid(%p) = %jx",
		(void *)pfp, (uintmax_t)ret);


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
	    "pmemfile_setfsuid(%p, %jx) = %jd",
		(void *)pfp,
		(uintmax_t)fsuid, (intmax_t)ret);


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
	    "pmemfile_setfsgid(%p, %jx) = %jd",
		(void *)pfp,
		(uintmax_t)fsgid, (intmax_t)ret);


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
	    "pmemfile_getgroups(%p, %jd, %p) = %jd",
		(void *)pfp,
		(intmax_t)size,
		(void *)list, (intmax_t)ret);


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
	    "pmemfile_setgroups(%p, %zu, %p) = %jd",
		(void *)pfp,
		size,
		(const void *)list, (intmax_t)ret);


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
	    "pmemfile_chown(%p, \"%s\", %jx, %jx) = %jd",
		(void *)pfp,
		pathname,
		(uintmax_t)owner,
		(uintmax_t)group, (intmax_t)ret);


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
	    "pmemfile_fchown(%p, %p, %jx, %jx) = %jd",
		(void *)pfp,
		(void *)file,
		(uintmax_t)owner,
		(uintmax_t)group, (intmax_t)ret);


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
	    "pmemfile_lchown(%p, \"%s\", %jx, %jx) = %jd",
		(void *)pfp,
		pathname,
		(uintmax_t)owner,
		(uintmax_t)group, (intmax_t)ret);


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
	    "pmemfile_fchownat(%p, %p, \"%s\", %jx, %jx, %jd) = %jd",
		(void *)pfp,
		(void *)dir,
		pathname,
		(uintmax_t)owner,
		(uintmax_t)group,
		(intmax_t)flags, (intmax_t)ret);


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
	    "pmemfile_access(%p, \"%s\", %jd) = %jd",
		(void *)pfp,
		path,
		(intmax_t)mode, (intmax_t)ret);


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
	    "pmemfile_euidaccess(%p, \"%s\", %jd) = %jd",
		(void *)pfp,
		pathname,
		(intmax_t)mode, (intmax_t)ret);


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
	    "pmemfile_faccessat(%p, %p, \"%s\", %jd, %jd) = %jd",
		(void *)pfp,
		(void *)dir,
		pathname,
		(intmax_t)mode,
		(intmax_t)flags, (intmax_t)ret);


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
	    "pmemfile_utime(%p, %p, %p) = %jd",
		(void *)pfp,
		(const void *)filename,
		(const void *)times, (intmax_t)ret);


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
	    "pmemfile_utimes(%p, %p, %p) = %jd",
		(void *)pfp,
		(const void *)filename,
		(const void *)times, (intmax_t)ret);


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
	    "pmemfile_futimes(%p, %p, %p) = %jd",
		(void *)pfp,
		(void *)file,
		(const void *)tv, (intmax_t)ret);


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
	    "pmemfile_futimesat(%p, %p, \"%s\", %p) = %jd",
		(void *)pfp,
		(void *)dir,
		pathname,
		(const void *)tv, (intmax_t)ret);


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
	    "pmemfile_lutimes(%p, %p, %p) = %jd",
		(void *)pfp,
		(const void *)filename,
		(const void *)tv, (intmax_t)ret);


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
	    "pmemfile_utimensat(%p, %p, \"%s\", %p, %jd) = %jd",
		(void *)pfp,
		(void *)dir,
		pathname,
		(const void *)times,
		(intmax_t)flags, (intmax_t)ret);


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
	    "pmemfile_futimens(%p, %p, %p) = %jd",
		(void *)pfp,
		(void *)file,
		(const void *)times, (intmax_t)ret);


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
	    "pmemfile_setcap(%p, %jd) = %jd",
		(void *)pfp,
		(intmax_t)cap, (intmax_t)ret);


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
	    "pmemfile_clrcap(%p, %jd) = %jd",
		(void *)pfp,
		(intmax_t)cap, (intmax_t)ret);


	return ret;
}

static inline void
wrapper_pmemfile_stats(PMEMfilepool *pfp,
		struct pmemfile_stats *stats)
{
	pmemfile_stats(pfp,
		stats);

	log_write(
	    "pmemfile_stats(%p, %p)",
		(void *)pfp,
		(void *)stats);

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
	    "pmemfile_truncate(%p, \"%s\", %jx) = %jd",
		(void *)pfp,
		path,
		(uintmax_t)length, (intmax_t)ret);


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
	    "pmemfile_ftruncate(%p, %p, %jx) = %jd",
		(void *)pfp,
		(void *)file,
		(uintmax_t)length, (intmax_t)ret);


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
	    "pmemfile_fallocate(%p, %p, %jd, %jx, %jx) = %jd",
		(void *)pfp,
		(void *)file,
		(intmax_t)mode,
		(uintmax_t)offset,
		(uintmax_t)length, (intmax_t)ret);


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
	    "pmemfile_posix_fallocate(%p, %p, %jx, %jx) = %jd",
		(void *)pfp,
		(void *)file,
		(uintmax_t)offset,
		(uintmax_t)length, (intmax_t)ret);


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
		(void *)pfp,
		(void *)dir,
		(void *)buf,
		size, (void *)ret);


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
	    "pmemfile_open_parent(%p, %p, %p, %zu, %jd) = %p",
		(void *)pfp,
		(void *)at,
		(void *)path,
		path_size,
		(intmax_t)flags, (void *)ret);


	return ret;
}

static inline const char *
wrapper_pmemfile_errormsg(void)
{
	const char *ret;

	ret = pmemfile_errormsg();

	log_write(
	    "pmemfile_errormsg() = %p", (const void *)ret);


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
	    "pmemfile_flock(%p, %p, %jd) = %jd",
		(void *)pfp,
		(void *)file,
		(intmax_t)operation, (intmax_t)ret);


	return ret;
}

static inline PMEMfile *
wrapper_pmemfile_dup(PMEMfilepool *pfp,
		PMEMfile *file)
{
	PMEMfile *ret;

	ret = pmemfile_dup(pfp,
		file);

	log_write(
	    "pmemfile_dup(%p, %p) = %p",
		(void *)pfp,
		(void *)file, (void *)ret);


	return ret;
}

static inline PMEMfile *
wrapper_pmemfile_dup2(PMEMfilepool *pfp,
		PMEMfile *file,
		PMEMfile *file2)
{
	PMEMfile *ret;

	ret = pmemfile_dup2(pfp,
		file,
		file2);

	log_write(
	    "pmemfile_dup2(%p, %p, %p) = %p",
		(void *)pfp,
		(void *)file,
		(void *)file2, (void *)ret);


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
	    "pmemfile_mmap(%p, %p, %zu, %jd, %jd, %p, %jx) = %p",
		(void *)pfp,
		(void *)addr,
		len,
		(intmax_t)prot,
		(intmax_t)flags,
		(void *)file,
		(uintmax_t)off, (void *)ret);


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
	    "pmemfile_munmap(%p, %p, %zu) = %jd",
		(void *)pfp,
		(void *)addr,
		len, (intmax_t)ret);


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
	    "pmemfile_mremap(%p, %p, %zu, %zu, %jd, %p) = %p",
		(void *)pfp,
		(void *)old_addr,
		old_size,
		new_size,
		(intmax_t)flags,
		(void *)new_addr, (void *)ret);


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
	    "pmemfile_msync(%p, %p, %zu, %jd) = %jd",
		(void *)pfp,
		(void *)addr,
		len,
		(intmax_t)flags, (intmax_t)ret);


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
	    "pmemfile_mprotect(%p, %p, %zu, %jd) = %jd",
		(void *)pfp,
		(void *)addr,
		len,
		(intmax_t)prot, (intmax_t)ret);


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
	    "pmemfile_copy_file_range(%p, %p, %p, %p, %p, %zu, %jx) = %zd",
		(void *)pfp,
		(void *)file_in,
		(void *)off_in,
		(void *)file_out,
		(void *)off_out,
		len,
		(uintmax_t)flags, ret);


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
	    "pmemfile_mknodat(%p, %p, \"%s\", %3jo, %jx) = %jd",
		(void *)pfp,
		(void *)dir,
		path,
		(uintmax_t)mode,
		(uintmax_t)dev, (intmax_t)ret);


	return ret;
}


#endif
