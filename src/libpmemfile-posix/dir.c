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

#include "callbacks.h"
#include "dir.h"
#include "file.h"
#include "inode.h"
#include "inode_array.h"
#include "internal.h"
#include "locks.h"
#include "os_thread.h"
#include "out.h"
#include "util.h"

/*
 * str_compare -- compares 2 strings
 *
 * s1 is NUL-terminated,
 * s2 is not - its length is s2n
 */
static int
str_compare(const char *s1, const char *s2, size_t s2n)
{
	int ret = strncmp(s1, s2, s2n);
	if (ret)
		return ret;
	if (s1[s2n] != 0)
		return 1;
	return 0;
}

/*
 * str_contains -- returns true if string contains specified character in first
 * len bytes
 */
bool
str_contains(const char *str, size_t len, char c)
{
	for (size_t i = 0; i < len; ++i)
		if (str[i] == c)
			return true;

	return false;
}

/*
 * more_than_1_component -- returns true if path contains more than one
 * component
 *
 * Deals with slashes at the end of path.
 */
bool
more_than_1_component(const char *path)
{
	path = strchr(path, '/');
	if (!path)
		return false;

	while (*path == '/')
		path++;

	if (*path == 0)
		return false;

	return true;
}

/*
 * component_length -- returns number of characters till the end of path
 * component
 */
size_t
component_length(const char *path)
{
	const char *slash = strchr(path, '/');
	if (!slash)
		return strlen(path);
	return (uintptr_t)slash - (uintptr_t)path;
}

#ifdef DEBUG
/*
 * util_strndup -- strndup (GNU extension) replacement
 */
static inline char *
util_strndup(const char *c, size_t len)
{
	char *cp = malloc(len + 1);
	if (!cp)
		return NULL;
	memcpy(cp, c, len);
	cp[len] = 0;
	return cp;
}
#endif

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
		child_vinode->path = util_strndup(name, namelen);
		if (!child_vinode->path)
			FATAL("!path allocation failed (%d)", 1);
		return;
	}

	if (strcmp(parent_vinode->path, "/") == 0) {
		child_vinode->path = malloc(namelen + 2);
		if (!child_vinode->path)
			FATAL("!path allocation failed (%d)", 2);
		sprintf(child_vinode->path, "/%.*s", (int)namelen, name);
		return;
	}

	char *p = malloc(strlen(parent_vinode->path) + 1 + namelen + 1);
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
	free(child_vinode->path);
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
 * vinode_add_dirent -- adds child inode to parent directory
 *
 * Must be called in a transaction. Caller must have exclusive access to parent
 * inode, by locking parent in WRITE mode.
 */
void
vinode_add_dirent(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) parent_tinode,
		const char *name,
		size_t namelen,
		TOID(struct pmemfile_inode) child_tinode,
		struct pmemfile_time tm)
{
	(void) pfp;

	LOG(LDBG, "parent 0x%" PRIx64 " name %.*s child_inode 0x%" PRIx64,
		parent_tinode.oid.off, (int)namelen, name,
		child_tinode.oid.off);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	if (namelen > PMEMFILE_MAX_FILE_NAME) {
		LOG(LUSR, "file name too long");
		pmemfile_tx_abort(ENAMETOOLONG);
	}

	if (str_contains(name, namelen, '/'))
		FATAL("trying to add dirent with slash: %.*s", (int)namelen,
				name);

	struct pmemfile_inode *parent = D_RW(parent_tinode);

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
			TX_SET_DIRECT(dir, next,
				TX_ZALLOC(struct pmemfile_dir, FILE_PAGE_SIZE));

			size_t sz = pmemfile_dir_size(dir->next);

			TX_ADD_DIRECT(&parent->size);
			parent->size += sz;

