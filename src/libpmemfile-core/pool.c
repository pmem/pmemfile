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
 * pool.c -- pool file operations
 */

#include <errno.h>
#include <inttypes.h>

#include "callbacks.h"
#include "dir.h"
#include "inode.h"
#include "internal.h"
#include "locks.h"
#include "out.h"
#include "pool.h"

#include "os_thread.h"
#include "util.h"

/*
 * initialize_super_block -- (internal) initializes super block
 */
static int
initialize_super_block(PMEMfilepool *pfp)
{
	LOG(LDBG, "pfp %p", pfp);

	int error = 0;
	struct pmemfile_super *super = D_RW(pfp->super);

	if (!TOID_IS_NULL(super->root_inode) &&
			super->version != PMEMFILE_SUPER_VERSION(0, 1)) {
		ERR("unknown superblock version: 0x%lx", super->version);
		errno = EINVAL;
		return -1;
	}

	os_rwlock_init(&pfp->cred_rwlock);
	os_rwlock_init(&pfp->rwlock);
	os_rwlock_init(&pfp->cwd_rwlock);

	pfp->inode_map = inode_map_alloc();
	if (!pfp->inode_map) {
		error = errno;
		ERR("!cannot allocate inode map");
		goto inode_map_alloc_fail;
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!TOID_IS_NULL(super->root_inode)) {
			pfp->root = inode_ref(pfp, super->root_inode, NULL,
					NULL, NULL, 0);
		} else {
			pfp->root = vinode_new_dir(pfp, NULL, "/", 1,
					PMEMFILE_ACCESSPERMS, false, NULL);

			TX_ADD(pfp->super);
			super->version = PMEMFILE_SUPER_VERSION(0, 1);
			super->root_inode = pfp->root->tinode;
		}
		pfp->root->parent = pfp->root;
#ifdef DEBUG
		pfp->root->path = strdup("/");
#endif

		pfp->cwd = vinode_ref(pfp, pfp->root);
	} TX_ONABORT {
		error = errno;
	} TX_END

	if (error) {
		ERR("!cannot initialize super block");
		goto tx_err;
	}

	return 0;
tx_err:
	inode_map_free(pfp->inode_map);
inode_map_alloc_fail:
	os_rwlock_destroy(&pfp->rwlock);
	os_rwlock_destroy(&pfp->cwd_rwlock);
	os_rwlock_destroy(&pfp->cred_rwlock);
	errno = error;
	return -1;
}

/*
 * cleanup_orphanded_inodes_single -- (internal) cleans up one batch of inodes
 */
static void
cleanup_orphanded_inodes_single(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode_array) single)
{
	LOG(LDBG, "pfp %p arr 0x%" PRIx64, pfp, single.oid.off);

	struct pmemfile_inode_array *op = D_RW(single);
	for (unsigned i = 0; op->used && i < NUMINODES_PER_ENTRY; ++i) {
		if (TOID_IS_NULL(op->inodes[i]))
			continue;

		LOG(LINF, "closing inode left by previous run");

		ASSERTeq(D_RW(op->inodes[i])->nlink, 0);
		inode_free(pfp, op->inodes[i]);

		op->inodes[i] = TOID_NULL(struct pmemfile_inode);

		op->used--;
	}

	ASSERTeq(op->used, 0);
}

/*
 * cleanup_orphanded_inodes -- (internal) removes inodes (and frees if there are
 * no dirents referencing it) from specified list
 */
static void
cleanup_orphanded_inodes(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode_array) single)
{
	LOG(LDBG, "pfp %p", pfp);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		TOID(struct pmemfile_inode_array) last = single;

		for (; !TOID_IS_NULL(single); single = D_RO(single)->next) {
			last = single;

			/*
			 * Both used and unused arrays will be changed. Used
			 * here, unused in the following loop.
			 */
			TX_ADD(single);

			if (D_RO(single)->used > 0)
				cleanup_orphanded_inodes_single(pfp, single);
		}

		if (!TOID_IS_NULL(last)) {
			TOID(struct pmemfile_inode_array) prev;

			while (!TOID_IS_NULL(D_RO(last)->prev)) {
				prev = D_RO(last)->prev;
				TX_FREE(last);
				last = prev;
			}

			D_RW(last)->next =
					TOID_NULL(struct pmemfile_inode_array);
		}
	} TX_ONABORT {
		FATAL("!cannot cleanup list of previously deleted files");
	} TX_END
}

/*
 * pmemfile_mkfs -- create pmem file system on specified file
 */
PMEMfilepool *
pmemfile_mkfs(const char *pathname, size_t poolsize, mode_t mode)
{
	LOG(LDBG, "pathname %s poolsize %zu mode %o", pathname, poolsize, mode);

	PMEMfilepool *pfp = calloc(1, sizeof(*pfp));
	if (!pfp)
		return NULL;

	int error;
	pfp->pop = pmemobj_create(pathname, POBJ_LAYOUT_NAME(pmemfile),
			poolsize, mode);
	if (!pfp->pop) {
		error = errno;
		ERR("pmemobj_create failed: %s", pmemobj_errormsg());
		goto pool_create;
	}

	pfp->super = POBJ_ROOT(pfp->pop, struct pmemfile_super);
	if (TOID_IS_NULL(pfp->super)) {
		error = ENODEV;
		ERR("cannot initialize super block");
		goto no_super;
	}

	if (initialize_super_block(pfp)) {
		error = errno;
		goto init_failed;
	}

	return pfp;

init_failed:
no_super:
	pmemobj_close(pfp->pop);
pool_create:
	free(pfp);
	errno = error;
	return NULL;
}

