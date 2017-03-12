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
 * stat.cpp -- unit test for pmemfile_stat & pmemfile_fstat
 */

#include "pmemfile_test.hpp"

class stat_test : public pmemfile_test {
public:
	stat_test() : pmemfile_test()
	{
	}
};

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
	T_OUT("path:       %s\n", path);
	T_OUT("st_dev:     0x%lx\n", st->st_dev);
	T_OUT("st_ino:     %ld\n", st->st_ino);
	T_OUT("st_mode:    0%o\n", st->st_mode);
	T_OUT("st_nlink:   %lu\n", st->st_nlink);
	T_OUT("st_uid:     %u\n", st->st_uid);
	T_OUT("st_gid:     %u\n", st->st_gid);
	T_OUT("st_rdev:    0x%lx\n", st->st_rdev);
	T_OUT("st_size:    %ld\n", st->st_size);
	T_OUT("st_blksize: %ld\n", st->st_blksize);
	T_OUT("st_blocks:  %ld\n", st->st_blocks);
	T_OUT("st_atim:    %ld.%.9ld, %s\n", st->st_atim.tv_sec,
	      st->st_atim.tv_nsec, timespec_to_str(&st->st_atim));
	T_OUT("st_mtim:    %ld.%.9ld, %s\n", st->st_mtim.tv_sec,
	      st->st_mtim.tv_nsec, timespec_to_str(&st->st_mtim));
	T_OUT("st_ctim:    %ld.%.9ld, %s\n", st->st_ctim.tv_sec,
	      st->st_ctim.tv_nsec, timespec_to_str(&st->st_ctim));
	T_OUT("---\n");
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

TEST_F(stat_test, 0)
{
	ASSERT_EQ(stat_and_dump(pfp, "/"), 0);

	errno = 0;
	ASSERT_EQ(stat_and_dump(pfp, "/file1"), -1);
	EXPECT_EQ(errno, ENOENT);
}

TEST_F(stat_test, 1)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY,
				    0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	ASSERT_EQ(stat_and_dump(pfp, "/file1"), 0);

	char buf[1024];
	memset(buf, 0xdd, 1024);

	for (int i = 0; i < 100; ++i) {
		pmemfile_ssize_t written = pmemfile_write(pfp, f, buf, 1024);
		ASSERT_EQ(written, 1024) << COND_ERROR(written);
	}

	ASSERT_EQ(stat_and_dump(pfp, "/file1"), 0);

	errno = 0;
	ASSERT_EQ(stat_and_dump(pfp, "/file1/"), -1);
	EXPECT_EQ(errno, ENOTDIR);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);

	errno = 0;
	ASSERT_EQ(stat_and_dump(pfp, "/file1"), -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_EQ(fstat_and_dump(pfp, f), 0);

	pmemfile_close(pfp, f);
}

TEST_F(stat_test, 2)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0755), 0);

	ASSERT_EQ(stat_and_dump(pfp, "/dir"), 0);

	ASSERT_TRUE(
		test_pmemfile_create(pfp, "/dir/file1", PMEMFILE_O_EXCL, 0644));

	ASSERT_EQ(stat_and_dump(pfp, "/dir/file1"), 0);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file1"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
}

int
main(int argc, char *argv[])
{
	START();

	if (argc < 2) {
		fprintf(stderr, "usage: %s global_path", argv[0]);
		exit(1);
	}

	global_path = argv[1];

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
