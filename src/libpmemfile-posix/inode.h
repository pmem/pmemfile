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

#ifndef PMEMFILE_INODE_H
#define PMEMFILE_INODE_H

#include <stdint.h>
#include <time.h>

#include "libpmemfile-posix.h"
#include "layout.h"
#include "os_thread.h"

/* volatile inode */
struct pmemfile_vinode {
	/* reference counter */
	uint32_t ref;

	/* read-write lock, also protects inode read/writes */
	os_rwlock_t rwlock;

	/* persistent inode */
	struct pmemfile_inode *inode;

	/* persistent inode oid */
	TOID(struct pmemfile_inode) tinode;

#ifdef DEBUG
	/*
	 * One of the full paths inode can be reached from.
	 * Used only for debugging.
	 */
	char *path;
#endif

	/* parent directory, valid only for directories */
	struct pmemfile_vinode *parent;

	/* pointer to the array of orphaned inodes */
	struct inode_orphan_info {
		struct pmemfile_inode_array *arr;
		unsigned idx;
	} orphaned;

	/* first free block slot */
	struct block_info {
		struct pmemfile_block_array *arr;
		uint32_t idx;
	} first_free_block;

	/* pointer to the array of suspended inodes */
	struct inode_suspend_info {
		struct pmemfile_inode_array *arr;
		unsigned idx;
	} suspended;

	/* first used block */
	struct pmemfile_block_desc *first_block;

	/* tree mapping offsets to blocks */
	struct ctree *blocks;

	/* space for volatile snapshots */
	struct {
		struct block_info first_free_block;
		struct pmemfile_block_desc *first_block;
	} snapshot;
};

static inline bool inode_is_dir(const struct pmemfile_inode *inode)
{
	return PMEMFILE_S_ISDIR(inode->flags);
}

static inline bool vinode_is_dir(struct pmemfile_vinode *vinode)
{
	return inode_is_dir(vinode->inode);
}

static inline bool inode_is_regular_file(const struct pmemfile_inode *inode)
{
	return PMEMFILE_S_ISREG(inode->flags);
}

static inline bool vinode_is_regular_file(struct pmemfile_vinode *vinode)
{
	return inode_is_regular_file(vinode->inode);
}

static inline bool inode_is_symlink(const struct pmemfile_inode *inode)
{
	return PMEMFILE_S_ISLNK(inode->flags);
}

static inline bool vinode_is_symlink(struct pmemfile_vinode *vinode)
{
	return inode_is_symlink(vinode->inode);
}

struct pmemfile_cred;
TOID(struct pmemfile_inode) inode_alloc(PMEMfilepool *pfp,
		struct pmemfile_cred *cred, uint64_t flags);

void inode_free(PMEMfilepool *pfp, TOID(struct pmemfile_inode) tinode);
void inode_trim(PMEMfilepool *pfp, TOID(struct pmemfile_inode) tinode);

struct pmemfile_vinode *vinode_ref(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode);

void inode_map_free(PMEMfilepool *pfp);

struct pmemfile_vinode *inode_ref(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) inode,
		struct pmemfile_vinode *parent,
		const char *name,
		size_t namelen);

void vinode_unref(PMEMfilepool *pfp, struct pmemfile_vinode *vinode);
void vinode_cleanup(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		bool preserve_errno);

struct inode_orphan_info inode_orphan(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode) tinode);

void vinode_orphan_unlocked(PMEMfilepool *pfp, struct pmemfile_vinode *vinode);
void vinode_orphan(PMEMfilepool *pfp, struct pmemfile_vinode *vinode);

void vinode_snapshot(struct pmemfile_vinode *vinode);
void vinode_restore_on_abort(struct pmemfile_vinode *vinode);

void vinode_rdlock2(struct pmemfile_vinode *v1, struct pmemfile_vinode *v2);
void vinode_wrlock2(struct pmemfile_vinode *v1, struct pmemfile_vinode *v2);
void vinode_unlock2(struct pmemfile_vinode *v1, struct pmemfile_vinode *v2);
void vinode_wrlockN(struct pmemfile_vinode *v[static 5],
		struct pmemfile_vinode *v1,
		struct pmemfile_vinode *v2,
		struct pmemfile_vinode *v3,
		struct pmemfile_vinode *v4);
void vinode_unlockN(struct pmemfile_vinode *v[static 5]);

static inline TOID(struct pmemfile_block_desc)
blockp_as_oid(struct pmemfile_block_desc *block)
{
	return (TOID(struct pmemfile_block_desc))pmemobj_oid(block);
}

int vinode_rdlock_with_block_tree(PMEMfilepool *, struct pmemfile_vinode *);

void vinode_suspend(PMEMfilepool *pfp, struct pmemfile_vinode *vinode);
void inode_restore(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		PMEMobjpool *old_pop);
void vinode_restore(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		PMEMobjpool *old_pop);

#endif
