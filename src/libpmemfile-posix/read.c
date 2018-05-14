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
 * read.c -- pmemfile_*read* implementation
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
time_cmp(const struct pmemfile_time *t1, const struct pmemfile_time *t2)
{
	if (t1->sec < t2->sec)
		return -1;
	if (t1->sec > t2->sec)
		return 1;
	if (t1->nsec < t2->nsec)
		return -1;
	if (t1->nsec > t2->nsec)
		return 1;
	return 0;
}

/*
 * pmemfile_preadv_args_check - checks some read arguments
 * The arguments here can be examined while holding the mutex for the
 * PMEMfile instance, while there is no need to hold the lock for the
 * corresponding vinode instance.
 */
static pmemfile_ssize_t
pmemfile_preadv_args_check(PMEMfile *file,
		const pmemfile_iovec_t *iov,
		int iovcnt)
{
	LOG(LDBG, "vinode %p iov %p iovcnt %d", file->vinode, iov, iovcnt);

	if (!vinode_is_regular_file(file->vinode)) {
		if (vinode_is_dir(file->vinode))
			errno = EISDIR;
		else
			errno = EINVAL;
		return -1;
	}

	if (!(file->flags & PFILE_READ)) {
		errno = EBADF;
		return -1;
	}

	if (iovcnt > 0 && iov == NULL) {
		errno = EFAULT;
		return -1;
	}

	for (int i = 0; i < iovcnt; ++i) {
		if (iov[i].iov_base == NULL) {
			errno = EFAULT;
			return -1;
		}
	}

	return 0;
}

static pmemfile_ssize_t
pmemfile_preadv_internal(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode,
		struct pmemfile_block_desc **last_block,
		size_t offset,
		const pmemfile_iovec_t *iov,
		int iovcnt)
{
	pmemfile_ssize_t ret = 0;

	for (int i = 0; i < iovcnt; ++i) {
		size_t len = iov[i].iov_len;
		if ((pmemfile_ssize_t)((size_t)ret + len) < 0)
			len = (size_t)(SSIZE_MAX - ret);
		ASSERT((pmemfile_ssize_t)((size_t)ret + len) >= 0);

		size_t bytes_read = vinode_read(pfp, vinode, offset, last_block,
				iov[i].iov_base, len);

		ret += (pmemfile_ssize_t)bytes_read;
		offset += bytes_read;
		if (bytes_read != len)
			break;
	}

	return ret;
}

/*
 * handle_atime -
 * Updates the atime field following a read operation, if necessary.
 * The vinode must not be locked when calling this function.
 */
static void
handle_atime(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode,
		uint64_t file_flags)
{
	if (file_flags & PFILE_NOATIME)
		return;

	struct pmemfile_inode *inode = vinode->inode;
	struct pmemfile_time tm;

	get_current_time(&tm);

	struct pmemfile_time tm1d = tm;
	tm1d.sec -= 86400;
	const struct pmemfile_time *atime = &vinode->atime;

	/* relatime */
	if ((time_cmp(atime, &tm1d) >= 0) &&
	    (time_cmp(atime, inode_get_ctime_ptr(inode)) >= 0) &&
	    (time_cmp(atime, inode_get_mtime_ptr(inode)) >= 0))
		return;

	os_rwlock_wrlock(&vinode->rwlock);
	vinode->atime = tm;
	vinode->atime_dirty = true;
	os_rwlock_unlock(&vinode->rwlock);
}

/*
 * pmemfile_read -- reads file
 */
pmemfile_ssize_t
pmemfile_read(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count)
{
	pmemfile_iovec_t element = { .iov_base = buf, .iov_len = count };
	return pmemfile_readv(pfp, file, &element, 1);
}

static pmemfile_ssize_t
pmemfile_readv_under_filelock(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_iovec_t *iov, int iovcnt)
{
	pmemfile_ssize_t ret;

	struct pmemfile_block_desc *last_block;

	ret = pmemfile_preadv_args_check(file, iov, iovcnt);
	if (ret != 0)
		return ret;

	if (iovcnt == 0)
		return 0;

	ret = vinode_rdlock_with_block_tree(pfp, file->vinode);
	if (ret != 0)
		return ret;

	if (file->last_block_pointer_invalidation_observed !=
			file->vinode->block_pointer_invalidation_counter) {
		file->block_pointer_cache = NULL;
		file->last_block_pointer_invalidation_observed =
			file->vinode->block_pointer_invalidation_counter;
	}

	uint64_t flags = file->flags;
	last_block = file->block_pointer_cache;

	ret = pmemfile_preadv_internal(pfp, file->vinode,
		&last_block, file->offset, iov, iovcnt);


	os_rwlock_unlock(&file->vinode->rwlock);

	if (ret > 0) {
		file->offset += (size_t)ret;
		file->block_pointer_cache = last_block;
	} else {
		file->block_pointer_cache = NULL;
	}

	handle_atime(pfp, file->vinode, flags);

	return ret;
}

/*
 * pmemfile_readv - reads from a file while holding the locks both for the
 * PMEMfile instance, and the vinode instance.
 */
pmemfile_ssize_t
pmemfile_readv(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
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

	pmemfile_ssize_t ret =
		pmemfile_readv_under_filelock(pfp, file, iov, iovcnt);

	os_mutex_unlock(&file->mutex);

	return ret;
}

pmemfile_ssize_t
pmemfile_pread(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count,
		pmemfile_off_t offset)
{
	pmemfile_iovec_t element = { .iov_base = buf, .iov_len = count };
	return pmemfile_preadv(pfp, file, &element, 1, offset);
}

/*
 * pmemfile_preadv - reads from a file starting at a position supplied as
 * argument.
 *
 * Since this does not require making any modification to the PMEMfile instance,
 * the corresponding lock is held only for reading some fields from it. There is
 * no point in time where this function holds locks of both the PMEMfile
 * instance, and the vinode instance it points to.
 */
pmemfile_ssize_t
pmemfile_preadv(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
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

	pmemfile_ssize_t ret;

	os_mutex_lock(&file->mutex);

	ret = pmemfile_preadv_args_check(file, iov, iovcnt);

	uint64_t last_bp_iv_obs =
			file->last_block_pointer_invalidation_observed;
	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	uint64_t flags = file->flags;

	os_mutex_unlock(&file->mutex);

	if (ret != 0)
		return ret;

	if (iovcnt == 0)
		return 0;

	ret = vinode_rdlock_with_block_tree(pfp, file->vinode);
	if (ret != 0)
		return ret;

	if (last_bp_iv_obs != file->vinode->block_pointer_invalidation_counter)
		last_block = NULL;

	ret = pmemfile_preadv_internal(pfp, file->vinode, &last_block,
			(size_t)offset, iov, iovcnt);

	os_rwlock_unlock(&file->vinode->rwlock);

	handle_atime(pfp, file->vinode, flags);

	return ret;
}
