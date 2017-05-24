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
 * pool.c -- pool file/global operations
 */

#include <errno.h>
#include <inttypes.h>

#include "callbacks.h"
#include "compiler_utils.h"
#include "dir.h"
#include "hash_map.h"
#include "inode.h"
#include "internal.h"
#include "locks.h"
#include "os_thread.h"
#include "out.h"
#include "pool.h"

/*
 * initialize_super_block -- initializes super block
 *
 * Can't be called in a transaction.
 */
static int
initialize_super_block(PMEMfilepool *pfp)
{
	LOG(LDBG, "pfp %p", pfp);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);

	int error = 0;
	struct pmemfile_super *super = pfp->super;

	if (!TOID_IS_NULL(super->root_inode) &&
			super->version != PMEMFILE_SUPER_VERSION(0, 1)) {
		ERR("unknown superblock version: 0x%lx", super->version);
		errno = EINVAL;
		return -1;
	}

	os_rwlock_init(&pfp->cred_rwlock);
	os_rwlock_init(&pfp->super_rwlock);
	os_rwlock_init(&pfp->cwd_rwlock);
	os_rwlock_init(&pfp->inode_map_rwlock);

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred)) {
		error = errno;
		goto get_cred_fail;
	}

	pfp->inode_map = hash_map_alloc();
	if (!pfp->inode_map) {
		error = errno;
		ERR("!cannot allocate inode map");
		goto inode_map_alloc_fail;
	}

	if (TOID_IS_NULL(super->root_inode)) {
		TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
			TX_ADD_DIRECT(super);
			super->root_inode = vinode_new_dir(pfp, NULL, "/", 1,
					&cred, PMEMFILE_ACCESSPERMS);

			super->version = PMEMFILE_SUPER_VERSION(0, 1);
			super->orphaned_inodes =
					TX_ZNEW(struct pmemfile_inode_array);
		} TX_ONABORT {
			error = errno;
		} TX_END
	}

	pfp->root = inode_ref(pfp, super->root_inode, NULL, NULL, 0);
	if (!pfp->root)
		error = errno;

	if (error) {
		ERR("!cannot initialize super block");
		goto tx_err;
	}

	pfp->root->parent = pfp->root;
#ifdef DEBUG
	pfp->root->path = strdup("/");
#endif

	pfp->cwd = vinode_ref(pfp, pfp->root);
	cred_release(&cred);

	return 0;
tx_err:
	inode_map_free(pfp);
inode_map_alloc_fail:
	cred_release(&cred);
get_cred_fail:
	os_rwlock_destroy(&pfp->super_rwlock);
	os_rwlock_destroy(&pfp->cwd_rwlock);
	os_rwlock_destroy(&pfp->cred_rwlock);
	os_rwlock_destroy(&pfp->inode_map_rwlock);
	errno = error;
	return -1;
}

/*
 * cleanup_orphaned_inodes_single -- cleans up one batch of inodes
 *
 * Must be called in a transaction.
 */
static void
cleanup_orphaned_inodes_single(PMEMfilepool *pfp,
		struct pmemfile_inode_array *arr)
{
	LOG(LDBG, "pfp %p", pfp);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	if (arr->used == 0)
		return;

	TX_ADD_DIRECT(arr);

	for (unsigned i = 0; arr->used && i < NUMINODES_PER_ENTRY; ++i) {
		if (TOID_IS_NULL(arr->inodes[i]))
			continue;

		LOG(LINF, "closing inode left by previous run");

		ASSERTeq(D_RW(arr->inodes[i])->nlink, 0);
		inode_free(pfp, arr->inodes[i]);

		arr->inodes[i] = TOID_NULL(struct pmemfile_inode);

		arr->used--;
	}

	ASSERTeq(arr->used, 0);
}

/*
 * cleanup_orphaned_inodes -- removes inodes (and frees if there are
 * no dirents referencing it) from specified list
 *
 * Can't be called in a transaction.
 */
static void
cleanup_orphaned_inodes(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode_array) arr)
{
	LOG(LDBG, "pfp %p", pfp);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);

	struct pmemfile_inode_array *first = D_RW(arr);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		cleanup_orphaned_inodes_single(pfp, first);

		TOID(struct pmemfile_inode_array) tcur = first->next;
		TX_ADD_DIRECT(&first->next);
		first->next = TOID_NULL(struct pmemfile_inode_array);

		struct pmemfile_inode_array *cur = D_RW(tcur);

		while (cur) {
			cleanup_orphaned_inodes_single(pfp, cur);
			TOID(struct pmemfile_inode_array) tmp = cur->next;
			TX_FREE(tcur);
			tcur = tmp;
			cur = D_RW(tcur);
		}
	} TX_ONABORT {
		FATAL("!cannot cleanup list of previously deleted files");
	} TX_END
}

/*
 * pmemfile_pool_create -- create pmem file system on specified file
 */
