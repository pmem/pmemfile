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
 * libpmemfile-posix.h -- definitions of libpmemfile-posix entry points
 *
 * This library provides support for programming with persistent memory (pmem).
 *
 * libpmemfile-posix provides support for pmem-resident files.
 */
#ifndef LIBPMEMFILE_POSIX_H
#define LIBPMEMFILE_POSIX_H 1

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <utime.h>

#define PMEMFILE_PATH_MAX 4096

#define PMEMFILE_O_RDONLY	       00
#define PMEMFILE_O_WRONLY	       01
#define PMEMFILE_O_RDWR		       02
#define PMEMFILE_O_ACCMODE	     0003

#define PMEMFILE_O_CREAT	     0100
#define PMEMFILE_O_EXCL		     0200
#define PMEMFILE_O_NOCTTY	     0400
#define PMEMFILE_O_TRUNC	    01000
#define PMEMFILE_O_APPEND	    02000
#define PMEMFILE_O_NONBLOCK	    04000
#define PMEMFILE_O_NDELAY	PMEMFILE_O_NONBLOCK
#define PMEMFILE_O_SYNC		 04010000
#define PMEMFILE_O_ASYNC	   020000

#define PMEMFILE_O_LARGEFILE	        0
#define PMEMFILE_O_DIRECTORY	  0200000
#define PMEMFILE_O_NOFOLLOW	  0400000
#define PMEMFILE_O_CLOEXEC	 02000000
#define PMEMFILE_O_DIRECT	   040000
#define PMEMFILE_O_NOATIME	 01000000
#define PMEMFILE_O_PATH		010000000
#define PMEMFILE_O_DSYNC	   010000
#define PMEMFILE_O_TMPFILE	(020000000 | PMEMFILE_O_DIRECTORY)

#define PMEMFILE_S_IFMT		0170000
#define PMEMFILE_S_IFDIR	0040000
#define PMEMFILE_S_IFCHR	0020000
#define PMEMFILE_S_IFBLK	0060000
#define PMEMFILE_S_IFREG	0100000
#define PMEMFILE_S_IFIFO	0010000
#define PMEMFILE_S_IFLNK	0120000
#define PMEMFILE_S_IFSOCK	0140000

#define PMEMFILE_S_ISTYPE(mode, mask)	(((mode) & PMEMFILE_S_IFMT) == (mask))
#define PMEMFILE_S_ISDIR(mode)	PMEMFILE_S_ISTYPE((mode), PMEMFILE_S_IFDIR)
#define PMEMFILE_S_ISCHR(mode)	PMEMFILE_S_ISTYPE((mode), PMEMFILE_S_IFCHR)
#define PMEMFILE_S_ISBLK(mode)	PMEMFILE_S_ISTYPE((mode), PMEMFILE_S_IFBLK)
#define PMEMFILE_S_ISREG(mode)	PMEMFILE_S_ISTYPE((mode), PMEMFILE_S_IFREG)
#define PMEMFILE_S_ISFIFO(mode)	PMEMFILE_S_ISTYPE((mode), PMEMFILE_S_IFIFO)
#define PMEMFILE_S_ISLNK(mode)	PMEMFILE_S_ISTYPE((mode), PMEMFILE_S_IFLNK)
#define PMEMFILE_S_ISSOCK(mode)	PMEMFILE_S_ISTYPE((mode), PMEMFILE_S_IFSOCK)

#define PMEMFILE_S_ISUID	04000
#define PMEMFILE_S_ISGID	02000
#define PMEMFILE_S_ISVTX	01000

#define PMEMFILE_S_IRUSR	0400
#define PMEMFILE_S_IWUSR	0200
#define PMEMFILE_S_IXUSR	0100
#define PMEMFILE_S_IRWXU	0700

#define PMEMFILE_S_IRGRP	040
#define PMEMFILE_S_IWGRP	020
#define PMEMFILE_S_IXGRP	010
#define PMEMFILE_S_IRWXG	070

#define PMEMFILE_S_IROTH	04
#define PMEMFILE_S_IWOTH	02
#define PMEMFILE_S_IXOTH	01
#define PMEMFILE_S_IRWXO	07

#define PMEMFILE_ACCESSPERMS	0777
#define PMEMFILE_ALLPERMS		07777

#define PMEMFILE_AT_SYMLINK_NOFOLLOW	0x100
#define PMEMFILE_AT_REMOVEDIR		0x200
#define PMEMFILE_AT_SYMLINK_FOLLOW	0x400
#define PMEMFILE_AT_NO_AUTOMOUNT	0x800
#define PMEMFILE_AT_EMPTY_PATH		0x1000
#define PMEMFILE_AT_EACCESS		0x200

