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
#include <stdio.h>

#include "alloc.h"
#include "blocks.h"
#include "callbacks.h"
#include "compiler_utils.h"
#include "data.h"
#include "dir.h"
#include "hash_map.h"
#include "file.h"
#include "inode.h"
#include "inode_array.h"
#include "locks.h"
#include "mkdir.h"
#include "os_thread.h"
#include "os_util.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

COMPILE_ERROR_ON(PMEMFILE_ROOT_COUNT <= 0);

unsigned
pmemfile_pool_root_count(PMEMfilepool *pfp)
{
	if (pfp != NULL)
		return PMEMFILE_ROOT_COUNT;
	else
		return 0;
}

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

	if (!TOID_IS_NULL(super->root_inode[0]) &&
			super->version != PMEMFILE_CUR_VERSION) {
		ERR("unknown superblock version: 0x%lx", super->version);
		errno = EINVAL;
		return -1;
	}

	os_rwlock_init(&pfp->cred_rwlock);
	os_rwlock_init(&pfp->super_rwlock);
	os_rwlock_init(&pfp->cwd_rwlock);
	os_rwlock_init(&pfp->inode_map_rwlock);

	error = initialize_alloc_classes(pfp->pop);
	if (error) {
		error = 0;
		/* allocation classes will not be used - ignore error */
	}

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

	if (TOID_IS_NULL(super->root_inode[0])) {
		TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
			TX_ADD_DIRECT(super);
			for (unsigned i = 0; i < PMEMFILE_ROOT_COUNT; ++i) {
				super->root_inode[i] =
				    vinode_new_dir(pfp, NULL, "/", 1,
					&cred, PMEMFILE_ACCESSPERMS);
			}

			super->version = PMEMFILE_CUR_VERSION;
			super->orphaned_inodes = inode_array_alloc(pfp);
		} TX_ONABORT {
			error = errno;
		} TX_END

		if (error) {
			ERR("!cannot initialize super block");
			goto tx_err;
		}
	}

	for (unsigned i = 0; i < PMEMFILE_ROOT_COUNT; ++i) {
		pfp->root[i] = inode_ref(pfp, super->root_inode[i],
							NULL, NULL, 0);
		if (!pfp->root[i]) {
			error = errno;

			ERR("!cannot access root inode");
			goto ref_err;
		}

		pfp->root[i]->parent = pfp->root[i];
	}

#ifdef DEBUG
	for (unsigned i = 0; i < PMEMFILE_ROOT_COUNT; ++i) {
		pfp->root[i]->path = strdup("/");
		ASSERTne(pfp->root[i]->path, NULL);
	}
#endif

	pfp->cwd = vinode_ref(pfp, pfp->root[0]);
	pfp->dev = pfp->root[0]->tinode.oid.pool_uuid_lo;
	cred_release(&cred);

	return 0;
ref_err:
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

	PMEMfilepool *pfp = pf_calloc(1, sizeof(*pfp));
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
	pfp->super = PF_RW(pfp, super);

	if (initialize_super_block(pfp)) {
		error = errno;
		goto init_failed;
	}

	return pfp;

init_failed:
no_super:
	pmemobj_close(pfp->pop);
pool_create:
	pf_free(pfp);
	errno = error;
	return NULL;
}

static void
inode_trim_cb(PMEMfilepool *pfp, TOID(struct pmemfile_inode) inode)
{
	ASSERTeq(inode_get_nlink(PF_RW(pfp, inode)), 0);
	inode_trim(pfp, inode);
}

static void
inode_free_cb(PMEMfilepool *pfp, TOID(struct pmemfile_inode) inode)
{
	ASSERTeq(inode_get_nlink(PF_RW(pfp, inode)), 0);
	inode_free(pfp, inode);
}

/*
 * pmemfile_pool_open -- open pmem file system
 */
PMEMfilepool *
pmemfile_pool_open(const char *pathname)
{
	LOG(LDBG, "pathname %s", pathname);

	PMEMfilepool *pfp = pf_calloc(1, sizeof(*pfp));
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
	if (!inode_array_empty(pfp, orphaned) ||
			!inode_array_is_small(pfp, orphaned)) {
		inode_array_traverse(pfp, orphaned, inode_trim_cb);

		TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
			TX_ADD_FIELD_DIRECT(pfp->super, orphaned_inodes);

			inode_array_traverse(pfp, orphaned, inode_free_cb);

			inode_array_free(pfp, orphaned);

			pfp->super->orphaned_inodes = inode_array_alloc(pfp);
		} TX_ONABORT {
			FATAL("!cannot cleanup list of deleted files");
		} TX_END
	}

	return pfp;

init_failed:
no_super:
	pmemobj_close(pfp->pop);
