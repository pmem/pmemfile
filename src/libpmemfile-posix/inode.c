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
 * inode.c -- inode operations
 */

#include <errno.h>
#include <inttypes.h>

#include "callbacks.h"
#include "data.h"
#include "dir.h"
#include "hash_map.h"
#include "inode.h"
#include "inode_array.h"
#include "internal.h"
#include "locks.h"
#include "os_thread.h"
#include "out.h"

/*
 * pmfi_path -- returns one of the full paths inode can be reached on
 *
 * Only for debugging.
 */
const char *
pmfi_path(struct pmemfile_vinode *vinode)
{
#ifdef DEBUG
	if (!vinode)
		return NULL;
	if (!vinode->path)
		LOG(LTRC, "0x%lx: no vinode->path", vinode->tinode.oid.off);
	return vinode->path;
#else
	return NULL;
#endif
}

static void
log_leak(uint64_t key, void *value)
{
#ifdef DEBUG
	(void) key;
	struct pmemfile_vinode *vinode = value;
	LOG(LDBG, "inode reference leak %s", vinode->path ? vinode->path :
				"unknown path");
#else
	(void) key;
	(void) value;
#endif
}

/*
 * inode_map_free -- destroys inode hash map
 */
void
inode_map_free(PMEMfilepool *pfp)
{
	struct hash_map *map = pfp->inode_map;
	int ref_leaks = hash_map_traverse(map, log_leak);
	if (ref_leaks)
		FATAL("%d inode reference leaks", ref_leaks);

	hash_map_free(map);
	pfp->inode_map = NULL;
}

/*
 * inode_ref -- returns volatile inode for persistent inode
 *
 * If inode already exists in the map it will just increase its reference
 * counter. If it doesn't it will atomically allocate, insert vinode into
 * a map and set its reference counter.
 *
 * Can't be called from transaction.
 */
struct pmemfile_vinode *
inode_ref(PMEMfilepool *pfp, TOID(struct pmemfile_inode) inode,
		struct pmemfile_vinode *parent,
		const char *name, size_t namelen)
{
	struct hash_map *map = pfp->inode_map;

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);

	if (D_RO(inode)->version != PMEMFILE_INODE_VERSION(1)) {
		ERR("unknown inode version 0x%x for inode 0x%" PRIx64,
				D_RO(inode)->version, inode.oid.off);
		errno = EINVAL;
		return NULL;
	}

	os_rwlock_rdlock(&pfp->inode_map_rwlock);

	struct pmemfile_vinode *vinode =
			hash_map_get(map, inode.oid.off);
	if (vinode)
		goto end;

	os_rwlock_unlock(&pfp->inode_map_rwlock);

	vinode = calloc(1, sizeof(*vinode));
	if (!vinode) {
		ERR("!can't allocate vinode");
		return NULL;
	}

	os_rwlock_wrlock(&pfp->inode_map_rwlock);

	struct pmemfile_vinode *put =
			hash_map_put(map, inode.oid.off, vinode);
	/* have we managed to insert vinode into hash map? */
	if (put == vinode) {
		/* finish initialization */
		os_rwlock_init(&vinode->rwlock);
		vinode->tinode = inode;
		vinode->inode = D_RW(inode);
		if (inode_is_dir(vinode->inode) && parent)
			vinode->parent = vinode_ref(pfp, parent);

		if (parent && name && namelen)
			vinode_set_debug_path_locked(pfp, parent, vinode, name,
					namelen);
	} else {
		/* another thread did it first - use it */
		free(vinode);
		vinode = put;
	}

end:
	__sync_fetch_and_add(&vinode->ref, 1);
	os_rwlock_unlock(&pfp->inode_map_rwlock);

	return vinode;
}

/*
 * vinode_ref -- increases inode runtime reference counter
 *
 * Does not need transaction.
 */
struct pmemfile_vinode *
vinode_ref(PMEMfilepool *pfp, struct pmemfile_vinode *vinode)
{
	(void) pfp;

	__sync_fetch_and_add(&vinode->ref, 1);
	return vinode;
}

