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
 * chdir.c -- pmemfile_*chdir* implementation
 */

#include "creds.h"
#include "dir.h"
#include "file.h"
#include "internal.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

static int
_pmemfile_chdir(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *dir)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);

	struct inode_perms dir_perms = vinode_get_perms(dir);

	if (!PMEMFILE_S_ISDIR(dir_perms.flags)) {
		vinode_unref(pfp, dir);
		errno = ENOTDIR;
		return -1;
	}

	if (!can_access(cred, dir_perms, PFILE_WANT_EXECUTE)) {
		vinode_unref(pfp, dir);
		errno = EACCES;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cwd_rwlock);
	struct pmemfile_vinode *old_cwd = pfp->cwd;
	pfp->cwd = dir;
	os_rwlock_unlock(&pfp->cwd_rwlock);
	vinode_unref(pfp, old_cwd);

	return 0;
}

int
pmemfile_chdir(PMEMfilepool *pfp, const char *path)
{
	struct pmemfile_path_info info;
	struct pmemfile_vinode *at;
	struct pmemfile_cred cred;
	int ret = -1;
	int error = 0;
	bool at_unref;

	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!path) {
		errno = ENOENT;
		return -1;
	}
	if (cred_acquire(pfp, &cred))
		return -1;

	at = pool_get_dir_for_path(pfp, PMEMFILE_AT_CWD, path, &at_unref);

	struct pmemfile_vinode *dir =
		resolve_pathat_full(pfp, &cred, at, path, &info, 0, true);

	if (info.error) {
		error = info.error;
		goto end;
	}

	ret = _pmemfile_chdir(pfp, &cred, dir);
	if (ret)
		error = errno;

end:
	path_info_cleanup(pfp, &info);
	cred_release(&cred);

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);
	if (at_unref)
		vinode_unref(pfp, at);
	if (error)
		errno = error;

	return ret;
}

int
pmemfile_fchdir(PMEMfilepool *pfp, PMEMfile *dir)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!dir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return -1;
	}

	int ret;
	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;
	ret = _pmemfile_chdir(pfp, &cred, vinode_ref(pfp, dir->vinode));
	cred_release(&cred);
	return ret;
}
