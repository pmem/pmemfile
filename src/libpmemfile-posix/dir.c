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
 * dir.c -- directory operations
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>

#include "alloc.h"
#include "blocks.h"
#include "callbacks.h"
#include "dir.h"

#include "compiler_utils.h"
#include "file.h"
#include "inode.h"
#include "inode_array.h"
#include "locks.h"
#include "os_thread.h"
#include "out.h"
#include "utils.h"

/*
 * vinode_set_debug_path_locked -- sets full path in runtime
 * structures of child_inode based on parent inode and name.
 *
 * Works only in DEBUG mode.
 * Assumes child inode is already locked.
 */
void
vinode_set_debug_path_locked(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent_vinode,
		struct pmemfile_vinode *child_vinode,
		const char *name,
		size_t namelen)
{
	(void) pfp;

#ifdef DEBUG

	if (child_vinode->path)
		return;

	if (parent_vinode == NULL) {
		child_vinode->path = pmfi_strndup(name, namelen);
		if (!child_vinode->path)
			FATAL("!path allocation failed (%d)", 1);
		return;
	}

	if (strcmp(parent_vinode->path, "/") == 0) {
		child_vinode->path = pf_malloc(namelen + 2);
		if (!child_vinode->path)
			FATAL("!path allocation failed (%d)", 2);
		sprintf(child_vinode->path, "/%.*s", (int)namelen, name);
		return;
	}

	char *p = pf_malloc(strlen(parent_vinode->path) + 1 + namelen + 1);
	if (!p)
		FATAL("!path allocation failed (%d)", 3);
	sprintf(p, "%s/%.*s", parent_vinode->path, (int)namelen, name);
	child_vinode->path = p;

#else

	(void) parent_vinode;
	(void) child_vinode;
	(void) name;
	(void) namelen;

#endif
}

/*
 * vinode_replace_debug_path_locked -- replaces full path in runtime
 * structures of child_inode based on parent inode and name.
 *
 * Works only in DEBUG mode.
 */
void
vinode_replace_debug_path_locked(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent_vinode,
		struct pmemfile_vinode *child_vinode,
		const char *name,
		size_t namelen)
{
#ifdef DEBUG
	pf_free(child_vinode->path);
	child_vinode->path = NULL;

	vinode_set_debug_path_locked(pfp, parent_vinode, child_vinode, name,
			namelen);
#else
	(void) pfp;
	(void) parent_vinode;
	(void) name;
	(void) namelen;
#endif
}

/*
 * inode_add_dirent -- adds child inode to parent directory
 *
 * Must be called in a transaction. Caller must have exclusive access to parent
 * inode, by locking parent in WRITE mode.
 */
void
inode_add_dirent(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) parent_tinode,
		const char *name,
		size_t namelen,
		TOID(struct pmemfile_inode) child_tinode,
		struct pmemfile_time tm)
{
	LOG(LDBG, "parent 0x%" PRIx64 " name %.*s child_inode 0x%" PRIx64,
		parent_tinode.oid.off, (int)namelen, name,
		child_tinode.oid.off);

	ASSERT_IN_TX();

	if (namelen > PMEMFILE_MAX_FILE_NAME) {
		LOG(LUSR, "file name too long");
		pmemfile_tx_abort(ENAMETOOLONG);
	}

	if (str_contains(name, namelen, '/'))
		FATAL("trying to add dirent with slash: %.*s", (int)namelen,
				name);

	struct pmemfile_inode *parent = PF_RW(pfp, parent_tinode);

	/* don't create files in deleted directories */
	if (parent->nlink == 0) {
		/* but let directory creation succeed */
		if (str_compare(".", name, namelen) != 0)
			pmemfile_tx_abort(ENOENT);
	}

	struct pmemfile_dir *dir = &parent->file_data.dir;

	struct pmemfile_dirent *dirent = NULL;
	bool found = false;

