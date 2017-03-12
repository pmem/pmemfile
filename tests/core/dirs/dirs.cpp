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
 * dirs.cpp -- unit test for directories
 */

#include "pmemfile_test.hpp"

class dirs : public pmemfile_test {
public:
	dirs() : pmemfile_test(256 * 1024 * 1024)
	{
		// XXX
		test_empty_dir_on_teardown = false;
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
	T_OUT("---");
}

struct linux_dirent64 {
	uint64_t d_ino;
	uint64_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

#define VAL_EXPECT_EQ(v1, v2)                                                  \
	do {                                                                   \
		if ((v1) != (v2)) {                                            \
			ADD_FAILURE() << (v1) << " != " << (v2);               \
			return false;                                          \
		}                                                              \
	} while (0)

static bool
list_files(PMEMfilepool *pfp, const char *dir, size_t expected_files,
	   int just_count, const char *name)
{
	T_OUT("\"%s\" start\n", name);
	PMEMfile *f = pmemfile_open(pfp, dir,
				    PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	if (!f) {
		EXPECT_NE(f, nullptr);
		return false;
	}

	char buf[32 * 1024];
	char path[PMEMFILE_PATH_MAX];
	struct linux_dirent64 *d = (struct linux_dirent64 *)buf;
	int r = pmemfile_getdents64(pfp, f, d, sizeof(buf));
	size_t num_files = 0;

	while ((uintptr_t)d < (uintptr_t)&buf[r]) {
		num_files++;
		if (!just_count) {
			T_OUT("ino: 0x%lx, off: 0x%lx, len: %d, type: %d, "
			      "name: \"%s\"\n",
			      d->d_ino, d->d_off, d->d_reclen, d->d_type,
			      d->d_name);
			sprintf(path, "/%s/%s", dir, d->d_name);

			struct stat st;
			int ret = pmemfile_stat(pfp, path, &st);
			VAL_EXPECT_EQ(ret, 0);
			dump_stat(&st, path);
		}
		d = (struct linux_dirent64 *)(((char *)d) + d->d_reclen);
	}

	pmemfile_close(pfp, f);

	T_OUT("\"%s\" end\n", name);
	VAL_EXPECT_EQ(num_files, expected_files);

	return true;
}

TEST_F(dirs, 0)
{
	PMEMfile *f;

	ASSERT_TRUE(test_pmemfile_create(pfp, "/file", PMEMFILE_O_EXCL, 0644));

	f = pmemfile_open(pfp, "//file", 0);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/../file", 0);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/../../file", 0);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0) << strerror(errno);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir////", 0755), 0) << strerror(errno);

	ASSERT_TRUE(list_files(pfp, "/", 3, 0, ". .. dir"));
	ASSERT_TRUE(list_files(pfp, "/dir", 2, 0, ". .."));

	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir//../dir/.//file",
					 PMEMFILE_O_EXCL, 0644));

	ASSERT_TRUE(list_files(pfp, "/dir", 3, 0, ". .. file"));

	f = pmemfile_open(pfp, "/dir/file", 0);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/dir/../dir////file", 0);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/dir/file/file", PMEMFILE_O_RDONLY);
	ASSERT_EQ(f, nullptr);
	EXPECT_EQ(errno, ENOTDIR);

	f = pmemfile_open(pfp, "/dir/file/file",
			  PMEMFILE_O_RDONLY | PMEMFILE_O_CREAT, 0644);
	ASSERT_EQ(f, nullptr);
	EXPECT_EQ(errno, ENOTDIR);

	f = pmemfile_open(pfp, "/dir/file/file", PMEMFILE_O_RDONLY |
				  PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
			  0644);
	ASSERT_EQ(f, nullptr);
	EXPECT_EQ(errno, ENOTDIR);

	/* file is not a directory */
	errno = 0;
	f = pmemfile_open(pfp, "/dir/file/", PMEMFILE_O_RDONLY);
	ASSERT_EQ(f, nullptr);
	EXPECT_EQ(errno, ENOTDIR);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir//file"), 0) << strerror(errno);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir//////"), 0) << strerror(errno);
}