#define PMEMFILE_F_DUPFD 0
#define PMEMFILE_F_GETFD 1
#define PMEMFILE_F_SETFD 2
#define PMEMFILE_F_GETFL 3
#define PMEMFILE_F_SETFL 4

#define PMEMFILE_F_RDLCK  0
#define PMEMFILE_F_WRLCK  1
#define PMEMFILE_F_UNLCK  2

#define PMEMFILE_F_GETLK  5
#define PMEMFILE_F_SETLK  6
#define PMEMFILE_F_SETLKW 7

#define PMEMFILE_SEEK_SET  0
#define PMEMFILE_SEEK_CUR  1
#define PMEMFILE_SEEK_END  2
#define PMEMFILE_SEEK_DATA 3
#define PMEMFILE_SEEK_HOLE 4

#define PMEMFILE_DT_UNKNOWN	0
#define PMEMFILE_DT_FIFO 	1
#define PMEMFILE_DT_CHR		2
#define PMEMFILE_DT_DIR		4
#define PMEMFILE_DT_BLK		6
#define PMEMFILE_DT_REG		8
#define PMEMFILE_DT_LNK		10
#define PMEMFILE_DT_SOCK	12
#define PMEMFILE_DT_WHT		14

#define PMEMFILE_R_OK 4
#define PMEMFILE_W_OK 2
#define PMEMFILE_X_OK 1
#define PMEMFILE_F_OK 0

#define PMEMFILE_FALLOC_FL_KEEP_SIZE       0x01
#define PMEMFILE_FALLOC_FL_PUNCH_HOLE      0x02
#define PMEMFILE_FALLOC_FL_COLLAPSE_RANGE  0x08
#define PMEMFILE_FALLOC_FL_ZERO_RANGE      0x10
#define PMEMFILE_FALLOC_FL_INSERT_RANGE    0x20

#define PMEMFILE_FD_CLOEXEC 1

#define PMEMFILE_RENAME_NOREPLACE	(1 << 0)
#define PMEMFILE_RENAME_EXCHANGE	(1 << 1)
#define PMEMFILE_RENAME_WHITEOUT	(1 << 2)

#define PMEMFILE_UTIME_NOW	((1l << 30) - 1l)
#define PMEMFILE_UTIME_OMIT	((1l << 30) - 2l)

#define PMEMFILE_MAP_FAILED	((void *) -1)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * opaque type internal to libpmemfile
 */
typedef struct pmemfilepool PMEMfilepool;
typedef struct pmemfile_file PMEMfile;

typedef mode_t pmemfile_mode_t;
typedef uid_t pmemfile_uid_t;
typedef gid_t pmemfile_gid_t;
typedef ssize_t pmemfile_ssize_t;
#ifndef __off64_t_defined
#error off64_t is not available, on glibc enable _LARGEFILE64_SOURCE, _XOPEN_SOURCE>=500 or _GNU_SOURCE to define it
#endif
typedef off64_t pmemfile_off_t;
typedef nlink_t pmemfile_nlink_t;
typedef blksize_t pmemfile_blksize_t;
typedef blkcnt64_t pmemfile_blkcnt_t;
typedef dev_t pmemfile_dev_t;
typedef ino64_t pmemfile_ino_t;
typedef struct iovec pmemfile_iovec_t;

typedef struct timespec pmemfile_timespec_t;
typedef struct utimbuf pmemfile_utimbuf_t;
typedef struct timeval pmemfile_timeval_t;
typedef struct stat pmemfile_stat_t;
typedef struct flock pmemfile_flock_t;

#define PMEMFILE_AT_CWD ((PMEMfile *)(((unsigned char *)0) - 1))

/* guaranteed to return at least 1 for any opened pool */
unsigned pmemfile_pool_root_count(PMEMfilepool *);

PMEMfilepool *pmemfile_pool_create(const char *pathname, size_t poolsize,
		pmemfile_mode_t mode);

PMEMfilepool *pmemfile_pool_open(const char *pathname);
void pmemfile_pool_close(PMEMfilepool *pfp);
void pmemfile_pool_set_device(PMEMfilepool *pfp, pmemfile_dev_t dev);

PMEMfile *pmemfile_open(PMEMfilepool *pfp, const char *pathname, int flags,
		...);
PMEMfile *pmemfile_openat(PMEMfilepool *pfp, PMEMfile *dir,
		const char *pathname, int flags, ...);
PMEMfile *pmemfile_open_root(PMEMfilepool *pfp, unsigned index, int flags);

PMEMfile *pmemfile_create(PMEMfilepool *pfp, const char *pathname,
		pmemfile_mode_t mode);
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

pmemfile_ssize_t pmemfile_read(PMEMfilepool *pfp, PMEMfile *file, void *buf,
		size_t count);
