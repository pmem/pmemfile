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

bool
str_contains(const char *str, size_t len, char c)
{
	for (size_t i = 0; i < len; ++i)
		if (str[i] == c)
			return true;

	return false;
}

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

size_t
component_length(const char *path)
{
	const char *slash = strchr(path, '/');
	if (!slash)
		return strlen(path);
	return (uintptr_t)slash - (uintptr_t)path;
}

#ifdef DEBUG
static inline char *
util_strndup(const char *c, size_t len)
{
	char *cp = malloc(len + 1);
	memcpy(cp, c, len);
	cp[len] = 0;
	return cp;
}
#endif

/*
 * vinode_set_debug_path_locked -- (internal) sets full path in runtime
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
		return;
	}

	if (strcmp(parent_vinode->path, "/") == 0) {
		child_vinode->path = malloc(namelen + 2);
		sprintf(child_vinode->path, "/%.*s", (int)namelen, name);
		return;
	}

	char *p = malloc(strlen(parent_vinode->path) + 1 + namelen + 1);
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
 * vinode_set_debug_path -- sets full path in runtime structures
 * of child_inode based on parent inode and name.
 */
void
vinode_set_debug_path(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent_vinode,
		struct pmemfile_vinode *child_vinode,
		const char *name,
		size_t namelen)
{
	os_rwlock_wrlock(&child_vinode->rwlock);

	vinode_set_debug_path_locked(pfp, parent_vinode, child_vinode, name,
			namelen);

	os_rwlock_unlock(&child_vinode->rwlock);
}

/*
 * vinode_clear_debug_path -- clears full path in runtime structures
 */
void
vinode_clear_debug_path(PMEMfilepool *pfp, struct pmemfile_vinode *vinode)
{
	(void) pfp;

	os_rwlock_wrlock(&vinode->rwlock);
#ifdef DEBUG
	free(vinode->path);
	vinode->path = NULL;
#endif
	os_rwlock_unlock(&vinode->rwlock);
}

/*
 * vinode_add_dirent -- adds child inode to parent directory
 *
 * Must be called in transaction. Caller must have exclusive access to parent
 * inode, by locking parent in WRITE mode.
 */
void
vinode_add_dirent(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent_vinode,
		const char *name,
		size_t namelen,
		struct pmemfile_vinode *child_vinode,
		const struct pmemfile_time *tm)
{
	(void) pfp;

	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s name %.*s "
			"child_inode 0x%" PRIx64, parent_vinode->tinode.oid.off,
			pmfi_path(parent_vinode), (int)namelen, name,
			child_vinode->tinode.oid.off);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	if (namelen > PMEMFILE_MAX_FILE_NAME) {
		LOG(LUSR, "file name too long");
		pmemfile_tx_abort(ENAMETOOLONG);
	}

	if (str_contains(name, namelen, '/'))
		FATAL("trying to add dirent with slash: %.*s", (int)namelen,
				name);

	struct pmemfile_inode *parent = parent_vinode->inode;

	/* don't create files in deleted directories */
	if (parent->nlink == 0)
		/* but let directory creation succeed */
		if (str_compare(".", name, namelen) != 0)
			pmemfile_tx_abort(ENOENT);

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

	dirent->inode = child_vinode->tinode;

	strncpy(dirent->name, name, namelen);
	dirent->name[namelen] = '\0';

	TX_ADD_FIELD_DIRECT(child_vinode->inode, nlink);
	child_vinode->inode->nlink++;

	/*
	 * From "stat" man page:
	 * "The field st_ctime is changed by writing or by setting inode
	 * information (i.e., owner, group, link count, mode, etc.)."
	 */
	TX_SET_DIRECT(child_vinode->inode, ctime, *tm);

	/*
	 * From "stat" man page:
	 * "st_mtime of a directory is changed by the creation
	 * or deletion of files in that directory."
	 */
	TX_SET_DIRECT(parent_vinode->inode, mtime, *tm);
}

/*
 * vinode_new_dir -- creates new directory relative to parent
 *
 * Note: caller must hold WRITE lock on parent.
 */
