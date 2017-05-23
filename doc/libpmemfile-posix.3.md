---
layout: "manual"
Content-Style: 'text/css'
title: libpmemfile-posix(3)
date: "pmemfile-posix API version 0.1.0
---[comment]: <> (Copyright 2017, Intel Corporation)

[comment]: <> (Redistribution and use in source and binary forms, with or without)
[comment]: <> (modification, are permitted provided that the following conditions)
[comment]: <> (are met:)
[comment]: <> (    * Redistributions of source code must retain the above copyright)
[comment]: <> (      notice, this list of conditions and the following disclaimer.)
[comment]: <> (    * Redistributions in binary form must reproduce the above copyright)
[comment]: <> (      notice, this list of conditions and the following disclaimer in)
[comment]: <> (      the documentation and/or other materials provided with the)
[comment]: <> (      distribution.)
[comment]: <> (    * Neither the name of the copyright holder nor the names of its)
[comment]: <> (      contributors may be used to endorse or promote products derived)
[comment]: <> (      from this software without specific prior written permission.)

[comment]: <> (THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS)
[comment]: <> ("AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT)
[comment]: <> (LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR)
[comment]: <> (A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT)
[comment]: <> (OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,)
[comment]: <> (SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT)
[comment]: <> (LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,)
[comment]: <> (DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY)
[comment]: <> (THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT)
[comment]: <> ((INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE)
[comment]: <> (OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.)

[comment]: <> (libpmemfile-posix.3 -- man page for libpmemfile-posix)

[NAME](#name)<br />
[SYNOPSIS](#synopsis)<br />
[DESCRIPTION](#description)<br />
[ERROR HANDLING](#error-handling)<br />
[SUPPORTED INTERFACES](#supported interfaces)<br />
[ACCESS MANAGEMENT](#access-management)<br />
[FILE CREATION AND DELETION](#file-creation-and-deletion)<br />
[FILE NAMING](#file-naming)<br />
[FILE I/O](#file-i/o)<br />
[OFFSET MANAGEMENT](#offset-management)<br />
[FILE STATUS](#file-status)<br />

# NAME #
**libpmemfile-posix**-- user space persistent memory file system

# SYNOPSIS #
```c
#include <libpmemfile-posix.h>
cc -std=gnu99 .. -lpmemfile-posix -lpmemobj
```

# DESCRIPTION #
**libpmemfile-posix** provides a file system for persistent memory which runs in user-space.

The library is intended for applications that want to create and manage a file system on persistent memory without the kernel overhead. The interfaces in this library are modeled after the corresponding POSIX interfaces for file management. Using interfaces modeled on POSIX allows for easier transition for application developers.

# ERROR HANDLING #
## Return Values ##

For functions returning 'int':
```
 0 success
-1 failure
```
Otherwise as defined by the function return type.

## Errors ##
```
Errno is set in all failure cases.
Exceptions to a standard Errno values are noted.
```

# SUPPORTED INTERFACES #

## Access Management ##
```c
int pmemfile_access(PMEMfilepool *pfp, const char *path, mode_t mode);
int pmemfile_faccessat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int mode, int flags);

int pmemfile_chdir(PMEMfilepool *, const char *path);
int pmemfile_fchdir(PMEMfilepool *, PMEMfile *dir);

int pmemfile_chmod(PMEMfilepool *pfp, const char *path, mode_t mode);
int pmemfile_fchmod(PMEMfilepool *pfp, PMEMfile *file, mode_t mode);
int pmemfile_fchmodat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		mode_t mode, int flags);

int pmemfile_chown(PMEMfilepool *pfp, const char *pathname, uid_t owner,
		gid_t group);
int pmemfile_fchown(PMEMfilepool *pfp, PMEMfile *file, uid_t owner, gid_t group);
int pmemfile_lchown(PMEMfilepool *pfp, const char *pathname, uid_t owner,
		gid_t group);
int pmemfile_fchownat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		uid_t owner, gid_t group, int flags);
```

## File Creation and Deletion ##
```c
PMEMfile *pmemfile_open(PMEMfilepool *pfp, const char *pathname, int flags,
		...);
PMEMfile *pmemfile_openat(PMEMfilepool *pfp, PMEMfile *dir,
		const char *pathname, int flags, ...);
PMEMfile *pmemfile_creat(PMEMfilepool *pfp, const char *pathname,
		mode_t mode);

void pmemfile_close(PMEMfilepool *pfp, PMEMfile *file);

int pmemfile_link(PMEMfilepool *pfp, const char *oldpath,
		const char *newpath);
int pmemfile_linkat(PMEMfilepool *pfp, PMEMfile *olddir, const char *oldpath,
                PMEMfile *newdir, const char *newpath, int flags);
int pmemfile_unlink(PMEMfilepool *pfp, const char *pathname);
int pmemfile_unlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
                int flags);
```

### open/creat Flags Support ###
```
O_ASYNC
	This flag is ignored.

O_CLOEXEC
	This flag is always set.

O_DIRECT
	This flag is ignored.

O_NOATIME
	Is supported.

O_NONBLOCK or O_NDELAY
	These flags are ignored.

O_NOCTTY
	Not supported.

O_PATH
	This flag will behave the same as is documented in the open()
	man-page. However, the use of the file descriptor returned as a
	result  of  this flag cannot be used to pass to another process
	via a UNIX domain socket. Sockets are not supported.

O_SYNC, O_DSYNC
	These flags are ignored. Writes to persistent memory are always synchronous.
```

## File Naming ##
libpmemfile-posix does not support renaming files between pmemfile file systems.

```c
int pmemfile_rename(PMEMfilepool *, const char *old_path, const char *new_path);
int pmemfile_renameat(PMEMfilepool *, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path);
int pmemfile_renameat2(PMEMfilepool *, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path, unsigned flags);
```

## File I/O ##
```c
ssize_t pmemfile_read(PMEMfilepool *pfp, PMEMfile *file, void *buf,
                size_t count);
ssize_t pmemfile_pread(PMEMfilepool *pfp, PMEMfile *file, void *buf,
                size_t count, off_t offset);
ssize_t pmemfile_readv(PMEMfilepool *pfp, PMEMfile *file, const struct iovec *iov,
                int iovcnt);
ssize_t pmemfile_preadv(PMEMfilepool *pfp, PMEMfile *file, const struct iovec *iov,
                int iovcnt, off_t offset);

ssize_t pmemfile_write(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
                size_t count);
ssize_t pmemfile_pwrite(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
                size_t count, off_t offset);
ssize_t pmemfile_writev(PMEMfilepool *pfp, PMEMfile *file, const struct iovec *iov,
                int iovcnt);
ssize_t pmemfile_pwritev(PMEMfilepool *pfp, PMEMfile *file, const struct iovec *iov,
                int iovcnt, off_t offset);
```

## Offset Management ##
```c
off_t pmemfile_lseek(PMEMfilepool *pfp, PMEMfile *file, off_t offset,
                int whence);

int pmemfile_truncate(PMEMfilepool *pfp, const char *path, off_t length);
int pmemfile_ftruncate(PMEMfilepool *pfp, PMEMfile *file, off_t length);
```

## File Status
```c
int pmemfile_stat(PMEMfilepool *, const char *path, struct stat *buf);
int pmemfile_lstat(PMEMfilepool *, const char *path, struct stat *buf);
int pmemfile_fstat(PMEMfilepool *, PMEMfile *file, struct stat *buf);
int pmemfile_fstatat(PMEMfilepool *, PMEMfile *dir, const char *path,
		struct stat *buf, int flags);
```

## Directory Management
```c
int pmemfile_mkdir(PMEMfilepool *, const char *path, mode_t mode);
int pmemfile_mkdirat(PMEMfilepool *, PMEMfile *dir, const char *path,
                mode_t mode);
int pmemfile_rmdir(PMEMfilepool *, const char *path);

int pmemfile_getdents(PMEMfilepool *, PMEMfile *file,
                struct linux_dirent *dirp, unsigned count);
int pmemfile_getdents64(PMEMfilepool *, PMEMfile *file,
                struct linux_dirent64 *dirp, unsigned count);

char *pmemfile_getcwd(PMEMfilepool *, char *buf, size_t size);
```

## File Descriptor Management ##
```c
int pmemfile_fcntl(PMEMfilepool *, PMEMfile *file, int cmd, ...);
```

**File Descriptor Flags**
```
F_SETFD
	O_CLOEXEC
	Is supported.
```

**File Status Flags**
```
F_SETFL FLAGS
	O_ASYNC
		This flag is ignored.

	O_DIRECT
		Is ignored.

	O_NONBLOCK
		Is ignored.

	O_APPEND
		Is supported.
```

**Locking Flags**
```
F_GETLK
	Is supported.

F_SETLK, F_SETLKW
	Not supported.

MANDATORY LOCKS
	Not supported.
```

**Signal Flags**
```
F_SETOWN, F_GETOWN_EX, F_SETOWN_EX
	Not supported.

F_GETSIG, F_SETSIG
	Not supported.
```

**Lease Flags**
```
F_SETLEASE, F_GETLEASE
	Not supported.
```

**Notification Flags**
```
F_NOTIFY
	Not supported.
```

In all cases of unsupported flags Errno will be set to *EINVAL*. Otherwise set as defined in the fcntl(2) manpage.

## Symbolic Link Management ##
```c
ssize_t pmemfile_readlink(PMEMfilepool *pfp, const char *path,
                char *buf, size_t buf_len);
ssize_t pmemfile_readlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
                char *buf, size_t bufsiz);
int pmemfile_symlink(PMEMfilepool *pfp, const char *path1, const char *path2);
int pmemfile_symlinkat(PMEMfilepool *pfp, const char *path1,
                PMEMfile *at, const char *path2);
```

## Timestamp Management ##
```c
int pmemfile_utime(PMEMfilepool *pfp, const char *filename,
                const struct utimbuf *times);
int pmemfile_utimes(PMEMfilepool *pfp, const char *filename,
                const struct timeval times[2]);
int pmemfile_futimes(PMEMfilepool *pfp, PMEMfile *file,
                const struct timeval tv[2]);
int pmemfile_lutimes(PMEMfilepool *pfp, const char *filename,
                const struct timeval tv[2]);
int pmemfile_utimensat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
                const struct timespec times[2], int flags);
int pmemfile_futimens(PMEMfilepool *pfp, PMEMfile *file,
                const struct timespec times[2]);
mode_t pmemfile_umask(PMEMfilepool *pfp, mode_t mask);

```