TEST_F(dirs, 1)
{
	int ret;
	PMEMfile *f;
	char buf[1001];
	pmemfile_ssize_t written;

	ASSERT_TRUE(test_empty_dir(pfp, "/"));
	memset(buf, 0xff, sizeof(buf));

	for (size_t i = 0; i < 100; ++i) {
		sprintf(buf, "/file%04lu", i);

		f = pmemfile_open(pfp, buf, PMEMFILE_O_CREAT | PMEMFILE_O_EXCL |
					  PMEMFILE_O_WRONLY,
				  0644);
		ASSERT_NE(f, nullptr) << strerror(errno);

		written = pmemfile_write(pfp, f, buf, i);
		ASSERT_EQ(written, (pmemfile_ssize_t)i) << COND_ERROR(written);

		pmemfile_close(pfp, f);

		ASSERT_TRUE(list_files(pfp, "/", i + 1 + 2, 0,
				       "test1: after one iter"));
	}

	for (int i = 0; i < 100; ++i) {
		sprintf(buf, "/file%04d", i);

		ret = pmemfile_unlink(pfp, buf);
		ASSERT_EQ(ret, 0) << strerror(errno);
	}

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 32680, "."},
						    {040777, 2, 32680, ".."},
					    }));
}

TEST_F(dirs, 2)
{
	char buf[1001];

	for (size_t i = 0; i < 100; ++i) {
		sprintf(buf, "/dir%04lu", i);

		ASSERT_EQ(pmemfile_mkdir(pfp, buf, 0755), 0);

		ASSERT_TRUE(list_files(pfp, "/", i + 1 + 2, 0,
				       "test2: after one iter"));
	}

	ASSERT_TRUE(list_files(pfp, "/", 100 + 2, 1, "test2: after loop"));
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir0007/another_directory", 0755), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/", 0755), -1);
	EXPECT_EQ(errno, EEXIST);

	errno = 0;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir0007", 0755), -1);
	EXPECT_EQ(errno, EEXIST);

	errno = 0;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2333/aaaa", 0755), -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_TRUE(list_files(pfp, "/", 100 + 2, 1, "test2: after2"));

	ASSERT_TRUE(test_pmemfile_create(pfp, "/file", PMEMFILE_O_EXCL, 0644));

	errno = 0;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/file/aaaa", 0755), -1);
	EXPECT_EQ(errno, ENOTDIR);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);

	ASSERT_TRUE(list_files(pfp, "/", 100 + 2, 1, "test2: after3"));

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir0100"), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir0099/inside"), -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/file", PMEMFILE_O_EXCL, 0644));

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/file"), -1);
	EXPECT_EQ(errno, ENOTDIR);

	errno = 0;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/file/", 0755), -1);
	EXPECT_EQ(errno, EEXIST);

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/file/"), -1);
	EXPECT_EQ(errno, ENOTDIR);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir0000"), -1);
	EXPECT_EQ(errno, EISDIR);

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir0007"), -1);
	EXPECT_EQ(errno, ENOTEMPTY);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir0007/another_directory"), 0);

	for (int i = 0; i < 100; ++i) {
		sprintf(buf, "/dir%04d", i);

		ASSERT_EQ(pmemfile_rmdir(pfp, buf), 0);
	}
}

TEST_F(dirs, 3)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0755), 0);
	ASSERT_TRUE(
		test_pmemfile_create(pfp, "/dir1/file", PMEMFILE_O_EXCL, 0644));

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), -1);
	ASSERT_EQ(errno, ENOTEMPTY);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/file"), 0);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1/dir2", 0755), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), -1);
	ASSERT_EQ(errno, ENOTEMPTY);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1/dir2"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
}

