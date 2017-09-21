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

static bool verbose;

static const char *
timespec_to_str(const pmemfile_timespec_t *t)
{
	time_t sec = t->tv_sec;
	char *s = asctime(localtime(&sec));
	s[strlen(s) - 1] = 0;
	return s;
}

static void
dump_stat(pmemfile_stat_t *st, const char *path)
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
test_stat(PMEMfilepool *pfp, const char *path, pmemfile_mode_t mode = 0,
	  pmemfile_nlink_t nlink = 0, pmemfile_off_t size = 0,
	  pmemfile_blksize_t blksize = 0, pmemfile_blkcnt_t blocks = 0)
{
	pmemfile_stat_t st;
	memset(&st, 0, sizeof(st));
	int ret = pmemfile_stat(pfp, path, &st);
	if (ret)
		return ret;
	EXPECT_EQ(mode, st.st_mode);
	EXPECT_EQ(nlink, st.st_nlink);
	EXPECT_EQ(size, st.st_size);
	EXPECT_EQ(blksize, st.st_blksize);
	EXPECT_EQ(blocks, st.st_blocks);

	if (mode != st.st_mode || nlink != st.st_nlink || size != st.st_size ||
	    blksize != st.st_blksize || blocks != st.st_blocks)
		return -1;

	if (verbose)
		dump_stat(&st, path);
	return 0;
}

static int
test_fstat(PMEMfilepool *pfp, PMEMfile *f, pmemfile_mode_t mode = 0,
	   pmemfile_nlink_t nlink = 0, pmemfile_off_t size = 0,
	   pmemfile_blksize_t blksize = 0, pmemfile_blkcnt_t blocks = 0)
{
	pmemfile_stat_t st;
	memset(&st, 0, sizeof(st));
	int ret = pmemfile_fstat(pfp, f, &st);
	if (ret)
		return ret;
	EXPECT_EQ(mode, st.st_mode);
	EXPECT_EQ(nlink, st.st_nlink);
	EXPECT_EQ(size, st.st_size);
	EXPECT_EQ(blksize, st.st_blksize);
	EXPECT_EQ(blocks, st.st_blocks);

	if (mode != st.st_mode || nlink != st.st_nlink || size != st.st_size ||
	    blksize != st.st_blksize || blocks != st.st_blocks)
		return -1;

	if (verbose)
		dump_stat(&st, NULL);
	return 0;
}

static int
test_fstatat(PMEMfilepool *pfp, PMEMfile *dir, const char *path, int flags,
	     pmemfile_mode_t mode = 0, pmemfile_nlink_t nlink = 0,
	     pmemfile_off_t size = 0, pmemfile_blksize_t blksize = 0,
	     pmemfile_blkcnt_t blocks = 0)
{
	pmemfile_stat_t st;
	memset(&st, 0, sizeof(st));
	int ret = pmemfile_fstatat(pfp, dir, path, &st, flags);
	if (ret)
		return ret;
	EXPECT_EQ(mode, st.st_mode);
	EXPECT_EQ(nlink, st.st_nlink);
	EXPECT_EQ(size, st.st_size);
	EXPECT_EQ(blksize, st.st_blksize);
	EXPECT_EQ(blocks, st.st_blocks);

	if (mode != st.st_mode || nlink != st.st_nlink || size != st.st_size ||
	    blksize != st.st_blksize || blocks != st.st_blocks)
		return -1;

	if (verbose)
		dump_stat(&st, NULL);
	return 0;
}

