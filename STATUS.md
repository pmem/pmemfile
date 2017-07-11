PMEMFILE SUPPORT FOR SYSCALLS
=============================


# Not supported #

- SYS_chroot
- SYS_setxattr
- SYS_lsetxattr
- SYS_fsetxattr
- SYS_listxattr
- SYS_llistxattr
- SYS_flistxattr
- SYS_removexattr
- SYS_lremovexattr
- SYS_fremovexattr
- SYS_execve
- SYS_execveat
- SYS_readahead
- SYS_fork
- SYS_vfork
- SYS_name_to_handle_at


# Not supported flags #

- SYS_renameat2:
	- RENAME_WHITEOUT
- SYS_openat:
	- O_ASYNC - unsupported,
	- O_CLOEXEC, O_DIRECT, O_DSYNC, O_NOCTTY, O_SYNC - always enabled,
	- O_NONBLOCK - ignored
- SYS_open - see openat
- SYS_fallocate:
	- FALLOC_FL_COLLAPSE_RANGE,
	- FALLOC_FL_ZERO_RANGE,
	- FALLOC_FL_INSERT_RANGE
- SYS_fcntl
	- F_GETLK, F_SETLK, F_SETLKW, F_SETOWN, F_GETOWN, F_SETSIG, F_GETSIG,
	  F_SETOWN_EX, F_GETOWN_EX, F_OFD_GETLK, F_OFD_SETLK, F_OFD_SETLKW,
	  F_SETLEASE, F_GETLEASE, F_NOTIFY, F_ADD_SEALS, F_GET_SEALS
	- F_SETFD - not possible to clear FD_CLOEXEC,
- SyS_clone - supported only for flags set by pthread_create() = CLONE_VM |
	CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM |
	CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID


# Not supported _YET_ #

- SYS_dup
- SYS_dup2
- SYS_dup3
- SYS_copy_file_range
- SYS_flock
- SYS_sendfile
- SYS_splice
- SYS_mmap (also affects SYS_mprotect, SYS_munmap, SYS_mremap, SYS_msync)


# Supported - does nothing #

- SYS_fadvise64
- SYS_fdatasync
- SYS_fsync
- SYS_syncfs
- SYS_getxattr - returns no attributes
- SYS_lgetxattr - returns no attributes
- SYS_fgetxattr - returns no attributes


# Supported #

- SYS_access
- SYS_chdir
- SYS_chmod
- SYS_chown
- SYS_close
- SYS_faccessat
- SYS_fchdir
- SYS_fchmodat
- SYS_fchmod
- SYS_fchownat
- SYS_fchown
- SYS_fstat
- SYS_ftruncate
- SYS_getcwd
- SYS_getdents64
- SYS_getdents
- SYS_lchown
- SYS_linkat
- SYS_link
- SYS_lseek
- SYS_lstat
- SYS_mkdirat
- SYS_mkdir
- SYS_newfstatat
- SYS_preadv
- SYS_preadv2
- SYS_pread64
- SYS_pwritev
- SYS_pwritev2
- SYS_pwrite64
- SYS_read
- SYS_readlinkat
- SYS_readlink
- SYS_readv
- SYS_renameat
- SYS_rename
- SYS_rmdir
- SYS_stat
- SYS_symlinkat
- SYS_symlink
- SYS_truncate
- SYS_unlinkat
- SYS_unlink
- SYS_write
- SYS_writev
- SYS_utime
- SYS_utimes
- SYS_utimensat
- SYS_futimesat