pmemfile_ssize_t pmemfile_pread(PMEMfilepool *pfp, PMEMfile *file, void *buf,
		size_t count, pmemfile_off_t offset);
pmemfile_ssize_t pmemfile_readv(PMEMfilepool *, PMEMfile *file,
	const pmemfile_iovec_t *iov, int iovcnt);
pmemfile_ssize_t pmemfile_preadv(PMEMfilepool *, PMEMfile *file,
	const pmemfile_iovec_t *iov, int iovcnt, pmemfile_off_t offset);

pmemfile_ssize_t pmemfile_write(PMEMfilepool *pfp, PMEMfile *file,
		const void *buf, size_t count);
pmemfile_ssize_t pmemfile_pwrite(PMEMfilepool *pfp, PMEMfile *file,
		const void *buf, size_t count, pmemfile_off_t offset);
pmemfile_ssize_t pmemfile_writev(PMEMfilepool *, PMEMfile *file,
	const pmemfile_iovec_t *iov, int iovcnt);
pmemfile_ssize_t pmemfile_pwritev(PMEMfilepool *, PMEMfile *file,
	const pmemfile_iovec_t *iov, int iovcnt, pmemfile_off_t offset);

pmemfile_off_t pmemfile_lseek(PMEMfilepool *pfp, PMEMfile *file,
		pmemfile_off_t offset, int whence);

int pmemfile_stat(PMEMfilepool *, const char *path, pmemfile_stat_t *buf);
int pmemfile_lstat(PMEMfilepool *, const char *path, pmemfile_stat_t *buf);
int pmemfile_fstat(PMEMfilepool *, PMEMfile *file, pmemfile_stat_t *buf);
int pmemfile_fstatat(PMEMfilepool *, PMEMfile *dir, const char *path,
		pmemfile_stat_t *buf, int flags);

struct linux_dirent;
struct linux_dirent64;
int pmemfile_getdents(PMEMfilepool *, PMEMfile *file,
			struct linux_dirent *dirp, unsigned count);
int pmemfile_getdents64(PMEMfilepool *, PMEMfile *file,
			struct linux_dirent64 *dirp, unsigned count);
int pmemfile_mkdir(PMEMfilepool *, const char *path, pmemfile_mode_t mode);
int pmemfile_mkdirat(PMEMfilepool *, PMEMfile *dir, const char *path,
		pmemfile_mode_t mode);
int pmemfile_rmdir(PMEMfilepool *, const char *path);

int pmemfile_chdir(PMEMfilepool *, const char *path);
int pmemfile_fchdir(PMEMfilepool *, PMEMfile *dir);
char *pmemfile_getcwd(PMEMfilepool *, char *buf, size_t size);
int pmemfile_fcntl(PMEMfilepool *, PMEMfile *file, int cmd, ...);

pmemfile_mode_t pmemfile_umask(PMEMfilepool *, pmemfile_mode_t mask);

int pmemfile_symlink(PMEMfilepool *, const char *path1, const char *path2);
int pmemfile_symlinkat(PMEMfilepool *, const char *path1,
				PMEMfile *at, const char *path2);

pmemfile_ssize_t pmemfile_readlink(PMEMfilepool *, const char *path,
			char *buf, size_t buf_len);
pmemfile_ssize_t pmemfile_readlinkat(PMEMfilepool *, PMEMfile *dir,
			const char *pathname, char *buf, size_t bufsiz);

int pmemfile_chmod(PMEMfilepool *, const char *path, pmemfile_mode_t mode);
int pmemfile_fchmod(PMEMfilepool *, PMEMfile *, pmemfile_mode_t mode);
int pmemfile_fchmodat(PMEMfilepool *, PMEMfile *dir, const char *pathname,
		pmemfile_mode_t mode, int flags);

int pmemfile_setreuid(PMEMfilepool *, pmemfile_uid_t ruid, pmemfile_uid_t euid);
int pmemfile_setregid(PMEMfilepool *, pmemfile_gid_t rgid, pmemfile_gid_t egid);

int pmemfile_setuid(PMEMfilepool *, pmemfile_uid_t uid);
int pmemfile_setgid(PMEMfilepool *, pmemfile_gid_t gid);
pmemfile_uid_t pmemfile_getuid(PMEMfilepool *);
pmemfile_gid_t pmemfile_getgid(PMEMfilepool *);

int pmemfile_seteuid(PMEMfilepool *, pmemfile_uid_t uid);
int pmemfile_setegid(PMEMfilepool *, pmemfile_gid_t gid);
pmemfile_uid_t pmemfile_geteuid(PMEMfilepool *);
pmemfile_gid_t pmemfile_getegid(PMEMfilepool *);

int pmemfile_setfsuid(PMEMfilepool *, pmemfile_uid_t fsuid);
int pmemfile_setfsgid(PMEMfilepool *, pmemfile_uid_t fsgid);

