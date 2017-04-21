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
 * file.c -- basic file operations
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>

#include "callbacks.h"
#include "data.h"
#include "dir.h"
#include "file.h"

#include "inode.h"
#include "inode_array.h"
#include "internal.h"
#include "locks.h"
#include "os_thread.h"
#include "out.h"
#include "pool.h"
#include "util.h"

/*
 * is_tmpfile -- returns true if "flags" contains O_TMPFILE flag
 *
 * It's needed because O_TMPFILE contains O_DIRECTORY.
 */
static bool
is_tmpfile(int flags)
{
	return (flags & PMEMFILE_O_TMPFILE) == PMEMFILE_O_TMPFILE;
}

/*
 * check_flags -- open(2) flags tester
 */
static int
check_flags(int flags)
{
	if (flags & PMEMFILE_O_APPEND) {
		LOG(LTRC, "O_APPEND");
		flags &= ~PMEMFILE_O_APPEND;
	}

	if (flags & PMEMFILE_O_ASYNC) {
		LOG(LSUP, "O_ASYNC is not supported");
		errno = EINVAL;
		return -1;
	}

	if (flags & PMEMFILE_O_CREAT) {
		LOG(LTRC, "O_CREAT");
		flags &= ~PMEMFILE_O_CREAT;
	}

	/* XXX: move to interposing layer */
	if (flags & PMEMFILE_O_CLOEXEC) {
		LOG(LINF, "O_CLOEXEC is always enabled");
		flags &= ~PMEMFILE_O_CLOEXEC;
	}

	if (flags & PMEMFILE_O_DIRECT) {
		LOG(LINF, "O_DIRECT is always enabled");
		flags &= ~PMEMFILE_O_DIRECT;
	}

	/* O_TMPFILE contains O_DIRECTORY */
	if ((flags & PMEMFILE_O_TMPFILE) == PMEMFILE_O_TMPFILE) {
		LOG(LTRC, "O_TMPFILE");
		flags &= ~PMEMFILE_O_TMPFILE;
	}

	if (flags & PMEMFILE_O_DIRECTORY) {
		LOG(LTRC, "O_DIRECTORY");
		flags &= ~PMEMFILE_O_DIRECTORY;
	}

	if (flags & PMEMFILE_O_DSYNC) {
		LOG(LINF, "O_DSYNC is always enabled");
		flags &= ~PMEMFILE_O_DSYNC;
	}

	if (flags & PMEMFILE_O_EXCL) {
		LOG(LTRC, "O_EXCL");
		flags &= ~PMEMFILE_O_EXCL;
	}

	if (flags & PMEMFILE_O_NOCTTY) {
		LOG(LINF, "O_NOCTTY is always enabled");
		flags &= ~PMEMFILE_O_NOCTTY;
	}

	if (flags & PMEMFILE_O_NOATIME) {
		LOG(LTRC, "O_NOATIME");
		flags &= ~PMEMFILE_O_NOATIME;
	}

	if (flags & PMEMFILE_O_NOFOLLOW) {
		LOG(LTRC, "O_NOFOLLOW");
		flags &= ~PMEMFILE_O_NOFOLLOW;
	}

	if (flags & PMEMFILE_O_NONBLOCK) {
		LOG(LINF, "O_NONBLOCK is ignored");
		flags &= ~PMEMFILE_O_NONBLOCK;
	}

	if (flags & PMEMFILE_O_PATH) {
		LOG(LTRC, "O_PATH");
		flags &= ~PMEMFILE_O_PATH;
	}

	if (flags & PMEMFILE_O_SYNC) {
		LOG(LINF, "O_SYNC is always enabled");
		flags &= ~PMEMFILE_O_SYNC;
	}

	if (flags & PMEMFILE_O_TRUNC) {
		LOG(LTRC, "O_TRUNC");
		flags &= ~PMEMFILE_O_TRUNC;
	}

	if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDONLY) {
		LOG(LTRC, "O_RDONLY");
		flags -= PMEMFILE_O_RDONLY;
	}

	if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_WRONLY) {
		LOG(LTRC, "O_WRONLY");
		flags -= PMEMFILE_O_WRONLY;
	}

	if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDWR) {
		LOG(LTRC, "O_RDWR");
		flags -= PMEMFILE_O_RDWR;
	}

	if (flags) {
		ERR("unknown flag 0x%x\n", flags);
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static struct pmemfile_vinode *
create_file(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		const char *filename, size_t namelen,
		struct pmemfile_vinode *parent_vinode, int flags,
		pmemfile_mode_t mode)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	rwlock_tx_wlock(&parent_vinode->rwlock);

	if (!_vinode_can_access(cred, parent_vinode, PFILE_WANT_WRITE))
		pmemfile_tx_abort(EACCES);

	struct pmemfile_vinode *vinode = inode_alloc(pfp,
			PMEMFILE_S_IFREG | mode,
			parent_vinode, NULL, filename, namelen);

	if (is_tmpfile(flags))
		vinode_orphan(pfp, vinode);
	else
		vinode_add_dirent(pfp, parent_vinode, filename,
				namelen, vinode, vinode->inode->ctime);

	rwlock_tx_unlock_on_commit(&parent_vinode->rwlock);

	return vinode;
}

static void
open_file(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, int flags)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	if (!(flags & PMEMFILE_O_PATH)) {
		int acc = flags & PMEMFILE_O_ACCMODE;

		if (acc == PMEMFILE_O_ACCMODE)
			pmemfile_tx_abort(EINVAL);

		int acc2;
		if (acc == PMEMFILE_O_RDWR)
			acc2 = PFILE_WANT_READ | PFILE_WANT_WRITE;
		else if (acc == PMEMFILE_O_RDONLY)
			acc2 = PFILE_WANT_READ;
		else
			acc2 = PFILE_WANT_WRITE;

		if (!vinode_can_access(cred, vinode, acc2))
			pmemfile_tx_abort(EACCES);
	}

	if ((flags & PMEMFILE_O_DIRECTORY) && !vinode_is_dir(vinode))
		pmemfile_tx_abort(ENOTDIR);

	if (flags & PMEMFILE_O_TRUNC) {
		if (!vinode_is_regular_file(vinode)) {
			LOG(LUSR, "truncating non regular file");
			pmemfile_tx_abort(EINVAL);
		}

		if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDONLY) {
			LOG(LUSR, "O_TRUNC without write permissions");
			pmemfile_tx_abort(EACCES);
		}

		rwlock_tx_wlock(&vinode->rwlock);

		vinode_truncate(pfp, vinode, 0);

		rwlock_tx_unlock_on_commit(&vinode->rwlock);
	}
}

