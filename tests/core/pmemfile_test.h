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

#ifndef PMEMFILE_TEST_H
#define PMEMFILE_TEST_H

#include "libpmemfile-core.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UT_FATAL(...) abort();
#define UT_ASSERT(cnd)			do { if (!(cnd)) abort(); } while (0)
#define UT_ASSERTinfo(cnd, info)	do { if (!(cnd)) abort(); } while (0)
#define UT_ASSERTeq(lhs, rhs)	do { if ((lhs) != (rhs)) abort(); } while (0)
#define UT_ASSERTne(lhs, rhs)	do { if ((lhs) == (rhs)) abort(); } while (0)
#define UT_OUT(...) fprintf(stdout, __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

static inline void *
MALLOC(size_t size)
{
	void *m = malloc(size);
	if (!m) abort();
	return m;
}
#define FREE(ptr) free(ptr)

static inline void
PTHREAD_CREATE(pthread_t *__restrict thread,
    const pthread_attr_t *__restrict attr,
    void *(*start_routine)(void *), void *__restrict arg)
{
	int ret = pthread_create(thread, attr, start_routine, arg);
	if (ret) {
		errno = ret;
		UT_FATAL("!pthread_create");
	}
}

static inline void
PTHREAD_JOIN(pthread_t thread, void **value_ptr)
{
	int ret = pthread_join(thread, value_ptr);
	if (ret) {
		errno = ret;
		UT_FATAL("!pthread_join");
	}
}

/*
 * is_zeroed -- check if given memory range is all zero
 */
static inline int
is_zeroed(const void *addr, size_t len)
{
	/* XXX optimize */
	const char *a = addr;
	while (len-- > 0)
		if (*a++)
			return 0;
	return 1;
}

/* pmemfile stuff */
PMEMfilepool *PMEMFILE_MKFS(const char *path);
PMEMfile *PMEMFILE_OPEN(PMEMfilepool *pfp, const char *path, int flags, ...);
PMEMfile *PMEMFILE_OPENAT(PMEMfilepool *pfp, PMEMfile *dir, const char *path,
		int flags, ...);
ssize_t PMEMFILE_WRITE(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
		size_t count, ssize_t expected, ...);
void PMEMFILE_CLOSE(PMEMfilepool *pfp, PMEMfile *file);
void PMEMFILE_CREATE(PMEMfilepool *pfp, const char *path, int flags,
		mode_t mode);
void PMEMFILE_LINK(PMEMfilepool *pfp, const char *oldpath, const char *newpath);
void PMEMFILE_LINKAT(PMEMfilepool *pfp, PMEMfile *olddir, const char *oldpath,
		PMEMfile *newdir, const char *newpath, int flags);
void PMEMFILE_UNLINK(PMEMfilepool *pfp, const char *path);
ssize_t PMEMFILE_READ(PMEMfilepool *pfp, PMEMfile *file, void *buf,
		size_t count, ssize_t expected, ...);
off_t PMEMFILE_LSEEK(PMEMfilepool *pfp, PMEMfile *file, off_t offset,
		int whence, off_t expected);
void PMEMFILE_MKDIR(PMEMfilepool *pfp, const char *path, mode_t mode);
void PMEMFILE_RMDIR(PMEMfilepool *pfp, const char *path);
void PMEMFILE_CHDIR(PMEMfilepool *pfp, const char *path);
void PMEMFILE_FCHDIR(PMEMfilepool *pfp, PMEMfile *dir);
char *PMEMFILE_GETCWD(PMEMfilepool *pfp, char *buf, size_t size,
		const char *cmp);
void PMEMFILE_RENAME(PMEMfilepool *pfp, const char *old_path,
		const char *new_path);

void PMEMFILE_STAT(PMEMfilepool *pfp, const char *path, struct stat *buf);
void PMEMFILE_LSTAT(PMEMfilepool *pfp, const char *path, struct stat *buf);
void PMEMFILE_FSTAT(PMEMfilepool *pfp, PMEMfile *file, struct stat *buf);
void PMEMFILE_FSTATAT(PMEMfilepool *pfp, PMEMfile *dir, const char *path,
		struct stat *buf, int flags);

void PMEMFILE_SYMLINK(PMEMfilepool *pfp, const char *target,
		const char *linkpath);
void PMEMFILE_SYMLINKAT(PMEMfilepool *pfp, const char *target, PMEMfile *newdir,
		const char *linkpath);
char *PMEMFILE_READLINK(PMEMfilepool *pfp, const char *pathname,
		const char *expected);
char *PMEMFILE_READLINKAT(PMEMfilepool *pfp, const char *dirpath,
		const char *pathname, const char *expected);

/* utilities */

struct pmemfile_ls {
	mode_t mode;
	nlink_t nlink;
	off_t size;
	const char *name;
	const char *link;

	uid_t uid;
	gid_t gid;
};

void PMEMFILE_STATS(PMEMfilepool *pfp, const struct pmemfile_stats expected);
ssize_t PMEMFILE_FILE_SIZE(PMEMfilepool *pfp, PMEMfile *file,
		ssize_t expected_size);
ssize_t PMEMFILE_PATH_SIZE(PMEMfilepool *pfp, const char *path,
		ssize_t expected_size);
const struct pmemfile_ls *PMEMFILE_PRINT_FILES64(PMEMfilepool *pfp,
		PMEMfile *dir, const char *dirp, unsigned length,
		const struct pmemfile_ls expected[], int print_attrs);
void PMEMFILE_LIST_FILES(PMEMfilepool *pfp, const char *path,
		const struct pmemfile_ls expected[]);
void PMEMFILE_LIST_FILES_WITH_ATTRS(PMEMfilepool *pfp, const char *path,
		const struct pmemfile_ls expected[]);
void PMEMFILE_ASSERT_EMPTY_DIR(PMEMfilepool *pfp, const char *path);

#ifdef __cplusplus
}
#endif

#endif
