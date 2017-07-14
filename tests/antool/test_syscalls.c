/*
 * Copyright 2017, Intel Corporation
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
 * test_syscalls.c -- functional tests for Analyzing Tool
 */

#define _GNU_SOURCE
#define N_WORKERS 10

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/xattr.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/select.h>
#include <sys/swap.h>
#include <sys/sendfile.h>

#include <linux/futex.h>
#include <linux/fs.h>
#include <linux/falloc.h>

#include <syscall.h>

#define F_ADD_SEALS		1033
#define F_GET_SEALS		1034
#define ANY_STR			"any-string"

#define PATTERN_START		((int)0x12345678)
#define PATTERN_END		((int)0x87654321)
#define BUF_SIZE		0x100

#define MARK_START()		close(PATTERN_START)
#define MARK_END()		close(PATTERN_END)

#define FILE_EXIST		"/etc/fstab"
#define FILE_CREATE		"/tmp/tmp-antool"

#define NON_EXIST_PATH_1	"111_non_exist"
#define NON_EXIST_PATH_2	"222_non_exist"

/* used to test all flags set */
#define FLAGS_SET		0x0FFFFFFFFFFFFFFF

#define STRING_10		"1234567890"
#define STRING_30 		STRING_10 STRING_10 STRING_10
#define STRING_60 		STRING_30 STRING_30
#define STRING_120		STRING_60 STRING_60
#define STRING_420		STRING_120 STRING_120 STRING_120 STRING_60
#define STRING_840		STRING_420 STRING_420
#define STRING_1260		STRING_420 STRING_420 STRING_420

#define STRING_126_1		"START_111_"STRING_10"_111_END"
#define STRING_126_2		"START_222_"STRING_10"_222_END"
#define STRING_126_3		"START_333_"STRING_10"_333_END"

#define STRING_382_1		"START_111_"STRING_120"_111_END"
#define STRING_382_2		"START_222_"STRING_120"_222_END"
#define STRING_382_3		"START_333_"STRING_120"_333_END"

#define STRING_765_1		"START_111_"STRING_420"_111_END"
#define STRING_765_2		"START_222_"STRING_420"_222_END"
#define STRING_765_3		"START_333_"STRING_420"_333_END"

#define STRING_1148_1		"START_111_"STRING_840"_111_END"
#define STRING_1148_2		"START_222_"STRING_840"_222_END"
#define STRING_1148_3		"START_333_"STRING_840"_333_END"

#define STRING_1531_1		"START_111_"STRING_1260"_111_END"
#define STRING_1531_2		"START_222_"STRING_1260"_222_END"
#define STRING_1531_3		"START_333_"STRING_1260"_333_END"

static char *strings[5][3] = {
	{
		STRING_126_1,
		STRING_126_2,
		STRING_126_3,
	},
	{
		STRING_382_1,
		STRING_382_2,
		STRING_382_3,
	},
	{
		STRING_765_1,
		STRING_765_2,
		STRING_765_3,
	},
	{
		STRING_1148_1,
		STRING_1148_2,
		STRING_1148_3,
	},
	{
		STRING_1531_1,
		STRING_1531_2,
		STRING_1531_3,
	},
};

/*
 * test_basic_syscalls -- test basic syscalls
 */
static void
test_basic_syscalls(void)
{
	char buffer[BUF_SIZE];
	struct utsname name;
	struct stat buf;
	int fd;

	/* PART #1 - real arguments */

	fd = open(FILE_EXIST, O_RDONLY);
	close(fd);

	fd = open(FILE_CREATE, O_RDWR | O_CREAT, 0666);
	write(fd, buffer, BUF_SIZE);
	lseek(fd, 0, SEEK_SET);
	read(fd, buffer, BUF_SIZE);
	fstat(fd, &buf);
	close(fd);
	unlink(FILE_CREATE);

	execve(FILE_CREATE, (char * const *)0x123456, (char * const *)0x654321);

	stat(FILE_EXIST, &buf);
	lstat(FILE_EXIST, &buf);

	uname(&name);

	syscall(SYS_getpid);
	syscall(SYS_gettid);

	/* PART #2 - test arguments */
	write(0x101, buffer, 1);
	read(0x102, buffer, 2);
	lseek(0x103, 3, SEEK_END);
	fstat(0x104, &buf);
	syscall(SYS_futex, 1, FUTEX_WAKE_OP, 3, 4, 5, FLAGS_SET);
}

