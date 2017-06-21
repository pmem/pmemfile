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
 * access.c -- pmemfile_*access* implementation
 */

#include "dir.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

static int
_pmemfile_faccessat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path, int mode, int flags)
{
	if (flags & ~(PMEMFILE_AT_EACCESS | PMEMFILE_AT_SYMLINK_NOFOLLOW)) {
		errno = EINVAL;
		return -1;
	}

	LOG(LDBG, "path %s", path);

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	int resolve_flags = 0;
	if (flags & PMEMFILE_AT_EACCESS)
		resolve_flags |= PMEMFILE_OPEN_PARENT_USE_EACCESS;
	else
		resolve_flags |= PMEMFILE_OPEN_PARENT_USE_RACCESS;

	int error = 0;
	struct pmemfile_path_info info;
	struct pmemfile_vinode *vinode =
			resolve_pathat_full(pfp, &cred, dir, path, &info,
				resolve_flags,
				(flags & PMEMFILE_AT_SYMLINK_NOFOLLOW) ?
						NO_RESOLVE_LAST_SYMLINK :
						RESOLVE_LAST_SYMLINK);

	if (info.error) {
		error = info.error;
		goto end;
	}

	int acc = 0;
	if (mode & PMEMFILE_R_OK)
		acc |= PFILE_WANT_READ;
	if (mode & PMEMFILE_W_OK)
		acc |= PFILE_WANT_WRITE;
	if (mode & PMEMFILE_X_OK)
		acc |= PFILE_WANT_EXECUTE;

	if (flags & PMEMFILE_AT_EACCESS)
		acc |= PFILE_USE_EACCESS;
	else
		acc |= PFILE_USE_RACCESS;

	if (!vinode_can_access(&cred, vinode, acc))
		error = EACCES;

end:
	path_info_cleanup(pfp, &info);
	cred_release(&cred);

	ASSERT_NOT_IN_TX();
	if (vinode)
		vinode_unref(pfp, vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_faccessat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int mode, int flags)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

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

	int ret = _pmemfile_faccessat(pfp, at, pathname, mode, flags);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_access(PMEMfilepool *pfp, const char *path, int mode)
{
	return pmemfile_faccessat(pfp, PMEMFILE_AT_CWD, path, mode, 0);
}

int
pmemfile_euidaccess(PMEMfilepool *pfp, const char *path, int mode)
{
	return pmemfile_faccessat(pfp, PMEMFILE_AT_CWD, path, mode,
			PMEMFILE_AT_EACCESS);
}