/*
 * vinode_unref -- decreases inode reference counter
 *
 * Can't be called in a transaction.
 */
void
vinode_unref(PMEMfilepool *pfp, struct pmemfile_vinode *vinode)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);

	os_rwlock_wrlock(&pfp->inode_map_rwlock);

	while (vinode) {
		struct pmemfile_vinode *to_unregister = NULL;
		struct pmemfile_vinode *parent = NULL;

		TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
			if (__sync_sub_and_fetch(&vinode->ref, 1) == 0) {
				if (vinode->inode->nlink == 0) {
					inode_array_unregister(pfp,
							vinode->orphaned.arr,
							vinode->orphaned.idx);

					inode_free(pfp, vinode->tinode);
				}

				to_unregister = vinode;
				/*
				 * We don't need to take the vinode lock to
				 * read parent because at this point (when
				 * ref count drops to 0) nobody should have
				 * access to this vinode.
				 */
				parent = vinode->parent;
			}
		} TX_ONABORT {
			FATAL("!vinode_unref");
		} TX_END

		if (vinode != pfp->root)
			vinode = parent;
		else
			vinode = NULL;

		if (to_unregister) {
			struct hash_map *map = pfp->inode_map;

			if (hash_map_remove(map,
					to_unregister->tinode.oid.off,
					to_unregister))
				FATAL("vinode not found");

			vinode_destroy_data_state(pfp, to_unregister);

#ifdef DEBUG
			/* "path" field is defined only in DEBUG builds */
			free(to_unregister->path);
#endif
			os_rwlock_destroy(&to_unregister->rwlock);
			free(to_unregister);
		}
	}

	os_rwlock_unlock(&pfp->inode_map_rwlock);
}

/*
 * file_get_time -- sets *t to current time
 */
void
file_get_time(struct pmemfile_time *t)
{
	pmemfile_timespec_t tm;
	if (clock_gettime(CLOCK_REALTIME, &tm)) {
		ERR("!clock_gettime");
		pmemfile_tx_abort(errno);
	}
	t->sec = tm.tv_sec;
	t->nsec = tm.tv_nsec;
}

/*
 * inode_alloc -- allocates inode
 *
 * Must be called in a transaction.
 */
TOID(struct pmemfile_inode)
inode_alloc(PMEMfilepool *pfp, uint64_t flags)
{
	LOG(LDBG, "flags 0x%lx", flags);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	TOID(struct pmemfile_inode) tinode = TX_ZNEW(struct pmemfile_inode);
	struct pmemfile_inode *inode = D_RW(tinode);

	struct pmemfile_time t;
	file_get_time(&t);

	inode->version = PMEMFILE_INODE_VERSION(1);
	inode->flags = flags;
	inode->ctime = t;
	inode->mtime = t;
	inode->atime = t;
	inode->nlink = 0;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	inode->uid = pfp->cred.euid;
	inode->gid = pfp->cred.egid;
	os_rwlock_unlock(&pfp->cred_rwlock);

	if (inode_is_regular_file(inode))
		inode->file_data.blocks.length =
				(sizeof(inode->file_data) -
				sizeof(inode->file_data.blocks)) /
				sizeof(struct pmemfile_block);
	else if (inode_is_dir(inode)) {
		inode->file_data.dir.num_elements =
				(sizeof(inode->file_data) -
				sizeof(inode->file_data.dir)) /
				sizeof(struct pmemfile_dirent);
		inode->size = sizeof(inode->file_data);
	}

	return tinode;
}

/*
 * vinode_orphan -- register specified inode in orphaned_inodes array
 *
 * Must be called in a transaction.
 *
 * Assumes superblock lock already has been taken.
 */
void
vinode_orphan_unlocked(PMEMfilepool *pfp, struct pmemfile_vinode *vinode)
{
	LOG(LDBG, "inode 0x%" PRIx64 " path %s", vinode->tinode.oid.off,
			pmfi_path(vinode));

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);
	ASSERTeq(vinode->orphaned.arr, NULL);

	TOID(struct pmemfile_inode_array) orphaned =
			pfp->super->orphaned_inodes;

	inode_array_add(pfp, orphaned, vinode->tinode,
			&vinode->orphaned.arr, &vinode->orphaned.idx);
}