struct pmemfile_vinode *
vinode_new_dir(PMEMfilepool *pfp, struct pmemfile_vinode *parent,
		const char *name, size_t namelen, pmemfile_mode_t mode,
		bool add_to_parent, volatile bool *parent_refed)
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

	struct pmemfile_time t;
	struct pmemfile_vinode *child = inode_alloc(pfp,
			PMEMFILE_S_IFDIR | mode, &t, parent, parent_refed,
			name, namelen);

	/* add . and .. to new directory */
	vinode_add_dirent(pfp, child, ".", 1, child, &t);

	if (parent == NULL) /* special case - root directory */
		vinode_add_dirent(pfp, child, "..", 2, child, &t);
	else
		vinode_add_dirent(pfp, child, "..", 2, parent, &t);

	if (add_to_parent)
		vinode_add_dirent(pfp, parent, name, namelen, child, &t);

	return child;
}

/*
 * vinode_lookup_dirent_by_name_locked -- looks up file name in passed directory
 *
 * Caller must hold lock on parent.
 */
static struct pmemfile_dirent *
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
 * Does not need transaction.
 */
struct pmemfile_vinode *
vinode_lookup_dirent(PMEMfilepool *pfp, struct pmemfile_vinode *parent,
		const char *name, size_t namelen, int flags)
{
	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s name %s",
			parent->tinode.oid.off, pmfi_path(parent), name);

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

	struct pmemfile_dirent *dirent =
		vinode_lookup_dirent_by_name_locked(pfp, parent, name, namelen);
	if (dirent) {
		bool parent_refed = false;
		vinode = inode_ref(pfp, dirent->inode, parent, &parent_refed,
				name, namelen);

		if (!vinode && parent_refed)
			vinode_unref_tx(pfp, parent);
	}

end:
	os_rwlock_unlock(&parent->rwlock);

	return vinode;
}

/*
 * vinode_unlink_dirent -- removes dirent from directory
 *
 * Must be called in transaction. Caller must have exclusive access to parent
 * inode, eg by locking parent in WRITE mode.
 */
