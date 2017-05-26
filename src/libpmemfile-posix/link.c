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
 * link.c -- pmemfile_link* implementation
 */

#include "callbacks.h"
#include "dir.h"
#include "internal.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

static int
_pmemfile_linkat(PMEMfilepool *pfp,
		struct pmemfile_vinode *olddir, const char *oldpath,
		struct pmemfile_vinode *newdir, const char *newpath,
		int flags)
{
	LOG(LDBG, "oldpath %s newpath %s", oldpath, newpath);

	if ((flags & ~(PMEMFILE_AT_SYMLINK_FOLLOW | PMEMFILE_AT_EMPTY_PATH))
			!= 0) {
		errno = EINVAL;
		return -1;
	}

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	struct pmemfile_path_info src, dst = { NULL, NULL, 0 };
	struct pmemfile_vinode *src_vinode;

	int error = 0;

	if (oldpath[0] == 0 && (flags & PMEMFILE_AT_EMPTY_PATH)) {
		memset(&src, 0, sizeof(src));

		src_vinode = vinode_ref(pfp, olddir);
	} else {
		src_vinode = resolve_pathat_full(pfp, &cred, olddir, oldpath,
				&src, 0, flags & PMEMFILE_AT_SYMLINK_FOLLOW);
		if (src.error) {
			error = src.error;
			goto end;
		}
	}

	if (vinode_is_dir(src_vinode)) {
		error = EPERM;
		goto end;
	}

	resolve_pathat(pfp, &cred, newdir, newpath, &dst, 0);

	if (dst.error) {
		error = dst.error;
		goto end;
	}

	/* XXX: handle protected_hardlinks (see man 5 proc) */

	size_t dst_namelen = component_length(dst.remaining);

	vinode_wrlock2(dst.parent, src_vinode);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!_vinode_can_access(&cred, dst.parent, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		struct pmemfile_time t;
		get_current_time(&t);
		inode_add_dirent(pfp, dst.parent->tinode, dst.remaining,
				dst_namelen, src_vinode->tinode, t);
	} TX_ONABORT {
		error = errno;
	} TX_END

	if (error == 0) {
		vinode_replace_debug_path_locked(pfp, dst.parent, src_vinode,
				dst.remaining, dst_namelen);
	}

	vinode_unlock2(dst.parent, src_vinode);

end:
	path_info_cleanup(pfp, &dst);
	path_info_cleanup(pfp, &src);
	cred_release(&cred);

	if (src_vinode)
		vinode_unref(pfp, src_vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_linkat(PMEMfilepool *pfp, PMEMfile *olddir, const char *oldpath,
		PMEMfile *newdir, const char *newpath, int flags)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	struct pmemfile_vinode *olddir_at, *newdir_at;
	bool olddir_at_unref, newdir_at_unref;

	if (!oldpath || !newpath) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	if (oldpath[0] != '/' && !olddir) {
		LOG(LUSR, "NULL old dir");
		errno = EFAULT;
		return -1;
	}

	if (newpath[0] != '/' && !newdir) {
		LOG(LUSR, "NULL new dir");
		errno = EFAULT;
		return -1;
	}

	olddir_at = pool_get_dir_for_path(pfp, olddir, oldpath,
			&olddir_at_unref);
	newdir_at = pool_get_dir_for_path(pfp, newdir, newpath,
			&newdir_at_unref);

	int ret = _pmemfile_linkat(pfp, olddir_at, oldpath, newdir_at, newpath,
			flags);
	int error;
	if (ret)
		error = errno;

	if (olddir_at_unref)
		vinode_unref(pfp, olddir_at);

	if (newdir_at_unref)
		vinode_unref(pfp, newdir_at);

	if (ret)
		errno = error;

	return ret;
}

/*
 * pmemfile_link -- make a new name for a file
 */
int
pmemfile_link(PMEMfilepool *pfp, const char *oldpath, const char *newpath)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	struct pmemfile_vinode *at;

	if (!oldpath || !newpath) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	if (oldpath[0] == '/' && newpath[0] == '/')
		at = NULL;
	else
		at = pool_get_cwd(pfp);

	int ret = _pmemfile_linkat(pfp, at, oldpath, at, newpath, 0);

	if (at)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}
