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
 * unlink.c -- pmemfile_unlink* implementation
 */

#include <inttypes.h>

#include "callbacks.h"
#include "dir.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "rmdir.h"
#include "unlink.h"
#include "utils.h"

/*
 * vinode_unlink_file -- removes file dirent from directory
 *
 * Must be called in a transaction. Caller must have exclusive access to both
 * parent and child inode by locking them in WRITE mode.
 */
void
vinode_unlink_file(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent,
		struct pmemfile_dirent *dirent,
		struct pmemfile_vinode *vinode)
{
	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s name %s",
		parent->tinode.oid.off, pmfi_path(parent), dirent->name);

	ASSERT_IN_TX();

	TOID(struct pmemfile_inode) tinode = dirent->inode;
	struct pmemfile_inode *inode = PF_RW(pfp, tinode);

	ASSERT(inode->nlink > 0);

	TX_ADD_DIRECT(&inode->nlink);
	/*
	 * Snapshot inode and the first byte of a name (because we are going
	 * to overwrite just one byte) using one call.
	 */
	pmemobj_tx_add_range_direct(dirent, sizeof(dirent->inode) + 1);

	struct pmemfile_time tm;
	tx_get_current_time(&tm);

	if (--inode->nlink > 0) {
		/*
		 * From "stat" man page:
		 * "The field st_ctime is changed by writing or by setting inode
		 * information (i.e., owner, group, link count, mode, etc.)."
		 */
		TX_SET_DIRECT(vinode->inode, ctime, tm);
	}
	/*
	 * From "stat" man page:
	 * "st_mtime of a directory is changed by the creation
	 * or deletion of files in that directory."
	 */
	TX_SET_DIRECT(parent->inode, mtime, tm);

	dirent->name[0] = '\0';
	dirent->inode = TOID_NULL(struct pmemfile_inode);
}

static int
_pmemfile_unlinkat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *pathname)
{
	LOG(LDBG, "pathname %s", pathname);

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	int error = 0;

	struct pmemfile_path_info info;
	resolve_pathat(pfp, &cred, dir, pathname, &info, 0);

	if (info.error) {
		error = info.error;
		goto end;
	}

	if (strchr(info.remaining, '/')) {
		error = ENOTDIR;
		goto end;
	}

	struct pmemfile_dirent_info dirent_info;
	/*
	 * lock_parent_and_child can race with another thread messing with
	 * parent directory. Loop as long as race occurs.
	 */
	do {
		error = lock_parent_and_child(pfp, &info, &dirent_info);
	} while (error == 1);

	if (error < 0) {
		error = -error;
		goto end;
	}

	if (!_vinode_can_access(&cred, info.parent, PFILE_WANT_WRITE)) {
		error = EACCES;
		goto end_vinode;
	}

	if (vinode_is_dir(dirent_info.vinode)) {
		error = EISDIR;
		goto end_vinode;
	}

	ASSERT_NOT_IN_TX();

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		vinode_unlink_file(pfp, info.parent, dirent_info.dirent,
				dirent_info.vinode);

		if (dirent_info.vinode->inode->nlink == 0)
			vinode_orphan(pfp, dirent_info.vinode);
	} TX_ONABORT {
		error = errno;
	} TX_END

end_vinode:
	vinode_unlock2(dirent_info.vinode, info.parent);

	vinode_unref(pfp, dirent_info.vinode);

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
pmemfile_unlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int flags)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pathname) {
		errno = ENOENT;
		return -1;
	}

	if (pathname[0] != '/' && !dir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, pathname, &at_unref);

	int ret;

	if (flags & PMEMFILE_AT_REMOVEDIR)
		ret = pmemfile_rmdirat(pfp, at, pathname);
	else {
		if (flags != 0) {
			errno = EINVAL;
			ret = -1;
		} else {
			ret = _pmemfile_unlinkat(pfp, at, pathname);
		}
	}

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

/*
 * pmemfile_unlink -- delete a name and possibly the file it refers to
 */
int
pmemfile_unlink(PMEMfilepool *pfp, const char *pathname)
{
	return pmemfile_unlinkat(pfp, PMEMFILE_AT_CWD, pathname, 0);
}
