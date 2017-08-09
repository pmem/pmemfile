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

#ifndef LIBPMEMFILE_POSIX_FD_FIRST_H
#define LIBPMEMFILE_POSIX_FD_FIRST_H

#include "libpmemfile-posix-wrappers.h"

static inline void
fd_first_pmemfile_close(struct fd_association *file)
{
	wrapper_pmemfile_close(file->pool->pool, file->file);
}

static inline pmemfile_ssize_t
fd_first_pmemfile_read(struct fd_association *file,
		long buf,
		long count)
{
	return wrapper_pmemfile_read(file->pool->pool, file->file,
		(void *)buf,
		(size_t)count);
}

static inline pmemfile_ssize_t
fd_first_pmemfile_pread(struct fd_association *file,
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
fd_first_pmemfile_readv(struct fd_association *file,
		long iov,
		long iovcnt)
{
	return wrapper_pmemfile_readv(file->pool->pool, file->file,
		(const pmemfile_iovec_t *)iov,
		(int)iovcnt);
}

static inline pmemfile_ssize_t
fd_first_pmemfile_preadv(struct fd_association *file,
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
fd_first_pmemfile_write(struct fd_association *file,
		long buf,
		long count)
{
	return wrapper_pmemfile_write(file->pool->pool, file->file,
		(const void *)buf,
		(size_t)count);
}

static inline pmemfile_ssize_t
fd_first_pmemfile_pwrite(struct fd_association *file,
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
fd_first_pmemfile_writev(struct fd_association *file,
		long iov,
		long iovcnt)
{
	return wrapper_pmemfile_writev(file->pool->pool, file->file,
		(const pmemfile_iovec_t *)iov,
		(int)iovcnt);
}

static inline pmemfile_ssize_t
fd_first_pmemfile_pwritev(struct fd_association *file,
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
fd_first_pmemfile_lseek(struct fd_association *file,
		long offset,
		long whence)
{
	return wrapper_pmemfile_lseek(file->pool->pool, file->file,
		(pmemfile_off_t)offset,
		(int)whence);
}

static inline int
fd_first_pmemfile_fstat(struct fd_association *file,
		long buf)
{
	return wrapper_pmemfile_fstat(file->pool->pool, file->file,
		(pmemfile_stat_t *)buf);
}

static inline int
fd_first_pmemfile_getdents(struct fd_association *file,
		long dirp,
		long count)
{
	return wrapper_pmemfile_getdents(file->pool->pool, file->file,
		(struct linux_dirent *)dirp,
		(unsigned)count);
}

static inline int
fd_first_pmemfile_getdents64(struct fd_association *file,
		long dirp,
		long count)
{
	return wrapper_pmemfile_getdents64(file->pool->pool, file->file,
		(struct linux_dirent64 *)dirp,
		(unsigned)count);
}

static inline int
fd_first_pmemfile_fchmod(struct fd_association *file,
		long mode)
{
	return wrapper_pmemfile_fchmod(file->pool->pool, file->file,
		(pmemfile_mode_t)mode);
}

static inline int
fd_first_pmemfile_fchown(struct fd_association *file,
		long owner,
		long group)
{
	return wrapper_pmemfile_fchown(file->pool->pool, file->file,
		(pmemfile_uid_t)owner,
		(pmemfile_gid_t)group);
}

static inline int
fd_first_pmemfile_ftruncate(struct fd_association *file,
		long length)
{
	return wrapper_pmemfile_ftruncate(file->pool->pool, file->file,
		(pmemfile_off_t)length);
}

static inline int
fd_first_pmemfile_fallocate(struct fd_association *file,
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
fd_first_pmemfile_flock(struct fd_association *file,
		long operation)
{
	return wrapper_pmemfile_flock(file->pool->pool, file->file,
		(int)operation);
}

#endif