void
vinode_unlink_dirent(PMEMfilepool *pfp, struct pmemfile_vinode *parent,
		const char *name, size_t namelen,
		struct pmemfile_vinode *volatile *vinode,
		volatile bool *parent_refed, bool abort_on_ENOENT)
{
	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s name %.*s",
			parent->tinode.oid.off, pmfi_path(parent), (int)namelen,
			name);

	struct pmemfile_dirent *dirent =
			vinode_lookup_dirent_by_name_locked(pfp, parent, name,
					namelen);
	if (!dirent) {
		if (errno == ENOENT && !abort_on_ENOENT)
			return;
		pmemfile_tx_abort(errno);
	}

	TOID(struct pmemfile_inode) tinode = dirent->inode;
	struct pmemfile_inode *inode = D_RW(tinode);

	if (inode_is_dir(inode))
		pmemfile_tx_abort(EISDIR);

	*vinode = inode_ref(pfp, tinode, parent, parent_refed, NULL, 0);
	rwlock_tx_wlock(&(*vinode)->rwlock);

	ASSERT(inode->nlink > 0);

	TX_ADD_FIELD(tinode, nlink);
	pmemobj_tx_add_range_direct(dirent, sizeof(dirent->inode) + 1);

	struct pmemfile_time tm;
	file_get_time(&tm);

	if (--inode->nlink == 0)
		vinode_orphan(pfp, *vinode);
	else {
		/*
		 * From "stat" man page:
		 * "The field st_ctime is changed by writing or by setting inode
		 * information (i.e., owner, group, link count, mode, etc.)."
		 */
		TX_SET_DIRECT((*vinode)->inode, ctime, tm);
	}
	/*
	 * From "stat" man page:
	 * "st_mtime of a directory is changed by the creation
	 * or deletion of files in that directory."
	 */
	TX_SET_DIRECT(parent->inode, mtime, tm);

	rwlock_tx_unlock_on_commit(&(*vinode)->rwlock);

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
	(void) pfp;

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
	(void) pfp;

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
resolve_pathat_nested(PMEMfilepool *pfp, struct pmemfile_cred *cred,
		struct pmemfile_vinode *parent, const char *path,
		struct pmemfile_path_info *path_info, int flags, int nest_level)
{
	if (nest_level > 40) {
		path_info->error = ELOOP;
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

		// XXX: handle protected_symlinks (see man 5 proc)
		if (PMEMFILE_S_ISLNK(child_perms.flags)) {
			const char *symlink_target =
					child->inode->file_data.data;
			char *new_path = malloc(strlen(symlink_target) + 1 +
					strlen(slash + 1) + 1);
			sprintf(new_path, "%s/%s", symlink_target, slash + 1);
			os_rwlock_unlock(&child->rwlock);
			vinode_unref_tx(pfp, child);

			resolve_pathat_nested(pfp, cred, parent, new_path,
					path_info, flags, nest_level + 1);

			vinode_unref_tx(pfp, parent);
			free(new_path);
			return;
		}

		os_rwlock_unlock(&child->rwlock);

		if (PMEMFILE_S_ISDIR(child_perms.flags)) {
			if (!can_access(cred, child_perms,
					PFILE_WANT_EXECUTE)) {
				vinode_unref_tx(pfp, child);
				path_info->error = EACCES;
				break;
			}
		}

		vinode_unref_tx(pfp, parent);
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
resolve_pathat(PMEMfilepool *pfp, struct pmemfile_cred *cred,
		struct pmemfile_vinode *parent, const char *path,
		struct pmemfile_path_info *path_info, int flags)
{
	memset(path_info, 0, sizeof(*path_info));

	resolve_pathat_nested(pfp, cred, parent, path, path_info, flags, 1);
}

void
resolve_symlink(PMEMfilepool *pfp, struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode,
		struct pmemfile_path_info *info)
{
	// XXX: handle protected_symlinks (see man 5 proc)

	char symlink_target[PMEMFILE_PATH_MAX];
	COMPILE_ERROR_ON(sizeof(symlink_target) < PMEMFILE_IN_INODE_STORAGE);

	os_rwlock_rdlock(&vinode->rwlock);
	strcpy(symlink_target, vinode->inode->file_data.data);
	os_rwlock_unlock(&vinode->rwlock);

	vinode_unref_tx(pfp, vinode);

	struct pmemfile_path_info info2;
	resolve_pathat(pfp, cred, info->vinode, symlink_target, &info2, 0);
	path_info_cleanup(pfp, info);
	memcpy(info, &info2, sizeof(*info));
}

void
path_info_cleanup(PMEMfilepool *pfp, struct pmemfile_path_info *path_info)
{
	if (path_info->vinode)
		vinode_unref_tx(pfp, path_info->vinode);
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
	volatile bool parent_refed = false;

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

	struct pmemfile_vinode *child = NULL;
	struct inode_perms perms = _vinode_get_perms(parent);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!can_access(&cred, perms, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		child = vinode_new_dir(pfp, parent, info.remaining, namelen,
				mode, true, &parent_refed);
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&parent->rwlock);

	if (!error)
		vinode_unref_tx(pfp, child);

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (error) {
		if (parent_refed)
			vinode_unref_tx(pfp, parent);

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

	if (!path) {
		errno = ENOENT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, path, &at_unref);

	int ret = _pmemfile_mkdirat(pfp, at, path, mode);

	if (at_unref) {
		int error;
		if (ret)
			error = errno;

		vinode_unref_tx(pfp, at);

		if (ret)
			errno = error;
	}

	return ret;
}

int
pmemfile_mkdir(PMEMfilepool *pfp, const char *path, pmemfile_mode_t mode)
{
	return pmemfile_mkdirat(pfp, PMEMFILE_AT_CWD, path, mode);
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

	struct pmemfile_vinode *vparent = info.vinode;
	struct pmemfile_vinode *vdir = NULL;
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
		ASSERT(vparent == pfp->root);
		error = EBUSY;
		goto end;
	}

	struct pmemfile_inode *iparent = vparent->inode;

	os_rwlock_wrlock(&vparent->rwlock);

	struct pmemfile_dirent *dirent =
			vinode_lookup_dirent_by_name_locked(pfp, vparent,
						info.remaining, namelen);
	if (!dirent) {
		error = ENOENT;
		goto vparent_end;
	}

	vdir = inode_ref(pfp, dirent->inode, vparent, NULL, info.remaining,
			namelen);
	if (!vdir) {
		error = errno;
		goto vparent_end;
	}

	if (!vinode_is_dir(vdir)) {
		error = ENOTDIR;
		goto vparent_end;
	}

	if (vdir == pfp->root) {
		error = EBUSY;
		goto vparent_end;
	}

	struct inode_perms perms = _vinode_get_perms(vparent);

	if (!can_access(&cred, perms, PFILE_WANT_WRITE)) {
		error = EACCES;
		goto vparent_end;
	}

	os_rwlock_wrlock(&vdir->rwlock);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		struct pmemfile_inode *idir = vdir->inode;
		struct pmemfile_dir *ddir = &idir->file_data.dir;
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

		vinode_orphan(pfp, vdir);

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
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&vdir->rwlock);

vparent_end:
	os_rwlock_unlock(&vparent->rwlock);

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (vdir)
		vinode_unref_tx(pfp, vdir);

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

	if (!path) {
		errno = ENOENT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, PMEMFILE_AT_CWD, path, &at_unref);

	int ret = _pmemfile_rmdirat(pfp, at, path);

	if (at_unref) {
		int error;
		if (ret)
			error = errno;

		vinode_unref_tx(pfp, at);

		if (ret)
			errno = error;
	}

	return ret;
}

static int
_pmemfile_chdir(PMEMfilepool *pfp, struct pmemfile_cred *cred,
		struct pmemfile_vinode *dir)
{
	struct inode_perms dir_perms = vinode_get_perms(dir);