	do {
		for (uint32_t i = 0; i < dir->num_elements; ++i) {
			if (str_compare(dir->dirents[i].name, name, namelen)
					== 0)
				pmemfile_tx_abort(EEXIST);

			if (!found && dir->dirents[i].name[0] == 0) {
				dirent = &dir->dirents[i];
				found = true;
			}
		}

		if (!found && TOID_IS_NULL(dir->next)) {
			const struct pmem_block_info *info =
				metadata_block_info();

			dir->next = TX_XALLOC(struct pmemfile_dir, info->size,
				POBJ_XALLOC_ZERO | info->class_id);

			PF_RW(pfp, dir->next)->version =
				PMEMFILE_DIR_VERSION(1);

			size_t sz = METADATA_BLOCK_SIZE;

			TX_ADD_DIRECT(&parent->size);
			parent->size += sz;

			PF_RW(pfp, dir->next)->num_elements =
				(uint32_t)(sz - sizeof(struct pmemfile_dir)) /
					sizeof(struct pmemfile_dirent);
		}

		dir = PF_RW(pfp, dir->next);
	} while (dir);

	ASSERT(dirent != NULL);
	pmemobj_tx_add_range_direct(dirent,
			sizeof(dirent->inode) + namelen + 1);

	dirent->inode = child_tinode;

	strncpy(dirent->name, name, namelen);
	dirent->name[namelen] = '\0';

	struct pmemfile_inode *child_inode = PF_RW(pfp, child_tinode);
	TX_ADD_DIRECT(&child_inode->nlink);
	child_inode->nlink++;

	/*
	 * From "stat" man page:
	 * "The field st_ctime is changed by writing or by setting inode
	 * information (i.e., owner, group, link count, mode, etc.)."
	 */
	TX_SET_DIRECT(child_inode, ctime, tm);

	/*
	 * From "stat" man page:
	 * "st_mtime of a directory is changed by the creation
	 * or deletion of files in that directory."
	 */
	TX_SET_DIRECT(parent, mtime, tm);

	/*
	 * From "open" man page:
	 * "If the file is newly created, its st_atime, st_ctime, st_mtime
	 * fields (...) are set to the current time, and so are the st_ctime
	 * and st_mtime fields of the parent directory."
	 */
	TX_SET_DIRECT(parent, ctime, tm);
}

/*
 * vinode_lookup_dirent_by_name_locked -- looks up file name in passed directory
 *
 * Caller must hold lock on parent.
 */
struct pmemfile_dirent *
vinode_lookup_dirent_by_name_locked(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent, const char *name,
		size_t namelen)
{
	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s name %.*s",
			parent->tinode.oid.off, pmfi_path(parent), (int)namelen,
			name);

	struct pmemfile_inode *iparent = parent->inode;
	if (!inode_is_dir(iparent)) {
		errno = ENOTDIR;
		return NULL;
	}

	if (namelen > PMEMFILE_MAX_FILE_NAME) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	ASSERTne(namelen, 0);
	ASSERTne(name[0], 0);

	struct pmemfile_dir *dir = &iparent->file_data.dir;

	while (dir != NULL) {
		for (uint32_t i = 0; i < dir->num_elements; ++i) {
			struct pmemfile_dirent *d = &dir->dirents[i];

			if (str_compare(d->name, name, namelen) == 0)
				return d;
		}

		dir = PF_RW(pfp, dir->next);
	}

	errno = ENOENT;
	return NULL;
}

/*
 * vinode_lookup_vinode_by_name_locked -- looks up file name in passed
 * directory and returns dirent and vinode
 */
struct pmemfile_dirent_info
vinode_lookup_vinode_by_name_locked(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent, const char *name,
		size_t namelen)
{
	ASSERT_NOT_IN_TX();

	struct pmemfile_dirent_info out;
	out.dirent =
		vinode_lookup_dirent_by_name_locked(pfp, parent, name, namelen);
	if (!out.dirent) {
		out.vinode = NULL;
		return out;
	}

	out.vinode = inode_ref(pfp, out.dirent->inode, parent, name, namelen);
	return out;
}

/*
 * vinode_lookup_dirent_by_vinode_locked -- looks up file name in passed
 * directory
 *
 * Caller must hold lock on parent.
 */
static struct pmemfile_dirent *
vinode_lookup_dirent_by_vinode_locked(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent,	struct pmemfile_vinode *child)
{
	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s", parent->tinode.oid.off,
			pmfi_path(parent));

	struct pmemfile_inode *iparent = parent->inode;
	if (!inode_is_dir(iparent)) {
		errno = ENOTDIR;
		return NULL;
	}

	struct pmemfile_dir *dir = &iparent->file_data.dir;

	while (dir != NULL) {
		for (uint32_t i = 0; i < dir->num_elements; ++i) {
			struct pmemfile_dirent *d = &dir->dirents[i];

			if (TOID_EQUALS(d->inode, child->tinode))
				return d;
		}

		dir = PF_RW(pfp, dir->next);
	}

	errno = ENOENT;
	return NULL;
}