TEST_F(dirs, 4)
{
	char buf[PMEMFILE_PATH_MAX];

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1/dir2", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1/dir2/dir3", 0755), 0);

	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/");

	ASSERT_EQ(pmemfile_chdir(pfp, "/dir1"), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir1");

	ASSERT_EQ(pmemfile_chdir(pfp, "/dir1/dir2"), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir1/dir2");

	ASSERT_EQ(pmemfile_chdir(pfp, "/dir1/dir2/dir3"), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir1/dir2/dir3");

	ASSERT_EQ(pmemfile_chdir(pfp, ".."), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir1/dir2");

	ASSERT_EQ(pmemfile_chdir(pfp, ".."), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir1");

	ASSERT_EQ(pmemfile_chdir(pfp, ".."), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/");

	ASSERT_EQ(pmemfile_chdir(pfp, ".."), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/");

	ASSERT_EQ(pmemfile_chdir(pfp, "dir1/.."), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/");

	ASSERT_EQ(pmemfile_chdir(pfp, "dir1"), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir1");

	ASSERT_EQ(pmemfile_chdir(pfp, "dir2"), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir1/dir2");

	ASSERT_EQ(pmemfile_chdir(pfp, "dir3"), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir1/dir2/dir3");

	ASSERT_EQ(pmemfile_chdir(pfp, "."), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir1/dir2/dir3");

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1/dir2/dir3"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1/dir2"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_EQ(errno, ENOENT);

	ASSERT_EQ(pmemfile_chdir(pfp, ".."), 0);
	ASSERT_EQ(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);

	ASSERT_EQ(pmemfile_chdir(pfp, ".."), 0);
	ASSERT_EQ(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);

	ASSERT_EQ(pmemfile_chdir(pfp, ".."), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/");

	ASSERT_EQ(pmemfile_chdir(pfp, "."), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/");

	ASSERT_EQ(pmemfile_chdir(pfp, "./././././"), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/");

	errno = 0;
	ASSERT_EQ(pmemfile_chdir(pfp, "dir1/../"), -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/file", 0, 0777));
	errno = 0;
	ASSERT_EQ(pmemfile_chdir(pfp, "file"), -1);
	EXPECT_EQ(errno, ENOTDIR);

	errno = 0;
	ASSERT_EQ(pmemfile_chdir(pfp, "file/file"), -1);
	EXPECT_EQ(errno, ENOTDIR);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0755), 0);
	PMEMfile *f = pmemfile_open(pfp, "dir1", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ASSERT_EQ(pmemfile_fchdir(pfp, f), 0);
	pmemfile_close(pfp, f);

	errno = 0;
	ASSERT_EQ(pmemfile_getcwd(pfp, buf, 0), nullptr);
	EXPECT_EQ(errno, EINVAL);

	char *t;

	t = pmemfile_getcwd(pfp, NULL, 0);
	ASSERT_NE(t, nullptr);
	ASSERT_STREQ(t, "/dir1");
	free(t);

	t = pmemfile_getcwd(pfp, NULL, 10);
	ASSERT_NE(t, nullptr);
	ASSERT_STREQ(t, "/dir1");
	free(t);

	t = pmemfile_getcwd(pfp, NULL, 2);
	ASSERT_EQ(t, nullptr);
	EXPECT_EQ(errno, ERANGE);

	for (size_t i = 1; i < strlen("/dir1") + 1; ++i) {
		errno = 0;
		ASSERT_EQ(pmemfile_getcwd(pfp, buf, i), nullptr);
		EXPECT_EQ(errno, ERANGE);
	}
	ASSERT_NE(pmemfile_getcwd(pfp, buf, strlen("/dir1") + 1), nullptr);
	ASSERT_STREQ(buf, "/dir1");

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
}

TEST_F(dirs, 5)
{
	struct stat stat;

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0755), 0);
	ASSERT_EQ(pmemfile_chdir(pfp, "/dir1"), 0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "../file1", 0, 0755));
	ASSERT_TRUE(test_pmemfile_create(pfp, "file2", 0, 0755));
	ASSERT_EQ(pmemfile_unlink(pfp, "file2"), 0);
	ASSERT_EQ(pmemfile_link(pfp, "../file1", "file2"), 0);
	ASSERT_EQ(pmemfile_stat(pfp, "file2", &stat), 0);
	ASSERT_EQ(pmemfile_stat(pfp, "../file1", &stat), 0);
	ASSERT_EQ(pmemfile_lstat(pfp, "file2", &stat), 0);
	ASSERT_EQ(pmemfile_lstat(pfp, "../file1", &stat), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "../dir2", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "dir3", 0755), 0);
	ASSERT_EQ(pmemfile_stat(pfp, "/dir2", &stat), 0);
	ASSERT_EQ(pmemfile_stat(pfp, "/dir1/dir3", &stat), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1/dir3/.."), -1);
	EXPECT_EQ(errno, ENOTEMPTY);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1/dir3/."), -1);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1/file2/file"), -1);
	EXPECT_EQ(errno, ENOTDIR);

	ASSERT_EQ(pmemfile_rmdir(pfp, "../dir2"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "dir3"), 0);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/file2"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
	ASSERT_EQ(pmemfile_chdir(pfp, "/"), 0);
}

TEST_F(dirs, 6)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2", 0755), 0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir1/file1", 0, 0755));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir2/file2", 0, 0755));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file3", 0, 0755));

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 4, 4008, "."},
						    {040777, 4, 4008, ".."},
						    {040755, 2, 4008, "dir1"},
						    {040755, 2, 4008, "dir2"},
						    {0100755, 1, 0, "file3"},
					    }));
	EXPECT_TRUE(test_compare_dirs(pfp, "/dir1",
				      std::vector<pmemfile_ls>{
					      {040755, 2, 4008, "."},
					      {040777, 4, 4008, ".."},
					      {0100755, 1, 0, "file1"},
				      }));
	EXPECT_TRUE(test_compare_dirs(pfp, "/dir2",
				      std::vector<pmemfile_ls>{
					      {040755, 2, 4008, "."},
					      {040777, 4, 4008, ".."},
					      {0100755, 1, 0, "file2"},
				      }));

	ASSERT_EQ(pmemfile_rename(pfp, "/file3", "/file4"), 0);
	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 4, 4008, "."},
						    {040777, 4, 4008, ".."},
						    {040755, 2, 4008, "dir1"},
						    {040755, 2, 4008, "dir2"},
						    {0100755, 1, 0, "file4"},
					    }));
	ASSERT_EQ(pmemfile_rename(pfp, "/dir1/file1", "/dir1/file11"), 0);
	EXPECT_TRUE(test_compare_dirs(pfp, "/dir1",
				      std::vector<pmemfile_ls>{
					      {040755, 2, 4008, "."},
					      {040777, 4, 4008, ".."},
					      {0100755, 1, 0, "file11"},
				      }));
	ASSERT_EQ(pmemfile_rename(pfp, "/dir2/file2", "/dir2/file22"), 0);
	EXPECT_TRUE(test_compare_dirs(pfp, "/dir2",
				      std::vector<pmemfile_ls>{
					      {040755, 2, 4008, "."},
					      {040777, 4, 4008, ".."},
					      {0100755, 1, 0, "file22"},
				      }));

	ASSERT_EQ(pmemfile_rename(pfp, "/file4", "/dir2/file4"), 0);
	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 4, 4008, "."},
						    {040777, 4, 4008, ".."},
						    {040755, 2, 4008, "dir1"},
						    {040755, 2, 4008, "dir2"},
					    }));
	EXPECT_TRUE(test_compare_dirs(pfp, "/dir2",
				      std::vector<pmemfile_ls>{
					      {040755, 2, 4008, "."},
					      {040777, 4, 4008, ".."},
					      {0100755, 1, 0, "file4"},
					      {0100755, 1, 0, "file22"},
				      }));
	ASSERT_EQ(pmemfile_rename(pfp, "/dir1/file11", "/dir2/file11"), 0);
	EXPECT_TRUE(
		test_compare_dirs(pfp, "/dir1", std::vector<pmemfile_ls>{
							{040755, 2, 4008, "."},
							{040777, 4, 4008, ".."},
						}));
	EXPECT_TRUE(test_compare_dirs(pfp, "/dir2",
				      std::vector<pmemfile_ls>{
					      {040755, 2, 4008, "."},
					      {040777, 4, 4008, ".."},
					      {0100755, 1, 0, "file4"},
					      {0100755, 1, 0, "file22"},
					      {0100755, 1, 0, "file11"},
				      }));
	ASSERT_EQ(pmemfile_rename(pfp, "/dir2/file11", "/dir2/file22"), 0);
	EXPECT_TRUE(test_compare_dirs(pfp, "/dir2",
				      std::vector<pmemfile_ls>{
					      {040755, 2, 4008, "."},
					      {040777, 4, 4008, ".."},
					      {0100755, 1, 0, "file4"},
					      {0100755, 1, 0, "file22"},
				      }));

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/file22"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/file4"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir2"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/"), -1);
	EXPECT_EQ(errno, EBUSY);
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
