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
 * chown.c -- pmemfile_*chown* implementation
 */

#include "callbacks.h"
#include "dir.h"
#include "file.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

/*
 * vinode_chown
 *
 * Can't be called in a transaction.
 */
static int
vinode_chown(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	struct pmemfile_inode *inode = vinode->inode;
	int error = 0;

	ASSERT_NOT_IN_TX();

	if (owner == (pmemfile_uid_t)-1 && group == (pmemfile_gid_t)-1)
		return 0;

	os_rwlock_wrlock(&vinode->rwlock);

	if (!(cred->caps & (1 << PMEMFILE_CAP_CHOWN))) {
		if (inode->uid != cred->fsuid) {
			error = EPERM;
			goto end;
		}

		if (owner != (pmemfile_uid_t)-1 && owner != inode->uid) {
			error = EPERM;
			goto end;
		}

		if (group != (pmemfile_gid_t)-1 && group != inode->gid) {
			if (group != cred->fsgid && !gid_in_list(cred, group)) {
				error = EPERM;
				goto end;
			}
		}
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		COMPILE_ERROR_ON(offsetof(struct pmemfile_inode, gid) !=
				offsetof(struct pmemfile_inode, uid) +
				sizeof(inode->uid));

		pmemobj_tx_add_range_direct(&inode->uid,
				sizeof(inode->uid) + sizeof(inode->gid));

		if (owner != (pmemfile_uid_t)-1)
			inode->uid = owner;
		if (group != (pmemfile_gid_t)-1)
			inode->gid = group;

		struct pmemfile_time tm;
		get_current_time(&tm);

		inode_tx_set_ctime(inode, tm);
	} TX_ONABORT {
		error = errno;
	} TX_END

end:
	os_rwlock_unlock(&vinode->rwlock);

	return error;
}

static int
_pmemfile_fchownat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path, pmemfile_uid_t owner, pmemfile_gid_t group,
		int flags)
{
	if (flags & ~(PMEMFILE_AT_EMPTY_PATH | PMEMFILE_AT_SYMLINK_NOFOLLOW)) {
		errno = EINVAL;
		return -1;
	}

	LOG(LDBG, "path %s", path);

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	int error = 0;
	struct pmemfile_path_info info;
	struct pmemfile_vinode *vinode;

	if (path[0] == 0 && (flags & PMEMFILE_AT_EMPTY_PATH)) {
		memset(&info, 0, sizeof(info));
		vinode = vinode_ref(pfp, dir);
	} else {
		vinode = resolve_pathat_full(pfp, &cred, dir, path, &info, 0,
				(flags & PMEMFILE_AT_SYMLINK_NOFOLLOW) ?
						NO_RESOLVE_LAST_SYMLINK :
						RESOLVE_LAST_SYMLINK);
		if (info.error) {
			error = info.error;
			goto end;
		}
	}

	error = vinode_chown(pfp, &cred, vinode, owner, group);

end:
	path_info_cleanup(pfp, &info);
	cred_release(&cred);

	ASSERT_NOT_IN_TX();
	if (vinode)
		vinode_unref(pfp, vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_fchownat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		pmemfile_uid_t owner, pmemfile_gid_t group, int flags)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

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

	int ret = _pmemfile_fchownat(pfp, at, pathname, owner, group, flags);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_chown(PMEMfilepool *pfp, const char *pathname, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	return pmemfile_fchownat(pfp, PMEMFILE_AT_CWD, pathname, owner, group,
			0);
}

int
pmemfile_lchown(PMEMfilepool *pfp, const char *pathname, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	return pmemfile_fchownat(pfp, PMEMFILE_AT_CWD, pathname, owner, group,
			PMEMFILE_AT_SYMLINK_NOFOLLOW);
}

int
pmemfile_fchown(PMEMfilepool *pfp, PMEMfile *file, pmemfile_uid_t owner,
		pmemfile_gid_t group)
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

	if (file->flags & PFILE_PATH) {
		errno = EBADF;
		return -1;
	}

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	int ret = vinode_chown(pfp, &cred, file->vinode, owner, group);

	cred_release(&cred);

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}
