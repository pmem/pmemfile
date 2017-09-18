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
 * fcntl.c -- pmemfile_fcntl implementation
 */

#include "file.h"
#include "libpmemfile-posix.h"
#include "out.h"

int
pmemfile_fcntl(PMEMfilepool *pfp, PMEMfile *file, int cmd, ...)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}


	switch (cmd) {
		case PMEMFILE_F_SETLK:
			if (file->flags & PFILE_PATH) {
				errno = EBADF;
				return -1;
			}

			/* XXX */
			return 0;
		case PMEMFILE_F_GETFL:
		{
			if (file->flags & PFILE_PATH)
				return PMEMFILE_O_PATH;

			int ret = 0;
			ret |= PMEMFILE_O_LARGEFILE;
			if (file->flags & PFILE_APPEND)
				ret |= PMEMFILE_O_APPEND;
			if (file->flags & PFILE_NOATIME)
				ret |= PMEMFILE_O_NOATIME;

			if ((file->flags & (PFILE_READ | PFILE_WRITE)) ==
					(PFILE_READ | PFILE_WRITE))
				ret |= PMEMFILE_O_RDWR;
			else if ((file->flags & PFILE_READ) == PFILE_READ)
				ret |= PMEMFILE_O_RDONLY;
			else if ((file->flags & PFILE_WRITE) == PFILE_WRITE)
				ret |= PMEMFILE_O_WRONLY;

			return ret;
		}
		case PMEMFILE_F_GETFD:
			return PMEMFILE_FD_CLOEXEC;
		case PMEMFILE_F_SETFD:
		{
			va_list ap;
			va_start(ap, cmd);
			int fd_flags = va_arg(ap, int);
			va_end(ap);

			if (fd_flags & PMEMFILE_FD_CLOEXEC) {
				fd_flags &= ~PMEMFILE_FD_CLOEXEC;
			} else {
				ERR("clearing FD_CLOEXEC isn't supported");
				errno = EINVAL;
				return -1;
			}


			if (fd_flags) {
				ERR("flag %d not supported", fd_flags);
				errno = EINVAL;
				return -1;
			}

			return 0;
		}
	}

	errno = ENOTSUP;
	return -1;
}