TEST_F(stat_test, basic)
{
	EXPECT_EQ(test_stat(pfp, "/", 040777, 2, 4096, 1, 8), 0);

	errno = 0;
	EXPECT_EQ(test_stat(pfp, "/file1"), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	EXPECT_EQ(pmemfile_stat(pfp, "/", NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	EXPECT_EQ(test_stat(pfp, NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	EXPECT_EQ(test_stat(NULL, "/file1"), -1);
	EXPECT_EQ(errno, EFAULT);
}

TEST_F(stat_test, stat_big_file)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY,
				    0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	EXPECT_EQ(test_stat(pfp, "/file1", 0100644, 1, 0, 1, 0), 0);

	char buf[1024];
	memset(buf, 0xdd, 1024);

	for (int i = 0; i < 100; ++i) {
		pmemfile_ssize_t written = pmemfile_write(pfp, f, buf, 1024);
		ASSERT_EQ(written, 1024) << COND_ERROR(written);
	}

	EXPECT_EQ(test_stat(pfp, "/file1", 0100644, 1, 102400, 1, 224), 0);

	errno = 0;
	EXPECT_EQ(test_stat(pfp, "/file1/"), -1);
	EXPECT_EQ(errno, ENOTDIR);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);

	errno = 0;
	EXPECT_EQ(test_stat(pfp, "/file1"), -1);
	EXPECT_EQ(errno, ENOENT);

	EXPECT_EQ(test_fstat(pfp, f, 0100644, 0, 102400, 1, 224), 0);

	pmemfile_stat_t stbuf;

	errno = 0;
	EXPECT_EQ(pmemfile_fstat(pfp, f, NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	EXPECT_EQ(pmemfile_fstat(pfp, NULL, &stbuf), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	EXPECT_EQ(pmemfile_fstat(NULL, f, &stbuf), -1);
	EXPECT_EQ(errno, EFAULT);

	pmemfile_close(pfp, f);
}

TEST_F(stat_test, stat_file_in_dir)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0755), 0);

	EXPECT_EQ(test_stat(pfp, "/dir", 040755, 2, 4096, 1, 8), 0);

	ASSERT_TRUE(
		test_pmemfile_create(pfp, "/dir/file1", PMEMFILE_O_EXCL, 0644));

	EXPECT_EQ(test_stat(pfp, "/dir/file1", 0100644, 1, 0, 1, 0), 0);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file1"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
}

TEST_F(stat_test, fstatat)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0755), 0);

	ASSERT_TRUE(
		test_pmemfile_create(pfp, "/dir/file1", PMEMFILE_O_EXCL, 0644));

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir/file1", "/file2"), 0);

	PMEMfile *dir = pmemfile_open(pfp, "/dir", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(dir, nullptr);

	errno = 0;
	EXPECT_EQ(test_fstatat(pfp, dir, NULL, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	EXPECT_EQ(test_fstatat(pfp, NULL, "file1", 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	EXPECT_EQ(test_fstatat(pfp, NULL, "/dir/file1", 0, 0100644, 1, 0, 1, 0),
		  0);
	EXPECT_EQ(errno, 0);

	errno = 0;
	EXPECT_EQ(test_fstatat(pfp, BADF, "/dir/file1", 0, 0100644, 1, 0, 1, 0),
		  0);
	EXPECT_EQ(errno, 0);

	errno = 0;
	EXPECT_EQ(test_fstatat(NULL, dir, "file1", 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	EXPECT_EQ(
		test_fstatat(pfp, dir, "file1", ~(PMEMFILE_AT_NO_AUTOMOUNT |
						  PMEMFILE_AT_SYMLINK_NOFOLLOW |
						  PMEMFILE_AT_EMPTY_PATH)),
		-1);
	EXPECT_EQ(errno, EINVAL);

	EXPECT_EQ(test_fstatat(pfp, dir, "file1", 0, 0100644, 1, 0, 1, 0), 0);

	EXPECT_EQ(test_fstatat(pfp, dir, "../file2", 0, 0100644, 1, 0, 1, 0),
		  0);

	EXPECT_EQ(test_fstatat(pfp, dir, "../file2",
			       PMEMFILE_AT_SYMLINK_NOFOLLOW, 0120777, 1, 10, 1,
			       0),
		  0);

	EXPECT_EQ(test_fstatat(pfp, dir, "", 0), -1);
	EXPECT_EQ(errno, ENOENT);

	EXPECT_EQ(test_fstatat(pfp, dir, "", PMEMFILE_AT_EMPTY_PATH, 040755, 2,
			       8192, 1, 16),
		  0);

#ifdef FAULT_INJECTION
	pmemfile_gid_t groups[1] = {1002};
	ASSERT_EQ(pmemfile_setgroups(pfp, 1, groups), 0);
	pmemfile_inject_fault_at(PF_MALLOC, 1, "copy_cred");
	pmemfile_stat_t st;
	memset(&st, 0, sizeof(st));
	errno = 0;
	EXPECT_EQ(pmemfile_fstatat(pfp, dir, "../file2", &st,
				   PMEMFILE_AT_SYMLINK_NOFOLLOW),
		  -1);
	EXPECT_EQ(errno, ENOMEM);
#endif

	pmemfile_close(pfp, dir);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file2"), 0);
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
	if (argc >= 3)
		verbose = strcmp(argv[2], "-v") == 0;

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
