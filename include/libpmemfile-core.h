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
 * libpmemfile-core.h -- definitions of libpmemfile-core entry points
 *
 * This library provides support for programming with persistent memory (pmem).
 *
 * libpmemfile-core provides support for pmem-resident files.
 */
#ifndef LIBPMEMFILE_CORE_H
#define LIBPMEMFILE_CORE_H 1

#include <sys/stat.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * opaque type internal to libpmemfile
 */
typedef struct pmemfilepool PMEMfilepool;
typedef struct pmemfile_file PMEMfile;

#define PMEMFILE_AT_CWD ((PMEMfile *)(((unsigned char *)0) - 1))

PMEMfilepool *pmemfile_mkfs(const char *pathname, size_t poolsize, mode_t mode);

PMEMfilepool *pmemfile_pool_open(const char *pathname);
void pmemfile_pool_close(PMEMfilepool *pfp);

PMEMfile *pmemfile_open(PMEMfilepool *pfp, const char *pathname, int flags,
		...);
PMEMfile *pmemfile_openat(PMEMfilepool *pfp, PMEMfile *dir,
		const char *pathname, int flags, ...);
/* XXX Should we get rid of PMEMfilepool pointer? */
void pmemfile_close(PMEMfilepool *pfp, PMEMfile *file);

int pmemfile_link(PMEMfilepool *pfp, const char *oldpath,
		const char *newpath);
int pmemfile_linkat(PMEMfilepool *pfp, PMEMfile *olddir, const char *oldpath,
		PMEMfile *newdir, const char *newpath, int flags);
int pmemfile_unlink(PMEMfilepool *pfp, const char *pathname);
int pmemfile_unlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int flags);
int pmemfile_rename(PMEMfilepool *, const char *old_path, const char *new_path);
int pmemfile_renameat(PMEMfilepool *, PMEMfile *old_at, const char *old_path,
				PMEMfile *new_at, const char *new_path);
int pmemfile_renameat2(PMEMfilepool *, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path, unsigned flags);

ssize_t pmemfile_write(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
		size_t count);

ssize_t pmemfile_read(PMEMfilepool *pfp, PMEMfile *file, void *buf,
		size_t count);

off_t pmemfile_lseek(PMEMfilepool *pfp, PMEMfile *file, off_t offset,
		int whence);

#ifdef __off64_t_defined
off64_t pmemfile_lseek64(PMEMfilepool *pfp, PMEMfile *file, off64_t offset,
		int whence);
#endif

ssize_t pmemfile_pwrite(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
		size_t count, off_t offset);

ssize_t pmemfile_pread(PMEMfilepool *pfp, PMEMfile *file, void *buf,
		size_t count, off_t offset);

int pmemfile_stat(PMEMfilepool *, const char *path, struct stat *buf);
int pmemfile_lstat(PMEMfilepool *, const char *path, struct stat *buf);
int pmemfile_fstat(PMEMfilepool *, PMEMfile *file, struct stat *buf);
int pmemfile_fstatat(PMEMfilepool *, PMEMfile *dir, const char *path,
		struct stat *buf, int flags);

struct linux_dirent;
struct linux_dirent64;
int pmemfile_getdents(PMEMfilepool *, PMEMfile *file,
			struct linux_dirent *dirp, unsigned count);
int pmemfile_getdents64(PMEMfilepool *, PMEMfile *file,
			struct linux_dirent64 *dirp, unsigned count);
int pmemfile_mkdir(PMEMfilepool *, const char *path, mode_t mode);
int pmemfile_mkdirat(PMEMfilepool *, PMEMfile *dir, const char *path,
		mode_t mode);
int pmemfile_rmdir(PMEMfilepool *, const char *path);

int pmemfile_chdir(PMEMfilepool *, const char *path);
int pmemfile_fchdir(PMEMfilepool *, PMEMfile *dir);
char *pmemfile_getcwd(PMEMfilepool *, char *buf, size_t size);
int pmemfile_fcntl(PMEMfilepool *, PMEMfile *file, int cmd, ...);

int pmemfile_symlink(PMEMfilepool *, const char *path1, const char *path2);
int pmemfile_symlinkat(PMEMfilepool *, const char *path1,
				PMEMfile *at, const char *path2);

ssize_t pmemfile_readlink(PMEMfilepool *, const char *path,
			char *buf, size_t buf_len);
ssize_t pmemfile_readlinkat(PMEMfilepool *, PMEMfile *dir, const char *pathname,
			char *buf, size_t bufsiz);

struct pmemfile_stats {
	unsigned inodes;
	unsigned dirs;
	unsigned block_arrays;
	unsigned inode_arrays;
	unsigned blocks;
};
void pmemfile_stats(PMEMfilepool *pfp, struct pmemfile_stats *stats);

char *pmemfile_get_dir_path(PMEMfilepool *pfp, PMEMfile *dir, char *buf,
		size_t size);

#define PMEMFILE_OPEN_PARENT_STOP_AT_ROOT (1<<0)
#define PMEMFILE_OPEN_PARENT_SYMLINK_FOLLOW (1<<1)
PMEMfile *pmemfile_open_parent(PMEMfilepool *pfp, PMEMfile *at,
		char *path, size_t path_size, int flags);
/*
 * PMEMFILE_MAJOR_VERSION and PMEMFILE_MINOR_VERSION provide the current version
 * of the libpmemfile API as provided by this header file.  Applications can
 * verify that the version available at run-time is compatible with the version
 * used at compile-time by passing these defines to pmemfile_check_version().
 */
#define PMEMFILE_MAJOR_VERSION 0
#define PMEMFILE_MINOR_VERSION 1
const char *pmemfile_check_version(
		unsigned major_required,
		unsigned minor_required);

const char *pmemfile_errormsg(void);

#include "libpmemfile-core-stubs.h"

#ifdef __cplusplus
}
#endif
#endif	/* libpmemfile-core.h */
