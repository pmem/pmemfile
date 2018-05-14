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
#ifndef PMEMFILE_POOL_H
#define PMEMFILE_POOL_H

/*
 * Runtime pool state.
 */

#include "creds.h"
#include "hash_map.h"
#include "inode.h"
#include "layout.h"
#include "os_thread.h"

/* Pool */
struct pmemfilepool {
	/* pmemobj pool pointer */
	PMEMobjpool *pop;

	pmemfile_dev_t dev;

	/* root directories */
	struct pmemfile_vinode *root[PMEMFILE_ROOT_COUNT];
	mode_t umask;

	/* current working directory */
	struct pmemfile_vinode *cwd;
	os_rwlock_t cwd_rwlock;

	/* superblock */
	struct pmemfile_super *super;
	os_rwlock_t super_rwlock;

	/* map between inodes and vinodes */
	struct hash_map *inode_map;
	os_rwlock_t inode_map_rwlock;

	/* current credentials */
	struct pmemfile_cred cred;
	os_rwlock_t cred_rwlock;

	uintptr_t suspense; /* XXX perhaps a better name for this field? */
};

#endif
