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

/*
 * pmemfile_pwritev_args_check - checks some write arguments
 * The arguments here can be examined while holding the mutex for the
 * PMEMfile instance, while there is no need to hold the lock for the
 * corresponding vinode instance.
 */
static pmemfile_ssize_t
pmemfile_pwritev_args_check(struct pmemfile_file *file,
		const pmemfile_iovec_t *iov,
		int iovcnt)
{
	LOG(LDBG, "vinode %p iov %p iovcnt %d", file->vinode, iov, iovcnt);

	if (!vinode_is_regular_file(file->vinode)) {
		errno = EINVAL;
		return -1;
	}

	if (!(file->flags & PFILE_WRITE)) {
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

/*
 * pmemfile_allocate_space -- allocates space between offset and offset + len
 */
static int
pmemfile_allocate_space(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode, size_t offset, size_t len,
		bool expect_changes)
{
	struct pmemfile_inode *inode = vinode->inode;
	int error = 0;

	vinode_snapshot(vinode);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		size_t allocated_space = inode_get_allocated_space(inode) +
			vinode_allocate_interval(pfp, vinode, offset, len);

		if (expect_changes)
			/* Non-fatal condition we would like to know about. */
			ASSERT(inode_get_allocated_space(inode) !=
					allocated_space);
		else
			/*
			 * Fatal condition. This means
			 * vinode_is_interval_allocated is buggy.
			 */
			ASSERT(inode_get_allocated_space(inode) ==
					allocated_space);

		inode_tx_set_allocated_space(inode, allocated_space);
	} TX_ONABORT {
		if (errno == ENOMEM)
			errno = ENOSPC;
		error = errno;
		vinode_restore_on_abort(vinode);
	} TX_END

	return error;
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
	int error = 0;

	struct pmemfile_inode *inode = vinode->inode;

	size_t ret = 0;

	ASSERT_NOT_IN_TX();

	if (!vinode->blocks) {
		error = vinode_rebuild_block_tree(pfp, vinode);
		if (error)
			goto end;
	}

	if (file_flags & PFILE_APPEND)
		offset = inode_get_size(inode);

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

	if (sum_len == 0)
		return 0;

	if (!vinode_is_interval_allocated(pfp, vinode, offset, sum_len,
			*last_block)) {
		error = pmemfile_allocate_space(pfp, vinode, offset, sum_len,
				true);
	} else {
#ifdef DEBUG
		static int verify = -1;
		if (verify == -1) {
			const char *ver =
				getenv("PMEMFILE_DEBUG_VERIFY_SPACE_ALLOCATED");
			if (ver && ver[0] == '0')
				verify = 0;
			else
				verify = 1;
		}

		if (verify)
			error = pmemfile_allocate_space(pfp, vinode, offset,
					sum_len, false);
#endif
	}
	if (error)
		goto end;

	struct pmemfile_time tm;
	if (get_current_time(&tm)) {
		error = errno;
		goto end;
	}

	/*
	 * We have to update mtime before actually modifying file contents,
	 * just in case of crash/power failure.
	 */
	inode_slot mtime_slot = inode_next_mtime_slot(inode);
	inode->mtime[mtime_slot] = tm;
	/*
	 * Flush and sfence, because we can modify slot info only after data
	 * has hit medium.
	 */
	pmemfile_persist(pfp, &inode->mtime[mtime_slot]);

	inode->slots.bits.mtime = mtime_slot;
	/*
	 * Again, flush and sfence. We can modify file contents only after we
	 * are sure mtime has hit medium.
	 */
	pmemfile_persist(pfp, &inode->slots);

	/*
	 * Now write the data. It uses pmemobj_memcpy_persist, which has
	 * a built-in fence. We actually don't need its fence here, but there's
	 * no way to opt out of it without introducing new API to pmemobj.
	 */
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
	ASSERT(ret > 0);

	struct pmemfile_time starttm = tm;
	if (get_current_time(&tm)) {
		error = errno;
		goto end;
	}

	int64_t tm_diff = (tm.sec - starttm.sec) * 1000000000 +
			tm.nsec - starttm.nsec;

	bool update_size = offset > inode_get_size(inode);

	/*
	 * In theory we don't have to do update mtime here. We have a write lock
	 * on vinode, so file changes can't be observed by another thread.
	 * However if we performed long write which took more than, let's say,
	 * 1ms we can update mtime. Time taken to do this will be negligible
	 * comparing to time spent copying data.
	 *
	 * If we'll update ctime and size there's no penalty in updating
	 * mtime too. mtime < ctime may confuse some applications into thinking
	 * only non-content related metadata changed, so it's safer to do it.
	 */
	bool update_mtime = (tm_diff >= 1000000) || update_size;

	inode_slot size_slot = inode->slots.bits.size;
	inode_slot ctime_slot = inode->slots.bits.ctime;

	if (update_mtime) {
		mtime_slot = inode_next_mtime_slot(inode);
		inode->mtime[mtime_slot] = tm;
		pmemfile_flush(pfp, &inode->mtime[mtime_slot]);
	}

	if (update_size) {
		size_slot = inode_next_size_slot(inode);
		inode->size[size_slot] = offset;
		pmemfile_flush(pfp, &inode->size[size_slot]);

		ctime_slot = inode_next_ctime_slot(inode);
		inode->ctime[ctime_slot] = tm;
		pmemfile_flush(pfp, &inode->ctime[ctime_slot]);
	}

	if (update_mtime || update_size) {
		/*
		 * We will update slot info now, so all slots must be on
		 * the medium. Issue sfence to wait for that.
		 */
		pmemfile_drain(pfp);

		union pmemfile_inode_slots slots = inode->slots;
		slots.bits.ctime = ctime_slot;
		slots.bits.size = size_slot;
		slots.bits.mtime = mtime_slot;

		/*
		 * All slot infos must be updated using one store. Force it by
		 * using atomic store.
		 */
		__atomic_store_n(&inode->slots.value, slots.value,
				__ATOMIC_RELAXED);
		pmemfile_persist(pfp, &inode->slots);
	}


end:
	if (error) {
		errno = error;
		return -1;
	}

	return (pmemfile_ssize_t)ret;
}

/*
 * pmemfile_write - same as pmemfile_writev with a single iov buffer
 */
pmemfile_ssize_t
pmemfile_write(PMEMfilepool *pfp, PMEMfile *file, const void *buf, size_t count)
{
	pmemfile_iovec_t element = {.iov_base = (void *)buf, .iov_len = count};
	return pmemfile_writev(pfp, file, &element, 1);
}

/*
 * pmemfile_writev_under_filelock - write to a file
 * This function expects the PMEMfile instance to be locked while being called.
 * Since the offset field is used to determine where to read from, and is also
 * updated after a successful read operation, the PMEMfile instance can not be
 * accessed by others while this is happening.
 *
 */
static pmemfile_ssize_t
pmemfile_writev_under_filelock(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_iovec_t *iov, int iovcnt)
{
	pmemfile_ssize_t ret;

	struct pmemfile_block_desc *last_block;

	ret = pmemfile_pwritev_args_check(file, iov, iovcnt);
	if (ret != 0)
		return ret;

	if (iovcnt == 0)
		return 0;

	os_rwlock_wrlock(&file->vinode->rwlock);

	if (file->last_block_pointer_invalidation_observed !=
			file->vinode->block_pointer_invalidation_counter) {
		file->block_pointer_cache = NULL;
		file->last_block_pointer_invalidation_observed =
			file->vinode->block_pointer_invalidation_counter;
	}

	last_block = file->block_pointer_cache;

	ret = pmemfile_pwritev_internal(pfp,
					file->vinode,
					&last_block,
					file->flags,
					file->offset, iov, iovcnt);


	os_rwlock_unlock(&file->vinode->rwlock);

	if (ret > 0) {
		file->offset += (size_t)ret;
		file->block_pointer_cache = last_block;
	} else {
		file->block_pointer_cache = NULL;
	}

	return ret;
}

/*
 * pmemfile_writev - write to a file while holding the locks both for the
 * PMEMfile instance, and the vinode instance.
 */
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

	pmemfile_ssize_t ret =
		pmemfile_writev_under_filelock(pfp, file, iov, iovcnt);

	os_mutex_unlock(&file->mutex);

	return ret;
}