/*
 * vinode_lookup_dirent -- looks up file name in passed directory
 *
 * Takes reference on found inode. Caller must hold reference to parent inode.
 *
 * Can't be run in transaction.
 */
struct pmemfile_vinode *
vinode_lookup_dirent(PMEMfilepool *pfp, struct pmemfile_vinode *parent,
		const char *name, size_t namelen, int flags)
{
	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s name %s",
			parent->tinode.oid.off, pmfi_path(parent), name);
	ASSERT_NOT_IN_TX();

	if (namelen == 0) {
		errno = ENOENT;
		return NULL;
	}

	if ((flags & PMEMFILE_OPEN_PARENT_STOP_AT_ROOT) &&
			vinode_is_root(parent) &&
			str_compare("..", name, namelen) == 0) {
		errno = EXDEV;
		return NULL;
	}

	struct pmemfile_vinode *vinode = NULL;

	os_rwlock_rdlock(&parent->rwlock);

	if (str_compare("..", name, namelen) == 0) {
		vinode = vinode_ref(pfp, parent->parent);
		goto end;
	}

	struct pmemfile_dirent_info info =
		vinode_lookup_vinode_by_name_locked(pfp, parent, name, namelen);
	vinode = info.vinode;

end:
	os_rwlock_unlock(&parent->rwlock);

	return vinode;
}

static void
resolve_pathat_nested(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *parent, const char *path,
		struct pmemfile_path_info *path_info, int flags, int nest_level)
{
	ASSERT_NOT_IN_TX();

	if (nest_level > 40) {
		path_info->error = ELOOP;
		return;
	}

	if (path[0] == 0) {
		path_info->error = ENOENT;
		return;
	}

	if (path[0] == '/') {
		while (path[0] == '/')
			path++;
		parent = pfp->root[0];
	}

	const char *ending_slash = NULL;

	size_t off = strlen(path);
	while (off >= 1 && path[off - 1] == '/') {
		ending_slash = path + off - 1;
		off--;
	}

	parent = vinode_ref(pfp, parent);
	while (1) {
		struct pmemfile_vinode *child;
		const char *slash = strchr(path, '/');

		if (slash == NULL || slash == ending_slash)
			break;

		size_t namelen = (uintptr_t)slash - (uintptr_t)path;
		if (namelen > PMEMFILE_MAX_FILE_NAME) {
			path_info->error = ENAMETOOLONG;
			break;
		}

		child = vinode_lookup_dirent(pfp, parent, path, namelen, flags);
		if (!child) {
			path_info->error = errno;
			break;
		}

		os_rwlock_rdlock(&child->rwlock);
		struct inode_perms child_perms = _vinode_get_perms(child);

		/* XXX: handle protected_symlinks (see man 5 proc) */
		if (PMEMFILE_S_ISLNK(child_perms.flags)) {
			const char *symlink_target =
					child->inode->file_data.data;
			char *new_path = pf_malloc(strlen(symlink_target) + 1 +
					strlen(slash + 1) + 1);
			if (!new_path)
				path_info->error = errno;
			else
				sprintf(new_path, "%s/%s", symlink_target,
						slash + 1);
			os_rwlock_unlock(&child->rwlock);
			vinode_unref(pfp, child);

			if (!path_info->error)
				resolve_pathat_nested(pfp, cred, parent,
						new_path, path_info, flags,
						nest_level + 1);

			vinode_unref(pfp, parent);
			pf_free(new_path);
			return;
		}

		os_rwlock_unlock(&child->rwlock);

		if (PMEMFILE_S_ISDIR(child_perms.flags)) {
			int want = PFILE_WANT_EXECUTE;
			if (flags & PMEMFILE_OPEN_PARENT_USE_EACCESS)
				want |= PFILE_USE_EACCESS;
			else if (flags & PMEMFILE_OPEN_PARENT_USE_RACCESS)
				want |= PFILE_USE_RACCESS;

			if (!can_access(cred, child_perms, want)) {
				vinode_unref(pfp, child);
				path_info->error = EACCES;
				break;
			}
		}

		vinode_unref(pfp, parent);
		parent = child;
		path = slash + 1;

		while (path[0] == '/')
			path++;
	}

