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
 * rmdir.c -- pmemfile_rmdir* implementation
 */

#include "callbacks.h"
#include "creds.h"
#include "dir.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "rmdir.h"
#include "utils.h"

/*
 * vinode_unlink_dir -- unlinks directory "vdir" from directory "vparent"
 * assuming "dirent" is used for storing this entry
 *
 * Must be called in transaction.
 */
void
vinode_unlink_dir(PMEMfilepool *pfp,
		struct pmemfile_vinode *vparent,
		struct pmemfile_dirent *dirent,
		struct pmemfile_vinode *vdir,
		const char *path)
{
	struct pmemfile_inode *iparent = vparent->inode;
	struct pmemfile_inode *idir = vdir->inode;
	struct pmemfile_dir *ddir = &idir->file_data.dir;

	ASSERT_IN_TX();

	struct pmemfile_dirent *dirdot = &ddir->dirents[0];
	struct pmemfile_dirent *dirdotdot = &ddir->dirents[1];

	ASSERTeq(strcmp(dirdot->name, "."), 0);
	ASSERT(TOID_EQUALS(dirdot->inode, vdir->tinode));

	ASSERTeq(strcmp(dirdotdot->name, ".."), 0);
	ASSERT(TOID_EQUALS(dirdotdot->inode, vparent->tinode));

	for (uint32_t i = 2; i < ddir->num_elements; ++i) {
		struct pmemfile_dirent *d = &ddir->dirents[i];

		if (!TOID_IS_NULL(d->inode)) {
			LOG(LUSR, "directory %s not empty", path);
			pmemfile_tx_abort(ENOTEMPTY);
		}
	}

	ddir = PF_RW(pfp, ddir->next);
	while (ddir) {
		for (uint32_t i = 0; i < ddir->num_elements; ++i) {
			struct pmemfile_dirent *d = &ddir->dirents[i];

			if (!TOID_IS_NULL(d->inode)) {
				LOG(LUSR, "directory %s not empty", path);
				pmemfile_tx_abort(ENOTEMPTY);
			}
		}

		ddir = PF_RW(pfp, ddir->next);
	}

	pmemobj_tx_add_range_direct(dirdot, sizeof(dirdot->inode) + 1);
	dirdot->name[0] = '\0';
	dirdot->inode = TOID_NULL(struct pmemfile_inode);

	pmemobj_tx_add_range_direct(dirdotdot,
			sizeof(dirdotdot->inode) + 1);
	dirdotdot->name[0] = '\0';
	dirdotdot->inode = TOID_NULL(struct pmemfile_inode);

	ASSERTeq(idir->nlink, 2);
	TX_ADD_DIRECT(&idir->nlink);
	idir->nlink = 0;

	pmemobj_tx_add_range_direct(dirent, sizeof(dirent->inode) + 1);
	dirent->name[0] = '\0';
	dirent->inode = TOID_NULL(struct pmemfile_inode);

	TX_ADD_DIRECT(&iparent->nlink);
	iparent->nlink--;

	struct pmemfile_time tm;
	get_current_time(&tm);

	/*
	 * From "stat" man page:
	 * "The field st_ctime is changed by writing or by setting inode
	 * information (i.e., owner, group, link count, mode, etc.)."
	 */
	TX_SET_DIRECT(iparent, ctime, tm);

	/*
	 * From "stat" man page:
	 * "st_mtime of a directory is changed by the creation
	 * or deletion of files in that directory."
	 */
	TX_SET_DIRECT(iparent, mtime, tm);
}

int
pmemfile_rmdirat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path)
{
	struct pmemfile_path_info info;
	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	resolve_pathat(pfp, &cred, dir, path, &info, 0);

	int error = 0;

	if (info.error) {
		error = info.error;
		goto end;
	}

	size_t namelen = component_length(info.remaining);

	/* Does not make sense, but it's specified by POSIX. */
	if (str_compare(".", info.remaining, namelen) == 0) {
		error = EINVAL;
		goto end;
	}

	/*
	 * If we managed to enter a directory, then the parent directory has
	 * at least this entry as child.
	 */
	if (str_compare("..", info.remaining, namelen) == 0) {
		error = ENOTEMPTY;
		goto end;
	}

	if (namelen == 0) {
		ASSERT(info.parent == pfp->root);
		error = EBUSY;
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

	if (!vinode_is_dir(dirent_info.vinode)) {
		error = ENOTDIR;
		goto vdir_end;
	}

	if (!_vinode_can_access(&cred, info.parent, PFILE_WANT_WRITE)) {
		error = EACCES;
		goto vdir_end;
	}

	ASSERT_NOT_IN_TX();

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		vinode_unlink_dir(pfp, info.parent, dirent_info.dirent,
				dirent_info.vinode, path);

		vinode_orphan(pfp, dirent_info.vinode);
	} TX_ONABORT {
		error = errno;
	} TX_END

vdir_end:
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
pmemfile_rmdir(PMEMfilepool *pfp, const char *path)
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

	at = pool_get_dir_for_path(pfp, PMEMFILE_AT_CWD, path, &at_unref);

	int ret = pmemfile_rmdirat(pfp, at, path);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}
