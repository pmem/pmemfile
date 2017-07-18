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
 * lseek.c -- pmemfile_lseek implementation
 */

#include "data.h"
#include "file.h"
#include "inode.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "utils.h"

/*
 * lseek_seek_data -- part of the lseek implementation
 * Looks for data (not a hole), starting at the specified offset.
 */
static pmemfile_off_t
lseek_seek_data(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		pmemfile_off_t offset, pmemfile_off_t fsize)
{
	if (vinode->blocks == NULL) {
		int err = vinode_rebuild_block_tree(pfp, vinode);
		if (err)
			return err;
	}

	struct pmemfile_block_desc *block =
			find_closest_block(vinode, (uint64_t)offset);
	if (block == NULL) {
		/* offset is before the first block */
		if (vinode->first_block == NULL)
			return fsize; /* No data in the whole file */
		else
			return (pmemfile_off_t)vinode->first_block->offset;
	}

	if (is_offset_in_block(block, (uint64_t)offset))
		return offset;

	block = PF_RW(pfp, block->next);

	if (block == NULL)
		return fsize; /* No more data in file */

	return (pmemfile_off_t)block->offset;
}

/*
 * lseek_seek_hole -- part of the lseek implementation
 * Looks for a hole, starting at the specified offset.
 */
static pmemfile_off_t
lseek_seek_hole(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		pmemfile_off_t offset, pmemfile_off_t fsize)
{
	if (vinode->blocks == NULL) {
		int err = vinode_rebuild_block_tree(pfp, vinode);
		if (err)
			return err;
	}

	struct pmemfile_block_desc *block =
			find_closest_block(vinode, (uint64_t)offset);

	while (block != NULL && offset < fsize) {
		pmemfile_off_t block_end =
				(pmemfile_off_t)block->offset + block->size;

		struct pmemfile_block_desc *next = PF_RW(pfp, block->next);

		if (block_end >= offset)
			offset = block_end; /* seek to the end of block */

		if (next == NULL)
			break; /* the rest of the file is treated as a hole */
		else if (offset < (pmemfile_off_t)next->offset)
			break; /* offset is in a hole between two blocks */

		block = next;
	}

	return offset;
}

/*
 * lseek_seek_data_or_hole -- part of the lseek implementation
 * Expects the vinode to be locked while being called.
 */
static pmemfile_off_t
lseek_seek_data_or_hole(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
			pmemfile_off_t offset, int whence)
{
	pmemfile_ssize_t fsize = (pmemfile_ssize_t)vinode->inode->size;

	if (!vinode_is_regular_file(vinode))
		return -ENXIO;

	if (offset < 0 || offset > fsize) {
		/*
		 * offset < 0
		 * on xfs calling lseek data or hole with negative offset
		 * will return -1 with ENXIO errno
		 * this also happens with proper ext4 implementation
		 * (Linux 4.4.76 is fine, however Linux 4.9.37 has
		 * a bug which causes EFSCORRUPTED errno)
		 *
		 * offset > fsize
		 * From GNU man page: ENXIO if
		 * "...ENXIO  whence is SEEK_DATA or SEEK_HOLE, and the file
		 * offset is beyond the end of the file..."
		 */
		return -ENXIO;
	}

	if (whence == PMEMFILE_SEEK_DATA) {
		offset = lseek_seek_data(pfp, vinode, offset, fsize);
	} else {
		ASSERT(whence == PMEMFILE_SEEK_HOLE);
		offset = lseek_seek_hole(pfp, vinode, offset, fsize);
	}

	if (offset > fsize)
		offset = fsize;

	return offset;
}

/*
 * pmemfile_lseek_locked -- changes file current offset
 */
static pmemfile_off_t
pmemfile_lseek_locked(PMEMfilepool *pfp, PMEMfile *file, pmemfile_off_t offset,
		int whence)
{
	LOG(LDBG, "file %p offset %ld whence %d", file, offset, whence);

	if (file->flags & PFILE_PATH) {
		errno = EBADF;
		return -1;
	}

	if (vinode_is_dir(file->vinode)) {
		if (whence == PMEMFILE_SEEK_END) {
			errno = EINVAL;
			return -1;
		}
	} else if (vinode_is_regular_file(file->vinode)) {
		/* Nothing to do for now */
	} else {
		errno = EINVAL;
		return -1;
	}

	struct pmemfile_vinode *vinode = file->vinode;
	struct pmemfile_inode *inode = vinode->inode;
	pmemfile_off_t ret;
	int new_errno = EINVAL;

	switch (whence) {
		case PMEMFILE_SEEK_SET:
			ret = offset;
			if (ret < 0) {
				/*
				 * From POSIX: EINVAL if
				 * "...the resulting file offset would be
				 * negative for a regular file..."
				 */
				new_errno = EINVAL;
			}
			break;
		case PMEMFILE_SEEK_CUR:
			ret = (pmemfile_off_t)file->offset + offset;
			if (ret < 0) {
				if (offset < 0) {
					new_errno = EINVAL;
				} else {
					/*
					 * From POSIX: EOVERFLOW if
					 * "...The resulting file offset would
					 * be a value which cannot be
					 * represented correctly in an object
					 * of type off_t..."
					 */
					new_errno = EOVERFLOW;
				}
			}
			break;
		case PMEMFILE_SEEK_END:
			os_rwlock_rdlock(&vinode->rwlock);
			ret = (pmemfile_off_t)inode->size + offset;
			os_rwlock_unlock(&vinode->rwlock);
			if (ret < 0) {
				/* Errors as in SEEK_CUR */
				if (offset < 0)
					new_errno = EINVAL;
				else
					new_errno = EOVERFLOW;
			}
			break;
		case PMEMFILE_SEEK_DATA:
		case PMEMFILE_SEEK_HOLE:
			/*
			 * We may need to rebuild the block tree, so we have to
			 * take vinode lock in write mode.
			 */
			os_rwlock_wrlock(&vinode->rwlock);
			ret = lseek_seek_data_or_hole(pfp, vinode, offset,
				whence);
			if (ret < 0) {
				new_errno = (int)-ret;
				ret = -1;
			}
			os_rwlock_unlock(&vinode->rwlock);
			break;
		default:
			ret = -1;
			break;
	}

	if (ret < 0) {
		ret = -1;
		errno = new_errno;
	} else {
		if (file->offset != (size_t)ret)
			LOG(LDBG, "off diff: old %lu != new %lu", file->offset,
					(size_t)ret);
		file->offset = (size_t)ret;
	}

	return ret;
}

/*
 * pmemfile_lseek -- changes file current offset
 */
pmemfile_off_t
pmemfile_lseek(PMEMfilepool *pfp, PMEMfile *file, pmemfile_off_t offset,
		int whence)
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

	COMPILE_ERROR_ON(sizeof(offset) != 8);
	pmemfile_off_t ret;

	os_mutex_lock(&file->mutex);
	ret = pmemfile_lseek_locked(pfp, file, offset, whence);
	os_mutex_unlock(&file->mutex);

	return ret;
}