/*
 * _pmemfile_openat -- open file
 */
static PMEMfile *
_pmemfile_openat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *pathname, int flags, ...)
{
	LOG(LDBG, "pathname %s flags 0x%x", pathname, flags);

	const char *orig_pathname = pathname;

	if (flags & PMEMFILE_O_PATH) {
		flags &= PMEMFILE_O_PATH | PMEMFILE_O_NOFOLLOW |
				PMEMFILE_O_CLOEXEC;
	}

	if (check_flags(flags))
		return NULL;

	va_list ap;
	va_start(ap, flags);
	pmemfile_mode_t mode = 0;

	/* NOTE: O_TMPFILE contains O_DIRECTORY */
	if ((flags & PMEMFILE_O_CREAT) || is_tmpfile(flags)) {
		mode = va_arg(ap, pmemfile_mode_t);
		LOG(LDBG, "mode %o", mode);
		mode &= PMEMFILE_ALLPERMS;
	}
	va_end(ap);

	int error = 0;
	PMEMfile *file = NULL;

	struct pmemfile_path_info info;
	struct pmemfile_vinode *volatile vinode;
	struct pmemfile_vinode *vparent;
	bool path_info_changed;
	size_t namelen;
	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return NULL;

	resolve_pathat(pfp, &cred, dir, pathname, &info, 0);

	do {
		path_info_changed = false;
		vparent = info.vinode;
		vinode = NULL;

		if (info.error) {
			error = info.error;
			goto end;
		}

		namelen = component_length(info.remaining);

		if (namelen == 0) {
			ASSERT(vparent == pfp->root);
			vinode = vinode_ref(pfp, vparent);
		} else {
			vinode = vinode_lookup_dirent(pfp, info.vinode,
					info.remaining, namelen, 0);
		}

		if (vinode && vinode_is_symlink(vinode)) {
			if (flags & PMEMFILE_O_NOFOLLOW) {
				error = ELOOP;
				goto end;
			}

			/*
			 * From open manpage: "When these two flags (O_CREAT &
			 * O_EXCL) are specified, symbolic links are not
			 * followed: if pathname is a symbolic link, then open()
			 * fails regardless of where the symbolic link points
			 * to."
			 *
			 * When only O_CREAT is specified, symlinks *are*
			 * followed.
			 */
			if ((flags & (PMEMFILE_O_CREAT|PMEMFILE_O_EXCL)) ==
					(PMEMFILE_O_CREAT|PMEMFILE_O_EXCL))
				break;

			/* XXX handle infinite symlink loop */
			resolve_symlink(pfp, &cred, vinode, &info);
			path_info_changed = true;
		}
	} while (path_info_changed);

	if (vinode && !vinode_is_dir(vinode) && strchr(info.remaining, '/')) {
		error = ENOTDIR;
		goto end;
	}

	if (is_tmpfile(flags)) {
		if (!vinode) {
			error = ENOENT;
			goto end;
		}

		if (!vinode_is_dir(vinode)) {
			error = ENOTDIR;
			goto end;
		}

		if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDONLY) {
			error = EINVAL;
			goto end;
		}
	} else if ((flags & (PMEMFILE_O_CREAT | PMEMFILE_O_EXCL)) ==
			(PMEMFILE_O_CREAT | PMEMFILE_O_EXCL)) {
		if (vinode) {
			LOG(LUSR, "file %s already exists", pathname);
			error = EEXIST;
			goto end;
		}

		if (!vinode_is_dir(vparent)) {
			error = ENOTDIR;
			goto end;
		}
	} else if ((flags & PMEMFILE_O_CREAT) == PMEMFILE_O_CREAT) {
		/* nothing to be done here */
	} else {
		if (!vinode) {
			error = ENOENT;
			goto end;
		}
	}

	if (is_tmpfile(flags)) {
		vinode_unref(pfp, vparent);
		vparent = vinode;
		vinode = NULL;
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (vinode == NULL) {
			vinode = create_file(pfp, &cred, info.remaining,
					namelen, vparent, flags, mode);
		} else {
			open_file(pfp, &cred, vinode, flags);
		}

		file = calloc(1, sizeof(*file));
		if (!file)
			pmemfile_tx_abort(errno);

		file->vinode = vinode;

		if (flags & PMEMFILE_O_PATH)
			file->flags = PFILE_PATH;
		else if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDONLY)
			file->flags = PFILE_READ;
		else if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_WRONLY)
			file->flags = PFILE_WRITE;
		else if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDWR)
			file->flags = PFILE_READ | PFILE_WRITE;

		if (flags & PMEMFILE_O_NOATIME)
			file->flags |= PFILE_NOATIME;
		if (flags & PMEMFILE_O_APPEND)
			file->flags |= PFILE_APPEND;
	} TX_ONABORT {
		error = errno;
	} TX_END

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (error) {
		if (vinode != NULL)
			vinode_unref(pfp, vinode);

		errno = error;
		LOG(LDBG, "!");

		return NULL;
	}

	ASSERT(file != NULL);
	os_mutex_init(&file->mutex);

	LOG(LDBG, "pathname %s opened inode 0x%" PRIx64, orig_pathname,
			file->vinode->tinode.oid.off);
	return file;
}

/*
 * pmemfile_openat -- open file
 */
PMEMfile *
pmemfile_openat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int flags, ...)
{
	if (!pathname) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return NULL;
	}

	va_list ap;
	va_start(ap, flags);
	pmemfile_mode_t mode = 0;
	if ((flags & PMEMFILE_O_CREAT) || is_tmpfile(flags))
		mode = va_arg(ap, pmemfile_mode_t);
	va_end(ap);

	struct pmemfile_vinode *at;
	bool at_unref;

	at = pool_get_dir_for_path(pfp, dir, pathname, &at_unref);

	PMEMfile *ret = _pmemfile_openat(pfp, at, pathname, flags, mode);

	if (at_unref)
		vinode_cleanup(pfp, at, ret == NULL);

	return ret;
}

/*
 * pmemfile_open -- open file
 */
PMEMfile *
pmemfile_open(PMEMfilepool *pfp, const char *pathname, int flags, ...)
{
	va_list ap;
	va_start(ap, flags);
	pmemfile_mode_t mode = 0;
	if ((flags & PMEMFILE_O_CREAT) || is_tmpfile(flags))
		mode = va_arg(ap, pmemfile_mode_t);
	va_end(ap);

	return pmemfile_openat(pfp, PMEMFILE_AT_CWD, pathname, flags, mode);
}