pool_open:
	pf_free(pfp);
	errno = error;
	return NULL;
}

/*
 * pmemfile_pool_set_device -- set device id for pool
 */
void
pmemfile_pool_set_device(PMEMfilepool *pfp, pmemfile_dev_t dev)
{
	pfp->dev = dev;
}

/*
 * pmemfile_pool_close -- close pmem file system
 */
void
pmemfile_pool_close(PMEMfilepool *pfp)
{
	LOG(LDBG, "pfp %p", pfp);

	pf_free(pfp->cred.groups);

	vinode_unref(pfp, pfp->cwd);
	for (unsigned i = 0; i < PMEMFILE_ROOT_COUNT; ++i)
		vinode_unref(pfp, pfp->root[i]);
	inode_map_free(pfp);
	os_rwlock_destroy(&pfp->cred_rwlock);
	os_rwlock_destroy(&pfp->super_rwlock);
	os_rwlock_destroy(&pfp->cwd_rwlock);
	os_rwlock_destroy(&pfp->inode_map_rwlock);

	pmemobj_close(pfp->pop);

	memset(pfp, 0, sizeof(*pfp));

	pf_free(pfp);
}

struct resume_info {
	PMEMfilepool *pfp;
	PMEMobjpool *old_pop;
};

struct suspend_info {
	PMEMfilepool *pfp;
	unsigned count;
	struct pmemfile_vinode *dst_vinode;
	struct pmemfile_block_desc *last_block;
};

static size_t
print_toid(size_t buf_size, char buf[buf_size],
		TOID(struct pmemfile_inode) *tinode)
{
	uint64_t raw[2];
	memcpy(raw, tinode, sizeof(raw));

	return (size_t)snprintf(buf, buf_size,
			"0x%016" PRIx64 ":0x%016" PRIx64 "\n", raw[0], raw[1]);
}

static void
vinode_suspend_append_special_file(struct pmemfile_vinode *vinode,
		struct suspend_info *desc)
{
	char line[SUSPENDED_INODE_LINE_LENGTH + 1];
	size_t line_len = print_toid(sizeof(line), line, &vinode->tinode);

	struct pmemfile_inode *dst_inode = desc->dst_vinode->inode;

	size_t allocated = inode_get_allocated_space(dst_inode);
	allocated += vinode_allocate_interval(desc->pfp, desc->dst_vinode,
				inode_get_size(dst_inode), line_len);
	*(inode_get_allocated_space_ptr(dst_inode)) = allocated;
	vinode_write(desc->pfp, desc->dst_vinode, inode_get_size(dst_inode),
				&desc->last_block, line, line_len);
	*(inode_get_size_ptr(dst_inode)) += line_len;
}

static void
vinode_suspend_cb(uint64_t off, void *vinode, void *arg)
{
	struct suspend_info *desc = (struct suspend_info *)arg;

	if (vinode == desc->dst_vinode)
		return;

	vinode_suspend_append_special_file(vinode, desc);

	vinode_suspend(desc->pfp, vinode);
}

static void
vinode_resume_cb(uint64_t off, void *vinode, void *arg)
{
	struct resume_info *info = arg;
	vinode_resume(info->pfp, vinode, info->old_pop);
}

static int
check_paths_on_resume(PMEMfilepool *pfp, PMEMfile *file_at,
			const char *const *paths)
{
	for (const char *const *path = paths; *path != NULL; ++path) {
		uintptr_t off;
		PMEMfile *file;

		file = pmemfile_openat(pfp, file_at, *path, PMEMFILE_O_RDONLY);
		if (file == NULL)
			return -1;
		off = (uintptr_t)file->vinode->inode - (uintptr_t)pfp->pop;
		pmemfile_close(pfp, file);

		if (off != pfp->suspense) {
			errno = EINVAL;
			return -1;
		}
	}

	return 0;
}

/*
 * pmemfile_pool_resume -- notifies pmemfile that pool is now going to be used
 *
 * Can be called only after pmemfile_pool_suspend.
 */
