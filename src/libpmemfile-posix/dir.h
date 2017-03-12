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
#ifndef PMEMFILE_DIR_H
#define PMEMFILE_DIR_H

#include "inode.h"
#include "internal.h"
#include "pool.h"

struct pmemfile_path_info {
	/*
	 * Vinode of the last reachable component in the path, except for
	 * the last part.
	 */
	struct pmemfile_vinode *vinode;
	/* Remaining part of the path. */
	char *remaining;

	int error;
};

void resolve_pathat(PMEMfilepool *pfp, struct pmemfile_cred *cred,
		struct pmemfile_vinode *parent, const char *path,
		struct pmemfile_path_info *path_info, int flags);

struct pmemfile_vinode *
resolve_pathat_full(PMEMfilepool *pfp, struct pmemfile_cred *cred,
		struct pmemfile_vinode *parent, const char *path,
		struct pmemfile_path_info *path_info, int flags,
		bool resolve_last_symlink);

void resolve_symlink(PMEMfilepool *pfp, struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode,
		struct pmemfile_path_info *info);

void path_info_cleanup(PMEMfilepool *pfp, struct pmemfile_path_info *path_info);
bool str_contains(const char *str, size_t len, char c);
bool more_than_1_component(const char *path);
size_t component_length(const char *path);

struct pmemfile_vinode *vinode_new_dir(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent, const char *name,
		size_t namelen, pmemfile_mode_t mode, bool add_to_parent,
		volatile bool *parent_refed);

void vinode_add_dirent(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent_vinode,
		const char *name,
		size_t namelen,
		struct pmemfile_vinode *child_vinode,
		struct pmemfile_time tm);

void vinode_set_debug_path_locked(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent_vinode,
		struct pmemfile_vinode *child_vinode,
		const char *name,
		size_t namelen);

void vinode_set_debug_path(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent_vinode,
		struct pmemfile_vinode *child_vinode,
		const char *name,
		size_t namelen);

void vinode_clear_debug_path(PMEMfilepool *pfp, struct pmemfile_vinode *vinode);

struct pmemfile_vinode *vinode_lookup_dirent(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent, const char *name,
		size_t namelen, int flags);

void vinode_unlink_dirent(PMEMfilepool *pfp,
		struct pmemfile_vinode *parent,
		const char *name,
		size_t namelen,
		struct pmemfile_vinode *volatile *vinode,
		volatile bool *parent_refed,
		bool abort_on_ENOENT);

struct pmemfile_vinode *pool_get_cwd(PMEMfilepool *pfp);
struct pmemfile_vinode *pool_get_dir_for_path(PMEMfilepool *pfp, PMEMfile *dir,
		const char *path, bool *unref);

int _pmemfile_rmdirat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path);

static inline size_t
pmemfile_dir_size(TOID(struct pmemfile_dir) dir)
{
	return page_rounddown(pmemobj_alloc_usable_size(dir.oid));
}

#endif