			D_RW(dir->next)->num_elements =
				(uint32_t)(sz - sizeof(struct pmemfile_dir)) /
					sizeof(struct pmemfile_dirent);
		}

		dir = D_RW(dir->next);
	} while (dir);

	ASSERT(dirent != NULL);
	pmemobj_tx_add_range_direct(dirent,
			sizeof(dirent->inode) + namelen + 1);

	dirent->inode = child_tinode;

	strncpy(dirent->name, name, namelen);
	dirent->name[namelen] = '\0';

	struct pmemfile_inode *child_inode = D_RW(child_tinode);
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
}

/*
 * vinode_update_parent -- update .. entry of "vinode" from "src_parent" to
 * "dst_parent"
 */
void
vinode_update_parent(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode,
		struct pmemfile_vinode *src_parent,
		struct pmemfile_vinode *dst_parent)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

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

		dir = D_RW(dir->next);
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
 * vinode_new_dir -- creates new directory relative to parent
 *
 * Note: caller must hold WRITE lock on parent.
 * Must be called in a transaction.
 */
TOID(struct pmemfile_inode)
vinode_new_dir(PMEMfilepool *pfp, struct pmemfile_vinode *parent,
		const char *name, size_t namelen, pmemfile_mode_t mode)
{
	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s new_name %.*s",
			parent ? parent->tinode.oid.off : 0,
			pmfi_path(parent), (int)namelen, name);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	if (mode & ~(pmemfile_mode_t)PMEMFILE_ACCESSPERMS) {
		/* XXX: what the kernel does in this case? */
		ERR("invalid mode flags 0%o", mode);
		pmemfile_tx_abort(EINVAL);
	}

	TOID(struct pmemfile_inode) tchild =
			inode_alloc(pfp, PMEMFILE_S_IFDIR | mode);
	struct pmemfile_inode *child = D_RW(tchild);
	struct pmemfile_time t = child->ctime;

	/* add . and .. to new directory */
	vinode_add_dirent(pfp, tchild, ".", 1, tchild, t);

	if (parent == NULL) { /* special case - root directory */
		vinode_add_dirent(pfp, tchild, "..", 2, tchild, t);
	} else {
		vinode_add_dirent(pfp, tchild, "..", 2, parent->tinode, t);
		vinode_add_dirent(pfp, parent->tinode, name, namelen, tchild,
				t);
	}

	return tchild;
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
	(void) pfp;

	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s name %.*s",
			parent->tinode.oid.off, pmfi_path(parent), (int)namelen,
			name);

	struct pmemfile_inode *iparent = parent->inode;
	if (!inode_is_dir(iparent)) {
		errno = ENOTDIR;
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

		dir = D_RW(dir->next);
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
	(void) pfp;

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

		dir = D_RW(dir->next);
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
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);

	if (namelen == 0) {
		errno = ENOENT;
		return NULL;
	}

	if ((flags & PMEMFILE_OPEN_PARENT_STOP_AT_ROOT) &&
			parent == pfp->root &&
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

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	TOID(struct pmemfile_inode) tinode = dirent->inode;
	struct pmemfile_inode *inode = D_RW(tinode);

	ASSERT(inode->nlink > 0);

	TX_ADD_FIELD(tinode, nlink);
	/*
	 * Snapshot inode and the first byte of a name (because we are going
	 * to overwrite just one byte) using one call.
	 */
	pmemobj_tx_add_range_direct(dirent, sizeof(dirent->inode) + 1);

	struct pmemfile_time tm;
	file_get_time(&tm);

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

#define DIRENT_ID_MASK 0xffffffffULL

#define DIR_ID(offset) ((offset) >> 32)
#define DIRENT_ID(offset) ((offset) & DIRENT_ID_MASK)

/*
 * file_seek_dir - translates between file->offset and dir/dirent
 *
 * returns 0 on EOF
 * returns !0 on successful translation
 */
static int
file_seek_dir(PMEMfile *file, struct pmemfile_dir **dir, unsigned *dirent)
{
	struct pmemfile_inode *inode = file->vinode->inode;

	if (file->offset == 0) {
		*dir = &inode->file_data.dir;
	} else if (DIR_ID(file->offset) == file->dir_pos.dir_id) {
		*dir = file->dir_pos.dir;
		if (*dir == NULL)
			return 0;
	} else {
		*dir = &inode->file_data.dir;

		unsigned dir_id = 0;
		while (DIR_ID(file->offset) != dir_id) {
			if (TOID_IS_NULL((*dir)->next))
				return 0;
			*dir = D_RW((*dir)->next);
			++dir_id;
		}

		file->dir_pos.dir = *dir;
		file->dir_pos.dir_id = dir_id;
	}
	*dirent = DIRENT_ID(file->offset);

	while (*dirent >= (*dir)->num_elements) {
		if (TOID_IS_NULL((*dir)->next))
			return 0;

		*dirent -= (*dir)->num_elements;
		*dir = D_RW((*dir)->next);

		file->dir_pos.dir = *dir;
		file->dir_pos.dir_id++;
	}

	file->offset = ((size_t)file->dir_pos.dir_id) << 32 | *dirent;

	return 1;
}

static int
file_getdents(PMEMfile *file, struct linux_dirent *dirp,
		unsigned count)
{
	struct pmemfile_dir *dir;
	unsigned dirent_id;

	if (file_seek_dir(file, &dir, &dirent_id) == 0)
		return 0;

	int read1 = 0;
	char *data = (void *)dirp;

	while (true) {
		if (dirent_id >= dir->num_elements) {
			if (TOID_IS_NULL(dir->next))
				break;

			dir = D_RW(dir->next);
			file->dir_pos.dir = dir;
			file->dir_pos.dir_id++;
			dirent_id = 0;
			file->offset = ((size_t)file->dir_pos.dir_id) << 32 | 0;
		}
		ASSERT(dir != NULL);

		struct pmemfile_dirent *dirent = &dir->dirents[dirent_id];
		if (TOID_IS_NULL(dirent->inode)) {
			++dirent_id;
			++file->offset;
			continue;
		}

		size_t namelen = strlen(dirent->name);
		unsigned short slen = (unsigned short)
				(8 + 8 + 2 + namelen + 1 + 1);
		unsigned short alignment = (unsigned short)(8 - (slen & 7));
		if (alignment == 8)
			alignment = 0;
		slen = (unsigned short)(slen + alignment);
		uint64_t next_off = file->offset + 1;
		if (dirent_id + 1 >= dir->num_elements)
			next_off = ((next_off >> 32) + 1) << 32;

		if (count < slen)
			break;

		memcpy(data, &dirent->inode.oid.off, 8);
		data += 8;

		memcpy(data, &next_off, 8);
		data += 8;

		memcpy(data, &slen, 2);
		data += 2;

		memcpy(data, dirent->name, namelen + 1);
		data += namelen + 1;

		while (alignment--)
			*data++ = 0;

		const struct pmemfile_inode *inode = D_RO(dirent->inode);
		if (inode_is_regular_file(inode))
			*data = PMEMFILE_DT_REG;
		else if (inode_is_symlink(inode))
			*data = PMEMFILE_DT_LNK;
		else if (inode_is_dir(inode))
			*data = PMEMFILE_DT_DIR;
		else
			ASSERT(0);
		data++;

		read1 += slen;

		++dirent_id;
		++file->offset;
	}

	return read1;
}

int
pmemfile_getdents(PMEMfilepool *pfp, PMEMfile *file,
			struct linux_dirent *dirp, unsigned count)
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

	struct pmemfile_vinode *vinode = file->vinode;

	ASSERT(vinode != NULL);
	if (!vinode_is_dir(vinode)) {
		errno = ENOTDIR;
		return -1;
	}

	if (!(file->flags & PFILE_READ)) {
		errno = EBADF;
		return -1;
	}

	if ((int)count < 0)
		count = INT_MAX;

	int bytes_read = 0;

	os_mutex_lock(&file->mutex);
	os_rwlock_rdlock(&vinode->rwlock);

	bytes_read = file_getdents(file, dirp, count);
	ASSERT(bytes_read >= 0);

	os_rwlock_unlock(&vinode->rwlock);
	os_mutex_unlock(&file->mutex);

	ASSERT((unsigned)bytes_read <= count);
	return bytes_read;
}

