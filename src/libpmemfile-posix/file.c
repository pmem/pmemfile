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
 * file.c -- pmemfile_*open*, create, close, dup* implementation
 */

#include <inttypes.h>

#include "alloc.h"
#include "callbacks.h"
#include "dir.h"
#include "file.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "truncate.h"
#include "utils.h"

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
		ERR("O_ASYNC is not supported");
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
	bool tmpfile = is_tmpfile(flags);

	if ((flags & PMEMFILE_O_CREAT) || tmpfile) {
		mode = va_arg(ap, pmemfile_mode_t);
		LOG(LDBG, "mode %o", mode);
		mode &= PMEMFILE_ALLPERMS;
		mode &= ~pfp->umask;
	}
	va_end(ap);

	int error = 0;
	PMEMfile *file = NULL;

	struct pmemfile_path_info info;
	struct pmemfile_vinode *vinode;
	struct pmemfile_vinode *vparent;
	bool path_info_changed;
	size_t namelen;
	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return NULL;

	resolve_pathat(pfp, &cred, dir, pathname, &info, 0);

	do {
		path_info_changed = false;
		vparent = info.parent;
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
			vinode = vinode_lookup_dirent(pfp, info.parent,
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

	int accmode = flags & PMEMFILE_O_ACCMODE;

	if (tmpfile) {
		if (!vinode) {
			error = ENOENT;
			goto end;
		}

		if (!vinode_is_dir(vinode)) {
			error = ENOTDIR;
			goto end;
		}

		if (accmode == PMEMFILE_O_RDONLY) {
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

	if (tmpfile) {
		vinode_unref(pfp, vparent);
		vparent = vinode;
		vinode = NULL;
	}

	if (vinode) {
		if (!(flags & PMEMFILE_O_PATH)) {
			if (accmode == PMEMFILE_O_ACCMODE) {
				error = EINVAL;
				goto end;
			}

			int acc2;
			if (accmode == PMEMFILE_O_RDWR)
				acc2 = PFILE_WANT_READ | PFILE_WANT_WRITE;
			else if (accmode == PMEMFILE_O_RDONLY)
				acc2 = PFILE_WANT_READ;
			else
				acc2 = PFILE_WANT_WRITE;

			if (!vinode_can_access(&cred, vinode, acc2)) {
				error = EACCES;
				goto end;
			}
		}

		if ((flags & PMEMFILE_O_DIRECTORY) && !vinode_is_dir(vinode)) {
			error = ENOTDIR;
			goto end;
		}

		if (((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_WRONLY ||
		    (flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDWR) &&
		    vinode_is_dir(vinode)) {
			error = EISDIR;
			goto end;
		}

		if (flags & PMEMFILE_O_TRUNC) {
			if (!vinode_is_regular_file(vinode)) {
				LOG(LUSR, "truncating non regular file");
				error = EINVAL;
				goto end;
			}

			if (accmode == PMEMFILE_O_RDONLY) {
				LOG(LUSR, "O_TRUNC without write permissions");
				error = EACCES;
				goto end;
			}
		}
	}

	file = pf_calloc(1, sizeof(*file));
	if (!file) {
		error = errno;
		goto end;
	}

	if (flags & PMEMFILE_O_PATH)
		file->flags = PFILE_PATH;
	else if (accmode == PMEMFILE_O_RDONLY)
		file->flags = PFILE_READ;
	else if (accmode == PMEMFILE_O_WRONLY)
		file->flags = PFILE_WRITE;
	else if (accmode == PMEMFILE_O_RDWR)
		file->flags = PFILE_READ | PFILE_WRITE;

	if (flags & PMEMFILE_O_NOATIME)
		file->flags |= PFILE_NOATIME;
	if (flags & PMEMFILE_O_APPEND)
		file->flags |= PFILE_APPEND;

	ASSERT_NOT_IN_TX();

	if (vinode == NULL) {
		TOID(struct pmemfile_inode) tinode;

		os_rwlock_wrlock(&vparent->rwlock);

		if (!_vinode_can_access(&cred, vparent, PFILE_WANT_WRITE)) {
			os_rwlock_unlock(&vparent->rwlock);
			error = EACCES;
			goto end;
		}

		if (tmpfile)
			/* access to orphaned list requires superblock lock */
			os_rwlock_wrlock(&pfp->super_rwlock);

		struct inode_orphan_info orphan_info;

		TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
			tinode = inode_alloc(pfp, &cred,
					PMEMFILE_S_IFREG | mode);

			if (tmpfile)
				orphan_info = inode_orphan(pfp, tinode);
			else
				inode_add_dirent(pfp, vparent->tinode,
					info.remaining, namelen, tinode,
					PF_RO(pfp, tinode)->ctime);
		} TX_ONABORT {
			error = errno;
		} TX_END

		if (tmpfile)
			os_rwlock_unlock(&pfp->super_rwlock);

		if (error) {
			os_rwlock_unlock(&vparent->rwlock);
			goto end;
		}

		/*
		 * Refing needs to happen before anyone can access this inode.
		 * vparent write lock guarantees that. Without that another
		 * thread may unlink this file before we ref it and make our
		 * tinode invalid.
		 */
		vinode = inode_ref(pfp, tinode, vparent, info.remaining,
				namelen);
		if (vinode == NULL) {
			error = errno;
			os_rwlock_unlock(&vparent->rwlock);
			goto end;
		}

		if (tmpfile)
			vinode->orphaned = orphan_info;

		os_rwlock_unlock(&vparent->rwlock);
	} else {
		if (flags & PMEMFILE_O_TRUNC) {
			os_rwlock_wrlock(&vinode->rwlock);

			vinode->data_modification_counter++;
			vinode->metadata_modification_counter++;
			memory_barrier();

			error = vinode_truncate(pfp, vinode, 0);

			os_rwlock_unlock(&vinode->rwlock);

			if (error)
				goto end;
		}

		if (file->flags & PFILE_WRITE) {
			struct pmemfile_inode *inode = vinode->inode;

			uint64_t clrflags = PMEMFILE_S_ISUID | PMEMFILE_S_ISGID;

			if ((inode->flags & clrflags) &&
					cred.fsuid != inode->uid) {

				TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
					TX_ADD_DIRECT(&inode->flags);
					inode->flags &= ~clrflags;
				} TX_ONABORT {
					error = errno;
				} TX_END
			}
		}
	}

	if (error)
		goto end;

	file->vinode = vinode;
	if (vinode_is_dir(vinode))
		file->dir_pos.dir = &vinode->inode->file_data.dir;

end:
	path_info_cleanup(pfp, &info);
	cred_release(&cred);

	if (error) {
		if (vinode != NULL)
			vinode_unref(pfp, vinode);
		pf_free(file);

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
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return NULL;
	}

	if (!pathname) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return NULL;
	}

	if (pathname[0] != '/' && !dir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
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
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return NULL;
	}

	if (!path) {
		LOG(LUSR, "NULL path");
		errno = ENOENT;
		return NULL;
	}

	if (path[0] != '/' && !dir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return NULL;
	}

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
	if (cred_acquire(pfp, &cred))
		return NULL;

	at = pool_get_dir_for_path(pfp, dir, path, &at_unref);

	struct pmemfile_path_info info;
	resolve_pathat(pfp, &cred, at, path, &info, flags);

	struct pmemfile_vinode *vparent;
	bool path_info_changed;

	do {
		path_info_changed = false;
		vparent = info.parent;

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
					vinode_lookup_dirent(pfp, info.parent,
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

	ret = pf_calloc(1, sizeof(*ret));
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
	cred_release(&cred);

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

	pf_free(file);
}

PMEMfile *
pmemfile_dup(PMEMfilepool *pfp, PMEMfile *file)
{
	errno = ENOTSUP;
	return NULL;
}

PMEMfile *
pmemfile_dup2(PMEMfilepool *pfp, PMEMfile *file, PMEMfile *file2)
{
	errno = ENOTSUP;
	return NULL;
}

pmemfile_mode_t
pmemfile_umask(PMEMfilepool *pfp, pmemfile_mode_t mask)
{
	mode_t prev_umask = pfp->umask;

	pfp->umask = mask;

	return prev_umask;
}
