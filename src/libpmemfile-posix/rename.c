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
 * rename.c -- pmemfile_rename* implementation
 */

#include "callbacks.h"
#include "dir.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "rmdir.h"
#include "unlink.h"
#include "utils.h"

/*
 * vinode_update_parent -- update .. entry of "vinode" from "src_parent" to
 * "dst_parent"
 */
static void
vinode_update_parent(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode,
		struct pmemfile_vinode *src_parent,
		struct pmemfile_vinode *dst_parent)
{
	ASSERT_IN_TX();

	struct pmemfile_dir *dir = &vinode->inode->file_data.dir;

	struct pmemfile_dirent *dirent = NULL;

	do {
		for (uint32_t i = 0; i < dir->num_elements; ++i) {
			if (strcmp(dir->dirents[i].name, "..") == 0) {
				dirent = &dir->dirents[i];
				break;
			}
		}

		if (dirent)
			break;

		dir = PF_RW(pfp, dir->next);
	} while (dir);

	ASSERTne(dirent, NULL);
	ASSERT(TOID_EQUALS(dirent->inode, src_parent->tinode));
	ASSERTeq(vinode->parent, src_parent);

	TX_ADD_DIRECT(&src_parent->inode->nlink);
	src_parent->inode->nlink--;

	TX_ADD_DIRECT(&dst_parent->inode->nlink);
	dst_parent->inode->nlink++;

	TX_ADD_DIRECT(&dirent->inode);
	dirent->inode = dst_parent->tinode;

	vinode->parent = vinode_ref(pfp, dst_parent);
}

/*
 * vinode_exchange -- swaps directory entries
 *
 * Must NOT be called in transaction.
 */
static int
vinode_exchange(PMEMfilepool *pfp,
		struct pmemfile_path_info *src,
		struct pmemfile_dirent_info *src_info,
		struct pmemfile_path_info *dst,
		struct pmemfile_dirent_info *dst_info)
{
	int error = 0;
	ASSERT_NOT_IN_TX();

	bool src_is_dir = vinode_is_dir(src_info->vinode);
	bool dst_is_dir = vinode_is_dir(dst_info->vinode);

	struct pmemfile_vinode *src_oldparent = src_info->vinode->parent;
	struct pmemfile_vinode *dst_oldparent = dst_info->vinode->parent;

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		TX_ADD_DIRECT(&src_info->dirent->inode);
		TX_ADD_DIRECT(&dst_info->dirent->inode);
		src_info->dirent->inode = dst_info->vinode->tinode;
		dst_info->dirent->inode = src_info->vinode->tinode;

		/*
		 * If both are regular files or have the same parent, then
		 * we don't have to do anything.
		 */
		if ((src_is_dir || dst_is_dir) && src->parent != dst->parent) {
			/*
			 * If only one of them is a directory, then we have to
			 * update both parent's link count.
			 */
			if (src_is_dir != dst_is_dir) {
				TX_ADD_DIRECT(&src->parent->inode->nlink);
				TX_ADD_DIRECT(&dst->parent->inode->nlink);

				if (src_is_dir) {
					src->parent->inode->nlink--;
					dst->parent->inode->nlink++;
				} else {
					src->parent->inode->nlink++;
					dst->parent->inode->nlink--;
				}
			}

			/* Update ".." entries of exchanged directories. */

			if (src_is_dir) {
				struct pmemfile_dirent *dirent =
					vinode_lookup_dirent_by_name_locked(pfp,
						src_info->vinode, "..", 2);
				TX_ADD_DIRECT(&dirent->inode);
				dirent->inode = dst->parent->tinode;
				src_info->vinode->parent = dst->parent;
			}

			if (dst_is_dir) {
				struct pmemfile_dirent *dirent =
					vinode_lookup_dirent_by_name_locked(pfp,
						dst_info->vinode, "..", 2);
				TX_ADD_DIRECT(&dirent->inode);
				dirent->inode = src->parent->tinode;
				dst_info->vinode->parent = src->parent;
			}
		}
	} TX_ONABORT {
		error = errno;
	} TX_END

	if (!error && src->parent != dst->parent) {
		if (src_is_dir) {
			vinode_ref(pfp, src_info->vinode->parent);
			vinode_unref(pfp, src_oldparent);
		}
		if (dst_is_dir) {
			vinode_ref(pfp, dst_info->vinode->parent);
			vinode_unref(pfp, dst_oldparent);
		}
	}

	return error;
}

/*
 * vinode_rename -- renames src/src_info to dst/dst_info
 *
 * Must NOT be called in transaction.
 */