int
pmemfile_pool_resume(PMEMfilepool *pfp, const char *pool_path,
		unsigned root_index, const char *const *paths, int flags)
{
	if (flags != 0) {
		errno = EINVAL;
		return -1;
	}

	PMEMobjpool *new_pop = NULL;

	while (new_pop == NULL) {
		new_pop = pmemobj_open(pool_path, POBJ_LAYOUT_NAME(pmemfile));
		if (new_pop == NULL)
			// XXX
			os_usleep(1000);
	}

	int error = 0;
	PMEMfile *file_at = NULL;

	PMEMobjpool *old_pop = pfp->pop;
	struct pmemfile_super *old_super = pfp->super;

	if (new_pop != pfp->pop) {
		pfp->pop = new_pop;
		pfp->super = pmemobj_direct(pmemobj_root(pfp->pop, 0));
	}

	error = initialize_alloc_classes(pfp->pop);
	if (error) {
		error = 0;
		/* allocation classes will not be used - ignore error */
	}

	struct resume_info arg = {pfp, old_pop};

	hash_map_traverse(pfp->inode_map, vinode_resume_cb, &arg);

	file_at = pmemfile_open_root(pfp, root_index, 0);
	if (file_at == NULL) {
		error = errno;
		goto err;
	}

	if (check_paths_on_resume(pfp, file_at, paths) != 0)
		goto err;

	for (const char *const *path = paths; *path != NULL; ++path) {
		if (pmemfile_unlinkat(pfp, file_at, *path, 0) != 0)
			goto err;
	}

	pmemfile_close(pfp, file_at);

	return 0;

err:
	if (file_at != NULL)
		pmemfile_close(pfp, file_at);

	pmemobj_close(new_pop);
	errno = error;

	pfp->pop = old_pop;
	pfp->super = old_super;
	return -1;
}

/*
 * pmemfile_pool_suspend -- notifies pmemfile that pool is not going to be used
 *                          until pmemfile_pool_restore
 *
 * This function CAN NOT be called while any pmemfile function (including this
 * one) is in progress (even for other pools, because of pmemobj_close/open
 * not being safe)!
 */
int
pmemfile_pool_suspend(PMEMfilepool *pfp, unsigned root_index,
			const char *const *paths, int flags)
{
	if (flags != 0 || paths == NULL || paths[0] == NULL) {
		errno = EINVAL;
		return -1;
	}

	struct suspend_info sinfo = {.pfp = pfp, };
	sinfo.count = 0;

	while (paths[sinfo.count] != NULL) {
		if (paths[sinfo.count][0] == '\0') {
			errno = EINVAL;
			return -1;
		}

		sinfo.count++;
	}

	int error = 0;
	PMEMfile *file_at = NULL;
	PMEMfile *file = NULL;

	file_at = pmemfile_open_root(pfp, root_index, 0);
	if (file_at == NULL)
		return -1;

	file = pmemfile_openat(pfp, file_at, paths[0],
			PMEMFILE_O_CREAT | PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
			0400);

	if (file == NULL) {
		error = errno;
		goto err;
	}

	os_rwlock_wrlock(&file->vinode->rwlock);
	if (!file->vinode->blocks)
		error = vinode_rebuild_block_tree(pfp, file->vinode);
	os_rwlock_unlock(&file->vinode->rwlock);
	if (error)
		goto err;

	sinfo.dst_vinode = file->vinode;

	for (unsigned i = 1; i < sinfo.count; ++i) {
		if (pmemfile_linkat(pfp, file_at, paths[0],
					file_at, paths[i], 0) != 0) {
			error = errno;
			goto err;
		}
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		/*
		 * Set the flag inidicating a special file in the transaction.
		 * If the transaction fails, and power goes out before removing
		 * the file, then it just stays there as a regular (empty) file,
		 * not cousing a lot of trouble.
		 */
		TX_ADD_DIRECT(inode_get_flags_ptr(sinfo.dst_vinode->inode));
		*(inode_get_flags_ptr(sinfo.dst_vinode->inode)) |=
		    PMEMFILE_I_SUSPENDED_REF;

		/*
		 * These two fields are updated with each entry written to the
		 * special file.
		 */
		TX_ADD_DIRECT(
		    inode_get_allocated_space_ptr(sinfo.dst_vinode->inode));
		TX_ADD_DIRECT(inode_get_size_ptr(sinfo.dst_vinode->inode));

		hash_map_traverse(pfp->inode_map, vinode_suspend_cb, &sinfo);
	} TX_ONABORT {
		error = errno;
	} TX_END

	if (error)
		goto err;

	pfp->suspense = (uintptr_t)file->vinode->inode - (uintptr_t)pfp->pop;

	pmemfile_close(pfp, file_at);
	pmemfile_close(pfp, file);
	pmemobj_close(pfp->pop);
	return 0;

err:
	if (file != NULL)
		pmemfile_close(pfp, file);

	for (unsigned i = 0; i < sinfo.count; ++i) {
		/*
		 * If something went wrong, the files are unlinked here. It is
		 * important that they should not have the relevant internal
		 * flag set, as pmemfile_unlink would attempt to decrement
		 * suspended reference counters.
		 * They must still be actual regular files at this point.
		 */
		pmemfile_unlinkat(pfp, file_at, paths[i], 0);
	}

	if (file_at != NULL)
		pmemfile_close(pfp, file_at);

	errno = error;
	return -1;
}
