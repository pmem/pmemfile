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
 * timestamps.c -- pmemfile_*utime* implementation
 */

#include <errno.h>
#include "callbacks.h"
#include "creds.h"
#include "dir.h"
#include "file.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

enum utime_macros { UTIME_MACROS_DISABLED, UTIME_MACROS_ENABLED };

static bool
is_tm_valid(const struct pmemfile_time *tm)
{
	return tm->nsec >= 0 && tm->nsec <= 999999999 && tm->sec >= 0;
}

static int
vinode_file_time_set(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		const struct pmemfile_time *tm, enum utime_macros utm)
{
	struct pmemfile_time tm_buf[2];
	if (!tm) {
		tm = tm_buf;

		if (get_current_time(&tm_buf[0]))
			return -1;

		tm_buf[1] = tm_buf[0];

		tm = tm_buf;
	} else if (utm == UTIME_MACROS_ENABLED) {
		memcpy(tm_buf, tm, sizeof(tm_buf));

		for (int i = 0; i < 2; ++i) {
			if (tm_buf[i].nsec == PMEMFILE_UTIME_NOW) {
				if (get_current_time(&tm_buf[i]))
					return -1;
			} else if (tm_buf[i].nsec == PMEMFILE_UTIME_OMIT) {
				/* nothing */
			} else if (!is_tm_valid(&tm_buf[i])) {
				errno = EINVAL;
				return -1;
			}
		}

		tm = tm_buf;
	} else {
		for (int i = 0; i < 2; ++i) {
			if (!is_tm_valid(&tm[i])) {
				errno = EINVAL;
				return -1;
			}
		}
	}

	os_rwlock_wrlock(&vinode->rwlock);

	int error = 0;
	struct pmemfile_inode *inode = vinode->inode;

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (utm == UTIME_MACROS_DISABLED ||
				tm[0].nsec != PMEMFILE_UTIME_OMIT) {
			TX_ADD_DIRECT(&inode->atime);
			inode->atime = tm[0];
		}

		if (utm == UTIME_MACROS_DISABLED ||
				tm[1].nsec != PMEMFILE_UTIME_OMIT) {
			TX_ADD_DIRECT(&inode->mtime);
			inode->mtime = tm[1];
		}
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&vinode->rwlock);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

static int
pmemfile_file_time_set(PMEMfilepool *pfp, PMEMfile *dir, const char *filename,
		const struct pmemfile_time *tm,
		enum symlink_resolve last_symlink, enum utime_macros utm)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!filename) {
		LOG(LUSR, "NULL filename");
		errno = ENOENT;
		return -1;
	}

	if (filename[0] != '/' && !dir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return -1;
	}

	/*
	 * From man utimensat:
	 * "If both tv_nsec fields are specified as UTIME_OMIT, then the Linux
	 * implementation of utimensat() succeeds even if the file referred to
	 * by dirfd and pathname does not exist."
	 */
	if (utm == UTIME_MACROS_ENABLED &&
			tm[0].nsec == PMEMFILE_UTIME_OMIT &&
			tm[1].nsec == PMEMFILE_UTIME_OMIT) {
		return 0;
	}

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	int error = 0;
	bool at_unref;

	struct pmemfile_vinode *at =
			pool_get_dir_for_path(pfp, dir, filename, &at_unref);

	struct pmemfile_path_info info;
	struct pmemfile_vinode *vinode = resolve_pathat_full(pfp, &cred, at,
			filename, &info, 0, last_symlink);

	if (info.error) {
		error = info.error;
		goto end;
	}

	if (!_vinode_can_access(&cred, vinode, PFILE_WANT_WRITE)) {
		error = EACCES;
		goto end;
	}

	if (vinode_file_time_set(pfp, vinode, tm, utm))
		error = errno;

end:
	path_info_cleanup(pfp, &info);
	cred_release(&cred);

	ASSERT_NOT_IN_TX();
	if (vinode)
		vinode_unref(pfp, vinode);

	if (at_unref)
		vinode_cleanup(pfp, at, error != 0);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_utime(PMEMfilepool *pfp, const char *filename,
		const pmemfile_utimbuf_t *times)
{
	if (!times)
		return pmemfile_file_time_set(pfp, PMEMFILE_AT_CWD, filename,
				NULL, RESOLVE_LAST_SYMLINK,
				UTIME_MACROS_DISABLED);

	struct pmemfile_time tm[2];

	tm[0].sec = times->actime;
	tm[0].nsec = 0;
	tm[1].sec = times->modtime;
	tm[1].nsec = 0;

	return pmemfile_file_time_set(pfp, PMEMFILE_AT_CWD, filename, tm,
			RESOLVE_LAST_SYMLINK, UTIME_MACROS_DISABLED);
}

static bool
is_timeval_valid(const pmemfile_timeval_t *tm)
{
	return tm->tv_usec >= 0 && tm->tv_usec <= 999999 && tm->tv_sec >= 0;
}

static int
timeval_to_time(const pmemfile_timeval_t times[2], struct pmemfile_time tm[2])
{
	for (int i = 0; i < 2; ++i) {
		if (!is_timeval_valid(&times[i])) {
			errno = EINVAL;
			return -1;
		}

		tm[i].sec = times[i].tv_sec;
		tm[i].nsec = times[i].tv_usec * 1000;
	}

	return 0;
}

