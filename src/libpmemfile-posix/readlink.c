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
 * readlink.c -- pmemfile_readlink* implementation
 */

#include "creds.h"
#include "dir.h"
#include "internal.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "utils.h"

static pmemfile_ssize_t
_pmemfile_readlinkat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *pathname, char *buf, size_t bufsiz)
{
	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	int error = 0;
	pmemfile_ssize_t ret = -1;
	struct pmemfile_vinode *vinode = NULL;
	struct pmemfile_path_info info;
	resolve_pathat(pfp, &cred, dir, pathname, &info, 0);

	if (info.error) {
		error = info.error;
		goto end;
	}

	size_t namelen = component_length(info.remaining);

	vinode = vinode_lookup_dirent(pfp, info.parent, info.remaining,
			namelen, 0);
	if (!vinode) {
		error = ENOENT;
		goto end;
	}

	if (!vinode_is_symlink(vinode)) {
		error = EINVAL;
		goto end;
	}

	if (strchr(info.remaining, '/')) {
		error = ENOTDIR;
		goto end;
	}

	os_rwlock_rdlock(&vinode->rwlock);

	const char *data = vinode->inode->file_data.data;
	size_t len = strlen(data);
	if (len > bufsiz)
		len = bufsiz;
	memcpy(buf, data, len);
	ret = (pmemfile_ssize_t)len;

	os_rwlock_unlock(&vinode->rwlock);

end:
	path_info_cleanup(pfp, &info);
	cred_release(&cred);

	if (vinode)
		vinode_unref(pfp, vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return ret;
}

pmemfile_ssize_t
pmemfile_readlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		char *buf, size_t bufsiz)
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

	pmemfile_ssize_t ret =
			_pmemfile_readlinkat(pfp, at, pathname, buf, bufsiz);

	if (at_unref)
		vinode_cleanup(pfp, at, ret < 0);

	return ret;
}

pmemfile_ssize_t
pmemfile_readlink(PMEMfilepool *pfp, const char *pathname, char *buf,
		size_t bufsiz)
{
	return pmemfile_readlinkat(pfp, PMEMFILE_AT_CWD, pathname, buf, bufsiz);
}