	path_info->remaining = strdup(path);
	path_info->parent = parent;

	if (!path_info->error) {
		if (!vinode_is_dir(path_info->parent))
			path_info->error = ENOTDIR;
		else if (more_than_1_component(path_info->remaining))
			path_info->error = ENOENT;
	}
}

/*
 * resolve_pathat - traverses directory structure
 *
 * Traverses directory structure starting from parent using pathname
 * components from path, stopping at parent of the last component.
 * If there's any problem in path resolution path_info->vinode is set to
 * the deepest inode reachable. If there's no problem in path resolution
 * path_info->vinode is set to the parent of the last component.
 * path_info->remaining is set to the part of a path that was not resolved.
 *
 * Takes reference on path_info->vinode.
 */
void
resolve_pathat(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *parent, const char *path,
		struct pmemfile_path_info *path_info, int flags)
{
	ASSERT_NOT_IN_TX();

	memset(path_info, 0, sizeof(*path_info));

	resolve_pathat_nested(pfp, cred, parent, path, path_info, flags, 1);
}

/*
 * resolve_pathat_full -- resolves full path
 */
struct pmemfile_vinode *
resolve_pathat_full(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *parent, const char *path,
		struct pmemfile_path_info *path_info, int flags,
		enum symlink_resolve last_symlink)
{
	ASSERT_NOT_IN_TX();

	resolve_pathat(pfp, cred, parent, path, path_info, flags);

	bool path_info_changed;
	struct pmemfile_vinode *vinode;
	do {
		path_info_changed = false;

		if (path_info->error)
			return NULL;

		size_t namelen = component_length(path_info->remaining);
		if (namelen > PMEMFILE_MAX_FILE_NAME) {
			path_info->error = ENAMETOOLONG;
			return NULL;
		}

		if (namelen == 0) {
			ASSERT(vinode_is_root(path_info->parent));
			vinode = vinode_ref(pfp, path_info->parent);
		} else {
			vinode = vinode_lookup_dirent(pfp, path_info->parent,
					path_info->remaining, namelen, 0);

			if (vinode && vinode_is_regular_file(vinode) &&
					path_info->remaining[namelen] == '/') {
				vinode_unref(pfp, vinode);
				path_info->error = ENOTDIR;
				return NULL;
			}

			if (vinode && vinode_is_symlink(vinode) &&
					last_symlink == RESOLVE_LAST_SYMLINK) {
				resolve_symlink(pfp, cred, vinode, path_info);
				path_info_changed = true;
			}
		}

		if (!vinode) {
			path_info->error = ENOENT;
			return NULL;
		}
	} while (path_info_changed);

	return vinode;
}

void
resolve_symlink(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode,
		struct pmemfile_path_info *info)
{
	ASSERT_NOT_IN_TX();

	/* XXX: handle protected_symlinks (see man 5 proc) */

	char symlink_target[PMEMFILE_PATH_MAX];
	COMPILE_ERROR_ON(sizeof(symlink_target) < PMEMFILE_IN_INODE_STORAGE);

	os_rwlock_rdlock(&vinode->rwlock);
	strcpy(symlink_target, vinode->inode->file_data.data);
	os_rwlock_unlock(&vinode->rwlock);

	vinode_unref(pfp, vinode);

	struct pmemfile_path_info info2;
	resolve_pathat(pfp, cred, info->parent, symlink_target, &info2, 0);
	path_info_cleanup(pfp, info);
	memcpy(info, &info2, sizeof(*info));
}

/*
 * path_info_cleanup -- clean up pmemfile_path_info object
 */
void
path_info_cleanup(PMEMfilepool *pfp, struct pmemfile_path_info *path_info)
{
	ASSERT_NOT_IN_TX();

	if (path_info->parent)
		vinode_unref(pfp, path_info->parent);
	pf_free(path_info->remaining);
	memset(path_info, 0, sizeof(*path_info));
}

/*
 * lock_parent_and_child -- resolve file with respect to parent directory and
 * lock both inodes in write mode
 *
 * Returns 0 on success.
 * Returns negated errno when failed.
 * Returns 1 when there was a race.
 */
int
lock_parent_and_child(PMEMfilepool *pfp,
		struct pmemfile_path_info *path,
		struct pmemfile_dirent_info *info)
{
	ASSERT_NOT_IN_TX();

