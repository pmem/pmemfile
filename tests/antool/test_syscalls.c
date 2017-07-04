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
 * test_syscalls.c -- functional tests for vltrace
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

#define F_ADD_SEALS		1033
#define F_GET_SEALS		1034
#define ANY_STR			"any-string"

#define PATTERN_START		((int)0x12345678)
#define PATTERN_END		((int)0x87654321)
#define BUF_SIZE		0x100

#define MARK_START()		close(PATTERN_START)
#define MARK_END()		close(PATTERN_END)

#define N_ITERATIONS		1000000

static int counter;

/*
 * s -- busy wait for a while
 */
static void
s()
{
	for (int i = 0; i < N_ITERATIONS; i++)
		counter += rand();
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
 * test_analyzing_tool -- test unsupported syscalls #2
 */
static void
test_analyzing_tool(char *dir, char *pmem, char *nonp)
{
	char buf[BUF_SIZE];
	char *const argv[2] = {pmem, pmem};
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

	int i;
	pthread_t workers[N_WORKERS];
	for (i = 0; i < N_WORKERS; ++i) {
		s(); pthread_create(&workers[i], NULL, worker, NULL);
	}
	for (i = i - 1; i >= 0; --i) {
		s(); pthread_join(workers[i], NULL);
	}

	s(); rv = chroot(nonp);
	s(); rv = chroot(pmem);
	s(); rv = chroot(absnonp);
	s(); rv = chroot(abspmem);

	s(); setxattr(pmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	s(); lsetxattr(pmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	s(); getxattr(pmem, ANY_STR, buf, BUF_SIZE);
	s(); lgetxattr(pmem, ANY_STR, buf, BUF_SIZE);

	s(); setxattr(absnonp, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	s(); setxattr(abspmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	s(); lsetxattr(absnonp, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	s(); lsetxattr(abspmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	s(); fsetxattr(fdnonp, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);
	s(); fsetxattr(fdpmem, ANY_STR, buf, BUF_SIZE, XATTR_CREATE);

	s(); getxattr(absnonp, ANY_STR, buf, BUF_SIZE);
	s(); getxattr(abspmem, ANY_STR, buf, BUF_SIZE);
	s(); lgetxattr(absnonp, ANY_STR, buf, BUF_SIZE);
	s(); lgetxattr(abspmem, ANY_STR, buf, BUF_SIZE);
	s(); fgetxattr(fdnonp, ANY_STR, buf, BUF_SIZE);
	s(); fgetxattr(fdpmem, ANY_STR, buf, BUF_SIZE);

	s(); listxattr(absnonp, ANY_STR, 0x101);
	s(); listxattr(abspmem, ANY_STR, 0x101);
	s(); llistxattr(absnonp, ANY_STR, 0x102);
	s(); llistxattr(abspmem, ANY_STR, 0x102);
	s(); flistxattr(fdnonp, ANY_STR, 0x104);
	s(); flistxattr(fdpmem, ANY_STR, 0x104);

	s(); removexattr(absnonp, ANY_STR);
	s(); removexattr(abspmem, ANY_STR);
	s(); lremovexattr(absnonp, ANY_STR);
	s(); lremovexattr(abspmem, ANY_STR);
	s(); fremovexattr(fdnonp, ANY_STR);
	s(); fremovexattr(fdpmem, ANY_STR);

	s(); rv = dup(fdnonp);
	s(); rv = dup(fdpmem);
	s(); dup2(fdnonp, 100);
	s(); dup2(fdpmem, 101);
	s(); dup3(fdnonp, 200, O_CLOEXEC);
	s(); dup3(fdpmem, 201, O_CLOEXEC);

	s(); mmap(NULL, 100, PROT_READ, MAP_SHARED, fdnonp, 0);
	s(); mmap(NULL, 100, PROT_READ, MAP_SHARED, fdpmem, 0);

	s(); execve(absnonp, argv, NULL);
	s(); execve(abspmem, argv, NULL);
	s(); syscall(__NR_execveat, dirfd, nonp, NULL, NULL);
	s(); syscall(__NR_execveat, dirfd, pmem, NULL, NULL);

	s(); flock(fdnonp, 0);
	s(); flock(fdpmem, 0);

	s(); readahead(fdnonp, 0, 0);
	s(); readahead(fdpmem, 0, 0);

	s(); sendfile(fdnonp, fdnonp, 0, 0);
	s(); sendfile(fdpmem, fdnonp, 0, 0);
	s(); sendfile(fdnonp, fdpmem, 0, 0);
	s(); sendfile(fdpmem, fdpmem, 0, 0);

	s(); syscall(__NR_splice, fdnonp, 0, fdnonp, 0, 0);
	s(); syscall(__NR_splice, fdpmem, 0, fdnonp, 0, 0);
	s(); syscall(__NR_splice, fdnonp, 0, fdpmem, 0, 0);
	s(); syscall(__NR_splice, fdpmem, 0, fdpmem, 0, 0);

	s(); name_to_handle_at(dirfd, nonp, NULL, NULL, 0);
	s(); name_to_handle_at(dirfd, pmem, NULL, NULL, 0);

	s(); syscall(__NR_copy_file_range, fdnonp, 0, fdnonp, 0, 1, 0);
	s(); syscall(__NR_copy_file_range, fdpmem, 0, fdnonp, 0, 1, 0);
	s(); syscall(__NR_copy_file_range, fdnonp, 0, fdpmem, 0, 1, 0);
	s(); syscall(__NR_copy_file_range, fdpmem, 0, fdpmem, 0, 1, 0);

	s(); open(absnonp, O_RDONLY);
	s(); open(abspmem, O_RDONLY);
	s(); open(absnonp, O_RDONLY | O_ASYNC);
	s(); open(abspmem, O_RDONLY | O_ASYNC);

	s(); openat(dirfd, nonp, O_RDONLY);
	s(); openat(dirfd, pmem, O_RDONLY);
	s(); openat(dirfd, nonp, O_RDONLY | O_ASYNC);
	s(); openat(dirfd, pmem, O_RDONLY | O_ASYNC);

	s(); syscall(__NR_renameat, dirfd, nonp, dirfd, nonp);
	s(); syscall(__NR_renameat, dirfd, pmem, dirfd, pmem);
	s(); syscall(__NR_renameat2, dirfd, nonp, dirfd, nonp, 0);
	s(); syscall(__NR_renameat2, dirfd, pmem, dirfd, pmem, 0);
	s(); syscall(__NR_renameat2, dirfd, nonp, dirfd, nonp, RENAME_WHITEOUT);
	s(); syscall(__NR_renameat2, dirfd, pmem, dirfd, pmem, RENAME_WHITEOUT);

	s(); fallocate(fdnonp, FALLOC_FL_COLLAPSE_RANGE, 0, 0);
	s(); fallocate(fdpmem, FALLOC_FL_COLLAPSE_RANGE, 0, 0);
	s(); fallocate(fdnonp, FALLOC_FL_ZERO_RANGE, 0, 0);
	s(); fallocate(fdpmem, FALLOC_FL_ZERO_RANGE, 0, 0);
	s(); fallocate(fdnonp, FALLOC_FL_INSERT_RANGE, 0, 0);
	s(); fallocate(fdpmem, FALLOC_FL_INSERT_RANGE, 0, 0);

	s(); int fdfnonp = fcntl(fdnonp, F_GETFD);
	s(); int fdfpmem = fcntl(fdpmem, F_GETFD);

	s(); fcntl(fdnonp, F_SETFD, fdfnonp & (~FD_CLOEXEC));
	s(); fcntl(fdpmem, F_SETFD, fdfpmem & (~FD_CLOEXEC));

	s(); fcntl(fdnonp, F_GETLK, 0);
	s(); fcntl(fdpmem, F_GETLK, 0);

	s(); fcntl(fdnonp, F_SETLK, 0);
	s(); fcntl(fdpmem, F_SETLK, 0);

	s(); fcntl(fdnonp, F_SETLKW, 0);
	s(); fcntl(fdpmem, F_SETLKW, 0);

	s(); fcntl(fdnonp, F_SETOWN, 0);
	s(); fcntl(fdpmem, F_SETOWN, 0);

	s(); fcntl(fdnonp, F_GETOWN, 0);
	s(); fcntl(fdpmem, F_GETOWN, 0);

	s(); fcntl(fdnonp, F_SETSIG, 0);
	s(); fcntl(fdpmem, F_SETSIG, 0);

	s(); fcntl(fdnonp, F_GETSIG, 0);
	s(); fcntl(fdpmem, F_GETSIG, 0);

	s(); fcntl(fdnonp, F_SETOWN_EX, 0);
	s(); fcntl(fdpmem, F_SETOWN_EX, 0);

	s(); fcntl(fdnonp, F_GETOWN_EX, 0);
	s(); fcntl(fdpmem, F_GETOWN_EX, 0);

	s(); fcntl(fdnonp, F_OFD_GETLK, 0);
	s(); fcntl(fdpmem, F_OFD_GETLK, 0);

	s(); fcntl(fdnonp, F_OFD_SETLK, 0);
	s(); fcntl(fdpmem, F_OFD_SETLK, 0);

	s(); fcntl(fdnonp, F_OFD_SETLKW, 0);
	s(); fcntl(fdpmem, F_OFD_SETLKW, 0);

	s(); fcntl(fdnonp, F_SETLEASE, 0);
	s(); fcntl(fdpmem, F_SETLEASE, 0);

	s(); fcntl(fdnonp, F_GETLEASE, 0);
	s(); fcntl(fdpmem, F_GETLEASE, 0);

	s(); fcntl(fdnonp, F_NOTIFY, 0);
	s(); fcntl(fdpmem, F_NOTIFY, 0);

	s(); fcntl(fdnonp, F_ADD_SEALS, 0);
	s(); fcntl(fdpmem, F_ADD_SEALS, 0);

	s(); fcntl(fdnonp, F_GET_SEALS, 0);
	s(); fcntl(fdpmem, F_GET_SEALS, 0);

	s(); fork();
	s(); syscall(SYS_fork);
	s(); if (vfork() == 0) _exit(0);

	s(); close(fdpmem);
	
	(void) rv;
}

/*
 * test_0 -- test unsupported syscalls
 */
static void test_0(char *dir, char *pmem, char *nonp)
{
	MARK_START();
	test_analyzing_tool(dir, pmem, nonp);
	MARK_END();
}

/*
 * run_test -- array of tests
 */
static void (*run_test[])(char *, char *, char *) = {
	test_0,
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
		fprintf(stderr, "Error: test number can take only following"
				" values: 0..%i (%i is not allowed)\n",
				max, n);
		return -1;
	}

	printf("Starting: test_%i ...\n", n);

	run_test[n](argc > 2 ? argv[2] : NULL,
			argc > 3 ? argv[3] : NULL,
			argc > 4 ? argv[4] : NULL);

	printf("Done (test_%i)\n", n);
}