int pmemfile_getgroups(PMEMfilepool *, int size, pmemfile_gid_t list[]);
int pmemfile_setgroups(PMEMfilepool *, size_t size, const pmemfile_gid_t *list);

int pmemfile_chown(PMEMfilepool *, const char *pathname, pmemfile_uid_t owner,
		pmemfile_gid_t group);
int pmemfile_fchown(PMEMfilepool *, PMEMfile *file, pmemfile_uid_t owner,
		pmemfile_gid_t group);
int pmemfile_lchown(PMEMfilepool *, const char *pathname, pmemfile_uid_t owner,
		pmemfile_gid_t group);
int pmemfile_fchownat(PMEMfilepool *, PMEMfile *dir, const char *pathname,
		pmemfile_uid_t owner, pmemfile_gid_t group, int flags);

int pmemfile_access(PMEMfilepool *, const char *path, int mode);
int pmemfile_euidaccess(PMEMfilepool *, const char *pathname, int mode);
int pmemfile_faccessat(PMEMfilepool *, PMEMfile *dir, const char *pathname,
		int mode, int flags);

int pmemfile_utime(PMEMfilepool *, const char *filename,
		const pmemfile_utimbuf_t *times);
int pmemfile_utimes(PMEMfilepool *, const char *filename,
		const pmemfile_timeval_t times[2]);
int pmemfile_futimes(PMEMfilepool *, PMEMfile *file,
		const pmemfile_timeval_t tv[2]);
int pmemfile_futimesat(PMEMfilepool *, PMEMfile *dir, const char *pathname,
		const pmemfile_timeval_t tv[2]);
int pmemfile_lutimes(PMEMfilepool *, const char *filename,
		const pmemfile_timeval_t tv[2]);
int pmemfile_utimensat(PMEMfilepool *, PMEMfile *dir, const char *pathname,
		const pmemfile_timespec_t times[2], int flags);
int pmemfile_futimens(PMEMfilepool *, PMEMfile *file,
		const pmemfile_timespec_t times[2]);

#define PMEMFILE_CAP_CHOWN 0
#define PMEMFILE_CAP_FOWNER 3
#define PMEMFILE_CAP_FSETID 4
int pmemfile_setcap(PMEMfilepool *, int cap);
int pmemfile_clrcap(PMEMfilepool *, int cap);

struct pmemfile_stats {
	unsigned inodes;
	unsigned dirs;
	unsigned block_arrays;
	unsigned inode_arrays;
	unsigned blocks;
};
void pmemfile_stats(PMEMfilepool *pfp, struct pmemfile_stats *stats);

int pmemfile_truncate(PMEMfilepool *, const char *path, pmemfile_off_t length);
int pmemfile_ftruncate(PMEMfilepool *, PMEMfile *file, pmemfile_off_t length);

/*
 * Not in POSIX:
 * The fallocate glibc routine/syscall is Linux/glibc specific,
 * The pmemfile_fallocate supports allocating, and punching holes.
 */
int pmemfile_fallocate(PMEMfilepool *, PMEMfile *file, int mode,
		pmemfile_off_t offset, pmemfile_off_t length);

int pmemfile_posix_fallocate(PMEMfilepool *pfp, PMEMfile *file,
		pmemfile_off_t offset, pmemfile_off_t length);

char *pmemfile_get_dir_path(PMEMfilepool *pfp, PMEMfile *dir, char *buf,
		size_t size);

#define PMEMFILE_OPEN_PARENT_STOP_AT_ROOT (1<<0)
#define PMEMFILE_OPEN_PARENT_SYMLINK_FOLLOW (1<<1)

#define PMEMFILE_OPEN_PARENT_USE_FACCESS (0<<2)
#define PMEMFILE_OPEN_PARENT_USE_EACCESS (1<<2)
#define PMEMFILE_OPEN_PARENT_USE_RACCESS (2<<2)
#define PMEMFILE_OPEN_PARENT_ACCESS_MASK (3<<2)

PMEMfile *pmemfile_open_parent(PMEMfilepool *pfp, PMEMfile *at,
		char *path, size_t path_size, int flags);

const char *pmemfile_errormsg(void);

int pmemfile_pool_resume(PMEMfilepool *pfp, const char *pathname);
int pmemfile_pool_suspend(PMEMfilepool *pfp);

#include "libpmemfile-posix-stubs.h"

#ifdef FAULT_INJECTION
enum pf_allocation_type { PF_MALLOC, PF_CALLOC, PF_REALLOC };
void pmemfile_inject_fault_at(enum pf_allocation_type type, int nth,
		const char *at);
#endif

#ifdef __cplusplus
}
#endif
#endif	/* libpmemfile-posix.h */