static int
file_getdents64(PMEMfile *file, struct linux_dirent64 *dirp,
		unsigned count)
{
	struct pmemfile_dir *dir;
	unsigned dirent_id;

	if (file_seek_dir(file, &dir, &dirent_id) == 0)
		return 0;

	int read1 = 0;
	char *data = (void *)dirp;

	while (true) {
		if (dirent_id >= dir->num_elements) {
			if (TOID_IS_NULL(dir->next))
				break;

			dir = D_RW(dir->next);
			file->dir_pos.dir = dir;
			file->dir_pos.dir_id++;
			dirent_id = 0;
			file->offset = ((size_t)file->dir_pos.dir_id) << 32 | 0;
		}
		ASSERT(dir != NULL);

		struct pmemfile_dirent *dirent = &dir->dirents[dirent_id];
		if (TOID_IS_NULL(dirent->inode)) {
			++dirent_id;
			++file->offset;
			continue;
		}

		size_t namelen = strlen(dirent->name);
		unsigned short slen = (unsigned short)
				(8 + 8 + 2 + 1 + namelen + 1);
		unsigned short alignment = (unsigned short)(8 - (slen & 7));
		if (alignment == 8)
			alignment = 0;
		slen = (unsigned short)(slen + alignment);
		uint64_t next_off = file->offset + 1;
		if (dirent_id + 1 >= dir->num_elements)
			next_off = ((next_off >> 32) + 1) << 32;

		if (count < slen)
			break;

		memcpy(data, &dirent->inode.oid.off, 8);
		data += 8;

		memcpy(data, &next_off, 8);
		data += 8;

		memcpy(data, &slen, 2);
		data += 2;

		const struct pmemfile_inode *inode = D_RO(dirent->inode);
		if (inode_is_regular_file(inode))
			*data = PMEMFILE_DT_REG;
		else if (inode_is_symlink(inode))
			*data = PMEMFILE_DT_LNK;
		else if (inode_is_dir(inode))
			*data = PMEMFILE_DT_DIR;
		else
			ASSERT(0);
		data++;

		memcpy(data, dirent->name, namelen + 1);
		data += namelen + 1;
		while (alignment--)
			*data++ = 0;

		read1 += slen;

		++dirent_id;
		++file->offset;
	}

	return read1;
}

