---
layout: manual
Content-Style: 'text/css'
title: pmemfile(1)
header: PMEMFile
date: pmemfile API version 0.1.0
...

[comment]: <> (Copyright 2017, Intel Corporation)

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

[comment]: <> (pmemfile.1 -- man page for pmemfile)

[NAME](#name)<br />
[SYNOPSIS](#synopsis)<br />
[DESCRIPTION](#description)<br />
[EXCEPTIONS TO POSIX SUPPORT](#exceptions-to-posix-support)<br />

# NAME #
**Pmemfile** - persistent memory file system.

# SYNOPSIS #

```c
export LD_PRELOAD=libpmemfile.so
```
**or**
```c
LD_PRELOAD=libpmemfile.so <invoke application via cli>
```

# DESCRIPTION #
**Pmemfile** is a user space transactional persistent memory file system which is automatically enabled for an application when the **libpmemfile** library is pre-loaded into the application environment as shown in the **SYNOPSIS** section.  **Pmemfile** provides applications nearly transparent direct access to persistent memory resident files. **Pmemfile**, when enabled as described, provides intercept points for the standard Linux glibc interfaces. There are some exceptions to the standard glibc behavior with **Pmemfile** and these are described in this man page.

# EXCEPTIONS TO POSIX SUPPORT #
This section outlines the exceptions to the specified file operations.

# MULTI-PROCESS SUPPORT #
The NVM library **libpmemobj** does not provide multi-process support. **libpmemfile** is built upon **libpmemobj** so therefore **Pmemfile**  does not provide multi-process support.

```c
int clone(int (*fn)(void *), void *child_stack,
	int flags, void *arg, ...
	/* pid_t *ptid, struct user_desc *tls, pid_t *ctid */);
pid_t fork(void);
pid_t vfork(void);
```

## clone ##
Is supported with the following flag exceptions:

```
CLONE_IO
	Not supported.

CLONE_NEWNET
	Not supported.

CLONE_NEWNS
	Not supported.

CLONE_NEWUTS
	Not supported.

CLONE_NEWPID
	Not supported.

CLONE_PARENT
	Not supported.

CLONE_PID
	Not supported. Obsolete.

CLONE_VFORK
	Not supported.

CLONE_VM
	Is supported. However, even if a thread is created with shared
	virtual memory the child will not be able to access, create or
	modify any pmem-resident files.
```

_RETURN VALUE_
```
-1 in the case of the unsupported flags. Otherwise, as defined in the
clone(2) manpage.
```

_ERRORS_
```
EINVAL in the case of unsupported flags.  Otherwise, as defined in
the clone(2) manpage.
```


## fork ##

A child created with fork() will not be allowed to access any of the existing pmem-resident files nor create new ones.

_RETURN VALUE_
```
As defined in the fork() manpage.
```

_ERRORS_
```
As defined in the fork() manpage.
```


## vfork ##
Not supported.

_RETURN VALUE_
```
-1
```

_ERRORS_
```
ENOSYS Not supported on this platform.
```


# I/O EVENT NOTIFICATION #
I/O event notification is not supported, which includes the following system calls:

```c
int epoll_ctl(int epfd, intop, intfd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events,
		int maxevents, int timeout);
int epoll_pwait(int epfd, struct epoll_event *events,
		int maxevents, int timeout, const sigset_t *sigmask);
int fanotify_mark(int fanotify_fd, unsigned int flags,
		int dirfd, const char * pathname);
int inotify_add_watch(int fd, const char *pathname, uint32_t mask);
int inotify_rm_watch(int fd, int wd);
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int ppoll(struct pollfd *fds, nfds_t nfds,
		const struct timespec *timeout_ts, const sigset_t *sigmask);
int select(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *exceptfds, struct timeval *timeout);
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set * exceptfds, const struct timespec * timeout,
		const sigset_t * sigmask);
```

_RETURN VALUE_
```
-1
```

_ERRORS_
```
EBADF in all cases.
```


# PROGRAM EXECUTION #
Execution of a program is not supported when the executable file is a pmem-resident file.

```c
int execve(const char *filename, char * const argv[], char *const envp[]);
```

_RETURN VALUE_
```
-1 on error. On success, execve(2) does not return.
```

_ERRORS_
```
EACCESS Execute permission is denied for pmem resident files. Otherwise
as defined in the execve() manpage.
```


# EXTENEDED ATTRIBUTES #
**Pmemfile** does not support extended attributes. The following system calls are not
supported.

```c
ssize_t lgetxattr(const char *path, const char *name, void  *value,
		size_t size);
ssize_t fgetxattr(intfd, const char *name, void *value, size_t size);
ssize_t listxattr(const char *path, char *list, size_t size);
ssize_t llistxattr(const char *path, char *list, size_t size);
ssize_t flistxattr(int fd, char *list, size_t size);
int setxattr(const char *path, const char *name, const void *value,
		size_t size, int flags);
int lsetxattr(const char *path, const char *name, const void *value,
		size_t size, int flags);
int fsetxattr(int fd, const char *name, const void *value, size_t size,
		int flags);
```

_RETURN VALUE_
```
-1
```

_ERRORS_
```
ENOTSUP Not supported.
```


# FLUSHING #
All writes are synchronous with persistent memory therefore **Pmemfile** supports only synchronous writes. All calls to to any of the functions below will return success except in the case of a bad file descriptor.

```c
void sync(void);
int sync_file_range(int fd, off64_t offset, off64_t nbytes,
		unsigned int flags);
int syncfs(int fd);
int fsync(int fd);
int fdatasync(int fd);
```

_RETURN VALUE_
```
0 or -1
```

_ERRORS_
```
As per manpage.
```


# SPECIAL FILES #
The system calls that manage block or character special files are not supported when pathname points to a pmemfile-backed file system.

```c
int mknod(const char *pathname, mode_t mode, dev_t dev);
int mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev);
```

_RETURN VALUE_
```
-1
```

_ERRORS_
```
EACCESS No write permission.
```


# MEMORY MAPPING #
Memory mapping is not supported.

```c
void *mmap(void *addr, size_t length, int prot, int flags,
		   int fd, off_t offset);
```

_RETURN VALUE_
```
-1
```

_ERRORS_
```
ENODEV The underlying file system of the specified file does not support
memory mapping.
```


# FILE MANAGEMENT #
The open/at() and creat() system calls are supported. Noted in this section are the flags and mode bits that are not supported or have modified behavior.

```c
int open(const char *pathname, int flags);
int open(const char *pathname, int flags, mode_t mode);
int creat(const char* pathname, mode_t mode);
int openat(int dirfd, const char * pathname, int flags);
int openat(int dirfd, const char * pathname, int flags, mode_t mode);
```


## Flags and Mode Bits ##
```
O_ASYNC
	Not supported.

O_CLOEXEC
	This flag is always set

O_DIRECT
	This flag is ignored.

O_NONBLOCK or O_NDELAY
	These flags are ignored.

O_NOCTTY
	Not supported.

O_PATH
	This flag will behave the same as is documented in the open(2) manpage.
	However, the use of the file descriptor returned as a result of this flag
	cannot be used to pass to another process via a UNIX domain socket. **Pmemfile**
	does not provide UNIX socket support.

O_SYNC, O_DSYNC
	These flags are ignored.
```

All mode flags are supported.

_RETURN VALUE_
```
-1 in the case of the unsupported flags. Otherwise as defined in the open(2) manpage.
```

_ERRORS_
```
EINVAL in the case of unsupported flags. Otherwise as defined in the open(2) manpage.
```


# FILE LOCKING #
File locking is not supported.

```c
int flock(int fd, int operation);
```

_RETURN VALUE_
```
-1
```

_ERRORS_
```
EINVAL Operation is invalid.
```


# FILE DESCRIPTOR MANAGEMENT #
Is supported with the following exceptions.

```c
int fcntl(int fd, int cmd, ... /* arg */ );
```


## Duplicating File Descriptors ##
```
F_DUPFD_CLOEXEC
	Pmemfile always sets this flag for every file. Setting it is a no-op.
```

## File Descriptor Flags ##
```
F_SETFD
	Is supported. Currently, the only flag supported is O_CLOEXEC as it is always
	set.
```

## File Status ##
```
F_SETFL
	Is supported as follows:
	O_ASYNC
		Never

	O_DIRECT
		Always

	O_NONBLOCK
		Ignored
```

_RETURN VALUE_
```
0 for cases noted above. Otherwise as defined in the fcntl(2) manpage.
```

_ERRORS_
```
As defined in the fcntl(2) manpage.
```

## Locking ##
```
F_SETLK, F_SETLKW, F_GETLK
	Not supported.

F_SETOWN, F_GETOWN_EX, F_SETOWN_EX
	Not supported.

F_GETSIG, F_SETSIG
	Not supported.

F_SETLEASE, F_GETLEASE
	Not supported.

F_NOTIFY
	Not supported.
```

**Mandatory Locks**

Are Not Supported

_RETURN VALUE_
```
-1 for all flags not supported. Otherwise as defined in the fcntl() manpage.
```

_ERRORS_
```
**EINVAL** for flags noted as not supported. Otherwise as defined in the fcntl() manpage.
```

# DUPLICATION OF FILE DESCRIPTORS #
Duplication of file descriptors is supported.

```c
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int dup3(int oldfd, int newfd, int flags);
```

dup3() Allows the user to force the setting of the O\_CLOEXEC flag. This flag is always set with **Pmemfile** so setting of this flag with dup3() is a no-op.

_RETURN VALUE_
```
As specified in the manpage.
```

_ERRORS_
```
As specified in the manpage.
```

# READAHEAD #
Is not supported. **Pmemfile** does not support caching as it always operates in direct access mode.

```c
ssize_t readahead(int fd, off64_t offset, size_t count);
```

_RETURN VALUE_
```
-1
```

_ERRORS_
```
EINVAL fd does not refer to a file type to which readahead can be applied.
```


# RENAME #
All renameX() functions are supported as long as the old and new files are within the same **Pmemfile** pool.  For:
```c
int renameat2(int olddirfd, const char *oldpath,
	      int newdirfd, const char *newpath, unsigned int flags);
```
The following flag are not supported:
```
RENAME_WHITEOUT
```

_RETURN VALUE_
```
-1 For the error case specified above. Otherwise as defined in the respective manpage.
```

_ERRORS_
```
EINVAL for renameat2() flag RENAME_WITEHOUT. Otherwise,
As defined in the respective manpage.
```


# ASYNCHRONOUS I/O #
**Pmemfile** does not support POSIX asynchronous I/O.

# MISCELLANEOUS OPERATIONS #
```c
int chroot(const char *path);
int ioctl(int d, int request,...);
int pivot_root(const char *new_root, const char* put_old);
int swapon(const char *path, int swapflags);
int swapoff(const char *path);
int fadvisa64(int fd, off_t offset, off_t len, int advice);
```

Are not supported.

_RETURN VALUE_
```
-1
```

_ERRORS_
```
chroot()
	EPERM Insufficient privilege.

ioctl()
	EFAULT Requesting an inaccessible memory area.

pivot\_root()
	EPERM Insufficient privilege.

swapon(), swapoff()
	EINVAL Invalid Path

fadvise64()
	EBADF
```