/*
 * test_other_syscalls -- test more syscalls
 */
static void
test_other_syscalls(void)
{
	char buf[BUF_SIZE];

	chroot(NON_EXIST_PATH_1);
	syscall(SYS_fcntl, 0x104, FLAGS_SET, FLAGS_SET, 0x105, 0x106, 0x107);
	flock(0x108, 0x109);

	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);
	setsockopt(0x101, 0x102, 0x103, (void *)0x104, (socklen_t)0x105);
	getsockopt(0x106, 0x107, 0x108, (void *)0x109, (socklen_t *)0x110);
	getsockname(0x101, &addr, &addrlen);

	inotify_add_watch(0x104, NON_EXIST_PATH_1, 0x105);
	inotify_rm_watch(0x106, 0x107);

	syscall(SYS_io_cancel, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106);
	syscall(SYS_io_destroy, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107);
	syscall(SYS_io_getevents, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108);
	syscall(SYS_io_setup, 0x104, 0x105, 0x106, 0x107, 0x108, 0x109);
	syscall(SYS_io_submit, 0x105, 0x106, 0x107, 0x108, 0x109, 0x110);
	syscall(SYS_ioctl, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106);

	mknod(FILE_EXIST, 0x101, 0x102);
	mknodat(0x103, FILE_EXIST, 0x104, 0x105);

	mmap((void *)0x101, 0x102, 0x103, 0xFFFF, 0x105, 0x106);
	munmap((void *)0x102, 0x103);

	struct timeval time1;
	struct timespec time2;
	memset(&time1, 0, sizeof(time1));
	memset(&time2, 0, sizeof(time2));

	select(0, (fd_set *)0x104, (fd_set *)0x105, (fd_set *)0x106, &time1);
	pselect(0, (fd_set *)0x105, (fd_set *)0x106, (fd_set *)0x107, &time2,
		(const sigset_t *)0x108);

	swapon(NON_EXIST_PATH_1, 0x101);
	swapoff(NON_EXIST_PATH_2);

	syscall(SYS_poll, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107);

	mount(NON_EXIST_PATH_1, NON_EXIST_PATH_2, "ext3", 0x101, (void *)0x102);
	umount(NON_EXIST_PATH_1);
	umount2(NON_EXIST_PATH_2, 0x123);

	setxattr(NON_EXIST_PATH_1, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	lsetxattr(NON_EXIST_PATH_2, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	fsetxattr(0x107, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);

	getxattr(NON_EXIST_PATH_1, ANY_STR, buf, BUF_SIZE);
	lgetxattr(NON_EXIST_PATH_2, ANY_STR, buf, BUF_SIZE);
	fgetxattr(0x105, ANY_STR, buf, BUF_SIZE);

	listxattr(NON_EXIST_PATH_1, ANY_STR, 0x101);
	llistxattr(NON_EXIST_PATH_2, ANY_STR, 0x102);
	flistxattr(0x103, ANY_STR, 0x104);

	removexattr(NON_EXIST_PATH_1, ANY_STR);
	lremovexattr(NON_EXIST_PATH_2, ANY_STR);
	fremovexattr(0x101, ANY_STR);

	syscall(SYS_ppoll, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106);
	epoll_ctl(0x101, 0x102, 0x103, (struct epoll_event *)0x104);
	epoll_wait(0x102, (struct epoll_event *)0x103, 0x104, 0x105);
	epoll_pwait(0x103, (struct epoll_event *)0x104, 0x105, 0x106,
			(const sigset_t *)0x107);

	syscall(SYS_open, NON_EXIST_PATH_2, FLAGS_SET, FLAGS_SET, FLAGS_SET,
		FLAGS_SET, FLAGS_SET);
	syscall(SYS_clone, FLAGS_SET, FLAGS_SET, FLAGS_SET, FLAGS_SET,
		FLAGS_SET, FLAGS_SET);
}

/*
 * test_strings -- test syscalls with string arguments
 */
static void
test_strings(char *string[3])
{
	/* string args: 1 (open) */
	syscall(SYS_open, string[0], 0x102, 0x103, 0x104, 0x105, 0x106);

	/* string args: 2 (openat) */
	syscall(SYS_openat, 0x101, string[1], 0x103, 0x104, 0x105, 0x106);

	/* string args: 1 2 (rename) */
	rename(string[0], string[1]);

	/* string args: 1 2 (llistxattr) */
	llistxattr(string[1], string[0], 0x103);

	/* string args: 1 3 (symlinkat) */
	syscall(SYS_symlinkat, string[0], 0x102, string[1]);

	/* string args: 2 4 (renameat) */
	syscall(SYS_renameat, 0x101, string[0], 0x103, string[1]);

	/* string args: 1 2 3 (mount) */
	mount(string[0], string[1], string[2], 0x101, (void *)0x102);

	/* string args: 1 2 3 (request_key) */
	syscall(SYS_request_key, string[0], string[1], string[2], 0x104);

	/* string args: 3 (init_module) */
	syscall(SYS_init_module, 0x101, 0x102, string[0]);

	/* string args: 4 (kexec_file_load) */
	syscall(SYS_kexec_file_load, 0x101, 0x102, 0x103, string[1], 0x105);

	/* string args: 5 (fanotify_mark) */
	syscall(SYS_fanotify_mark, 0x101, 0x102, 0x103, 0x104, string[0]);

}

/* testing signals */
static int Signalled;

/*
 * sig_user_handler -- SIGALARM signal handler.
 */
static void
sig_user_handler(int sig, siginfo_t *si, void *unused)
{
	(void) sig;
	(void) si;
	(void) unused;

	Signalled = 1;
}

/*
 * test_signal -- test the syscall 'sigaction'
 */
static void
test_signal(void)
{
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sig_user_handler;
	sa.sa_flags = (int)(SA_RESTART | SA_RESETHAND);
	(void) sigaction(SIGUSR1, &sa, NULL);

	Signalled = 0;

	raise(SIGUSR1);

	while (Signalled == 0)
		sleep(1);
}

/*
 * test_0 -- test basic syscalls
 */
static void
test_0(char *a, char *b, char *c)
{
	MARK_START();
	test_basic_syscalls();
	MARK_END();
}

/*
 * test_1 -- test basic syscalls with fork()
 */
static void
test_1(char *a, char *b, char *c)
{
	syscall(SYS_fork);
	test_0(a, b, c);
}

/*
 * test_2 -- test other syscalls
 */
static void
test_2(char *a, char *b, char *c)
{
	MARK_START();
	test_other_syscalls();
	MARK_END();
}

/*
 * test_3 -- test other syscalls with fork()
 */
static void
test_3(char *a, char *b, char *c)
{
	syscall(SYS_fork);
	test_2(a, b, c);
}

/*
 * test_4 -- test vfork()
 */
static void
test_4(char *a, char *b, char *c)
{
	MARK_START();

	/*
	 * test if other syscalls are correctly detected,
	 * when vfork is present
	 */
	syscall(SYS_open, NON_EXIST_PATH_1, 0x101, 0x102, 0x103, 0x104, 0x105);
	syscall(SYS_close, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106);
	if (vfork() == 0) { /* vfork - handle child */
		execve(NON_EXIST_PATH_1, (char * const *)0x123456,
					(char * const *)0x654321);
		_exit(1);
	}

	/*
	 * test if other syscalls are correctly detected,
	 * when vfork is present
	 */
	syscall(SYS_open, NON_EXIST_PATH_2, 0x102, 0x103, 0x104, 0x105, 0x106);
	syscall(SYS_close, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107);
	MARK_END();
}


/*
 * test_5 -- test basic syscalls after double fork()
 */
static void
test_5(char *a, char *b, char *c)
{
	syscall(SYS_fork);
	test_1(a, b, c);
}

/*
 * test_6 -- test other syscalls after double fork()
 */
static void
test_6(char *a, char *b, char *c)
{
	syscall(SYS_fork);
	test_3(a, b, c);
}

/*
 * test_7 -- test the syscall 'signal'
 */
static void
test_7(char *a, char *b, char *c)
{
	MARK_START();
	test_signal();
	MARK_END();
}

/*
 * test_8 -- test syscalls with string arguments of length < 126
 */
static void
test_8(char *a, char *b, char *c)
{
	MARK_START();
	test_strings(strings[0]);
	MARK_END();
}

/*
 * test_9 -- test syscalls with string arguments of length < 382
 */
static void
test_9(char *a, char *b, char *c)
{
	MARK_START();
	test_strings(strings[1]);
	MARK_END();
}

/*
 * test_10 -- test syscalls with string arguments of length < 765
 */
static void
test_10(char *a, char *b, char *c)
{
	MARK_START();
	test_strings(strings[2]);
	MARK_END();
}

/*
 * test_11 -- test syscalls with string arguments of length < 1148
 */
static void
test_11(char *a, char *b, char *c)
{
	MARK_START();
	test_strings(strings[3]);
	MARK_END();
}

/*
 * test_12 -- test syscalls with string arguments of length < 1531
 */
static void
test_12(char *a, char *b, char *c)
{
	MARK_START();
	test_strings(strings[4]);
	MARK_END();
}

/*
 * test_13 -- test syscalls with string arguments of length < 1531
 *            with single fork
 */
static void
test_13(char *a, char *b, char *c)
{
	syscall(SYS_fork);
	test_12(a, b, c);
}

/*
 * test_14 -- test syscalls with string arguments of length < 1531
 *            with double fork
 */
static void
test_14(char *a, char *b, char *c)
{
	syscall(SYS_fork);
	test_13(a, b, c);
}

/*
 * worker -- thread worker
 */
static void *
worker(void *arg)
{
	return NULL;
}

/*
 * test_unsupported_by_pmemfile -- test syscalls unsupported  by pmemfile
 */
static void
test_unsupported_by_pmemfile(const char *dir, const char *pmem,
				const char *nonp)
{
	char buf[BUF_SIZE];
	char *const argv[2] = {"path1", "path2"};
	int rv;

	if (!dir || !pmem || !nonp) {
		fprintf(stderr, "Error: Not enough parameters:\n");
		if (!dir)
			fprintf(stderr, "\t 'dir' is not set\n");
		if (!pmem)
			fprintf(stderr, "\t 'pmem' is not set\n");
		if (!nonp)
			fprintf(stderr, "\t 'nonp' is not set\n");
		exit(-1);
	}

	char *abspmem = calloc(1, strlen(dir) + strlen(pmem) + 2);
	char *absnonp = calloc(1, strlen(dir) + strlen(nonp) + 2);
	assert(abspmem && absnonp);

	strcat(abspmem, dir);
	strcat(abspmem, "/");
	strcat(abspmem, pmem);

	strcat(absnonp, dir);
	strcat(absnonp, "/");
	strcat(absnonp, nonp);

	int dirfd = open(dir, O_RDONLY);
	if (dirfd == -1)
		perror(dir);
	int fdpmem = open(abspmem, O_RDWR);
	if (fdpmem == -1)
		perror(abspmem);
	int fdnonp = open(absnonp, O_RDWR);
	if (fdnonp == -1)
		perror(absnonp);

	chdir(dir);
	fchdir(dirfd);

	rv = chroot(nonp);
	rv = chroot(pmem);
	rv = chroot(absnonp);
	rv = chroot(abspmem);

	setxattr(pmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	lsetxattr(pmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	getxattr(pmem, ANY_STR, buf, BUF_SIZE);
	lgetxattr(pmem, ANY_STR, buf, BUF_SIZE);

	setxattr(absnonp, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	setxattr(abspmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	lsetxattr(absnonp, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	lsetxattr(abspmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	fsetxattr(fdnonp, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	fsetxattr(fdpmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);

	getxattr(absnonp, ANY_STR, buf, BUF_SIZE);
	getxattr(abspmem, ANY_STR, buf, BUF_SIZE);
	lgetxattr(absnonp, ANY_STR, buf, BUF_SIZE);
	lgetxattr(abspmem, ANY_STR, buf, BUF_SIZE);
	fgetxattr(fdnonp, ANY_STR, buf, BUF_SIZE);
	fgetxattr(fdpmem, ANY_STR, buf, BUF_SIZE);

	listxattr(absnonp, ANY_STR, 0x101);
	listxattr(abspmem, ANY_STR, 0x101);
	llistxattr(absnonp, ANY_STR, 0x102);
	llistxattr(abspmem, ANY_STR, 0x102);
	flistxattr(fdnonp, ANY_STR, 0x104);
	flistxattr(fdpmem, ANY_STR, 0x104);

	removexattr(absnonp, ANY_STR);
	removexattr(abspmem, ANY_STR);
	lremovexattr(absnonp, ANY_STR);
	lremovexattr(abspmem, ANY_STR);
	fremovexattr(fdnonp, ANY_STR);
	fremovexattr(fdpmem, ANY_STR);

	rv = dup(fdnonp);
	rv = dup(fdpmem);
	dup2(fdnonp, 100);
	dup2(fdpmem, 101);
	dup3(fdnonp, 200, O_CLOEXEC);
	dup3(fdpmem, 201, O_CLOEXEC);

	mmap(NULL, 100, PROT_READ, MAP_SHARED, fdnonp, 0);
	mmap(NULL, 100, PROT_READ, MAP_SHARED, fdpmem, 0);

	execve(absnonp, argv, NULL);
	execve(abspmem, argv, NULL);
	syscall(__NR_execveat, dirfd, nonp, NULL, NULL);
	syscall(__NR_execveat, dirfd, pmem, NULL, NULL);

	flock(fdnonp, 0);
	flock(fdpmem, 0);

	readahead(fdnonp, 0, 0);
	readahead(fdpmem, 0, 0);

	sendfile(fdnonp, fdnonp, 0, 0);
	sendfile(fdpmem, fdnonp, 0, 0);
	sendfile(fdnonp, fdpmem, 0, 0);
	sendfile(fdpmem, fdpmem, 0, 0);

	syscall(__NR_splice, fdnonp, 0, fdnonp, 0, 0);
	syscall(__NR_splice, fdpmem, 0, fdnonp, 0, 0);
	syscall(__NR_splice, fdnonp, 0, fdpmem, 0, 0);
	syscall(__NR_splice, fdpmem, 0, fdpmem, 0, 0);

	name_to_handle_at(dirfd, nonp, NULL, NULL, 0);
	name_to_handle_at(dirfd, pmem, NULL, NULL, 0);

	syscall(__NR_copy_file_range, fdnonp, 0, fdnonp, 0, 1, 0);
	syscall(__NR_copy_file_range, fdpmem, 0, fdnonp, 0, 1, 0);
	syscall(__NR_copy_file_range, fdnonp, 0, fdpmem, 0, 1, 0);
	syscall(__NR_copy_file_range, fdpmem, 0, fdpmem, 0, 1, 0);

	open(absnonp, O_RDONLY);
	open(abspmem, O_RDONLY);
	open(absnonp, O_RDONLY | O_ASYNC);
	open(abspmem, O_RDONLY | O_ASYNC);

	openat(dirfd, nonp, O_RDONLY);
	openat(dirfd, pmem, O_RDONLY);
	openat(dirfd, nonp, O_RDONLY | O_ASYNC);
	openat(dirfd, pmem, O_RDONLY | O_ASYNC);

	syscall(__NR_renameat, dirfd, nonp, dirfd, nonp);
	syscall(__NR_renameat, dirfd, pmem, dirfd, pmem);
	syscall(__NR_renameat2, dirfd, nonp, dirfd, nonp, 0);
	syscall(__NR_renameat2, dirfd, pmem, dirfd, pmem, 0);
	syscall(__NR_renameat2, dirfd, nonp, dirfd, nonp, RENAME_WHITEOUT);
	syscall(__NR_renameat2, dirfd, pmem, dirfd, pmem, RENAME_WHITEOUT);

	fallocate(fdnonp, FALLOC_FL_COLLAPSE_RANGE, 0, 0);
	fallocate(fdpmem, FALLOC_FL_COLLAPSE_RANGE, 0, 0);
	fallocate(fdnonp, FALLOC_FL_ZERO_RANGE, 0, 0);
	fallocate(fdpmem, FALLOC_FL_ZERO_RANGE, 0, 0);
	fallocate(fdnonp, FALLOC_FL_INSERT_RANGE, 0, 0);
	fallocate(fdpmem, FALLOC_FL_INSERT_RANGE, 0, 0);

	int fdfnonp = fcntl(fdnonp, F_GETFD);
	int fdfpmem = fcntl(fdpmem, F_GETFD);

	fcntl(fdnonp, F_SETFD, fdfnonp & (~FD_CLOEXEC));
	fcntl(fdpmem, F_SETFD, fdfpmem & (~FD_CLOEXEC));

	fcntl(fdnonp, F_GETLK, 0);
	fcntl(fdpmem, F_GETLK, 0);

	fcntl(fdnonp, F_SETLK, 0);
	fcntl(fdpmem, F_SETLK, 0);

	fcntl(fdnonp, F_SETLKW, 0);
	fcntl(fdpmem, F_SETLKW, 0);

	fcntl(fdnonp, F_SETOWN, 0);
	fcntl(fdpmem, F_SETOWN, 0);

	fcntl(fdnonp, F_GETOWN, 0);
	fcntl(fdpmem, F_GETOWN, 0);

	fcntl(fdnonp, F_SETSIG, 0);
	fcntl(fdpmem, F_SETSIG, 0);

	fcntl(fdnonp, F_GETSIG, 0);
	fcntl(fdpmem, F_GETSIG, 0);

	fcntl(fdnonp, F_SETOWN_EX, 0);
	fcntl(fdpmem, F_SETOWN_EX, 0);

	fcntl(fdnonp, F_GETOWN_EX, 0);
	fcntl(fdpmem, F_GETOWN_EX, 0);

	fcntl(fdnonp, F_OFD_GETLK, 0);
	fcntl(fdpmem, F_OFD_GETLK, 0);

	fcntl(fdnonp, F_OFD_SETLK, 0);
	fcntl(fdpmem, F_OFD_SETLK, 0);

	fcntl(fdnonp, F_OFD_SETLKW, 0);
	fcntl(fdpmem, F_OFD_SETLKW, 0);

	fcntl(fdnonp, F_SETLEASE, 0);
	fcntl(fdpmem, F_SETLEASE, 0);

	fcntl(fdnonp, F_GETLEASE, 0);
	fcntl(fdpmem, F_GETLEASE, 0);

	fcntl(fdnonp, F_NOTIFY, 0);
	fcntl(fdpmem, F_NOTIFY, 0);

	fcntl(fdnonp, F_ADD_SEALS, 0);
	fcntl(fdpmem, F_ADD_SEALS, 0);

	fcntl(fdnonp, F_GET_SEALS, 0);
	fcntl(fdpmem, F_GET_SEALS, 0);

	int i;
	pthread_t workers[N_WORKERS];
	for (i = 0; i < N_WORKERS; ++i) {
		pthread_create(&workers[i], NULL, worker, NULL);
	}
	for (i = i - 1; i >= 0; --i) {
		pthread_join(workers[i], NULL);
	}

	fork();
	syscall(SYS_fork);
	if (vfork() == 0) _exit(0);

	close(fdpmem);

	(void) rv;
}

/*
 * test_15 -- test Analyzing Tool
 */
static void
test_15(char *dir, char *pmem, char *nonp)
{
	MARK_START();
	test_unsupported_by_pmemfile(dir, pmem, nonp);
	MARK_END();
}

/*
 * run_test -- array of tests
 */
static void (*run_test[])(char *, char *, char *) = {
	test_0,
	test_1,
	test_2,
	test_3,
	test_4,
	test_5,
	test_6,
	test_7,
	test_8,
	test_9,
	test_10,
	test_11,
	test_12,
	test_13,
	test_14,
	test_15
};

int
main(int argc, char *argv[])
{
	int max = sizeof(run_test) / sizeof(run_test[0]) - 1;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <test-number: 0..%i>\n",
				argv[0], max);
		return -1;
	}

	int n = atoi(argv[1]);
	if (n > max) {
		fprintf(stderr,
			"Error: test number can take only following values: 0..%i (%i is not allowed)\n",
			max, n);
		return -1;
	}

	printf("Starting: test_%i ...\n", n);

	run_test[n](argc > 2 ? argv[2] : NULL,
			argc > 3 ? argv[3] : NULL,
			argc > 4 ? argv[4] : NULL);

	printf("Done (test_%i)\n", n);
}