	memset(info, 0, sizeof(*info));

	size_t src_namelen = component_length(path->remaining);

	os_rwlock_rdlock(&path->parent->rwlock);

	/* resolve file */
	*info = vinode_lookup_vinode_by_name_locked(pfp, path->parent,
			path->remaining, src_namelen);
	if (!info->vinode) {
		int error = errno;

		os_rwlock_unlock(&path->parent->rwlock);

		return -error;
	}

	/* drop the lock on parent */
	os_rwlock_unlock(&path->parent->rwlock);

	/* and now lock both inodes in correct order */
	vinode_wrlock2(path->parent, info->vinode);

	/* another thread may have modified parent, refresh */
	info->dirent = vinode_lookup_dirent_by_name_locked(pfp, path->parent,
			path->remaining, src_namelen);

	/* now we have to validate the file didn't change */

	/* file no longer exists */
	if (!info->dirent)
		goto race;

	/* another thread replaced the file with another file */
	if (!TOID_EQUALS(info->dirent->inode, info->vinode->tinode))
		goto race;

	return 0;

race:
	vinode_unlock2(path->parent, info->vinode);

	vinode_unref(pfp, info->vinode);
	info->vinode = NULL;

	info->dirent = NULL;

	return 1;
}

/*
 * lock_parents_and_children -- resolve 2 files with respect to parent
 * directories and lock all 4 inodes in write mode
 *
 * Returns 0 on success.
 * Returns negated errno when failed.
 * Returns 1 when there was a race.
 */
int
lock_parents_and_children(PMEMfilepool *pfp,
		struct pmemfile_path_info *src,
		struct pmemfile_dirent_info *src_info,

		struct pmemfile_path_info *dst,
		struct pmemfile_dirent_info *dst_info,

		struct pmemfile_vinode *vinodes[static 5])
{
	ASSERT_NOT_IN_TX();

	memset(src_info, 0, sizeof(*src_info));
	memset(dst_info, 0, sizeof(*dst_info));

	size_t src_namelen = component_length(src->remaining);
	size_t dst_namelen = component_length(dst->remaining);

	/* lock 2 parents in correct order */
	vinode_rdlock2(src->parent, dst->parent);

	/* find source file */
	*src_info = vinode_lookup_vinode_by_name_locked(pfp, src->parent,
			src->remaining, src_namelen);
	if (!src_info->vinode) {
		int error = errno;

		vinode_unlock2(src->parent, dst->parent);

		return -error;
	}

	/* find destination file (it may not exist) */
	*dst_info = vinode_lookup_vinode_by_name_locked(pfp, dst->parent,
			dst->remaining, dst_namelen);
	if (dst_info->dirent && !dst_info->vinode) {
		int error = errno;

		vinode_unlock2(src->parent, dst->parent);

		vinode_unref(pfp, src_info->vinode);
		src_info->vinode = NULL;

		return -error;
	}

	/* drop the locks on parent */
	vinode_unlock2(src->parent, dst->parent);

	/*
	 * and now lock all 4 inodes (both parents and children) in correct
	 * order
	 */
	vinode_wrlockN(vinodes,
			src->parent, src_info->vinode,
			dst->parent, dst_info->vinode);

	/* another thread may have modified [src|dst]_parent, refresh */
	src_info->dirent = vinode_lookup_dirent_by_name_locked(pfp, src->parent,
			src->remaining, src_namelen);

	dst_info->dirent = vinode_lookup_dirent_by_name_locked(pfp, dst->parent,
			dst->remaining, dst_namelen);

	/* now we have to validate the files didn't change */

	/* source file no longer exists */
	if (!src_info->dirent)
		goto race;

	/* another thread replaced the source file with another file */
	if (!TOID_EQUALS(src_info->dirent->inode, src_info->vinode->tinode))
		goto race;

	/* destination file didn't exist before, now it exists */
	if (dst_info->vinode == NULL && dst_info->dirent != NULL)
		goto race;

	/* destination file existed before */
	if (dst_info->vinode != NULL) {
		/* but now it doesn't */
		if (dst_info->dirent == NULL)
			goto race;

		/* but now path points to another file */
		if (!TOID_EQUALS(dst_info->dirent->inode,
				dst_info->vinode->tinode))
			goto race;
	}

