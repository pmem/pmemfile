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
 * write.c -- pmemfile_*write* implementation
 */

#include <limits.h>

#include "callbacks.h"
#include "data.h"
#include "file.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

/*
 * vinode_write -- writes to file
 */
static void
vinode_write(PMEMfilepool *pfp, struct pmemfile_vinode *vinode, size_t offset,
		struct pmemfile_block_desc **last_block,
		const char *buf, size_t count)
{
	ASSERT_IN_TX();

	ASSERT(count > 0);

	/*
	 * Two steps:
	 * - Zero Fill some new blocks, in case the file is extended by
	 *   writing to the file after seeking past file size ( optionally )
	 * - Copy the data from the users buffer
	 */

	/* All blocks needed for writing are properly allocated at this point */

	struct pmemfile_block_desc *block =
		find_closest_block_with_hint(vinode, offset, *last_block);

	block = iterate_on_file_range(pfp, vinode, block, offset,
			count, (char *)buf, write_to_blocks);

	if (block)
		*last_block = block;
}

static pmemfile_ssize_t
pmemfile_pwritev_internal(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode,
		struct pmemfile_block_desc **last_block,
		uint64_t file_flags,
		size_t offset,
		const pmemfile_iovec_t *iov,
		int iovcnt)
{
	LOG(LDBG, "vinode %p iov %p iovcnt %d", vinode, iov, iovcnt);

	if (!vinode_is_regular_file(vinode)) {
		errno = EINVAL;
		return -1;
	}

	if (!(file_flags & PFILE_WRITE)) {
		errno = EBADF;
		return -1;
	}

	if (iovcnt == 0)
		return 0;

	if (iov == NULL) {
		errno = EFAULT;
		return -1;
	}

	for (int i = 0; i < iovcnt; ++i) {
		if (iov[i].iov_base == NULL) {
			errno = EFAULT;
			return -1;
		}
	}

	int error = 0;

	struct pmemfile_inode *inode = vinode->inode;

	os_rwlock_wrlock(&vinode->rwlock);

	size_t ret = 0;

	ASSERT_NOT_IN_TX();

	if (!vinode->blocks) {
		error = vinode_rebuild_block_tree(pfp, vinode);
		if (error)
			goto end;
	}

	vinode_snapshot(vinode);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (file_flags & PFILE_APPEND)
			offset = inode->size;

		size_t sum_len = 0;
		for (int i = 0; i < iovcnt; ++i) {
			size_t len = iov[i].iov_len;

			if ((pmemfile_ssize_t)len < 0)
				len = SSIZE_MAX;

			if ((pmemfile_ssize_t)(sum_len + len) < 0)
				len = SSIZE_MAX - sum_len;

			/* overflow check */
			if (offset + sum_len + len < offset)
				len = SIZE_MAX - offset - sum_len;

			sum_len += len;

			if (len != iov[i].iov_len)
				break;
		}

		if (sum_len > 0)
			vinode_allocate_interval(pfp, vinode, offset, sum_len);

		for (int i = 0; i < iovcnt; ++i) {
			size_t len = iov[i].iov_len;

			if ((pmemfile_ssize_t)len < 0)
				len = SSIZE_MAX;

			if ((pmemfile_ssize_t)(ret + len) < 0)
				len = SSIZE_MAX - ret;

			if (offset + len < offset) /* overflow check */
				len = SIZE_MAX - offset;

			if (len > 0)
				vinode_write(pfp, vinode, offset, last_block,
						iov[i].iov_base, len);

			ret += len;
			offset += len;

			if (len != iov[i].iov_len)
				break;
		}

		/*
		 * Update metadata only when any of the buffer lengths
		 * was != 0.
		 */
		if (ret > 0) {
			if (offset > inode->size) {
				TX_ADD_FIELD_DIRECT(inode, size);
				inode->size = offset;
			}

			struct pmemfile_time tm;
			get_current_time(&tm);
			TX_SET_DIRECT(inode, mtime, tm);
		}
	} TX_ONABORT {
		error = errno;
		vinode_restore_on_abort(vinode);
	} TX_END

end:
	os_rwlock_unlock(&vinode->rwlock);

	if (error) {
		errno = error;
		return -1;
	}

	return (pmemfile_ssize_t)ret;
}

/*
 * pmemfile_write -- writes to file
 */
pmemfile_ssize_t
pmemfile_write(PMEMfilepool *pfp, PMEMfile *file, const void *buf, size_t count)
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

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	pmemfile_iovec_t vec;
	vec.iov_base = (void *)buf;
	vec.iov_len = count;

	pmemfile_ssize_t ret = pmemfile_pwritev_internal(pfp, file->vinode,
			&last_block, file->flags, file->offset, &vec, 1);
	if (ret >= 0) {
		file->offset += (size_t)ret;
		file->block_pointer_cache = last_block;
	}

	os_mutex_unlock(&file->mutex);

	return ret;
}

pmemfile_ssize_t
pmemfile_writev(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt)
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

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;

	pmemfile_ssize_t ret = pmemfile_pwritev_internal(pfp, file->vinode,
			&last_block, file->flags, file->offset, iov, iovcnt);
	if (ret >= 0) {
		file->offset += (size_t)ret;
		file->block_pointer_cache = last_block;
	}

	os_mutex_unlock(&file->mutex);

	return ret;
}

pmemfile_ssize_t
pmemfile_pwrite(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
		size_t count, pmemfile_off_t offset)
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

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	struct pmemfile_vinode *vinode = file->vinode;
	uint64_t flags = file->flags;

	os_mutex_unlock(&file->mutex);

	pmemfile_iovec_t vec;
	vec.iov_base = (void *)buf;
	vec.iov_len = count;

	return pmemfile_pwritev_internal(pfp, vinode, &last_block, flags,
			(size_t)offset, &vec, 1);
}

pmemfile_ssize_t
pmemfile_pwritev(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt, pmemfile_off_t offset)
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

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	struct pmemfile_vinode *vinode = file->vinode;
	uint64_t flags = file->flags;

	os_mutex_unlock(&file->mutex);

	return pmemfile_pwritev_internal(pfp, vinode, &last_block, flags,
			(size_t)offset, iov, iovcnt);
}