int
pmemfile_getdents64(PMEMfilepool *pfp, PMEMfile *file,
			struct linux_dirent64 *dirp, unsigned count)
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

	struct pmemfile_vinode *vinode = file->vinode;

	if (!vinode_is_dir(vinode)) {
		errno = ENOTDIR;
		return -1;
	}

	if (!(file->flags & PFILE_READ)) {
		errno = EBADF;
		return -1;
	}

	if ((int)count < 0)
		count = INT_MAX;

	int bytes_read = 0;

	os_mutex_lock(&file->mutex);
	os_rwlock_rdlock(&vinode->rwlock);

	bytes_read = file_getdents64(file, dirp, count);
	ASSERT(bytes_read >= 0);

	os_rwlock_unlock(&vinode->rwlock);
	os_mutex_unlock(&file->mutex);

	ASSERT((unsigned)bytes_read <= count);
	return bytes_read;
}

static void
resolve_pathat_nested(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *parent, const char *path,
		struct pmemfile_path_info *path_info, int flags, int nest_level)
{
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
		parent = pfp->root;
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

		child = vinode_lookup_dirent(pfp, parent, path,
				(uintptr_t)slash - (uintptr_t)path, flags);
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
			char *new_path = malloc(strlen(symlink_target) + 1 +
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
			free(new_path);
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
	path_info->vinode = parent;

	if (!path_info->error) {
		if (!vinode_is_dir(path_info->vinode))
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
		bool resolve_last_symlink)
{
	resolve_pathat(pfp, cred, parent, path, path_info, flags);

	bool path_info_changed;
	struct pmemfile_vinode *vinode;
	do {
		path_info_changed = false;

		if (path_info->error)
			return NULL;

		size_t namelen = component_length(path_info->remaining);

		if (namelen == 0) {
			ASSERT(path_info->vinode == pfp->root);
			vinode = vinode_ref(pfp, path_info->vinode);
		} else {
			vinode = vinode_lookup_dirent(pfp, path_info->vinode,
					path_info->remaining, namelen, 0);
			if (vinode && vinode_is_symlink(vinode) &&
					resolve_last_symlink) {
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
	/* XXX: handle protected_symlinks (see man 5 proc) */

	char symlink_target[PMEMFILE_PATH_MAX];
	COMPILE_ERROR_ON(sizeof(symlink_target) < PMEMFILE_IN_INODE_STORAGE);

	os_rwlock_rdlock(&vinode->rwlock);
	strcpy(symlink_target, vinode->inode->file_data.data);
	os_rwlock_unlock(&vinode->rwlock);

	vinode_unref(pfp, vinode);

	struct pmemfile_path_info info2;
	resolve_pathat(pfp, cred, info->vinode, symlink_target, &info2, 0);
	path_info_cleanup(pfp, info);
	memcpy(info, &info2, sizeof(*info));
}

/*
 * path_info_cleanup -- clean up pmemfile_path_info object
 */
void
path_info_cleanup(PMEMfilepool *pfp, struct pmemfile_path_info *path_info)
{
	if (path_info->vinode)
		vinode_unref(pfp, path_info->vinode);
	if (path_info->remaining)
		free(path_info->remaining);
	memset(path_info, 0, sizeof(*path_info));
}

static int
_pmemfile_mkdirat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path, pmemfile_mode_t mode)
{
	struct pmemfile_path_info info;
	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return -1;

	resolve_pathat(pfp, &cred, dir, path, &info, 0);

	struct pmemfile_vinode *parent = info.vinode;
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

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!_vinode_can_access(&cred, parent, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		vinode_new_dir(pfp, parent, info.remaining, namelen, mode);
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&parent->rwlock);

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

void
vinode_cleanup(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		bool preserve_errno)
{
	int error;
	if (preserve_errno)
		error = errno;

	vinode_unref(pfp, vinode);

	if (preserve_errno)
		errno = error;
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

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	if (!TOID_IS_NULL(ddir->next)) {
		LOG(LUSR, "directory %s not empty", path);
		pmemfile_tx_abort(ENOTEMPTY);
	}

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
	file_get_time(&tm);

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
	memset(info, 0, sizeof(*info));

	size_t src_namelen = component_length(path->remaining);

	os_rwlock_rdlock(&path->vinode->rwlock);

	/* resolve file */
	*info = vinode_lookup_vinode_by_name_locked(pfp, path->vinode,
			path->remaining, src_namelen);
	if (!info->vinode) {
		int error = errno;

		os_rwlock_unlock(&path->vinode->rwlock);

		return -error;
	}

	/* drop the lock on parent */
	os_rwlock_unlock(&path->vinode->rwlock);

	/* and now lock both inodes in correct order */
	vinode_wrlock2(path->vinode, info->vinode);

	/* another thread may have modified parent, refresh */
	info->dirent = vinode_lookup_dirent_by_name_locked(pfp, path->vinode,
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
	vinode_unlock2(path->vinode, info->vinode);

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
	memset(src_info, 0, sizeof(*src_info));
	memset(dst_info, 0, sizeof(*dst_info));

	size_t src_namelen = component_length(src->remaining);
	size_t dst_namelen = component_length(dst->remaining);

	/* lock 2 parents in correct order */
	vinode_rdlock2(src->vinode, dst->vinode);

	/* find source file */
	*src_info = vinode_lookup_vinode_by_name_locked(pfp, src->vinode,
			src->remaining, src_namelen);
	if (!src_info->vinode) {
		int error = errno;

		vinode_unlock2(src->vinode, dst->vinode);

		return -error;
	}

	/* find destination file (it may not exist) */
	*dst_info = vinode_lookup_vinode_by_name_locked(pfp, dst->vinode,
			dst->remaining, dst_namelen);
	if (dst_info->dirent && !dst_info->vinode) {
		int error = errno;

		vinode_unlock2(src->vinode, dst->vinode);

		vinode_unref(pfp, src_info->vinode);
		src_info->vinode = NULL;

		return -error;
	}

	/* drop the locks on parent */
	vinode_unlock2(src->vinode, dst->vinode);

	/*
	 * and now lock all 4 inodes (both parents and children) in correct
	 * order
	 */
	vinode_wrlockN(vinodes,
			src->vinode, src_info->vinode,
			dst->vinode, dst_info->vinode);

	/* another thread may have modified [src|dst]_parent, refresh */
	src_info->dirent = vinode_lookup_dirent_by_name_locked(pfp, src->vinode,
			src->remaining, src_namelen);

	dst_info->dirent = vinode_lookup_dirent_by_name_locked(pfp, dst->vinode,
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

int
_pmemfile_rmdirat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path)
{
	struct pmemfile_path_info info;
	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
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
		ASSERT(info.vinode == pfp->root);
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

	if (dirent_info.vinode == pfp->root) {
		error = EBUSY;
		goto vdir_end;
	}

	if (!_vinode_can_access(&cred, info.vinode, PFILE_WANT_WRITE)) {
		error = EACCES;
		goto vdir_end;
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		vinode_unlink_dir(pfp, info.vinode, dirent_info.dirent,
				dirent_info.vinode, path);

		vinode_orphan(pfp, dirent_info.vinode);
	} TX_ONABORT {
		error = errno;
	} TX_END

vdir_end:
	vinode_unlock2(dirent_info.vinode, info.vinode);

	if (dirent_info.vinode)
		vinode_unref(pfp, dirent_info.vinode);

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

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

	int ret = _pmemfile_rmdirat(pfp, at, path);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

static int
_pmemfile_chdir(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *dir)
{
	struct inode_perms dir_perms = vinode_get_perms(dir);

	if (!PMEMFILE_S_ISDIR(dir_perms.flags)) {
		vinode_unref(pfp, dir);
		errno = ENOTDIR;
		return -1;
	}

	if (!can_access(cred, dir_perms, PFILE_WANT_EXECUTE)) {
		vinode_unref(pfp, dir);
		errno = EACCES;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cwd_rwlock);
	struct pmemfile_vinode *old_cwd = pfp->cwd;
	pfp->cwd = dir;
	os_rwlock_unlock(&pfp->cwd_rwlock);
	vinode_unref(pfp, old_cwd);

	return 0;
}

int
pmemfile_chdir(PMEMfilepool *pfp, const char *path)
{
	struct pmemfile_path_info info;
	struct pmemfile_vinode *at;
	struct pmemfile_cred cred;
	int ret = -1;
	int error = 0;
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
	if (get_cred(pfp, &cred))
		return -1;

	at = pool_get_dir_for_path(pfp, PMEMFILE_AT_CWD, path, &at_unref);

	struct pmemfile_vinode *dir =
		resolve_pathat_full(pfp, &cred, at, path, &info, 0, true);

	if (info.error) {
		error = info.error;
		goto end;
	}

	ret = _pmemfile_chdir(pfp, &cred, dir);
	if (ret)
		error = errno;

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (at_unref)
		vinode_unref(pfp, at);
	if (error)
		errno = error;

	return ret;
}

int
pmemfile_fchdir(PMEMfilepool *pfp, PMEMfile *dir)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!dir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return -1;
	}

	int ret;
	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return -1;
	ret = _pmemfile_chdir(pfp, &cred, vinode_ref(pfp, dir->vinode));
	put_cred(&cred);
	return ret;
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

	if (child == pfp->root)
		parent = NULL;
	else
		parent = vinode_ref(pfp, child->parent);

	os_rwlock_unlock(&child->rwlock);

	if (size == 0)
		size = PMEMFILE_PATH_MAX;

	bool allocated = false;
	if (!buf) {
		buf = malloc(size);
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
		if (parent == pfp->root)
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
		free(buf);
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