int
pmemfile_utimes(PMEMfilepool *pfp, const char *filename,
		const pmemfile_timeval_t times[2])
{
	if (!times)
		return pmemfile_file_time_set(pfp, PMEMFILE_AT_CWD, filename,
				NULL, RESOLVE_LAST_SYMLINK,
				UTIME_MACROS_DISABLED);

	struct pmemfile_time tm[2];
	if (timeval_to_time(times, tm))
		return -1;

	return pmemfile_file_time_set(pfp, PMEMFILE_AT_CWD, filename, tm,
			RESOLVE_LAST_SYMLINK, UTIME_MACROS_DISABLED);
}

int
pmemfile_futimes(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_timeval_t tv[2])
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	uint64_t flags;

	os_mutex_lock(&file->mutex);
	flags = file->flags;
	os_mutex_unlock(&file->mutex);

	if (!(flags & PFILE_WRITE)) {
		errno = EBADF;
		return -1;
	}

	if (!tv)
		return vinode_file_time_set(pfp, file->vinode, NULL,
				UTIME_MACROS_DISABLED);

	struct pmemfile_time tm[2];
	if (timeval_to_time(tv, tm))
		return -1;

	return vinode_file_time_set(pfp, file->vinode, tm,
			UTIME_MACROS_DISABLED);
}

int
pmemfile_lutimes(PMEMfilepool *pfp, const char *filename,
		const pmemfile_timeval_t tv[2])
{
	if (!tv)
		return pmemfile_file_time_set(pfp, PMEMFILE_AT_CWD, filename,
				NULL, NO_RESOLVE_LAST_SYMLINK,
				UTIME_MACROS_DISABLED);

	struct pmemfile_time tm[2];

	if (timeval_to_time(tv, tm))
		return -1;

	return pmemfile_file_time_set(pfp, PMEMFILE_AT_CWD, filename, tm,
			NO_RESOLVE_LAST_SYMLINK, UTIME_MACROS_DISABLED);
}

int
pmemfile_futimesat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		const pmemfile_timeval_t tv[2])
{
	if (!tv)
		return pmemfile_file_time_set(pfp, dir, pathname, NULL,
				RESOLVE_LAST_SYMLINK, UTIME_MACROS_DISABLED);

	struct pmemfile_time tm[2];

	if (timeval_to_time(tv, tm))
		return -1;

	return pmemfile_file_time_set(pfp, dir, pathname, tm,
			RESOLVE_LAST_SYMLINK, UTIME_MACROS_DISABLED);
}

int
pmemfile_utimensat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		const pmemfile_timespec_t times[2], int flags)
{
	if (flags & ~PMEMFILE_AT_SYMLINK_NOFOLLOW) {
		errno = EINVAL;
		return -1;
	}

	enum symlink_resolve last_symlink =
			(flags & PMEMFILE_AT_SYMLINK_NOFOLLOW) ?
			NO_RESOLVE_LAST_SYMLINK : RESOLVE_LAST_SYMLINK;

	if (!times) {
		/*
		 * Linux nonstandard syscall-level feature. Glibc behaves
		 * differently, but we have to emulate kernel behavior because
		 * futimens at glibc level is implemented using utimensat with
		 * NULL pathname. See "C library/ kernel ABI differences"
		 * section in man utimensat.
		 */
		if (pathname == NULL) {
			if (!dir || dir == PMEMFILE_AT_CWD) {
				errno = EFAULT;
				return -1;
			}

			return vinode_file_time_set(pfp, dir->vinode, NULL,
					UTIME_MACROS_DISABLED);
		}
		return pmemfile_file_time_set(pfp, dir, pathname, NULL,
				last_symlink, UTIME_MACROS_DISABLED);
	}

	struct pmemfile_time tm[2];

	tm[0].sec = times[0].tv_sec;
	tm[0].nsec = times[0].tv_nsec;
	tm[1].sec = times[1].tv_sec;
	tm[1].nsec = times[1].tv_nsec;

	/* see comment above */
	if (pathname == NULL) {
		if (!dir || dir == PMEMFILE_AT_CWD) {
			errno = EFAULT;
			return -1;
		}

		return vinode_file_time_set(pfp, dir->vinode, tm,
				UTIME_MACROS_ENABLED);
	}

	return pmemfile_file_time_set(pfp, dir, pathname, tm, last_symlink,
			UTIME_MACROS_ENABLED);
}

int
pmemfile_futimens(PMEMfilepool *pfp, PMEMfile *file,
		const pmemfile_timespec_t times[2])
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	uint64_t flags;

	os_mutex_lock(&file->mutex);
	flags = file->flags;
	os_mutex_unlock(&file->mutex);

	if (!(flags & PFILE_WRITE)) {
		errno = EBADF;
		return -1;
	}

	if (!times)
		return vinode_file_time_set(pfp, file->vinode, NULL,
				UTIME_MACROS_DISABLED);

	struct pmemfile_time tm[2];

	tm[0].sec = times[0].tv_sec;
	tm[0].nsec = times[0].tv_nsec;
	tm[1].sec = times[1].tv_sec;
	tm[1].nsec = times[1].tv_nsec;

	return vinode_file_time_set(pfp, file->vinode, tm,
			UTIME_MACROS_ENABLED);
}
