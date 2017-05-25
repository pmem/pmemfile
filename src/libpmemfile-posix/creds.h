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
#ifndef PMEMFILE_CREDS_H
#define PMEMFILE_CREDS_H

#include <stdbool.h>
#include "inode.h"
#include "libpmemfile-posix.h"

#define PFILE_WANT_READ (1<<0)
#define PFILE_WANT_WRITE (1<<1)
#define PFILE_WANT_EXECUTE (1<<2)

#define PFILE_USE_FACCESS (0<<3)
#define PFILE_USE_EACCESS (1<<3)
#define PFILE_USE_RACCESS (2<<3)
#define PFILE_ACCESS_MASK (3<<3)

struct pmemfile_cred {
	/* real user id */
	pmemfile_uid_t ruid;
	/* real group id */
	pmemfile_gid_t rgid;

	/* effective user id */
	pmemfile_uid_t euid;
	/* effective group id */
	pmemfile_gid_t egid;

	/* filesystem user id */
	pmemfile_uid_t fsuid;
	/* filesystem group id */
	pmemfile_gid_t fsgid;

	/* supplementary group IDs */
	pmemfile_gid_t *groups;
	size_t groupsnum;

	/* capabilities */
	int caps;
};

/* inode permission information */
struct inode_perms {
	/* file flags (contains mode) */
	uint64_t flags;

	/* owner */
	uint32_t uid;

	/* group */
	uint32_t gid;
};

static inline struct inode_perms
_vinode_get_perms(struct pmemfile_vinode *vinode)
{
	struct inode_perms perms;
	struct pmemfile_inode *inode = vinode->inode;
	perms.flags = inode->flags;
	perms.uid = inode->uid;
	perms.gid = inode->gid;
	return perms;
}

static inline struct inode_perms
vinode_get_perms(struct pmemfile_vinode *vinode)
{
	struct inode_perms perms;
	os_rwlock_rdlock(&vinode->rwlock);
	perms = _vinode_get_perms(vinode);
	os_rwlock_unlock(&vinode->rwlock);
	return perms;
}

bool can_access(const struct pmemfile_cred *cred,
		struct inode_perms perms,
		int acc);

bool vinode_can_access(const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, int acc);

bool _vinode_can_access(const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, int acc);

bool gid_in_list(const struct pmemfile_cred *cred, pmemfile_gid_t gid);

int cred_acquire(PMEMfilepool *pfp, struct pmemfile_cred *cred);
void cred_release(struct pmemfile_cred *cred);

#endif