PMEMfile *
pmemfile_create(PMEMfilepool *pfp, const char *pathname, pmemfile_mode_t mode)
{
	return pmemfile_open(pfp, pathname, PMEMFILE_O_CREAT |
			PMEMFILE_O_WRONLY | PMEMFILE_O_TRUNC, mode);
}

/*
 * pmemfile_open_parent -- open a parent directory and return filename
 *
 * Together with *at interfaces it's very useful for path resolution when
 * pmemfile is mounted in place other than "/".
 */
PMEMfile *
pmemfile_open_parent(PMEMfilepool *pfp, PMEMfile *dir, char *path,
		size_t path_size, int flags)
{
	PMEMfile *ret = NULL;
	struct pmemfile_vinode *at;
	bool at_unref;
	int error = 0;

	if ((flags & PMEMFILE_OPEN_PARENT_ACCESS_MASK) ==
			PMEMFILE_OPEN_PARENT_ACCESS_MASK) {
		errno = EINVAL;
		return NULL;
	}

	if (flags & ~(PMEMFILE_OPEN_PARENT_STOP_AT_ROOT |
			PMEMFILE_OPEN_PARENT_SYMLINK_FOLLOW |
			PMEMFILE_OPEN_PARENT_ACCESS_MASK)) {
		errno = EINVAL;
		return NULL;
	}

	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return NULL;

	at = pool_get_dir_for_path(pfp, dir, path, &at_unref);

	struct pmemfile_path_info info;
	resolve_pathat(pfp, &cred, at, path, &info, flags);

	struct pmemfile_vinode *vparent;
	bool path_info_changed;

	do {
		path_info_changed = false;
		vparent = info.vinode;

		if (vparent == NULL) {
			error = ELOOP;
			goto end;
		}

		if (flags & PMEMFILE_OPEN_PARENT_SYMLINK_FOLLOW) {
			if (more_than_1_component(info.remaining))
				break;

			size_t namelen = component_length(info.remaining);

			if (namelen == 0)
				break;

			struct pmemfile_vinode *vinode =
					vinode_lookup_dirent(pfp, info.vinode,
					info.remaining, namelen, 0);

			if (vinode) {
				if (vinode_is_symlink(vinode)) {
					resolve_symlink(pfp, &cred, vinode,
							&info);
					path_info_changed = true;
				} else {
					vinode_unref(pfp, vinode);
				}
			}
		}
	} while (path_info_changed);

	ret = calloc(1, sizeof(*ret));
	if (!ret) {
		error = errno;
		goto end;
	}

	ret->vinode = vinode_ref(pfp, vparent);
	ret->flags = PFILE_READ | PFILE_NOATIME;
	os_mutex_init(&ret->mutex);
	size_t len = strlen(info.remaining);
	if (len >= path_size)
		len = path_size - 1;
	memmove(path, info.remaining, len);
	path[len] = 0;

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (at_unref)
		vinode_unref(pfp, at);

	if (error) {
		errno = error;
		return NULL;
	}

	return ret;
}

/*
 * pmemfile_close -- close file
 */
void
pmemfile_close(PMEMfilepool *pfp, PMEMfile *file)
{
	LOG(LDBG, "inode 0x%" PRIx64 " path %s", file->vinode->tinode.oid.off,
			pmfi_path(file->vinode));

	vinode_unref(pfp, file->vinode);

	os_mutex_destroy(&file->mutex);

	free(file);
}