/*
 * pmemfile_pool_open -- open pmem file system
 */
PMEMfilepool *
pmemfile_pool_open(const char *pathname)
{
	LOG(LDBG, "pathname %s", pathname);

	PMEMfilepool *pfp = calloc(1, sizeof(*pfp));
	if (!pfp)
		return NULL;

	int error;
	pfp->pop = pmemobj_open(pathname, POBJ_LAYOUT_NAME(pmemfile));
	if (!pfp->pop) {
		error = errno;
		ERR("pmemobj_open failed: %s", pmemobj_errormsg());
		goto pool_open;
	}

	pfp->super = (TOID(struct pmemfile_super))pmemobj_root(pfp->pop, 0);
	if (pmemobj_root_size(pfp->pop) != sizeof(struct pmemfile_super)) {
		error = ENODEV;
		ERR("pool in file %s is not initialized", pathname);
		goto no_super;
	}

	if (initialize_super_block(pfp)) {
		error = errno;
		goto init_failed;
	}

	cleanup_orphanded_inodes(pfp, D_RO(pfp->super)->orphaned_inodes);

	return pfp;

init_failed:
no_super:
	pmemobj_close(pfp->pop);
pool_open:
	free(pfp);
	errno = error;
	return NULL;
}

/*
 * pmemfile_pool_close -- close pmem file system
 */
void
pmemfile_pool_close(PMEMfilepool *pfp)
{
	LOG(LDBG, "pfp %p", pfp);

	if (pfp->cred.groups)
		free(pfp->cred.groups);

	vinode_unref_tx(pfp, pfp->cwd);
	vinode_unref_tx(pfp, pfp->root);
	inode_map_free(pfp->inode_map);
	os_rwlock_destroy(&pfp->cred_rwlock);
	os_rwlock_destroy(&pfp->rwlock);
	os_rwlock_destroy(&pfp->cwd_rwlock);

	pmemobj_close(pfp->pop);

	memset(pfp, 0, sizeof(*pfp));

	free(pfp);
}

static bool
gid_in_list(const struct pmemfile_cred *cred, gid_t gid)
{
	for (size_t i = 0; i < cred->groupsnum; ++i) {
		if (cred->groups[i] == gid)
			return true;
	}

	return false;
}

bool
can_access(const struct pmemfile_cred *cred, struct inode_perms perms, int acc)
{
	mode_t perm = perms.flags & PMEMFILE_ACCESSPERMS;
	mode_t req = 0;

	if (perms.uid == cred->fsuid) {
		if (acc & PFILE_WANT_READ)
			req |=  PMEMFILE_S_IRUSR;
		if (acc & PFILE_WANT_WRITE)
			req |=  PMEMFILE_S_IWUSR;
		if (acc & PFILE_WANT_EXECUTE)
			req |=  PMEMFILE_S_IXUSR;
	} else if (perms.gid == cred->fsgid || gid_in_list(cred, perms.gid)) {
		if (acc & PFILE_WANT_READ)
			req |=  PMEMFILE_S_IRGRP;
		if (acc & PFILE_WANT_WRITE)
			req |=  PMEMFILE_S_IWGRP;
		if (acc & PFILE_WANT_EXECUTE)
			req |=  PMEMFILE_S_IXGRP;
	} else {
		if (acc & PFILE_WANT_READ)
			req |=  PMEMFILE_S_IROTH;
		if (acc & PFILE_WANT_WRITE)
			req |=  PMEMFILE_S_IWOTH;
		if (acc & PFILE_WANT_EXECUTE)
			req |=  PMEMFILE_S_IXOTH;
	}

	return ((perm & req) == req);
}

static int
copy_cred(struct pmemfile_cred *dst_cred, struct pmemfile_cred *src_cred)
{
	dst_cred->fsuid = src_cred->fsuid;
	dst_cred->fsgid = src_cred->fsgid;
	dst_cred->groupsnum = src_cred->groupsnum;
	if (dst_cred->groupsnum) {
		dst_cred->groups = malloc(dst_cred->groupsnum *
				sizeof(dst_cred->groups[0]));
		if (!dst_cred->groups)
			return -1;
		memcpy(dst_cred->groups, src_cred->groups, dst_cred->groupsnum *
				sizeof(dst_cred->groups[0]));
	} else {
		dst_cred->groups = NULL;
	}

	return 0;
}

int
get_cred(PMEMfilepool *pfp, struct pmemfile_cred *cred)
{
	int ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = copy_cred(cred, &pfp->cred);
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

void
put_cred(struct pmemfile_cred *cred)
{
	free(cred->groups);
	memset(cred, 0, sizeof(*cred));
}
