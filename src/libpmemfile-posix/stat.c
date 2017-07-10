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
 * stat.c -- pmemfile_*stat* implementation
 */

#include "creds.h"
#include "dir.h"
#include "file.h"
#include "inode.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "utils.h"
#include "cache.h"

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
vinode_stat(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		pmemfile_stat_t *buf)
{
	struct pmemfile_inode *inode = vinode->inode;

	if (!buf)
		return EFAULT;

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

		if (is_cache_valid(vinode->stat_block_cache)) {
			sz = vinode->stat_block_cache;
		} else {
			while (arr) {
				for (uint32_t i = 0; i < arr->length; ++i)
					sz += arr->blocks[i].size;
				arr = PF_RO(pfp, arr->next);
			}

			vinode->stat_block_cache = sz;
		}

		/*
		 * XXX This doesn't match reality. It will match once we start
		 * getting 4k-aligned blocks from pmemobj allocator.
		 */
		blks = (pmemfile_blkcnt_t)((sz + 511) / 512);
	} else if (inode_is_dir(inode)) {
		const struct pmemfile_dir *arr = &inode->file_data.dir;
		size_t sz = 0;

		if (is_cache_valid(vinode->stat_block_cache)) {
			sz = vinode->stat_block_cache;
		} else {
			while (arr) {
				sz += pmemfile_dir_size(arr->next);
				arr = PF_RO(pfp, arr->next);
			}

			vinode->stat_block_cache = sz;
		}

		/*
		 * XXX This doesn't match reality. It will match once we start
		 * getting 4k-aligned blocks from pmemobj allocator.
		 */
		blks = (pmemfile_blkcnt_t)((sz + 511) / 512);
	} else if (inode_is_symlink(inode)) {
		blks = 0;
	} else {
		ASSERT(0);
	}
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

	if (path[0] == 0 && (flags & PMEMFILE_AT_EMPTY_PATH)) {
		error = vinode_stat(pfp, dir, buf);
		goto ret;
	}

	if (flags & ~(PMEMFILE_AT_NO_AUTOMOUNT | PMEMFILE_AT_SYMLINK_NOFOLLOW |
			PMEMFILE_AT_EMPTY_PATH)) {
		error = EINVAL;
		goto ret;
	}

	if (cred_acquire(pfp, &cred)) {
		error = errno;
		goto ret;
	}

	vinode = resolve_pathat_full(pfp, &cred, dir, path, &info, 0,
				(flags & PMEMFILE_AT_SYMLINK_NOFOLLOW) ?
						NO_RESOLVE_LAST_SYMLINK :
						RESOLVE_LAST_SYMLINK);

	if (info.error) {
		error = info.error;
		goto end;
	}

	error = vinode_stat(pfp, vinode, buf);

end:
	path_info_cleanup(pfp, &info);
	cred_release(&cred);

	ASSERT_NOT_IN_TX();
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
		errno = EFAULT;
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

	int ret = vinode_stat(pfp, file->vinode, buf);

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