static int
_pmemfile_linkat(PMEMfilepool *pfp,
		struct pmemfile_vinode *olddir, const char *oldpath,
		struct pmemfile_vinode *newdir, const char *newpath,
		int flags)
{
	LOG(LDBG, "oldpath %s newpath %s", oldpath, newpath);

	if ((flags & ~(PMEMFILE_AT_SYMLINK_FOLLOW | PMEMFILE_AT_EMPTY_PATH))
			!= 0) {
		errno = EINVAL;
		return -1;
	}

	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return -1;

	struct pmemfile_path_info src, dst = { NULL, NULL, 0 };
	struct pmemfile_vinode *src_vinode;

	int error = 0;

	if (oldpath[0] == 0 && (flags & PMEMFILE_AT_EMPTY_PATH)) {
		memset(&src, 0, sizeof(src));

		src_vinode = vinode_ref(pfp, olddir);
	} else {
		src_vinode = resolve_pathat_full(pfp, &cred, olddir, oldpath,
				&src, 0, flags & PMEMFILE_AT_SYMLINK_FOLLOW);
		if (src.error) {
			error = src.error;
			goto end;
		}

		if (strchr(src.remaining, '/')) {
			error = ENOTDIR;
			goto end;
		}
	}

	if (vinode_is_dir(src_vinode)) {
		error = EPERM;
		goto end;
	}

	resolve_pathat(pfp, &cred, newdir, newpath, &dst, 0);

	if (dst.error) {
		error = dst.error;
		goto end;
	}

	/* XXX: handle protected_hardlinks (see man 5 proc) */

	size_t dst_namelen = component_length(dst.remaining);

	os_rwlock_wrlock(&dst.vinode->rwlock);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!_vinode_can_access(&cred, dst.vinode, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		struct pmemfile_time t;
		file_get_time(&t);
		vinode_add_dirent(pfp, dst.vinode, dst.remaining, dst_namelen,
				src_vinode, t);
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&dst.vinode->rwlock);

	if (error == 0) {
		vinode_clear_debug_path(pfp, src_vinode);
		vinode_set_debug_path(pfp, dst.vinode, src_vinode,
				dst.remaining, dst_namelen);
	}

end:
	path_info_cleanup(pfp, &dst);
	path_info_cleanup(pfp, &src);
	put_cred(&cred);

	if (src_vinode)
		vinode_unref(pfp, src_vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_linkat(PMEMfilepool *pfp, PMEMfile *olddir, const char *oldpath,
		PMEMfile *newdir, const char *newpath, int flags)
{
	struct pmemfile_vinode *olddir_at, *newdir_at;
	bool olddir_at_unref, newdir_at_unref;

	if (!oldpath || !newpath) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	olddir_at = pool_get_dir_for_path(pfp, olddir, oldpath,
			&olddir_at_unref);
	newdir_at = pool_get_dir_for_path(pfp, newdir, newpath,
			&newdir_at_unref);

	int ret = _pmemfile_linkat(pfp, olddir_at, oldpath, newdir_at, newpath,
			flags);
	int error;
	if (ret)
		error = errno;

	if (olddir_at_unref)
		vinode_unref(pfp, olddir_at);

	if (newdir_at_unref)
		vinode_unref(pfp, newdir_at);

	if (ret)
		errno = error;

	return ret;
}

/*
 * pmemfile_link -- make a new name for a file
 */
int
pmemfile_link(PMEMfilepool *pfp, const char *oldpath, const char *newpath)
{
	struct pmemfile_vinode *at;

	if (!oldpath || !newpath) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	if (oldpath[0] == '/' && newpath[0] == '/')
		at = NULL;
	else
		at = pool_get_cwd(pfp);

	int ret = _pmemfile_linkat(pfp, at, oldpath, at, newpath, 0);

	if (at)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

static int
_pmemfile_unlinkat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *pathname)
{
	LOG(LDBG, "pathname %s", pathname);

	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return -1;

	int error = 0;

	struct pmemfile_path_info info;
	resolve_pathat(pfp, &cred, dir, pathname, &info, 0);
	struct pmemfile_vinode *vparent = info.vinode;
	struct pmemfile_vinode *volatile vinode = NULL;
	volatile bool parent_refed = false;

	if (info.error) {
		error = info.error;
		goto end;
	}

	size_t namelen = component_length(info.remaining);

	if (strchr(info.remaining, '/')) {
		error = ENOTDIR;
		goto end;
	}

	os_rwlock_wrlock(&vparent->rwlock);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!_vinode_can_access(&cred, vparent, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		vinode_unlink_dirent(pfp, vparent, info.remaining, namelen,
				&vinode, &parent_refed, true);
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&vparent->rwlock);

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (vinode)
		vinode_unref(pfp, vinode);

	if (error) {
		if (parent_refed)
			vinode_unref(pfp, vparent);
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_unlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int flags)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pathname) {
		errno = ENOENT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, pathname, &at_unref);

	int ret;

	if (flags & PMEMFILE_AT_REMOVEDIR)
		ret = _pmemfile_rmdirat(pfp, at, pathname);
	else {
		if (flags != 0) {
			errno = EINVAL;
			ret = -1;
		} else {
			ret = _pmemfile_unlinkat(pfp, at, pathname);
		}
	}

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

/*
 * pmemfile_unlink -- delete a name and possibly the file it refers to
 */
int
pmemfile_unlink(PMEMfilepool *pfp, const char *pathname)
{
	return pmemfile_unlinkat(pfp, PMEMFILE_AT_CWD, pathname, 0);
}

static int
_pmemfile_renameat2(PMEMfilepool *pfp,
		struct pmemfile_vinode *olddir, const char *oldpath,
		struct pmemfile_vinode *newdir, const char *newpath,
		unsigned flags)
{
	LOG(LDBG, "oldpath %s newpath %s", oldpath, newpath);

	if (flags) {
		LOG(LSUP, "0 flags supported in rename");
		errno = EINVAL;
		return -1;
	}

	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return -1;

	struct pmemfile_vinode *volatile dst_unlinked = NULL;
	struct pmemfile_vinode *volatile src_unlinked = NULL;
	volatile bool dst_parent_refed = false;
	volatile bool src_parent_refed = false;
	struct pmemfile_vinode *src_vinode = NULL, *dst_vinode = NULL;

	struct pmemfile_path_info src, dst;
	resolve_pathat(pfp, &cred, olddir, oldpath, &src, 0);
	resolve_pathat(pfp, &cred, newdir, newpath, &dst, 0);

	int error = 0;

	if (src.error) {
		error = src.error;
		goto end;
	}

	if (dst.error) {
		error = dst.error;
		goto end;
	}

	size_t src_namelen = component_length(src.remaining);
	size_t dst_namelen = component_length(dst.remaining);

	src_vinode = vinode_lookup_dirent(pfp, src.vinode, src.remaining,
			src_namelen, 0);
	if (!src_vinode) {
		error = ENOENT;
		goto end;
	}

	dst_vinode = vinode_lookup_dirent(pfp, dst.vinode, dst.remaining,
			dst_namelen, 0);

	struct pmemfile_vinode *src_parent = src.vinode;
	struct pmemfile_vinode *dst_parent = dst.vinode;

	if (vinode_is_dir(src_vinode)) {
		LOG(LSUP, "renaming directories is not supported yet");
		error = ENOTSUP;
		goto end;
	}

	if (src_parent == dst_parent)
		os_rwlock_wrlock(&dst_parent->rwlock);
	else if (src_parent < dst_parent) {
		os_rwlock_wrlock(&src_parent->rwlock);
		os_rwlock_wrlock(&dst_parent->rwlock);
	} else {
		os_rwlock_wrlock(&dst_parent->rwlock);
		os_rwlock_wrlock(&src_parent->rwlock);
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		/*
		 * XXX, when src dir == dst dir we can just update dirent,
		 * without linking and unlinking
		 */

		if (!_vinode_can_access(&cred, src_parent, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		if (!_vinode_can_access(&cred, dst_parent, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		vinode_unlink_dirent(pfp, dst_parent, dst.remaining,
				dst_namelen, &dst_unlinked, &dst_parent_refed,
				false);

		struct pmemfile_time t;
		file_get_time(&t);
		vinode_add_dirent(pfp, dst_parent, dst.remaining, dst_namelen,
				src_vinode, t);

		vinode_unlink_dirent(pfp, src_parent, src.remaining,
				src_namelen, &src_unlinked, &src_parent_refed,
				true);

		if (src_unlinked != src_vinode)
			/* XXX restart? lookups under lock? */
			pmemfile_tx_abort(ENOENT);

	} TX_ONABORT {
		error = errno;
	} TX_END

	if (src_parent == dst_parent)
		os_rwlock_unlock(&dst_parent->rwlock);
	else {
		os_rwlock_unlock(&src_parent->rwlock);
		os_rwlock_unlock(&dst_parent->rwlock);
	}

	if (dst_parent_refed)
		vinode_unref(pfp, dst_parent);

	if (src_parent_refed)
		vinode_unref(pfp, src_parent);

	if (dst_unlinked)
		vinode_unref(pfp, dst_unlinked);

	if (src_unlinked)
		vinode_unref(pfp, src_unlinked);

	if (error == 0) {
		vinode_clear_debug_path(pfp, src_vinode);
		vinode_set_debug_path(pfp, dst.vinode, src_vinode,
				dst.remaining, dst_namelen);
	}

end:
	path_info_cleanup(pfp, &dst);
	path_info_cleanup(pfp, &src);
	put_cred(&cred);

	if (dst_vinode)
		vinode_unref(pfp, dst_vinode);
	if (src_vinode)
		vinode_unref(pfp, src_vinode);

	if (error) {
		if (dst_parent_refed)
			vinode_unref(pfp, dst.vinode);

		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_rename(PMEMfilepool *pfp, const char *old_path, const char *new_path)
{
	struct pmemfile_vinode *at;

	if (!old_path || !new_path) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	if (old_path[0] == '/' && new_path[0] == '/')
		at = NULL;
	else
		at = pool_get_cwd(pfp);

	int ret = _pmemfile_renameat2(pfp, at, old_path, at, new_path, 0);

	if (at)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_renameat2(PMEMfilepool *pfp, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path, unsigned flags)
{
	struct pmemfile_vinode *olddir_at, *newdir_at;
	bool olddir_at_unref, newdir_at_unref;

	if (!old_path || !new_path) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	olddir_at = pool_get_dir_for_path(pfp, old_at, old_path,
			&olddir_at_unref);
	newdir_at = pool_get_dir_for_path(pfp, new_at, new_path,
			&newdir_at_unref);

	int ret = _pmemfile_renameat2(pfp, olddir_at, old_path, newdir_at,
			new_path, flags);
	int error;
	if (ret)
		error = errno;

	if (olddir_at_unref)
		vinode_unref(pfp, olddir_at);

	if (newdir_at_unref)
		vinode_unref(pfp, newdir_at);

	if (ret)
		errno = error;

	return ret;
}

int
pmemfile_renameat(PMEMfilepool *pfp, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path)
{
	return pmemfile_renameat2(pfp, old_at, old_path, new_at, new_path, 0);
}

static int
_pmemfile_symlinkat(PMEMfilepool *pfp, const char *target,
		struct pmemfile_vinode *dir, const char *linkpath)
{
	LOG(LDBG, "target %s linkpath %s", target, linkpath);

	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return -1;

	int error = 0;

	struct pmemfile_path_info info;
	resolve_pathat(pfp, &cred, dir, linkpath, &info, 0);
	struct pmemfile_vinode *vinode = NULL;

	struct pmemfile_vinode *vparent = info.vinode;

	if (info.error) {
		error = info.error;
		goto end;
	}

	size_t namelen = component_length(info.remaining);

	vinode = vinode_lookup_dirent(pfp, info.vinode, info.remaining,
			namelen, 0);
	if (vinode) {
		error = EEXIST;
		goto end;
	}

	size_t len = strlen(target);

	if (len >= PMEMFILE_IN_INODE_STORAGE) {
		error = ENAMETOOLONG;
		goto end;
	}

	os_rwlock_wrlock(&vparent->rwlock);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!_vinode_can_access(&cred, vparent, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		vinode = inode_alloc(pfp, PMEMFILE_S_IFLNK |
				PMEMFILE_ACCESSPERMS, vparent, NULL,
				info.remaining, namelen);
		struct pmemfile_inode *inode = vinode->inode;
		pmemobj_memcpy_persist(pfp->pop, inode->file_data.data, target,
				len);
		inode->size = len;

		vinode_add_dirent(pfp, vparent, info.remaining, namelen,
				vinode, inode->ctime);
	} TX_ONABORT {
		error = errno;
		vinode = NULL;
	} TX_END

	os_rwlock_unlock(&vparent->rwlock);

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (vinode)
		vinode_unref(pfp, vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_symlinkat(PMEMfilepool *pfp, const char *target, PMEMfile *newdir,
		const char *linkpath)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!target || !linkpath) {
		errno = ENOENT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, newdir, linkpath, &at_unref);

	int ret = _pmemfile_symlinkat(pfp, target, at, linkpath);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_symlink(PMEMfilepool *pfp, const char *target, const char *linkpath)
{
	return pmemfile_symlinkat(pfp, target, PMEMFILE_AT_CWD, linkpath);
}

static pmemfile_ssize_t
_pmemfile_readlinkat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *pathname, char *buf, size_t bufsiz)
{
	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
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

	vinode = vinode_lookup_dirent(pfp, info.vinode, info.remaining,
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
	put_cred(&cred);

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

	if (!pathname) {
		errno = ENOENT;
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

int
pmemfile_fcntl(PMEMfilepool *pfp, PMEMfile *file, int cmd, ...)
{
	int ret = 0;

	(void) pfp;
	(void) file;

	switch (cmd) {
		case PMEMFILE_F_SETLK:
			if (file->flags & PFILE_PATH) {
				errno = EBADF;
				return -1;
			}

			/* XXX */
			return 0;
		case PMEMFILE_F_GETFL:
			if (file->flags & PFILE_PATH)
				return PMEMFILE_O_PATH;

			ret |= PMEMFILE_O_LARGEFILE;
			if (file->flags & PFILE_APPEND)
				ret |= PMEMFILE_O_APPEND;
			if (file->flags & PFILE_NOATIME)
				ret |= PMEMFILE_O_NOATIME;
			if ((file->flags & PFILE_READ) == PFILE_READ)
				ret |= PMEMFILE_O_RDONLY;
			if ((file->flags & PFILE_WRITE) == PFILE_WRITE)
				ret |= PMEMFILE_O_WRONLY;
			if ((file->flags & (PFILE_READ | PFILE_WRITE)) ==
					(PFILE_READ | PFILE_WRITE))
				ret |= PMEMFILE_O_RDWR;
			return ret;
	}

	errno = ENOTSUP;
	return -1;
}

/*
 * pmemfile_stats -- get pool statistics
 */
void
pmemfile_stats(PMEMfilepool *pfp, struct pmemfile_stats *stats)
{
	PMEMoid oid;
	unsigned inodes = 0, dirs = 0, block_arrays = 0, inode_arrays = 0,
			blocks = 0;

	POBJ_FOREACH(pfp->pop, oid) {
		unsigned t = (unsigned)pmemobj_type_num(oid);

		if (t == TOID_TYPE_NUM(struct pmemfile_inode))
			inodes++;
		else if (t == TOID_TYPE_NUM(struct pmemfile_dir))
			dirs++;
		else if (t == TOID_TYPE_NUM(struct pmemfile_block_array))
			block_arrays++;
		else if (t == TOID_TYPE_NUM(struct pmemfile_inode_array))
			inode_arrays++;
		else if (t == TOID_TYPE_NUM(char))
			blocks++;
		else
			FATAL("unknown type %u", t);
	}
	stats->inodes = inodes;
	stats->dirs = dirs;
	stats->block_arrays = block_arrays;
	stats->inode_arrays = inode_arrays;
	stats->blocks = blocks;
}

/*
 * vinode_chmod
 *
 * Can't be called in a transaction.
 */
static int
vinode_chmod(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		pmemfile_mode_t mode)
{
	struct pmemfile_inode *inode = vinode->inode;
	int error = 0;
	pmemfile_uid_t fsuid;
	int cap;

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);

	os_rwlock_rdlock(&pfp->cred_rwlock);
	fsuid = pfp->cred.fsuid;
	cap = pfp->cred.caps;
	os_rwlock_unlock(&pfp->cred_rwlock);

	os_rwlock_wrlock(&vinode->rwlock);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (vinode->inode->uid != fsuid &&
				!(cap & (1 << PMEMFILE_CAP_FOWNER)))
			pmemfile_tx_abort(EPERM);

		TX_ADD_DIRECT(&inode->flags);

		inode->flags = (inode->flags & ~(uint64_t)PMEMFILE_ALLPERMS)
				| mode;
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&vinode->rwlock);

	return error;
}

static int
_pmemfile_fchmodat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path, pmemfile_mode_t mode, int flags)
{
	mode &= PMEMFILE_ALLPERMS;

	if (flags & PMEMFILE_AT_SYMLINK_NOFOLLOW) {
		errno = ENOTSUP;
		return -1;
	}

	if (flags & ~(PMEMFILE_AT_SYMLINK_NOFOLLOW)) {
		errno = EINVAL;
		return -1;
	}

	LOG(LDBG, "path %s", path);

	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return -1;

	int error = 0;
	struct pmemfile_path_info info;
	struct pmemfile_vinode *vinode =
		resolve_pathat_full(pfp, &cred, dir, path, &info, 0, true);

	if (info.error) {
		error = info.error;
		goto end;
	}

	if (!vinode_is_dir(vinode) && strchr(info.remaining, '/')) {
		error = ENOTDIR;
		goto end;
	}

	error = vinode_chmod(pfp, vinode, mode);

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (vinode)
		vinode_unref(pfp, vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_fchmodat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		pmemfile_mode_t mode, int flags)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pathname) {
		errno = ENOENT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, pathname, &at_unref);

	int ret = _pmemfile_fchmodat(pfp, at, pathname, mode, flags);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_chmod(PMEMfilepool *pfp, const char *path, pmemfile_mode_t mode)
{
	return pmemfile_fchmodat(pfp, PMEMFILE_AT_CWD, path, mode, 0);
}

int
pmemfile_fchmod(PMEMfilepool *pfp, PMEMfile *file, pmemfile_mode_t mode)
{
	if (!file) {
		errno = EBADF;
		return -1;
	}

	if (file->flags & PFILE_PATH) {
		errno = EBADF;
		return -1;
	}

	int ret = vinode_chmod(pfp, file->vinode, mode);

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

/*
 * pmemfile_setreuid -- sets real and effective user id
 */
int
pmemfile_setreuid(PMEMfilepool *pfp, pmemfile_uid_t ruid, pmemfile_uid_t euid)
{
	if (ruid != (pmemfile_uid_t)-1 && ruid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	if (euid != (pmemfile_uid_t)-1 && euid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cred_rwlock);
	if (ruid != (pmemfile_uid_t)-1)
		pfp->cred.ruid = ruid;
	if (euid != (pmemfile_uid_t)-1) {
		pfp->cred.euid = euid;
		pfp->cred.fsuid = euid;
	}
	os_rwlock_unlock(&pfp->cred_rwlock);

	return 0;
}

/*
 * pmemfile_setregid -- sets real and effective group id
 */
int
pmemfile_setregid(PMEMfilepool *pfp, pmemfile_gid_t rgid, pmemfile_gid_t egid)
{
	if (rgid != (pmemfile_gid_t)-1 && rgid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	if (egid != (pmemfile_gid_t)-1 && egid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cred_rwlock);
	if (rgid != (pmemfile_gid_t)-1)
		pfp->cred.rgid = rgid;
	if (egid != (pmemfile_gid_t)-1) {
		pfp->cred.egid = egid;
		pfp->cred.fsgid = egid;
	}
	os_rwlock_unlock(&pfp->cred_rwlock);

	return 0;
}

/*
 * pmemfile_setuid -- sets effective user id
 */
int
pmemfile_setuid(PMEMfilepool *pfp, pmemfile_uid_t uid)
{
	return pmemfile_setreuid(pfp, (pmemfile_uid_t)-1, uid);
}

/*
 * pmemfile_setgid -- sets effective group id
 */
int
pmemfile_setgid(PMEMfilepool *pfp, pmemfile_gid_t gid)
{
	return pmemfile_setregid(pfp, (pmemfile_gid_t)-1, gid);
}

/*
 * pmemfile_getuid -- returns real user id
 */
pmemfile_uid_t
pmemfile_getuid(PMEMfilepool *pfp)
{
	pmemfile_uid_t ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = pfp->cred.ruid;
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_getgid -- returns real group id
 */
pmemfile_gid_t
pmemfile_getgid(PMEMfilepool *pfp)
{
	pmemfile_gid_t ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = pfp->cred.rgid;
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_seteuid -- sets effective user id
 */
int
pmemfile_seteuid(PMEMfilepool *pfp, pmemfile_uid_t uid)
{
	return pmemfile_setreuid(pfp, (pmemfile_uid_t)-1, uid);
}

/*
 * pmemfile_setegid -- sets effective group id
 */
int
pmemfile_setegid(PMEMfilepool *pfp, pmemfile_gid_t gid)
{
	return pmemfile_setregid(pfp, (pmemfile_gid_t)-1, gid);
}

/*
 * pmemfile_geteuid -- returns effective user id
 */
pmemfile_uid_t
pmemfile_geteuid(PMEMfilepool *pfp)
{
	pmemfile_uid_t ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = pfp->cred.euid;
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_getegid -- returns effective group id
 */
pmemfile_gid_t
pmemfile_getegid(PMEMfilepool *pfp)
{
	pmemfile_gid_t ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = pfp->cred.egid;
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_setfsuid -- sets filesystem user id
 */
int
pmemfile_setfsuid(PMEMfilepool *pfp, pmemfile_uid_t fsuid)
{
	if (fsuid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cred_rwlock);
	pmemfile_uid_t prev_fsuid = pfp->cred.fsuid;
	pfp->cred.fsuid = fsuid;
	os_rwlock_unlock(&pfp->cred_rwlock);

	return (int)prev_fsuid;
}

/*
 * pmemfile_setfsgid -- sets filesystem group id
 */
int
pmemfile_setfsgid(PMEMfilepool *pfp, pmemfile_gid_t fsgid)
{
	if (fsgid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cred_rwlock);
	pmemfile_uid_t prev_fsgid = pfp->cred.fsgid;
	pfp->cred.fsgid = fsgid;
	os_rwlock_unlock(&pfp->cred_rwlock);

	return (int)prev_fsgid;
}

/*
 * pmemfile_getgroups -- fills "list" with supplementary group ids
 */
int
pmemfile_getgroups(PMEMfilepool *pfp, int size, pmemfile_gid_t list[])
{
	if (size < 0) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_rdlock(&pfp->cred_rwlock);
	size_t groupsnum = pfp->cred.groupsnum;
	if (groupsnum > (size_t)size) {
		errno = EINVAL;
		os_rwlock_unlock(&pfp->cred_rwlock);
		return -1;
	}

	memcpy(list, pfp->cred.groups,
			pfp->cred.groupsnum * sizeof(pfp->cred.groups[0]));

	os_rwlock_unlock(&pfp->cred_rwlock);
	return (int)groupsnum;
}

/*
 * pmemfile_getgroups -- sets supplementary group ids
 */
int
pmemfile_setgroups(PMEMfilepool *pfp, size_t size, const pmemfile_gid_t *list)
{
	int error = 0;
	os_rwlock_wrlock(&pfp->cred_rwlock);
	if (size != pfp->cred.groupsnum) {
		void *r = realloc(pfp->cred.groups,
				size * sizeof(pfp->cred.groups[0]));
		if (!r) {
			error = errno;
			goto end;
		}

		pfp->cred.groups = r;
		pfp->cred.groupsnum = size;
	}
	memcpy(pfp->cred.groups, list, size * sizeof(*list));

end:
	os_rwlock_unlock(&pfp->cred_rwlock);

	if (error) {
		errno = error;
		return -1;
	}
	return 0;
}

static int
_pmemfile_ftruncate(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
			uint64_t length)
{
	if (!vinode_is_regular_file(vinode))
		return EINVAL;

	int error = 0;

	os_rwlock_wrlock(&vinode->rwlock);

	vinode_snapshot(vinode);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		vinode_truncate(pfp, vinode, length);
	} TX_ONABORT {
		error = errno;
		vinode_restore_on_abort(vinode);
	} TX_END

	os_rwlock_unlock(&vinode->rwlock);

	return error;
}

int
pmemfile_ftruncate(PMEMfilepool *pfp, PMEMfile *file, pmemfile_off_t length)
{
	int ret;

	if (length < 0) {
		errno = EINVAL;
		return -1;
	}

	if (length > SSIZE_MAX) {
		errno = EFBIG;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	if (file->flags & PFILE_WRITE) {
		int err;

		err = _pmemfile_ftruncate(pfp, file->vinode, (uint64_t)length);
		if (err == 0) {
			ret = 0;
		} else {
			errno = err;
			ret = -1;
		}
	} else {
		errno = EBADF;
		ret = -1;
	}

	os_mutex_unlock(&file->mutex);

	return ret;
}

int
pmemfile_truncate(PMEMfilepool *pfp, const char *path, pmemfile_off_t length)
{
	if (length < 0) {
		errno = EINVAL;
		return -1;
	}

	if (length > SSIZE_MAX) {
		errno = EFBIG;
		return -1;
	}

	struct pmemfile_cred cred[1];
	if (get_cred(pfp, cred))
		return -1;

	int error = 0;
	struct pmemfile_vinode *vinode = NULL;
	struct pmemfile_vinode *vparent = NULL;
	bool unref_vparent = false;
	struct pmemfile_path_info info;

	if (path[0] == '/') {
		vparent = pfp->root;
		unref_vparent = false;
	} else {
		vparent = pool_get_cwd(pfp);
		unref_vparent = true;
	}

	vinode = resolve_pathat_full(pfp, cred, vparent, path, &info, 0, true);

	if (info.error) {
		error = info.error;
		goto end;
	}

	if (!_vinode_can_access(cred, vinode, PFILE_WANT_WRITE)) {
		error = EACCES;
		goto end;
	}

	if (vinode_is_dir(vinode)) {
		error = EISDIR;
		goto end;
	}

	error = _pmemfile_ftruncate(pfp, vinode, (uint64_t)length);

end:
	path_info_cleanup(pfp, &info);
	put_cred(cred);

	if (vinode)
		vinode_unref(pfp, vinode);

	if (unref_vparent)
		vinode_unref(pfp, vparent);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

/*
 * fallocate_check_arguments - part of pmemfile_fallocate implementation
 * Perform some checks that are independent of the file being operated on.
 */
static int
fallocate_check_arguments(int mode, pmemfile_off_t offset,
		pmemfile_off_t length)
{
	/*
	 * from man 2 fallocate:
	 *
	 * "EINVAL - offset was less than 0, or len was less
	 * than or equal to 0."
	 */
	if (length <= 0 || offset < 0)
		return EINVAL;

	/*
	 * from man 2 fallocate:
	 *
	 * "EFBIG - offset+len exceeds the maximum file size."
	 */
	if (offset + length > SSIZE_MAX || offset + length < offset)
		return EFBIG;

	/*
	 * from man 2 fallocate:
	 *
	 * "EOPNOTSUPP -  The  filesystem containing the file referred to by
	 * fd does not support this operation; or the mode is not supported by
	 * the filesystem containing the file referred to by fd."
	 *
	 * As of now, pmemfile_fallocate supports allocating disk space, and
	 * punching holes.
	 */
	if (mode & PMEMFILE_FL_COLLAPSE_RANGE) {
		LOG(LSUP, "PMEMFILE_FL_COLLAPSE_RANGE is not supported");
		return EOPNOTSUPP;
	}

	if (mode & PMEMFILE_FL_ZERO_RANGE) {
		LOG(LSUP, "PMEMFILE_FL_ZERO_RANGE is not supported");
		return EOPNOTSUPP;
	}

	if (mode & PMEMFILE_FL_INSERT_RANGE) {
		LOG(LSUP, "PMEMFILE_FL_INSERT_RANGE is not supported");
		return EOPNOTSUPP;
	}

	if (mode & PMEMFILE_FL_PUNCH_HOLE) {
		/*
		 * from man 2 fallocate:
		 *
		 * "The FALLOC_FL_PUNCH_HOLE flag must be ORed
		 * with FALLOC_FL_KEEP_SIZE in mode; in other words,
		 * even when punching off the end of the file, the file size
		 * (as reported by stat(2)) does not change."
		 */
		if (mode != (PMEMFILE_FL_PUNCH_HOLE | PMEMFILE_FL_KEEP_SIZE))
			return EINVAL;
	} else { /* Allocating disk space */
		/*
		 * Note: According to 'man 2 fallocate' FALLOC_FL_UNSHARE
		 * is another possible flag to accept here. No equivalent of
		 * that flag is supported by pmemfile as of now. Also that man
		 * page is wrong anyways, the header files only refer to
		 * FALLOC_FL_UNSHARE_RANGE, so it is suspected that noone is
		 * using it anyways.
		 */
		if ((mode & ~PMEMFILE_FL_KEEP_SIZE) != 0)
			return EINVAL;
	}

	return 0;
}

/*
 * file_fallocate - part of pmemfile_fallocate implementation
 * Operates on a PMEMfile, and calls vinode_fallocate, which
 * does the actual fallocate operation on the inode.
 *
 * This function expected the file to locked while being called,
 * and makes sure the vinode is locked while calling vinode_fallocate.
 */
static int
file_fallocate(PMEMfilepool *pfp, PMEMfile *file, int mode,
		uint64_t offset, uint64_t length)
{
	int error;

	/*
	 * from man 2 fallocate:
	 *
	 * "EBADF  fd is not a valid file descriptor, or is not opened for
	 * writing."
	 */
	if ((file->flags & PFILE_WRITE) == 0)
		return EBADF;

	os_rwlock_wrlock(&file->vinode->rwlock);

	error = vinode_fallocate(pfp, file->vinode, mode, offset, length);

	os_rwlock_unlock(&file->vinode->rwlock);

	return error;
}

int
pmemfile_fallocate(PMEMfilepool *pfp, PMEMfile *file, int mode,
		pmemfile_off_t offset, pmemfile_off_t length)
{
	int error;

	error = fallocate_check_arguments(mode, offset, length);

	if (error == 0) {
		ASSERT(offset >= 0);
		ASSERT(length > 0);

		os_mutex_lock(&file->mutex);

		error = file_fallocate(pfp, file, mode,
		    (uint64_t)offset, (uint64_t)length);

		os_mutex_unlock(&file->mutex);
	}

	if (error != 0) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_posix_fallocate(PMEMfilepool *pfp, PMEMfile *file,
		pmemfile_off_t offset, pmemfile_off_t length)
{
	return pmemfile_fallocate(pfp, file, 0, offset, length);
}

/*
 * vinode_chown
 *
 * Can't be called in a transaction.
 */
static int
vinode_chown(PMEMfilepool *pfp, const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	struct pmemfile_inode *inode = vinode->inode;
	int error = 0;

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE);

	if (owner == (pmemfile_uid_t)-1 && group == (pmemfile_gid_t)-1)
		return 0;

	os_rwlock_wrlock(&vinode->rwlock);

	if (!(cred->caps & (1 << PMEMFILE_CAP_CHOWN))) {
		if (inode->uid != cred->fsuid) {
			error = EPERM;
			goto end;
		}

		if (owner != (pmemfile_uid_t)-1 && owner != inode->uid) {
			error = EPERM;
			goto end;
		}

		if (group != (pmemfile_gid_t)-1 && group != inode->gid) {
			if (group != cred->fsgid && !gid_in_list(cred, group)) {
				error = EPERM;
				goto end;
			}
		}
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		COMPILE_ERROR_ON(offsetof(struct pmemfile_inode, gid) !=
				offsetof(struct pmemfile_inode, uid) +
				sizeof(inode->uid));

		pmemobj_tx_add_range_direct(&inode->uid,
				sizeof(inode->uid) + sizeof(inode->gid));

		if (owner != (pmemfile_uid_t)-1)
			inode->uid = owner;
		if (group != (pmemfile_gid_t)-1)
			inode->gid = group;
	} TX_ONABORT {
		error = errno;
	} TX_END

end:
	os_rwlock_unlock(&vinode->rwlock);

	return error;
}

static int
_pmemfile_fchownat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path, pmemfile_uid_t owner, pmemfile_gid_t group,
		int flags)
{
	if (flags & ~(PMEMFILE_AT_EMPTY_PATH | PMEMFILE_AT_SYMLINK_NOFOLLOW)) {
		errno = EINVAL;
		return -1;
	}

	LOG(LDBG, "path %s", path);

	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return -1;

	int error = 0;
	struct pmemfile_path_info info;
	struct pmemfile_vinode *vinode;

	if (path[0] == 0 && (flags & PMEMFILE_AT_EMPTY_PATH)) {
		memset(&info, 0, sizeof(info));
		vinode = vinode_ref(pfp, dir);
	} else {
		vinode = resolve_pathat_full(pfp, &cred, dir, path, &info, 0,
				!(flags & PMEMFILE_AT_SYMLINK_NOFOLLOW));
		if (info.error) {
			error = info.error;
			goto end;
		}

		if (!vinode_is_dir(vinode) && strchr(info.remaining, '/')) {
			error = ENOTDIR;
			goto end;
		}
	}

	error = vinode_chown(pfp, &cred, vinode, owner, group);

end:
	path_info_cleanup(pfp, &info);
	put_cred(&cred);

	if (vinode)
		vinode_unref(pfp, vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_fchownat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		pmemfile_uid_t owner, pmemfile_gid_t group, int flags)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pathname) {
		errno = ENOENT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, pathname, &at_unref);

	int ret = _pmemfile_fchownat(pfp, at, pathname, owner, group, flags);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_chown(PMEMfilepool *pfp, const char *pathname, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	return pmemfile_fchownat(pfp, PMEMFILE_AT_CWD, pathname, owner, group,
			0);
}

int
pmemfile_lchown(PMEMfilepool *pfp, const char *pathname, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	return pmemfile_fchownat(pfp, PMEMFILE_AT_CWD, pathname, owner, group,
			PMEMFILE_AT_SYMLINK_NOFOLLOW);
}

int
pmemfile_fchown(PMEMfilepool *pfp, PMEMfile *file, pmemfile_uid_t owner,
		pmemfile_gid_t group)
{
	if (!file) {
		errno = EBADF;
		return -1;
	}

	if (file->flags & PFILE_PATH) {
		errno = EBADF;
		return -1;
	}

	struct pmemfile_cred cred;
	if (get_cred(pfp, &cred))
		return -1;

	int ret = vinode_chown(pfp, &cred, file->vinode, owner, group);

	put_cred(&cred);

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

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
	if (get_cred(pfp, &cred))
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
				!(flags & PMEMFILE_AT_SYMLINK_NOFOLLOW));

	if (info.error) {
		error = info.error;
		goto end;
	}

	if (!vinode_is_dir(vinode) && strchr(info.remaining, '/')) {
		error = ENOTDIR;
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
	put_cred(&cred);

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

	if (!pathname) {
		errno = ENOENT;
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
