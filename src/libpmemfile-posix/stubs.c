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
 * stubs.c -- placeholder routines for the functionality of
 * pmemfile that is not yet implemented.
 * All these set errno to ENOTSUP, except for pmemfile_getcwd.
 * Because pmemfile_getcwd is cool.
 */

#include <stddef.h>
#include <errno.h>
#include <stdlib.h>

#include "libpmemfile-posix.h"

static void
check_pfp(PMEMfilepool *pfp)
{
	if (pfp == NULL)
		abort();
}

static void
check_pfp_file(PMEMfilepool *pfp, PMEMfile *file)
{
	check_pfp(pfp);

	/* XXX: check that the PMEMfile* belongs to the pool */
	if (file == NULL)
		abort();
}

int
pmemfile_flock(PMEMfilepool *pfp, PMEMfile *file, int operation)
{
	check_pfp_file(pfp, file);

	(void) operation;

	errno = ENOTSUP;
	return -1;
}

PMEMfile *
pmemfile_dup(PMEMfilepool *pfp, PMEMfile *file)
{
	check_pfp_file(pfp, file);

	errno = ENOTSUP;
	return NULL;
}

PMEMfile *
pmemfile_dup2(PMEMfilepool *pfp, PMEMfile *file, PMEMfile *file2)
{
	check_pfp_file(pfp, file);
	check_pfp_file(pfp, file2);

	errno = ENOTSUP;
	return NULL;
}

void *
pmemfile_mmap(PMEMfilepool *pfp, void *addr, size_t len,
		int prot, int flags, PMEMfile *file, pmemfile_off_t off)
{
	check_pfp_file(pfp, file);

	(void) addr;
	(void) len;
	(void) prot;
	(void) flags;
	(void) off;

	errno = ENOTSUP;
	return PMEMFILE_MAP_FAILED;
}

int
pmemfile_munmap(PMEMfilepool *pfp, void *addr, size_t len)
{
	check_pfp(pfp);

	(void) addr;
	(void) len;

	errno = ENOTSUP;
	return -1;
}

void *
pmemfile_mremap(PMEMfilepool *pfp, void *old_addr, size_t old_size,
			size_t new_size, int flags, void *new_addr)
{
	check_pfp(pfp);

	(void) old_addr;
	(void) new_addr;
	(void) old_size;
	(void) new_size;
	(void) flags;

	errno = ENOTSUP;
	return PMEMFILE_MAP_FAILED;
}

int
pmemfile_msync(PMEMfilepool *pfp, void *addr, size_t len, int flags)
{
	check_pfp(pfp);

	(void) addr;
	(void) len;
	(void) flags;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_mprotect(PMEMfilepool *pfp, void *addr, size_t len, int prot)
{
	check_pfp(pfp);

	(void) addr;
	(void) len;
	(void) prot;

	errno = ENOTSUP;
	return -1;
}

pmemfile_ssize_t
pmemfile_readv(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_iovec_t *iov, int iovcnt)
{
	check_pfp(pfp);

	(void) file;
	(void) iov;
	(void) iovcnt;

	errno = ENOTSUP;
	return -1;
}

pmemfile_ssize_t
pmemfile_writev(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_iovec_t *iov, int iovcnt)
{
	check_pfp(pfp);

	(void) file;
	(void) iov;
	(void) iovcnt;

	errno = ENOTSUP;
	return -1;
}

pmemfile_ssize_t
pmemfile_preadv(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_iovec_t *iov, int iovcnt,
		pmemfile_off_t offset)
{
	check_pfp(pfp);

	(void) file;
	(void) iov;
	(void) iovcnt;
	(void) offset;

	errno = ENOTSUP;
	return -1;
}

pmemfile_ssize_t
pmemfile_pwritev(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_iovec_t *iov, int iovcnt,
		pmemfile_off_t offset)
{
	check_pfp(pfp);

	(void) file;
	(void) iov;
	(void) iovcnt;
	(void) offset;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_utime(PMEMfilepool *pfp, const char *filename,
		const pmemfile_utimbuf_t *times)
{
	check_pfp(pfp);

	(void) filename;
	(void) times;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_utimes(PMEMfilepool *pfp, const char *filename,
		const pmemfile_timeval_t times[2])
{
	check_pfp(pfp);

	(void) filename;
	(void) times;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_futimes(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_timeval_t tv[2])
{
	check_pfp(pfp);

	(void) file;
	(void) tv;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_lutimes(PMEMfilepool *pfp, const char *filename,
		const pmemfile_timeval_t tv[2])
{
	check_pfp(pfp);

	(void) filename;
	(void) tv;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_utimensat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		const pmemfile_timespec_t times[2], int flags)
{
	check_pfp(pfp);

	(void) dir;
	(void) pathname;
	(void) times;
	(void) flags;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_futimens(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_timespec_t times[2])
{
	check_pfp(pfp);

	(void) file;
	(void) times;

	errno = ENOTSUP;
	return -1;
}

pmemfile_mode_t
pmemfile_umask(PMEMfilepool *pfp, pmemfile_mode_t mask)
{
	check_pfp(pfp);

	(void) mask;

	return 0;
}

pmemfile_ssize_t
pmemfile_copy_file_range(PMEMfilepool *pfp,
		PMEMfile *file_in, pmemfile_off_t *off_in,
		PMEMfile *file_out, pmemfile_off_t *off_out,
		size_t len, unsigned flags)
{
	check_pfp(pfp);

	(void) file_in;
	(void) off_in;
	(void) file_out;
	(void) off_out;
	(void) len;
	(void) flags;

	errno = ENOTSUP;
	return -1;
}
