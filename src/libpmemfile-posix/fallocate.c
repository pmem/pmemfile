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
 * fallocate.c -- pmemfile_*fallocate implementation
 */

#include <limits.h>

#include "callbacks.h"
#include "data.h"
#include "file.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

static int
vinode_fallocate(PMEMfilepool *pfp, struct pmemfile_vinode *vinode, int mode,
		uint64_t offset, uint64_t length)
{
	struct pmemfile_inode *inode = vinode->inode;

	ASSERT_NOT_IN_TX();
	int error = 0;

	if (!vinode_is_regular_file(vinode))
		return EBADF;

	uint64_t off_plus_len = offset + length;

	if (!(mode & PMEMFILE_FALLOC_FL_PUNCH_HOLE))
		expand_to_full_pages(&offset, &length);

	if (length == 0)
		return 0;

	vinode_snapshot(vinode);

	if (vinode->blocks == NULL) {
		error = vinode_rebuild_block_tree(pfp, vinode);
		if (error)
			return error;
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		size_t allocated_space = inode->allocated_space;

		if (mode & PMEMFILE_FALLOC_FL_PUNCH_HOLE) {
			ASSERT(mode & PMEMFILE_FALLOC_FL_KEEP_SIZE);
			allocated_space -= vinode_remove_interval(pfp, vinode,
				offset, length);
		} else {
			allocated_space += vinode_allocate_interval(pfp, vinode,
				offset, length);
			if ((mode & PMEMFILE_FALLOC_FL_KEEP_SIZE) == 0) {
				if (inode->size < off_plus_len) {
					TX_ADD_DIRECT(&inode->size);
					inode->size = off_plus_len;
				}
			}
		}

		if (inode->allocated_space != allocated_space) {
			TX_ADD_DIRECT(&inode->allocated_space);
			inode->allocated_space = allocated_space;
		}
	} TX_ONABORT {
		error = errno;
		vinode_restore_on_abort(vinode);
	} TX_END

	return error;
}

/*
 * fallocate_check_arguments - part of pmemfile_fallocate implementation
 * Perform some checks that are independent of the file being operated on.
 */
static int
fallocate_check_arguments(int mode, pmemfile_off_t offset,
		pmemfile_off_t length)
{
	/*
	 * from man 2 fallocate:
	 *
	 * "EINVAL - offset was less than 0, or len was less
	 * than or equal to 0."
	 */
	if (length <= 0 || offset < 0)
		return EINVAL;

	/*
	 * from man 2 fallocate:
	 *
	 * "EFBIG - offset+len exceeds the maximum file size."
	 */
	if ((size_t)offset + (size_t)length > (size_t)SSIZE_MAX)
		return EFBIG;

	/*
	 * from man 2 fallocate:
	 *
	 * "EOPNOTSUPP -  The  filesystem containing the file referred to by
	 * fd does not support this operation; or the mode is not supported by
	 * the filesystem containing the file referred to by fd."
	 *
	 * As of now, pmemfile_fallocate supports allocating disk space, and
	 * punching holes.
	 */
	if (mode & PMEMFILE_FALLOC_FL_COLLAPSE_RANGE) {
		ERR("PMEMFILE_FL_COLLAPSE_RANGE is not supported");
		return EOPNOTSUPP;
	}

	if (mode & PMEMFILE_FALLOC_FL_ZERO_RANGE) {
		ERR("PMEMFILE_FL_ZERO_RANGE is not supported");
		return EOPNOTSUPP;
	}

	if (mode & PMEMFILE_FALLOC_FL_INSERT_RANGE) {
		ERR("PMEMFILE_FL_INSERT_RANGE is not supported");
		return EOPNOTSUPP;
	}

	if (mode & PMEMFILE_FALLOC_FL_PUNCH_HOLE) {
		/*
		 * from man 2 fallocate:
		 *
		 * "The FALLOC_FL_PUNCH_HOLE flag must be ORed
		 * with FALLOC_FL_KEEP_SIZE in mode; in other words,
		 * even when punching off the end of the file, the file size
		 * (as reported by stat(2)) does not change."
		 */
		if (mode != (PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				PMEMFILE_FALLOC_FL_KEEP_SIZE))
			return EOPNOTSUPP;
	} else { /* Allocating disk space */
		/*
		 * Note: According to 'man 2 fallocate' FALLOC_FL_UNSHARE
		 * is another possible flag to accept here. No equivalent of
		 * that flag is supported by pmemfile as of now. Also that man
		 * page is wrong anyways, the header files only refer to
		 * FALLOC_FL_UNSHARE_RANGE, so it is suspected that noone is
		 * using it anyways.
		 */
		if ((mode & ~PMEMFILE_FALLOC_FL_KEEP_SIZE) != 0)
			return EINVAL;
	}

	return 0;
}

int
pmemfile_fallocate(PMEMfilepool *pfp, PMEMfile *file, int mode,
		pmemfile_off_t offset, pmemfile_off_t length)
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

	int error;

	error = fallocate_check_arguments(mode, offset, length);
	if (error)
		goto end;

	ASSERT(offset >= 0);
	ASSERT(length > 0);

	os_mutex_lock(&file->mutex);
	uint64_t flags = file->flags;
	struct pmemfile_vinode *vinode = file->vinode;
	os_mutex_unlock(&file->mutex);

	/*
	 * from man 2 fallocate:
	 *
	 * "EBADF  fd is not a valid file descriptor, or is not opened for
	 * writing."
	 */
	if ((flags & PFILE_WRITE) == 0) {
		error = EBADF;
		goto end;
	}

	os_rwlock_wrlock(&vinode->rwlock);

	vinode->data_modification_counter++;
	vinode->metadata_modification_counter++;
	memory_barrier();

	error = vinode_fallocate(pfp, vinode, mode, (uint64_t)offset,
			(uint64_t)length);

	os_rwlock_unlock(&vinode->rwlock);


end:
	if (error != 0) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_posix_fallocate(PMEMfilepool *pfp, PMEMfile *file,
		pmemfile_off_t offset, pmemfile_off_t length)
{
	return pmemfile_fallocate(pfp, file, 0, offset, length);
}