struct inode_orphan_info
inode_orphan(PMEMfilepool *pfp, TOID(struct pmemfile_inode) tinode)
{
	LOG(LDBG, "inode 0x%" PRIx64, tinode.oid.off);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	struct inode_orphan_info info;

	inode_array_add(pfp, pfp->super->orphaned_inodes, tinode, &info.arr,
			&info.idx);

	return info;
}

/*
 * vinode_orphan -- register specified inode in orphaned_inodes array
 *
 * Must be called in a transaction.
 */
void
vinode_orphan(PMEMfilepool *pfp, struct pmemfile_vinode *vinode)
{
	rwlock_tx_wlock(&pfp->super_rwlock);

	vinode_orphan_unlocked(pfp, vinode);

	rwlock_tx_unlock_on_commit(&pfp->super_rwlock);
}

/*
 * dir_assert_no_dirents -- checks that directory has no entries
 */
static void
dir_assert_no_dirents(struct pmemfile_dir *dir)
{
	for (uint32_t i = 0; i < dir->num_elements; ++i)
		if (dir->dirents[i].inode.oid.off)
			FATAL("Trying to free non-empty directory");
}

/*
 * file_inode_free -- frees inode
 *
 * Must be called in a transaction.
 */
void
inode_free(PMEMfilepool *pfp, TOID(struct pmemfile_inode) tinode)
{
	(void) pfp;

	LOG(LDBG, "inode 0x%" PRIx64, tinode.oid.off);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	struct pmemfile_inode *inode = D_RW(tinode);
	if (inode_is_dir(inode)) {
		struct pmemfile_dir *dir = &inode->file_data.dir;
		TOID(struct pmemfile_dir) tdir = TOID_NULL(struct pmemfile_dir);

		while (dir != NULL) {
			/* should have been caught earlier */
			dir_assert_no_dirents(dir);

			TOID(struct pmemfile_dir) next = dir->next;
			if (!TOID_IS_NULL(tdir))
				TX_FREE(tdir);
			tdir = next;
			dir = D_RW(tdir);
		}
	} else if (inode_is_regular_file(inode)) {
		struct pmemfile_block_array *arr = &inode->file_data.blocks;
		TOID(struct pmemfile_block_array) tarr =
				TOID_NULL(struct pmemfile_block_array);

		while (arr != NULL) {
			for (unsigned i = 0; i < arr->length; ++i)
				TX_FREE(arr->blocks[i].data);

			TOID(struct pmemfile_block_array) next = arr->next;
			if (!TOID_IS_NULL(tarr))
				TX_FREE(tarr);
			tarr = next;
			arr = D_RW(tarr);
		}
	} else if (inode_is_symlink(inode)) {
		/* nothing to be done */
	} else {
		FATAL("unknown inode type 0x%lx", inode->flags);
	}
	TX_FREE(tinode);
}

/*
 * vinode_rdlock2 -- take READ locks on specified inodes in always
 * the same order
 */
void
vinode_rdlock2(struct pmemfile_vinode *v1, struct pmemfile_vinode *v2)
{
	if (v1 == v2)
		os_rwlock_rdlock(&v2->rwlock);
	else if ((uintptr_t)v1 < (uintptr_t)v2) {
		os_rwlock_rdlock(&v1->rwlock);
		os_rwlock_rdlock(&v2->rwlock);
	} else {
		os_rwlock_rdlock(&v2->rwlock);
		os_rwlock_rdlock(&v1->rwlock);
	}
}

/*
 * vinode_wrlock2 -- take WRITE locks on specified inodes in always
 * the same order
 */
void
vinode_wrlock2(struct pmemfile_vinode *v1, struct pmemfile_vinode *v2)
{
	if (v1 == v2)
		os_rwlock_wrlock(&v2->rwlock);
	else if ((uintptr_t)v1 < (uintptr_t)v2) {
		os_rwlock_wrlock(&v1->rwlock);
		os_rwlock_wrlock(&v2->rwlock);
	} else {
		os_rwlock_wrlock(&v2->rwlock);
		os_rwlock_wrlock(&v1->rwlock);
	}
}

