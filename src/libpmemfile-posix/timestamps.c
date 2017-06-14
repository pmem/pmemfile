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
 * timestamps.c -- pmemfile_*utime* implementation
 */

#include <errno.h>
#include "libpmemfile-posix.h"
#include "out.h"

int
pmemfile_utime(PMEMfilepool *pfp, const char *filename,
		const pmemfile_utimbuf_t *times)
{
	(void) filename;
	(void) times;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_utimes(PMEMfilepool *pfp, const char *filename,
		const pmemfile_timeval_t times[2])
{
	(void) filename;
	(void) times;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_futimes(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_timeval_t tv[2])
{
	(void) file;
	(void) tv;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_lutimes(PMEMfilepool *pfp, const char *filename,
		const pmemfile_timeval_t tv[2])
{
	(void) filename;
	(void) tv;

	errno = ENOTSUP;
	return -1;
}

int
pmemfile_utimensat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		const pmemfile_timespec_t times[2], int flags)
{
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
	(void) file;
	(void) times;

	errno = ENOTSUP;
	return -1;
}