static int
vinode_rename(PMEMfilepool *pfp,
		struct pmemfile_path_info *src,
		struct pmemfile_dirent_info *src_info,
		struct pmemfile_path_info *dst,
		struct pmemfile_dirent_info *dst_info,
		const char *new_path)
{
	int error = 0;
	ASSERT_NOT_IN_TX();

	size_t new_name_len = component_length(dst->remaining);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (dst_info->dirent) {
			if (vinode_is_dir(dst_info->vinode)) {
				vinode_unlink_dir(pfp, dst->parent,
						dst_info->dirent,
						dst_info->vinode,
						new_path);
			} else {
				vinode_unlink_file(pfp, dst->parent,
						dst_info->dirent,
						dst_info->vinode);
			}

			if (dst_info->vinode->inode->nlink == 0)
				vinode_orphan_unlocked(pfp, dst_info->vinode);
		}

		struct pmemfile_time t;
		tx_get_current_time(&t);

		if (src->parent == dst->parent) {
			/* optimized rename */
			pmemobj_tx_add_range_direct(src_info->dirent->name,
					new_name_len + 1);

			strncpy(src_info->dirent->name, dst->remaining,
					new_name_len);
			src_info->dirent->name[new_name_len] = '\0';

			/*
			 * From "stat" man page:
			 * "st_mtime of a directory is changed by the creation
			 * or deletion of files in that directory."
			 */
			TX_SET_DIRECT(src->parent->inode, mtime, t);
		} else {
			inode_add_dirent(pfp, dst->parent->tinode,
					dst->remaining, new_name_len,
					src_info->vinode->tinode, t);

			vinode_unlink_file(pfp, src->parent, src_info->dirent,
					src_info->vinode);

			if (vinode_is_dir(src_info->vinode))
				vinode_update_parent(pfp, src_info->vinode,
						src->parent, dst->parent);
		}
	} TX_ONABORT {
		error = errno;
	} TX_END

	if (error == 0 && src->parent != dst->parent &&
			vinode_is_dir(src_info->vinode))
		vinode_unref(pfp, src->parent);

	return error;
}

static bool
dir_is_parent_of(PMEMfilepool *pfp, struct pmemfile_vinode *possible_parent,
		struct pmemfile_vinode *possible_child)
{
	struct pmemfile_vinode *v = possible_child;

	while (v != pfp->root) {
		if (v == possible_parent)
			return true;
		v = v->parent;
	}

	return false;
}

static int
_pmemfile_renameat2(PMEMfilepool *pfp,
		struct pmemfile_vinode *olddir, const char *oldpath,
		struct pmemfile_vinode *newdir, const char *newpath,
		unsigned flags)
{
	LOG(LDBG, "oldpath %s newpath %s", oldpath, newpath);

#define PMEMFILE_RENAME_KNOWN_FLAGS ((unsigned)(PMEMFILE_RENAME_EXCHANGE | \
		PMEMFILE_RENAME_NOREPLACE | PMEMFILE_RENAME_WHITEOUT))

	if (flags & ~PMEMFILE_RENAME_KNOWN_FLAGS) {
		LOG(LSUP, "unknown flag %u",
				flags & ~PMEMFILE_RENAME_KNOWN_FLAGS);
		errno = EINVAL;
		return -1;
	}

	if (flags & PMEMFILE_RENAME_WHITEOUT) {
		LOG(LSUP, "RENAME_WHITEOUT is not supported");
		errno = EINVAL;
		return -1;
	}

	if ((flags & (PMEMFILE_RENAME_EXCHANGE | PMEMFILE_RENAME_NOREPLACE)) ==
		(PMEMFILE_RENAME_EXCHANGE | PMEMFILE_RENAME_NOREPLACE)) {
		LOG(LUSR, "both RENAME_EXCHANGE and RENAME_NOREPLACE are set");
		errno = EINVAL;
		return -1;
	}

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	struct pmemfile_path_info src, dst;
	resolve_pathat(pfp, &cred, olddir, oldpath, &src, 0);
	resolve_pathat(pfp, &cred, newdir, newpath, &dst, 0);

	int error = 0;

	if (src.error) {
		error = src.error;
		goto end;
	}

	if (dst.error) {
		error = dst.error;
		goto end;
	}

	size_t src_namelen = component_length(src.remaining);
	size_t dst_namelen = component_length(dst.remaining);

	struct pmemfile_vinode *vinodes[5];

	struct pmemfile_dirent_info src_info, dst_info;

	/*
	 * lock_parents_and_children can race with another thread messing with
	 * source or destination directory. Loop as long as race occurs.
	 */
	do {
		error = lock_parents_and_children(pfp, &src, &src_info, &dst,
				&dst_info, vinodes);
	} while (error == 1);

	if (error < 0) {
		error = -error;
		goto end;
	}

	/* both fields must be NULL or both not NULL */
	ASSERTeq(!!dst_info.vinode, !!dst_info.dirent);

	/*
	 * 2 threads doing:
	 * rename("/a/b", "/1/2/3/4/5")
	 * rename("/1/2/", "/a/b/c/d/e")
	 * could race with each other creating this situation:
	 * /1
	 * /a
	 * and unreachable cycle 3/4/c/d with "d" as parent of "3".
	 *
	 * Prevent this from happening by taking the file system lock for
	 * cross-directory renames.
	 */
	if (src.parent != dst.parent)
		os_rwlock_wrlock(&pfp->super_rwlock);

	if ((flags & PMEMFILE_RENAME_EXCHANGE) && !dst_info.vinode) {
		error = ENOENT;
		goto end_unlock;
	}

	if (!_vinode_can_access(&cred, src.parent, PFILE_WANT_WRITE)) {
		error = EACCES;
		goto end_unlock;
	}

	if (!_vinode_can_access(&cred, dst.parent, PFILE_WANT_WRITE)) {
		error = EACCES;
		goto end_unlock;
	}

	/*
	 * From "rename" manpage:
	 * "If oldpath and newpath are existing hard links referring to
	 * the same file, then rename() does nothing, and returns a success
	 * status."
	 */
	if (dst_info.vinode == src_info.vinode)
		goto end_unlock;

	/* destination file exists and user asked us to fail when it does */
	if (dst_info.dirent && (flags & PMEMFILE_RENAME_NOREPLACE)) {
		error = EEXIST;
		goto end_unlock;
	}

	/*
	 * From "rename" manpage:
	 * "EINVAL The new pathname contained a path prefix of the old, or,
	 * more generally, an attempt was made to make a directory
	 * a subdirectory of itself."
	 */
	if (src.parent != dst.parent) {
		if (vinode_is_dir(src_info.vinode) &&
			dir_is_parent_of(pfp, src_info.vinode, dst.parent)) {
			error = EINVAL;
			goto end_unlock;
		}

		if ((flags & PMEMFILE_RENAME_EXCHANGE) &&
			vinode_is_dir(dst_info.vinode) &&
			dir_is_parent_of(pfp, dst_info.vinode, src.parent)) {
			error = EINVAL;
			goto end_unlock;
		}
	}

	if (flags & PMEMFILE_RENAME_EXCHANGE) {
		error = vinode_exchange(pfp, &src, &src_info, &dst, &dst_info);
	} else {
		error = vinode_rename(pfp, &src, &src_info, &dst, &dst_info,
				newpath);
	}

	if (error == 0) {
		/* update debug information about vinodes */

		if (flags & PMEMFILE_RENAME_EXCHANGE) {
			vinode_replace_debug_path_locked(pfp, src.parent,
					dst_info.vinode, src.remaining,
					src_namelen);
		}

		vinode_replace_debug_path_locked(pfp, dst.parent,
				src_info.vinode, dst.remaining, dst_namelen);
	}