/*
 * vinode_unlock2 -- drop locks on specified inodes
 */
void
vinode_unlock2(struct pmemfile_vinode *v1, struct pmemfile_vinode *v2)
{
	if (v1 == v2) {
		os_rwlock_unlock(&v1->rwlock);
	} else {
		os_rwlock_unlock(&v1->rwlock);
		os_rwlock_unlock(&v2->rwlock);
	}
}

/*
 * vinode_cmp -- compares 2 inodes
 */
static int
vinode_cmp(const void *v1, const void *v2)
{
	uintptr_t v1num = (uintptr_t)*(void **)v1;
	uintptr_t v2num = (uintptr_t)*(void **)v2;
	if (v1num < v2num)
		return -1;
	if (v1num > v2num)
		return 1;
	return 0;
}

/*
 * vinode_in_array -- returns true when vinode is already in specified array
 */
static bool
vinode_in_array(const struct pmemfile_vinode *vinode,
		struct pmemfile_vinode * const *arr,
		size_t size)
{
	for (size_t i = 0; i < size; ++i)
		if (arr[i] == vinode)
			return true;
	return false;
}

/*
 * vinode_wrlockN -- take up to 4 WRITE locks on specified inodes in ascending
 * order and fill "v" with those inodes. "v" is NULL-terminated.
 */
void
vinode_wrlockN(struct pmemfile_vinode *v[static 5],
		struct pmemfile_vinode *v1,
		struct pmemfile_vinode *v2,
		struct pmemfile_vinode *v3,
		struct pmemfile_vinode *v4)
{
	size_t n = 0;
	v[n++] = v1;
	if (v2 && !vinode_in_array(v2, v, n))
		v[n++] = v2;
	if (v3 && !vinode_in_array(v3, v, n))
		v[n++] = v3;
	if (v4 && !vinode_in_array(v4, v, n))
		v[n++] = v4;
	v[n] = NULL;

	qsort(v, n, sizeof(v[0]), vinode_cmp);

	for (size_t i = n - 1; i >= 1; --i)
		ASSERT(v[i - 1] < v[i]);

	/* take all locks in order of increasing addresses */
	size_t i = 0;
	while (v[i])
		os_rwlock_wrlock(&v[i++]->rwlock);
}

/*
 * vinode_unlockN -- drop locks on specified inodes
 */
void
vinode_unlockN(struct pmemfile_vinode *v[static 5])
{
	size_t i = 0;
	while (v[i])
		os_rwlock_unlock(&v[i++]->rwlock);
}

/*
 * pmemfile_time_to_timespec -- convert between pmemfile_time and timespec
 */
static inline pmemfile_timespec_t
pmemfile_time_to_timespec(const struct pmemfile_time *t)
{
	pmemfile_timespec_t tm;
	tm.tv_sec = t->sec;
	tm.tv_nsec = t->nsec;
	return tm;
}

/*
 * vinode_stat -- fill struct stat using information from vinode
 */
