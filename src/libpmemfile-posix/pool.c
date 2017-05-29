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
#include "inode_array.h"
#include "internal.h"
#include "locks.h"
#include "mkdir.h"
#include "os_thread.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

#define PMEMFILE_CUR_VERSION \
	PMEMFILE_SUPER_VERSION(PMEMFILE_MAJOR_VERSION, PMEMFILE_MINOR_VERSION)
/*
 * initialize_super_block -- initializes super block
 *
 * Can't be called in a transaction.
 */
static int
initialize_super_block(PMEMfilepool *pfp)
{
	LOG(LDBG, "pfp %p", pfp);

	ASSERT_NOT_IN_TX();

	int error = 0;
	struct pmemfile_super *super = pfp->super;

	if (!TOID_IS_NULL(super->root_inode) &&
			super->version != PMEMFILE_CUR_VERSION) {
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

			super->version = PMEMFILE_CUR_VERSION;
			super->orphaned_inodes = inode_array_alloc();
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

static void
inode_free_cb(PMEMfilepool *pfp, TOID(struct pmemfile_inode) inode)
{
	ASSERTeq(D_RW(inode)->nlink, 0);
	inode_free(pfp, inode);
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

	TOID(struct pmemfile_inode_array) orphaned =
			pfp->super->orphaned_inodes;
	if (!inode_array_empty(orphaned) || !inode_array_is_small(orphaned)) {
		TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
			TX_ADD_FIELD_DIRECT(pfp->super, orphaned_inodes);

			inode_array_traverse(pfp, orphaned, inode_free_cb);

			inode_array_free(orphaned);

			pfp->super->orphaned_inodes = inode_array_alloc();
		} TX_ONABORT {
			FATAL("!cannot cleanup list of deleted files");
		} TX_END
	}

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
