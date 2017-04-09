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
 * truncate.c -- pmemfile_*truncate implementation
 */

#include <limits.h>

#include "callbacks.h"
#include "data.h"
#include "dir.h"
#include "file.h"
#include "internal.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "truncate.h"
#include "utils.h"

/*
 * vinode_truncate -- changes file size to size
 *
 * Should only be called inside pmemobj transactions.
 */
void
vinode_truncate(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		uint64_t size)
{
	struct pmemfile_inode *inode = vinode->inode;

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	if (vinode->blocks == NULL) {
		int err = vinode_rebuild_block_tree(vinode);
		if (err)
			pmemfile_tx_abort(err);
	}

	cb_push_front(TX_STAGE_ONABORT,
		(cb_basic)vinode_destroy_data_state,
		vinode);

	/*
	 * Might need to handle the special case where size == 0.
	 * Setting all the next and prev fields is pointless, when all the
	 * blocks are removed.
	 */
	vinode_remove_interval(vinode, size, UINT64_MAX - size);
	if (inode->size < size)
		vinode_allocate_interval(pfp, vinode,
		    inode->size, size - inode->size);

	if (inode->size != size) {
		TX_ADD_DIRECT(&inode->size);
		inode->size = size;

		struct pmemfile_time tm;
		get_current_time(&tm);
		TX_SET_DIRECT(inode, mtime, tm);
	}
}

static int
_pmemfile_ftruncate(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
			uint64_t length)
{
	if (!vinode_is_regular_file(vinode))
		return EINVAL;

	int error = 0;

	os_rwlock_wrlock(&vinode->rwlock);

	vinode_snapshot(vinode);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		vinode_truncate(pfp, vinode, length);
	} TX_ONABORT {
		error = errno;
		vinode_restore_on_abort(vinode);
	} TX_END

	os_rwlock_unlock(&vinode->rwlock);

	return error;
}

int
pmemfile_ftruncate(PMEMfilepool *pfp, PMEMfile *file, pmemfile_off_t length)
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

	if (length < 0) {
		errno = EINVAL;
		return -1;
	}

	if (length > PMEMFILE_SSIZE_MAX) {
		errno = EFBIG;
		return -1;
	}

	os_mutex_lock(&file->mutex);
	uint64_t flags = file->flags;
	struct pmemfile_vinode *vinode = file->vinode;
	os_mutex_unlock(&file->mutex);

	if (!(flags & PFILE_WRITE)) {
		errno = EBADF;
		return -1;
	}

	int err = _pmemfile_ftruncate(pfp, vinode, (uint64_t)length);
	if (err) {
		errno = err;
		return -1;
	}

	return 0;
}

int
pmemfile_truncate(PMEMfilepool *pfp, const char *path, pmemfile_off_t length)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!path) {
		LOG(LUSR, "NULL path");
		errno = EFAULT;
		return -1;
	}

	if (length < 0) {
		errno = EINVAL;
		return -1;
	}

	if (length > PMEMFILE_SSIZE_MAX) {
		errno = EFBIG;
		return -1;
	}

	struct pmemfile_cred cred[1];
	if (cred_acquire(pfp, cred))
		return -1;

	int error = 0;
	struct pmemfile_vinode *vinode = NULL;
	struct pmemfile_vinode *vparent = NULL;
	bool unref_vparent = false;
	struct pmemfile_path_info info;

	if (path[0] == '/') {
		vparent = pfp->root;
		unref_vparent = false;
	} else {
		vparent = pool_get_cwd(pfp);
		unref_vparent = true;
	}

	vinode = resolve_pathat_full(pfp, cred, vparent, path, &info, 0, true);

	if (info.error) {
		error = info.error;
		goto end;
	}

	if (!_vinode_can_access(cred, vinode, PFILE_WANT_WRITE)) {
		error = EACCES;
		goto end;
	}

	if (vinode_is_dir(vinode)) {
		error = EISDIR;
		goto end;
	}

	error = _pmemfile_ftruncate(pfp, vinode, (uint64_t)length);

end:
	path_info_cleanup(pfp, &info);
	cred_release(cred);

	if (vinode)
		vinode_unref(pfp, vinode);

	if (unref_vparent)
		vinode_unref(pfp, vparent);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}