end_unlock:
	if (src.parent != dst.parent)
		os_rwlock_unlock(&pfp->super_rwlock);
	vinode_unlockN(vinodes);

	ASSERT_NOT_IN_TX();
	if (dst_info.vinode)
		vinode_unref(pfp, dst_info.vinode);

	if (src_info.vinode)
		vinode_unref(pfp, src_info.vinode);

end:
	path_info_cleanup(pfp, &dst);
	path_info_cleanup(pfp, &src);
	cred_release(&cred);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_rename(PMEMfilepool *pfp, const char *old_path, const char *new_path)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	struct pmemfile_vinode *at;

	if (!old_path || !new_path) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	if (old_path[0] == '/' && new_path[0] == '/')
		at = NULL;
	else
		at = pool_get_cwd(pfp);

	int ret = _pmemfile_renameat2(pfp, at, old_path, at, new_path, 0);

	if (at)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_renameat2(PMEMfilepool *pfp, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path, unsigned flags)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	struct pmemfile_vinode *olddir_at, *newdir_at;
	bool olddir_at_unref, newdir_at_unref;

	if (!old_path || !new_path) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	if (old_path[0] != '/' && !old_at) {
		LOG(LUSR, "NULL old dir");
		errno = EFAULT;
		return -1;
	}

	if (new_path[0] != '/' && !new_at) {
		LOG(LUSR, "NULL new dir");
		errno = EFAULT;
		return -1;
	}

	olddir_at = pool_get_dir_for_path(pfp, old_at, old_path,
			&olddir_at_unref);
	newdir_at = pool_get_dir_for_path(pfp, new_at, new_path,
			&newdir_at_unref);

	int ret = _pmemfile_renameat2(pfp, olddir_at, old_path, newdir_at,
			new_path, flags);
	int error;
	if (ret)
		error = errno;

	ASSERT_NOT_IN_TX();
	if (olddir_at_unref)
		vinode_unref(pfp, olddir_at);

	if (newdir_at_unref)
		vinode_unref(pfp, newdir_at);

	if (ret)
		errno = error;

	return ret;
}

int
pmemfile_renameat(PMEMfilepool *pfp, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path)
{
	return pmemfile_renameat2(pfp, old_at, old_path, new_at, new_path, 0);
}