/*
 * pmemfile_pwrite - same as pmemfile_pwritev with a single iov buffer
 */
pmemfile_ssize_t
pmemfile_pwrite(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
		size_t count, pmemfile_off_t offset)
{
	pmemfile_iovec_t element = {.iov_base = (void *)buf, .iov_len = count};
	return pmemfile_pwritev(pfp, file, &element, 1, offset);
}

/*
 * pmemfile_pwritev - writes to a file starting at a position supplied as
 * argument.
 *
 * Since this does not require making any modification to the PMEMfile instance,
 * the corresponding lock is held only for reading some fields from it. There is
 * no point in time where this function holds locks of both the PMEMfile
 * instance, and the vinode instance it points to.
 *
 * +----------------------------------------------
 * | Erroneous scenario:
 * +----------------------------------------------
 * | lock(file);
 * |  Make a local copy of the PMEMfile instance's necessary fields.
 * | unlock(file);
 * | lock(vinode);
 * |  Write to the underlying file, using the a state of PMEMfile instance
 * |   that was observable previously, using the local copy.
 * | unlock(vinode);
 * +----------------------------------------------
 * The modification counters can not be directly checked while holding only
 * either one of the locks:
 *
 * +-------------------------------------------------------------------------+
 * | Erroneous scenario:                                                     |
 * | checking for modification while the vinode is not locked                |
 * +-------------------------------------------------------------------------+
 * | lock(file);                                                             |
 * |                                                                         |
 * |  if (is_data_modification_indicated(file)) {  ---+                      |
 * |     block_pointer_cache = NULL;                  |                      |
 * |  }                                               |                      |
 * |  Make a local copy of block_pointer_cache.       | The underlying file  |
 * |                                                  | can be modified here,|
 * | unlock(file);                                    | invalidating the     |
 * |                                                  | block_pointer_cache. |
 * | lock(vinode);                                    |                      |
 * |   Write to the file, using the local copy     ---+                      |
 * |    of block_pointer_cache.                                              |
 * | unlock(vinode);                                                         |
 * |                                                                         |
 * +-------------------------------------------------------------------------+
 *
 * +-------------------------------------------------------------------------+
 * | Other erroneous scenario:                                               |
 * | checking for modification while the file is not locked                  |
 * +-------------------------------------------------------------------------+
 * | lock(file);                                                             |
 * |                                                                         |
 * | unlock(file);                                                           |
 * |                                                                         |
 * | lock(vinode);                                                           |
 * |   if (is_data_modification_indicated(file)) { ---+                      |
 * |     block_pointer_cache = NULL;                  | block_pointer_cache  |
 * |  }                                               | can be modified here |
 * |                                                  |                      |
 * |   Write to the file, using the local copy     ---+                      |
 * |    of block_pointer_cache.                                              |
 * | unlock(vinode);                                                         |
 * |                                                                         |
 * +-------------------------------------------------------------------------+
 *
 */
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

	pmemfile_ssize_t ret;

	os_mutex_lock(&file->mutex);

	ret = pmemfile_pwritev_args_check(file, iov, iovcnt);

	uint64_t last_bp_iv_obs =
			file->last_block_pointer_invalidation_observed;
	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	uint64_t flags = file->flags;

	os_mutex_unlock(&file->mutex);

	if (ret != 0)
		return ret;

	if (iovcnt == 0)
		return 0;

	os_rwlock_wrlock(&file->vinode->rwlock);
	/*
	 * Using the variables last_bp_iv_obs, last_block, and flags, which
	 * serve to represent the state in which the PMEMfile instance was
	 * observable while the corresponding lock was held.
	 * Note: the file->vinode pointer can not be modified during the
	 * lifetime of the instance, so there is no need to work with a copy of
	 * that field.
	 */

	if (last_bp_iv_obs != file->vinode->block_pointer_invalidation_counter)
		last_block = NULL;

	ret = pmemfile_pwritev_internal(pfp, file->vinode, &last_block, flags,
		(size_t)offset, iov, iovcnt);

	os_rwlock_unlock(&file->vinode->rwlock);

	return ret;
}
