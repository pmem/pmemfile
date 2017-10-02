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
#include "data.h"
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
		struct pmemfile_vinode *vinode,
		struct pmemfile_time tm)
{
	LOG(LDBG, "parent 0x%" PRIx64 " ppath %s name %s",
		parent->tinode.oid.off, pmfi_path(parent), dirent->name);

	ASSERT_IN_TX();

	TOID(struct pmemfile_inode) tinode = dirent->inode;
	struct pmemfile_inode *inode = PF_RW(pfp, tinode);

	uint64_t *nlink = inode_get_nlink_ptr(inode);
	ASSERT(*nlink > 0);

	TX_ADD_DIRECT(nlink);
	/*
	 * Snapshot inode and the first byte of a name (because we are going
	 * to overwrite just one byte) using one call.
	 */
	pmemobj_tx_add_range_direct(dirent, sizeof(dirent->inode) + 1);

	if (-- *nlink > 0) {
		/*
		 * From "stat" man page:
		 * "The field st_ctime is changed by writing or by setting inode
		 * information (i.e., owner, group, link count, mode, etc.)."
		 */
		inode_tx_set_ctime(vinode->inode, tm);
	}
	/*
	 * From "stat" man page:
	 * "st_mtime of a directory is changed by the creation
	 * or deletion of files in that directory."
	 */
	inode_tx_set_mtime(parent->inode, tm);

	dirent->name[0] = '\0';
	dirent->inode = TOID_NULL(struct pmemfile_inode);
}

static struct pmemfile_inode *
parse_inode_toid(PMEMfilepool *pfp, const char *buf)
{
	if (strchr(buf, '\n') != buf + SUSPENDED_INODE_LINE_LENGTH - 1)
		pmemobj_tx_abort(EINVAL);

	uint64_t raw[2];
	TOID(struct pmemfile_inode) result;
	COMPILE_ERROR_ON(sizeof(result) != sizeof(raw));

	char *endptr;

	buf += 2; /* "0x" */
	uintmax_t n = strtoumax(buf, &endptr, 16);
	if (n == 0 || n >= UINT64_MAX || *endptr != ':')
		pmemobj_tx_abort(EINVAL);

	raw[0] = (uint64_t)n;

	buf = endptr;
	++buf; /* ":" */
	buf += 2; /* "0x" */

	n = strtoumax(buf, &endptr, 16);
	if (n == 0 || n >= UINT64_MAX || *endptr != '\n')
		pmemobj_tx_abort(EINVAL);

	raw[1] = (uint64_t)n;

	memcpy(&result, raw, sizeof(result));

	return PF_RW(pfp, result);
}

static void
decrement_susp_ref_counts(PMEMfilepool *pfp, struct pmemfile_vinode *vinode)
{
	char line[SUSPENDED_INODE_LINE_LENGTH];
	size_t offset = 0;
	struct pmemfile_block_desc *last_block = NULL;
	size_t r;

	while ((r = vinode_read(pfp, vinode, offset, &last_block,
			line, sizeof(line))) == sizeof(line)) {
		struct pmemfile_inode *inode = parse_inode_toid(pfp, line);

		TX_ADD_DIRECT(&inode->suspended_references);
		inode->suspended_references--;

		offset += sizeof(line);
	}

	if (r != 0) /* The file can't have a partial line */
		pmemobj_tx_abort(EINVAL);
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

	struct pmemfile_time t;
	if (get_current_time(&t)) {
		error = errno;
		goto end_vinode;
	}

	struct pmemfile_vinode *vinode = dirent_info.vinode;

	bool is_special_suspended_refs_inode =
	    inode_has_suspended_refs(vinode->inode);

	if (is_special_suspended_refs_inode) {
		if (!vinode->blocks)
			error = vinode_rebuild_block_tree(pfp, vinode);
		if (error)
			goto end_vinode;
		os_rwlock_wrlock(&pfp->super_rwlock);
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		vinode_unlink_file(pfp, info.parent, dirent_info.dirent,
				vinode, t);

		if (inode_get_nlink(vinode->inode) == 0) {
			if (is_special_suspended_refs_inode) {
				decrement_susp_ref_counts(pfp, vinode);
				vinode_orphan_unlocked(pfp, vinode);
			} else {
				vinode_orphan(pfp, vinode);
			}
		}
	} TX_ONABORT {
		error = errno;
	} TX_END

	if (is_special_suspended_refs_inode)
		os_rwlock_unlock(&pfp->super_rwlock);

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
