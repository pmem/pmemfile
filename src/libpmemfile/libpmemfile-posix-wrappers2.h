/* Generated source file, do not edit manually! */

#ifndef LIBPMEMFILE_POSIX_WRAPPERS2_H
#define LIBPMEMFILE_POSIX_WRAPPERS2_H

#include "libpmemfile-posix-wrappers.h"

static inline void
cast_wrapper_pmemfile_close(struct fd_association *file)
{
	wrapper_pmemfile_close(file->pool->pool, file->file);
}

static inline pmemfile_ssize_t
cast_wrapper_pmemfile_read(struct fd_association *file,
		long buf,
		long count)
{
	return wrapper_pmemfile_read(file->pool->pool, file->file,
		(void *)buf,
		(size_t)count);
}

static inline pmemfile_ssize_t
cast_wrapper_pmemfile_pread(struct fd_association *file,
		long buf,
		long count,
		long offset)
{
	return wrapper_pmemfile_pread(file->pool->pool, file->file,
		(void *)buf,
		(size_t)count,
		(pmemfile_off_t)offset);
}

static inline pmemfile_ssize_t
cast_wrapper_pmemfile_readv(struct fd_association *file,
		long iov,
		long iovcnt)
{
	return wrapper_pmemfile_readv(file->pool->pool, file->file,
		(const pmemfile_iovec_t *)iov,
		(int)iovcnt);
}

static inline pmemfile_ssize_t
cast_wrapper_pmemfile_preadv(struct fd_association *file,
		long iov,
		long iovcnt,
		long offset)
{
	return wrapper_pmemfile_preadv(file->pool->pool, file->file,
		(const pmemfile_iovec_t *)iov,
		(int)iovcnt,
		(pmemfile_off_t)offset);
}

static inline pmemfile_ssize_t
cast_wrapper_pmemfile_write(struct fd_association *file,
		long buf,
		long count)
{
	return wrapper_pmemfile_write(file->pool->pool, file->file,
		(const void *)buf,
		(size_t)count);
}

static inline pmemfile_ssize_t
cast_wrapper_pmemfile_pwrite(struct fd_association *file,
		long buf,
		long count,
		long offset)
{
	return wrapper_pmemfile_pwrite(file->pool->pool, file->file,
		(const void *)buf,
		(size_t)count,
		(pmemfile_off_t)offset);
}

static inline pmemfile_ssize_t
cast_wrapper_pmemfile_writev(struct fd_association *file,
		long iov,
		long iovcnt)
{
	return wrapper_pmemfile_writev(file->pool->pool, file->file,
		(const pmemfile_iovec_t *)iov,
		(int)iovcnt);
}

static inline pmemfile_ssize_t
cast_wrapper_pmemfile_pwritev(struct fd_association *file,
		long iov,
		long iovcnt,
		long offset)
{
	return wrapper_pmemfile_pwritev(file->pool->pool, file->file,
		(const pmemfile_iovec_t *)iov,
		(int)iovcnt,
		(pmemfile_off_t)offset);
}

static inline pmemfile_off_t
cast_wrapper_pmemfile_lseek(struct fd_association *file,
		long offset,
		long whence)
{
	return wrapper_pmemfile_lseek(file->pool->pool, file->file,
		(pmemfile_off_t)offset,
		(int)whence);
}

static inline int
cast_wrapper_pmemfile_fstat(struct fd_association *file,
		long buf)
{
	return wrapper_pmemfile_fstat(file->pool->pool, file->file,
		(pmemfile_stat_t *)buf);
}

static inline int
cast_wrapper_pmemfile_getdents(struct fd_association *file,
		long dirp,
		long count)
{
	return wrapper_pmemfile_getdents(file->pool->pool, file->file,
		(struct linux_dirent *)dirp,
		(unsigned)count);
}

static inline int
cast_wrapper_pmemfile_getdents64(struct fd_association *file,
		long dirp,
		long count)
{
	return wrapper_pmemfile_getdents64(file->pool->pool, file->file,
		(struct linux_dirent64 *)dirp,
		(unsigned)count);
}

static inline int
cast_wrapper_pmemfile_fchmod(struct fd_association *file,
		long mode)
{
	return wrapper_pmemfile_fchmod(file->pool->pool, file->file,
		(pmemfile_mode_t)mode);
}

static inline int
cast_wrapper_pmemfile_fchown(struct fd_association *file,
		long owner,
		long group)
{
	return wrapper_pmemfile_fchown(file->pool->pool, file->file,
		(pmemfile_uid_t)owner,
		(pmemfile_gid_t)group);
}

static inline int
cast_wrapper_pmemfile_ftruncate(struct fd_association *file,
		long length)
{
	return wrapper_pmemfile_ftruncate(file->pool->pool, file->file,
		(pmemfile_off_t)length);
}

static inline int
cast_wrapper_pmemfile_fallocate(struct fd_association *file,
		long mode,
		long offset,
		long length)
{
	return wrapper_pmemfile_fallocate(file->pool->pool, file->file,
		(int)mode,
		(pmemfile_off_t)offset,
		(pmemfile_off_t)length);
}

static inline int
cast_wrapper_pmemfile_flock(struct fd_association *file,
		long operation)
{
	return wrapper_pmemfile_flock(file->pool->pool, file->file,
		(int)operation);
}


#endif