	return 0;

race:
	vinode_unlockN(vinodes);

	vinode_unref(pfp, src_info->vinode);
	src_info->vinode = NULL;

	if (dst_info->vinode) {
		vinode_unref(pfp, dst_info->vinode);
		dst_info->vinode = NULL;
	}

	src_info->dirent = NULL;
	dst_info->dirent = NULL;

	return 1;
}

/*
 * pool_get_cwd -- returns current working directory
 *
 * Takes a reference on returned vinode.
 */
struct pmemfile_vinode *
pool_get_cwd(PMEMfilepool *pfp)
{
	struct pmemfile_vinode *cwd;

	os_rwlock_rdlock(&pfp->cwd_rwlock);
	cwd = vinode_ref(pfp, pfp->cwd);
	os_rwlock_unlock(&pfp->cwd_rwlock);

	return cwd;
}

struct pmemfile_vinode *
pool_get_dir_for_path(PMEMfilepool *pfp, PMEMfile *dir, const char *path,
		bool *unref)
{
	*unref = false;
	if (path[0] == '/')
		return NULL;

	if (dir == PMEMFILE_AT_CWD) {
		*unref = true;
		return pool_get_cwd(pfp);
	}

	return dir->vinode;
}

static char *
_pmemfile_get_dir_path(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		char *buf, size_t size)
{
	ASSERT_NOT_IN_TX();

	struct pmemfile_vinode *parent, *child = vinode;

	if (buf && size == 0) {
		vinode_unref(pfp, child);

		errno = EINVAL;
		return NULL;
	}

	os_rwlock_rdlock(&child->rwlock);

	if (child->orphaned.arr) {
		os_rwlock_unlock(&child->rwlock);
		vinode_unref(pfp, child);

		errno = ENOENT;
		return NULL;
	}

	if (vinode_is_root(child))
		parent = NULL;
	else
		parent = vinode_ref(pfp, child->parent);

	os_rwlock_unlock(&child->rwlock);

	if (size == 0)
		size = PMEMFILE_PATH_MAX;

	bool allocated = false;
	if (!buf) {
		buf = pf_malloc(size);
		if (!buf) {
			int oerrno = errno;
			vinode_unref(pfp, child);
			errno = oerrno;
			return NULL;
		}
		allocated = true;
	}

	char *curpos = buf + size;
	*(--curpos) = 0;

	if (parent == NULL) {
		if (curpos - 1 < buf)
			goto range_err;
		*(--curpos) = '/';
	}

	while (parent) {
		os_rwlock_rdlock(&parent->rwlock);
		struct pmemfile_dirent *dirent =
				vinode_lookup_dirent_by_vinode_locked(pfp,
						parent, child);
		size_t len = strlen(dirent->name);
		if (curpos - len - 1 < buf) {
			os_rwlock_unlock(&parent->rwlock);
			vinode_unref(pfp, parent);
			goto range_err;
		}
		curpos -= len;
		memcpy(curpos, dirent->name, len);

		*(--curpos) = '/';

		struct pmemfile_vinode *grandparent;
		if (vinode_is_root(parent))
			grandparent = NULL;
		else
			grandparent = vinode_ref(pfp, parent->parent);
		os_rwlock_unlock(&parent->rwlock);

		vinode_unref(pfp, child);

		child = parent;
		parent = grandparent;
	}

	vinode_unref(pfp, child);

	memmove(buf, curpos, (uintptr_t)(buf + size - curpos));

	return buf;

range_err:
	vinode_unref(pfp, child);
	if (allocated)
		pf_free(buf);
	errno = ERANGE;
	return NULL;
}

char *
pmemfile_get_dir_path(PMEMfilepool *pfp, PMEMfile *dir, char *buf, size_t size)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return NULL;
	}

	if (!dir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return NULL;
	}

	struct pmemfile_vinode *vdir;

	if (dir == PMEMFILE_AT_CWD)
		vdir = pool_get_cwd(pfp);
	else
		vdir = vinode_ref(pfp, dir->vinode);

	return _pmemfile_get_dir_path(pfp, vdir, buf, size);
}

char *
pmemfile_getcwd(PMEMfilepool *pfp, char *buf, size_t size)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return NULL;
	}

	return _pmemfile_get_dir_path(pfp, pool_get_cwd(pfp), buf, size);
}
