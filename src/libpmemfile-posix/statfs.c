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

/*
 * statfs.c -- pmemfile_statfs implementation
 */

#include "blocks.h"
#include "libpmemfile-posix.h"
#include "layout.h"

/*
 * pmemfile_statfs
 */
int
pmemfile_statfs(PMEMfilepool *pfp, pmemfile_statfs_t *buf)
{
	if (!pfp || !buf) {
		errno = -EFAULT;
		return -1;
	}

	memset(buf, 0, sizeof(*buf));

	buf->f_type = PMEMFILE_SUPER_VERSION(0, 0);
	buf->f_namelen = PMEMFILE_MAX_FILE_NAME;
	buf->f_frsize = PMEMFILE_PATH_MAX;
	buf->f_bsize = MIN_BLOCK_SIZE;
	buf->f_flags = PMEMFILE_ST_NODEV | PMEMFILE_ST_NOEXEC |
			PMEMFILE_ST_RELATIME | PMEMFILE_ST_SYNCHRONOUS;

	/*
	 * There's no way to get this info out of pmemobj.
	 * https://github.com/pmem/issues/issues/658.
	 * df command filters out 0-sized filesystem, so we have to lie :(.
	 */
	buf->f_blocks = 1;

	return 0;
}