PMEMfilepool *
pmemfile_pool_create(const char *pathname, size_t poolsize,
		pmemfile_mode_t mode)
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

	TOID(struct pmemfile_super) super =
			POBJ_ROOT(pfp->pop, struct pmemfile_super);
	if (TOID_IS_NULL(super)) {
		error = ENODEV;
		ERR("cannot initialize super block");
		goto no_super;
	}
	pfp->super = D_RW(super);

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

	PMEMoid super = pmemobj_root(pfp->pop, 0);
	if (pmemobj_root_size(pfp->pop) != sizeof(struct pmemfile_super)) {
		error = ENODEV;
		ERR("pool in file %s is not initialized", pathname);
		goto no_super;
	}
	pfp->super = pmemobj_direct(super);

	if (initialize_super_block(pfp)) {
		error = errno;
		goto init_failed;
	}

	cleanup_orphaned_inodes(pfp, pfp->super->orphaned_inodes);

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

	free(pfp->cred.groups);

	vinode_unref(pfp, pfp->cwd);
	vinode_unref(pfp, pfp->root);
	inode_map_free(pfp);
	os_rwlock_destroy(&pfp->cred_rwlock);
	os_rwlock_destroy(&pfp->super_rwlock);
	os_rwlock_destroy(&pfp->cwd_rwlock);
	os_rwlock_destroy(&pfp->inode_map_rwlock);

	pmemobj_close(pfp->pop);

	memset(pfp, 0, sizeof(*pfp));

	free(pfp);
}

/*
 * gid_in_list -- return true when gid is in supplementary groups list
 */
bool
gid_in_list(const struct pmemfile_cred *cred, pmemfile_gid_t gid)
{
	for (size_t i = 0; i < cred->groupsnum; ++i) {
		if (cred->groups[i] == gid)
			return true;
	}

	return false;
}

/*
 * can_access -- answers question: "can I access this inode with these
 * credentials to do specified action?"
 */
bool
can_access(const struct pmemfile_cred *cred, struct inode_perms perms, int acc)
{
	pmemfile_mode_t perm = perms.flags & PMEMFILE_ACCESSPERMS;
	pmemfile_mode_t req = 0;
	pmemfile_uid_t uid;
	pmemfile_gid_t gid;
	int acctype = acc & PFILE_ACCESS_MASK;

	if (acctype == PFILE_USE_FACCESS) {
		uid = cred->fsuid;
		gid = cred->fsgid;
	} else if (acctype == PFILE_USE_EACCESS) {
		uid = cred->euid;
		gid = cred->egid;
	} else if (acctype == PFILE_USE_RACCESS) {
		uid = cred->ruid;
		gid = cred->rgid;
	} else {
		return false;
	}

	if (perms.uid == uid) {
		if (acc & PFILE_WANT_READ)
			req |=  PMEMFILE_S_IRUSR;
		if (acc & PFILE_WANT_WRITE)
			req |=  PMEMFILE_S_IWUSR;
		if (acc & PFILE_WANT_EXECUTE)
			req |=  PMEMFILE_S_IXUSR;
	} else if (perms.gid == gid || gid_in_list(cred, perms.gid)) {
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

/*
 * vinode_can_access -- wrapper around "can_access" that deals with locked
 * vinode
 */
bool
_vinode_can_access(const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, int acc)
{
	struct inode_perms inode_perms = _vinode_get_perms(vinode);

	return can_access(cred, inode_perms, acc);
}

/*
 * vinode_can_access -- wrapper around "can_access" that deals with unlocked
 * vinode
 */
bool
vinode_can_access(const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, int acc)
{
	struct inode_perms inode_perms = vinode_get_perms(vinode);

	return can_access(cred, inode_perms, acc);
}

/*
 * copy_cred -- copies credentials
 */
static int
copy_cred(struct pmemfile_cred *dst_cred, const struct pmemfile_cred *src_cred)
{
	dst_cred->ruid = src_cred->ruid;
	dst_cred->rgid = src_cred->rgid;

	dst_cred->euid = src_cred->euid;
	dst_cred->egid = src_cred->egid;

	dst_cred->fsuid = src_cred->fsuid;
	dst_cred->fsgid = src_cred->fsgid;

	dst_cred->caps = src_cred->caps;
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

/*
 * cred_acquire -- gets current credentials in a safe way
 */
int
cred_acquire(PMEMfilepool *pfp, struct pmemfile_cred *cred)
{
	int ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = copy_cred(cred, &pfp->cred);
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * cred_release -- frees credentials obtained with "get_cred"
 */
void
cred_release(struct pmemfile_cred *cred)
{
	free(cred->groups);
	memset(cred, 0, sizeof(*cred));
}

/*
 * pmemfile_setcap -- sets current user capability
 */
int
pmemfile_setcap(PMEMfilepool *pfp, int cap)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	int ret = 0;
	os_rwlock_wrlock(&pfp->cred_rwlock);
	switch (cap) {
		case PMEMFILE_CAP_CHOWN:
		case PMEMFILE_CAP_FOWNER:
			pfp->cred.caps |= 1 << cap;
			break;
		default:
			errno = EINVAL;
			ret = -1;
			break;
	}
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_clrcap -- clears current user capability
 */
int
pmemfile_clrcap(PMEMfilepool *pfp, int cap)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	int ret = 0;
	os_rwlock_wrlock(&pfp->cred_rwlock);
	switch (cap) {
		case PMEMFILE_CAP_CHOWN:
		case PMEMFILE_CAP_FOWNER:
			pfp->cred.caps &= ~(1 << cap);
			break;
		default:
			errno = EINVAL;
			ret = -1;
			break;
	}
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}