static int
vinode_stat(struct pmemfile_vinode *vinode, pmemfile_stat_t *buf)
{
	struct pmemfile_inode *inode = vinode->inode;

	memset(buf, 0, sizeof(*buf));
	buf->st_dev = vinode->tinode.oid.pool_uuid_lo;
	buf->st_ino = vinode->tinode.oid.off;
	buf->st_mode = inode->flags & (PMEMFILE_S_IFMT | PMEMFILE_ALLPERMS);
	buf->st_nlink = inode->nlink;
	buf->st_uid = inode->uid;
	buf->st_gid = inode->gid;
	buf->st_rdev = 0;
	if ((pmemfile_off_t)inode->size < 0)
		return EOVERFLOW;
	buf->st_size = (pmemfile_off_t)inode->size;
	buf->st_blksize = 1;
	if ((pmemfile_blkcnt_t)inode->size < 0)
		return EOVERFLOW;

	pmemfile_blkcnt_t blks = 0;
	if (inode_is_regular_file(inode)) {
		const struct pmemfile_block_array *arr =
				&inode->file_data.blocks;
		size_t sz = 0;
		while (arr) {
			for (uint32_t i = 0; i < arr->length; ++i)
				sz += arr->blocks[i].size;
			arr = D_RO(arr->next);
		}

		/*
		 * XXX This doesn't match reality. It will match once we start
		 * getting 4k-aligned blocks from pmemobj allocator.
		 */
		blks = (pmemfile_blkcnt_t)((sz + 511) / 512);
	} else if (inode_is_dir(inode)) {
		const struct pmemfile_dir *arr = &inode->file_data.dir;
		size_t sz = 0;
		while (arr) {
			sz += pmemfile_dir_size(arr->next);
			arr = D_RO(arr->next);
		}

		/*
		 * XXX This doesn't match reality. It will match once we start
		 * getting 4k-aligned blocks from pmemobj allocator.
		 */
		blks = (pmemfile_blkcnt_t)((sz + 511) / 512);
	} else if (inode_is_symlink(inode)) {
		blks = 0;
	} else
		ASSERT(0);
	buf->st_blocks = blks;
	buf->st_atim = pmemfile_time_to_timespec(&inode->atime);
	buf->st_ctim = pmemfile_time_to_timespec(&inode->ctime);
	buf->st_mtim = pmemfile_time_to_timespec(&inode->mtime);

	return 0;
}

static int
_pmemfile_fstatat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path, pmemfile_stat_t *buf, int flags)
{
	int error = 0;
	struct pmemfile_cred cred;
	struct pmemfile_path_info info;
	struct pmemfile_vinode *vinode;

	LOG(LDBG, "path %s", path);

	if (!buf) {
		error = EFAULT;
		goto ret;
	}

	if (path[0] == 0 && (flags & PMEMFILE_AT_EMPTY_PATH)) {
		error = vinode_stat(dir, buf);
		goto ret;
	}

	if (flags & ~(PMEMFILE_AT_NO_AUTOMOUNT | PMEMFILE_AT_SYMLINK_NOFOLLOW |
			PMEMFILE_AT_EMPTY_PATH)) {
		error = EINVAL;
		goto ret;
	}

	if (get_cred(pfp, &cred)) {
		error = errno;
		goto ret;
	}

	vinode = resolve_pathat_full(pfp, &cred, dir, path, &info, 0,
				!(flags & PMEMFILE_AT_SYMLINK_NOFOLLOW));

	if (info.error) {
		error = info.error;
		goto end;
	}

	if (!vinode_is_dir(vinode) && strchr(info.remaining, '/')) {
		error = ENOTDIR;
		goto end;
	}

	error = vinode_stat(vinode, buf);

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (vinode)
		vinode_unref(pfp, vinode);
ret:
	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_fstatat(PMEMfilepool *pfp, PMEMfile *dir, const char *path,
		pmemfile_stat_t *buf, int flags)
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
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, path, &at_unref);

	int ret = _pmemfile_fstatat(pfp, at, path, buf, flags);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

/*
 * pmemfile_stat
 */
int
pmemfile_stat(PMEMfilepool *pfp, const char *path, pmemfile_stat_t *buf)
{
	return pmemfile_fstatat(pfp, PMEMFILE_AT_CWD, path, buf, 0);
}

/*
 * pmemfile_fstat
 */
int
pmemfile_fstat(PMEMfilepool *pfp, PMEMfile *file, pmemfile_stat_t *buf)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		errno = EFAULT;
		return -1;
	}

	if (!buf) {
		errno = EFAULT;
		return -1;
	}

	int ret = vinode_stat(file->vinode, buf);

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

/*
 * pmemfile_lstat
 */
int
pmemfile_lstat(PMEMfilepool *pfp, const char *path, pmemfile_stat_t *buf)
{
	return pmemfile_fstatat(pfp, PMEMFILE_AT_CWD, path, buf,
			PMEMFILE_AT_SYMLINK_NOFOLLOW);
}