	if (!PMEMFILE_S_ISDIR(dir_perms.flags)) {
		vinode_unref_tx(pfp, dir);
		errno = ENOTDIR;
		return -1;
	}

	if (!can_access(cred, dir_perms, PFILE_WANT_EXECUTE)) {
		vinode_unref_tx(pfp, dir);
		errno = EACCES;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cwd_rwlock);
	struct pmemfile_vinode *old_cwd = pfp->cwd;
	pfp->cwd = dir;
	os_rwlock_unlock(&pfp->cwd_rwlock);
	vinode_unref_tx(pfp, old_cwd);

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

	if (!path) {
		errno = ENOENT;
		return -1;
	}
	if (get_cred(pfp, &cred))
		return -1;

	at = pool_get_dir_for_path(pfp, PMEMFILE_AT_CWD, path, &at_unref);

	resolve_pathat(pfp, &cred, at, path, &info, 0);

	bool path_info_changed;
	struct pmemfile_vinode *dir;
	do {
		path_info_changed = false;

		if (info.error) {
			error = info.error;
			goto end;
		}

		size_t namelen = component_length(info.remaining);

		if (namelen == 0) {
			ASSERT(info.vinode == pfp->root);
			dir = vinode_ref(pfp, info.vinode);
		} else {
			dir = vinode_lookup_dirent(pfp, info.vinode,
					info.remaining, namelen, 0);
			if (dir && vinode_is_symlink(dir)) {
				resolve_symlink(pfp, &cred, dir, &info);
				path_info_changed = true;
			}
		}

		if (!dir) {
			error = ENOENT;
			goto end;
		}
	} while (path_info_changed);

	ret = _pmemfile_chdir(pfp, &cred, dir);
	if (ret)
		error = errno;

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (at_unref)
		vinode_unref_tx(pfp, at);
	if (error)
		errno = error;

	return ret;
}

int
pmemfile_fchdir(PMEMfilepool *pfp, PMEMfile *dir)
{
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
		vinode_unref_tx(pfp, child);

		errno = EINVAL;
		return NULL;
	}

	os_rwlock_rdlock(&child->rwlock);

	if (child->orphaned.arr) {
		os_rwlock_unlock(&child->rwlock);
		vinode_unref_tx(pfp, child);

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
			vinode_unref_tx(pfp, child);
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
			vinode_unref_tx(pfp, parent);
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

		vinode_unref_tx(pfp, child);

		child = parent;
		parent = grandparent;
	}

	vinode_unref_tx(pfp, child);

	memmove(buf, curpos, (uintptr_t)(buf + size - curpos));

	return buf;

range_err:
	vinode_unref_tx(pfp, child);
	if (allocated)
		free(buf);
	errno = ERANGE;
	return NULL;
}

char *
pmemfile_get_dir_path(PMEMfilepool *pfp, PMEMfile *dir, char *buf, size_t size)
{
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
	return _pmemfile_get_dir_path(pfp, pool_get_cwd(pfp), buf, size);
}
