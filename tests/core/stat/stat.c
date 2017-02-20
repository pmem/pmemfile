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
 * stat.c -- unit test for pmemfile_stat & pmemfile_fstat
 */

#include "pmemfile_test.h"

static const char *
timespec_to_str(const struct timespec *t)
{
	char *s = asctime(localtime(&t->tv_sec));
	s[strlen(s) - 1] = 0;
	return s;
}

static void
dump_stat(struct stat *st, const char *path)
{
	UT_OUT("path:       %s\n", path);
	UT_OUT("st_dev:     0x%lx\n", st->st_dev);
	UT_OUT("st_ino:     %ld\n", st->st_ino);
	UT_OUT("st_mode:    0%o\n", st->st_mode);
	UT_OUT("st_nlink:   %lu\n", st->st_nlink);
	UT_OUT("st_uid:     %u\n", st->st_uid);
	UT_OUT("st_gid:     %u\n", st->st_gid);
	UT_OUT("st_rdev:    0x%lx\n", st->st_rdev);
	UT_OUT("st_size:    %ld\n", st->st_size);
	UT_OUT("st_blksize: %ld\n", st->st_blksize);
	UT_OUT("st_blocks:  %ld\n", st->st_blocks);
	UT_OUT("st_atim:    %ld.%.9ld, %s\n", st->st_atim.tv_sec,
			st->st_atim.tv_nsec, timespec_to_str(&st->st_atim));
	UT_OUT("st_mtim:    %ld.%.9ld, %s\n", st->st_mtim.tv_sec,
			st->st_mtim.tv_nsec, timespec_to_str(&st->st_mtim));
	UT_OUT("st_ctim:    %ld.%.9ld, %s\n", st->st_ctim.tv_sec,
			st->st_ctim.tv_nsec, timespec_to_str(&st->st_ctim));
	UT_OUT("---\n");
}

static int
stat_and_dump(PMEMfilepool *pfp, const char *path)
{
	struct stat st;
	int ret = pmemfile_stat(pfp, path, &st);
	if (ret)
		return ret;

	dump_stat(&st, path);
	return 0;
}

static int
fstat_and_dump(PMEMfilepool *pfp, PMEMfile *f)
{
	struct stat st;
	int ret = pmemfile_fstat(pfp, f, &st);
	if (ret)
		return ret;

	dump_stat(&st, NULL);

	return 0;
}

static void
test1(PMEMfilepool *pfp)
{
	PMEMfile *f = PMEMFILE_OPEN(pfp, "/file1", O_CREAT | O_EXCL | O_WRONLY,
			0644);

	UT_ASSERTeq(stat_and_dump(pfp, "/file1"), 0);

	char buf[1024];
	memset(buf, 0xdd, 1024);

	for (int i = 0; i < 100; ++i)
		PMEMFILE_WRITE(pfp, f, buf, 1024, 1024);

	UT_ASSERTeq(stat_and_dump(pfp, "/file1"), 0);

	errno = 0;
	UT_ASSERTeq(stat_and_dump(pfp, "/file1/"), -1);
	UT_ASSERTeq(errno, ENOTDIR);

	PMEMFILE_UNLINK(pfp, "/file1");

	errno = 0;
	UT_ASSERTeq(stat_and_dump(pfp, "/file1"), -1);
	UT_ASSERTeq(errno, ENOENT);

	UT_ASSERTeq(fstat_and_dump(pfp, f), 0);

	PMEMFILE_CLOSE(pfp, f);
}

static void
test2(PMEMfilepool *pfp)
{
	PMEMFILE_MKDIR(pfp, "/dir", 0755);

	UT_ASSERTeq(stat_and_dump(pfp, "/dir"), 0);

	PMEMFILE_CREATE(pfp, "/dir/file1", O_EXCL, 0644);

	UT_ASSERTeq(stat_and_dump(pfp, "/dir/file1"), 0);

	PMEMFILE_UNLINK(pfp, "/dir/file1");

	PMEMFILE_RMDIR(pfp, "/dir");
}

int
main(int argc, char *argv[])
{
	if (argc < 2)
		UT_FATAL("usage: %s file-name", argv[0]);

	const char *path = argv[1];

	PMEMfilepool *pfp = PMEMFILE_MKFS(path);

	UT_ASSERTeq(stat_and_dump(pfp, "/"), 0);

	errno = 0;
	UT_ASSERTeq(stat_and_dump(pfp, "/file1"), -1);
	UT_ASSERTeq(errno, ENOENT);

	test1(pfp);
	test2(pfp);

	pmemfile_pool_close(pfp);
}
