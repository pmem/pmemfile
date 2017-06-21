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
 * mkdir.c -- pmemfile_mkdir* implementation
 */

#include <inttypes.h>

#include "callbacks.h"
#include "dir.h"
#include "inode.h"
#include "libpmemfile-posix.h"
#include "mkdir.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

/*
 * vinode_new_dir -- creates new directory relative to parent
 *
 * Note: caller must hold WRITE lock on parent.
 * Must be called in a transaction.
 */
TOID(struct pmemfile_inode)
vinode_new_dir(PMEMfilepool *pfp, struct pmemfile_vinode *parent,
		const char *name, size_t namelen, struct pmemfile_cred *cred,
		pmemfile_mode_t mode)
{
	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s new_name %.*s",
			parent ? parent->tinode.oid.off : 0,
			pmfi_path(parent), (int)namelen, name);

	ASSERT_IN_TX();

	if (mode & ~(pmemfile_mode_t)PMEMFILE_ACCESSPERMS) {
		/* XXX: what the kernel does in this case? */
		ERR("invalid mode flags 0%o", mode);
		pmemfile_tx_abort(EINVAL);
	}
	mode &= ~pfp->umask;

	TOID(struct pmemfile_inode) tchild =
			inode_alloc(pfp, cred, PMEMFILE_S_IFDIR | mode);
	struct pmemfile_inode *child = PF_RW(pfp, tchild);
	struct pmemfile_time t = child->ctime;

	/* add . and .. to new directory */
	inode_add_dirent(pfp, tchild, ".", 1, tchild, t);

	if (parent == NULL) { /* special case - root directory */
		inode_add_dirent(pfp, tchild, "..", 2, tchild, t);
	} else {
		inode_add_dirent(pfp, tchild, "..", 2, parent->tinode, t);
		inode_add_dirent(pfp, parent->tinode, name, namelen, tchild,
				t);
	}

	return tchild;
}

static int
_pmemfile_mkdirat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path, pmemfile_mode_t mode)
{
	struct pmemfile_path_info info;
	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	resolve_pathat(pfp, &cred, dir, path, &info, 0);

	struct pmemfile_vinode *parent = info.parent;
	int error = 0;

	if (info.error) {
		error = info.error;
		goto end;
	}

	size_t namelen = component_length(info.remaining);

	/* mkdir("/") */
	if (namelen == 0) {
		ASSERT(parent == pfp->root);
		error = EEXIST;
		goto end;
	}

	os_rwlock_wrlock(&parent->rwlock);

	ASSERT_NOT_IN_TX();

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!_vinode_can_access(&cred, parent, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		vinode_new_dir(pfp, parent, info.remaining, namelen, &cred,
				mode);
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&parent->rwlock);

end:
	path_info_cleanup(pfp, &info);
	cred_release(&cred);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_mkdirat(PMEMfilepool *pfp, PMEMfile *dir, const char *path,
		pmemfile_mode_t mode)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!path) {
		errno = ENOENT;
		return -1;
	}

	if (path[0] != '/' && !dir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, path, &at_unref);

	int ret = _pmemfile_mkdirat(pfp, at, path, mode);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_mkdir(PMEMfilepool *pfp, const char *path, pmemfile_mode_t mode)
{
	return pmemfile_mkdirat(pfp, PMEMFILE_AT_CWD, path, mode);
}
