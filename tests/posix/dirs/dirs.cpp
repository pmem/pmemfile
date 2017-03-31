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
	if (r < 0) {
		EXPECT_GE(r, 0);
		return false;
	}

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

TEST_F(dirs, paths)
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

TEST_F(dirs, lots_of_files)
{
	int ret;
	PMEMfile *f;
	char buf[1001];
	ssize_t written;

	ASSERT_TRUE(test_empty_dir(pfp, "/"));
	memset(buf, 0xff, sizeof(buf));

	for (size_t i = 0; i < 100; ++i) {
		sprintf(buf, "/file%04lu", i);

		f = pmemfile_open(pfp, buf, PMEMFILE_O_CREAT | PMEMFILE_O_EXCL |
					  PMEMFILE_O_WRONLY,
				  0644);
		ASSERT_NE(f, nullptr) << strerror(errno);

		written = pmemfile_write(pfp, f, buf, i);
		ASSERT_EQ(written, (ssize_t)i) << COND_ERROR(written);

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

TEST_F(dirs, mkdir_rmdir_unlink_errors)
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

TEST_F(dirs, mkdirat)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", PMEMFILE_S_IRWXU), 0);

	PMEMfile *dir = pmemfile_open(pfp, "/dir", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(dir, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_mkdirat(pfp, dir, "internal", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdirat(pfp, dir, "../external", PMEMFILE_S_IRWXU),
		  0);

	struct stat statbuf;
	ASSERT_EQ(pmemfile_stat(pfp, "/dir/internal", &statbuf), 0);
	ASSERT_EQ(PMEMFILE_S_ISDIR(statbuf.st_mode), 1);
	ASSERT_EQ(pmemfile_stat(pfp, "/external", &statbuf), 0);
	ASSERT_EQ(PMEMFILE_S_ISDIR(statbuf.st_mode), 1);

	ASSERT_EQ(pmemfile_chdir(pfp, "dir/internal"), 0);

	ASSERT_EQ(pmemfile_mkdirat(pfp, PMEMFILE_AT_CWD,
				   "dir-internal-internal", PMEMFILE_S_IRWXU),
		  0);
	ASSERT_EQ(pmemfile_mkdirat(pfp, PMEMFILE_AT_CWD, "../dir-internal2",
				   PMEMFILE_S_IRWXU),
		  0);
	ASSERT_EQ(pmemfile_mkdirat(pfp, PMEMFILE_AT_CWD, "../../external2",
				   PMEMFILE_S_IRWXU),
		  0);

	ASSERT_EQ(pmemfile_chdir(pfp, "../.."), 0);

	pmemfile_close(pfp, dir);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir/internal/dir-internal-internal"),
		  0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir/dir-internal2"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir/internal"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/external"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/external2"), 0);
}

TEST_F(dirs, unlinkat)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir/internal", PMEMFILE_S_IRWXU), 0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/file1", PMEMFILE_O_EXCL, 0644));

	PMEMfile *dir = pmemfile_open(pfp, "/dir", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(dir, nullptr) << strerror(errno);

	ASSERT_TRUE(
		test_pmemfile_create(pfp, "/dir/file", PMEMFILE_O_EXCL, 0644));

	ASSERT_EQ(pmemfile_unlinkat(pfp, dir, "file", 0), 0);
	ASSERT_EQ(pmemfile_unlinkat(pfp, dir, "../file1", 0), 0);

	ASSERT_EQ(pmemfile_unlinkat(pfp, dir, "internal", 0), -1);
	EXPECT_EQ(errno, EISDIR);

	ASSERT_EQ(
		pmemfile_unlinkat(pfp, dir, "internal", PMEMFILE_AT_REMOVEDIR),
		0);

	pmemfile_close(pfp, dir);
	ASSERT_EQ(pmemfile_unlinkat(pfp, PMEMFILE_AT_CWD, "dir", 0), -1);
	EXPECT_EQ(errno, EISDIR);

	ASSERT_EQ(pmemfile_unlinkat(pfp, PMEMFILE_AT_CWD, "dir",
				    PMEMFILE_AT_REMOVEDIR),
		  0);
}

TEST_F(dirs, rmdir_notempty)
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

TEST_F(dirs, chdir_getcwd)
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

TEST_F(dirs, relative_paths)
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

TEST_F(dirs, file_renames)
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

static bool
is_owned(PMEMfilepool *pfp, const char *path, uid_t owner)
{
	struct stat st;
	memset(&st, 0xff, sizeof(st));

	int r = pmemfile_lstat(pfp, path, &st);
	EXPECT_EQ(r, 0) << strerror(errno);
	if (r)
		return false;

	EXPECT_EQ(st.st_uid, owner);
	if (st.st_uid != owner)
		return false;

	return true;
}

TEST_F(dirs, fchownat)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", PMEMFILE_ACCESSPERMS), 0);
	ASSERT_TRUE(
		test_pmemfile_create(pfp, "/dir/file1", 0, PMEMFILE_S_IRWXU));
	ASSERT_EQ(pmemfile_symlink(pfp, "/dir/file1", "/symlink"), 0);

	PMEMfile *dir = pmemfile_open(pfp, "/dir", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(dir, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_setuid(pfp, 1000), 0);
	ASSERT_EQ(pmemfile_setcap(pfp, PMEMFILE_CAP_CHOWN), 0)
		<< strerror(errno);

	ASSERT_TRUE(is_owned(pfp, "/dir", 0));
	ASSERT_TRUE(is_owned(pfp, "/dir/file1", 0));

	ASSERT_EQ(pmemfile_fchownat(pfp, PMEMFILE_AT_CWD, "dir", 2000, 2000, 0),
		  0);
	ASSERT_TRUE(is_owned(pfp, "/dir", 2000));

	ASSERT_EQ(pmemfile_fchownat(pfp, dir, "", 1000, 1000, 0), -1);
	EXPECT_EQ(errno, ENOENT);
	ASSERT_TRUE(is_owned(pfp, "/dir", 2000));

	ASSERT_EQ(pmemfile_fchownat(pfp, dir, "", 1000, 1000,
				    PMEMFILE_AT_EMPTY_PATH),
		  0);
	ASSERT_TRUE(is_owned(pfp, "/dir", 1000));

	ASSERT_EQ(pmemfile_fchownat(pfp, dir, "file1", 1000, 1000, 0), 0);
	ASSERT_TRUE(is_owned(pfp, "/dir/file1", 1000));

	ASSERT_EQ(pmemfile_fchownat(pfp, PMEMFILE_AT_CWD, "symlink", 1001, 1001,
				    0),
		  0);
	ASSERT_TRUE(is_owned(pfp, "/symlink", 0));
	ASSERT_TRUE(is_owned(pfp, "/dir/file1", 1001));

	ASSERT_EQ(pmemfile_fchownat(pfp, PMEMFILE_AT_CWD, "symlink", 1002, 1002,
				    PMEMFILE_AT_SYMLINK_NOFOLLOW),
		  0);
	ASSERT_TRUE(is_owned(pfp, "/symlink", 1002));
	ASSERT_TRUE(is_owned(pfp, "/dir/file1", 1001));

	ASSERT_EQ(pmemfile_clrcap(pfp, PMEMFILE_CAP_CHOWN), 0)
		<< strerror(errno);

	pmemfile_close(pfp, dir);

	ASSERT_EQ(pmemfile_unlink(pfp, "/symlink"), 0) << strerror(errno);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file1"), 0) << strerror(errno);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
}

TEST_F(dirs, openat)
{
	PMEMfile *dir, *f;

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2", PMEMFILE_S_IRWXU), 0);
	ASSERT_TRUE(
		test_pmemfile_create(pfp, "/dir/file1", 0, PMEMFILE_S_IRWXU));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file2", 0, PMEMFILE_S_IRWXU));

	dir = pmemfile_open(pfp, "/dir", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(dir, nullptr) << strerror(errno);

	f = pmemfile_openat(pfp, dir, "file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	f = pmemfile_openat(pfp, dir, "file2", PMEMFILE_O_RDONLY);
	ASSERT_EQ(f, nullptr);
	EXPECT_EQ(errno, ENOENT);

	f = pmemfile_openat(pfp, dir, "../file2", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	f = pmemfile_openat(pfp, PMEMFILE_AT_CWD, "file1", PMEMFILE_O_RDONLY);
	ASSERT_EQ(f, nullptr);
	EXPECT_EQ(errno, ENOENT);

	f = pmemfile_openat(pfp, PMEMFILE_AT_CWD, "dir/file1",
			    PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	f = pmemfile_openat(pfp, PMEMFILE_AT_CWD, "file2", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_chdir(pfp, "dir2"), 0);

	f = pmemfile_openat(pfp, PMEMFILE_AT_CWD, "file1", PMEMFILE_O_RDONLY);
	ASSERT_EQ(f, nullptr);
	EXPECT_EQ(errno, ENOENT);

	f = pmemfile_openat(pfp, PMEMFILE_AT_CWD, "dir/file1",
			    PMEMFILE_O_RDONLY);
	ASSERT_EQ(f, nullptr);
	EXPECT_EQ(errno, ENOENT);

	f = pmemfile_openat(pfp, PMEMFILE_AT_CWD, "file2", PMEMFILE_O_RDONLY);
	ASSERT_EQ(f, nullptr);
	EXPECT_EQ(errno, ENOENT);

	f = pmemfile_openat(pfp, PMEMFILE_AT_CWD, "/dir/file1",
			    PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	f = pmemfile_openat(pfp, PMEMFILE_AT_CWD, "/file2", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	pmemfile_close(pfp, dir);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file2"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file1"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir2"), 0);
}

static bool
test_file_info(PMEMfilepool *pfp, const char *path, nlink_t nlink, ino_t ino)
{
	struct stat st;
	memset(&st, 0, sizeof(st));

	int r = pmemfile_lstat(pfp, path, &st);

	EXPECT_EQ(r, 0) << strerror(errno);
	if (r)
		return false;

	EXPECT_EQ(st.st_nlink, nlink);
	EXPECT_EQ(st.st_ino, ino);
	if (st.st_nlink != nlink || st.st_ino != ino)
		return false;

	return true;
}

TEST_F(dirs, linkat)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2", PMEMFILE_S_IRWXU), 0);

	ASSERT_TRUE(
		test_pmemfile_create(pfp, "/dir1/file1", 0, PMEMFILE_S_IRWXU));
	ASSERT_TRUE(
		test_pmemfile_create(pfp, "/dir2/file2", 0, PMEMFILE_S_IRWXU));

	struct stat st_file1, st_file2, st_file1_sym;
	ASSERT_EQ(pmemfile_lstat(pfp, "/dir1/file1", &st_file1), 0);
	ASSERT_EQ(pmemfile_lstat(pfp, "/dir2/file2", &st_file2), 0);

	ASSERT_TRUE(test_file_info(pfp, "/dir1/file1", 1, st_file1.st_ino));
	ASSERT_TRUE(test_file_info(pfp, "/dir2/file2", 1, st_file2.st_ino));

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir1/file1", "/dir2/file1-sym"), 0);

	ASSERT_EQ(pmemfile_lstat(pfp, "/dir2/file1-sym", &st_file1_sym), 0);

	PMEMfile *dir1 = pmemfile_open(pfp, "/dir1", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(dir1, nullptr) << strerror(errno);

	PMEMfile *dir2 = pmemfile_open(pfp, "/dir2", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(dir2, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_linkat(pfp, dir1, "file1", dir2, "file1", 0), 0);
	ASSERT_TRUE(test_file_info(pfp, "/dir1/file1", 2, st_file1.st_ino));
	ASSERT_TRUE(test_file_info(pfp, "/dir2/file1", 2, st_file1.st_ino));

	ASSERT_EQ(pmemfile_linkat(pfp, dir1, "file1", PMEMFILE_AT_CWD, "file1",
				  0),
		  0);
	ASSERT_TRUE(test_file_info(pfp, "/dir1/file1", 3, st_file1.st_ino));
	ASSERT_TRUE(test_file_info(pfp, "/dir2/file1", 3, st_file1.st_ino));
	ASSERT_TRUE(test_file_info(pfp, "/file1", 3, st_file1.st_ino));

	ASSERT_TRUE(
		test_file_info(pfp, "/dir2/file1-sym", 1, st_file1_sym.st_ino));
	ASSERT_EQ(pmemfile_linkat(pfp, dir2, "file1-sym", dir1,
				  "file1-link-to-symlink", 0),
		  0);
	ASSERT_TRUE(
		test_file_info(pfp, "/dir2/file1-sym", 2, st_file1_sym.st_ino));

	ASSERT_EQ(pmemfile_linkat(pfp, dir2, "file1-sym", dir1,
				  "file1-link-to-deref-symlink",
				  PMEMFILE_AT_SYMLINK_FOLLOW),
		  0);
	ASSERT_TRUE(
		test_file_info(pfp, "/dir2/file1-sym", 2, st_file1_sym.st_ino));
	ASSERT_TRUE(test_file_info(pfp, "/dir1/file1-link-to-deref-symlink", 4,
				   st_file1.st_ino));
	ASSERT_TRUE(test_file_info(pfp, "/dir1/file1", 4, st_file1.st_ino));

	ASSERT_EQ(pmemfile_linkat(pfp, dir1, "", dir2, "XXX",
				  PMEMFILE_AT_EMPTY_PATH),
		  -1);
	EXPECT_EQ(errno, EPERM);

	PMEMfile *file1 = pmemfile_open(pfp, "/dir1/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(file1, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_linkat(pfp, file1, "", dir2,
				  "file1-linked-at-empty-path",
				  PMEMFILE_AT_EMPTY_PATH),
		  0);

	ASSERT_TRUE(test_file_info(pfp, "/dir1/file1", 5, st_file1.st_ino));
	ASSERT_TRUE(test_file_info(pfp, "/dir2/file1-linked-at-empty-path", 5,
				   st_file1.st_ino));

	pmemfile_close(pfp, file1);
	pmemfile_close(pfp, dir1);
	pmemfile_close(pfp, dir2);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/file1-link-to-deref-symlink"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/file1-link-to-symlink"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/file1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/file2"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/file1-linked-at-empty-path"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/file1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/file1-sym"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir2"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
}

/*
 * Test file handles created with O_PATH for all functions that accept
 * PMEMfile*. O_PATH allows to create file handles for files user does
 * not have read or write permissions. Such handles are supposed to be
 * used only as a path reference, but if we won't enforce that it may
 * become a security issue.
 */
TEST_F(dirs, O_PATH)
{
	char buf[4096];
	memset(buf, 0, sizeof(buf));

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", PMEMFILE_S_IRWXU), 0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir/file", 0, 0));
	ASSERT_EQ(pmemfile_symlink(pfp, "/dir/file", "/dir/symlink"), 0);

	ASSERT_EQ(pmemfile_chmod(pfp, "/dir", PMEMFILE_S_IXUSR), 0);

	ASSERT_EQ(pmemfile_open(pfp, "/dir", 0), nullptr);
	EXPECT_EQ(errno, EACCES);

	PMEMfile *dir = pmemfile_open(
		pfp, "/dir", PMEMFILE_O_DIRECTORY /*ignored*/ |
			PMEMFILE_O_RDWR /*ignored*/ | PMEMFILE_O_PATH);
	ASSERT_NE(dir, nullptr);

	ASSERT_EQ(pmemfile_getdents(pfp, dir, (struct linux_dirent *)buf,
				    sizeof(buf)),
		  -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_getdents64(pfp, dir, (struct linux_dirent64 *)buf,
				      sizeof(buf)),
		  -1);
	EXPECT_EQ(errno, EBADF);

	PMEMfile *file =
		pmemfile_open(pfp, "/dir/file",
			      PMEMFILE_O_RDWR /*ignored*/ | PMEMFILE_O_PATH);
	ASSERT_NE(file, nullptr);

	ASSERT_EQ(pmemfile_read(pfp, file, buf, 10), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_pread(pfp, file, buf, 10, 0), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_write(pfp, file, buf, 10), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_pwrite(pfp, file, buf, 10, 0), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_lseek(pfp, file, 1, PMEMFILE_SEEK_SET), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_fchmodat(pfp, dir, "file",
				    PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR, 0),
		  0);

	PMEMfile *file2 = pmemfile_openat(pfp, dir, "file", PMEMFILE_O_RDWR);
	ASSERT_NE(file2, nullptr) << strerror(errno);

	memset(buf, 0xff, 10);
	ASSERT_EQ(pmemfile_write(pfp, file2, buf, 10), 10);
	ASSERT_EQ(pmemfile_lseek(pfp, file2, 0, PMEMFILE_SEEK_SET), 0);
	ASSERT_EQ(pmemfile_read(pfp, file2, &buf[100], 10), 10);
	EXPECT_EQ(memcmp(&buf[0], &buf[100], 10), 0);

	pmemfile_close(pfp, file2);

	struct stat st;

	memset(&st, 0xff, sizeof(st));
	ASSERT_EQ(pmemfile_fstat(pfp, file, &st), 0);
	EXPECT_EQ(st.st_size, 10);

	memset(&st, 0xff, sizeof(st));
	ASSERT_EQ(pmemfile_fstatat(pfp, dir, "file", &st, 0), 0);
	EXPECT_EQ(st.st_size, 10);

	memset(&st, 0xff, sizeof(st));
	ASSERT_EQ(pmemfile_fstatat(pfp, file, "", &st, PMEMFILE_AT_EMPTY_PATH),
		  0);
	EXPECT_EQ(st.st_size, 10);

	ASSERT_EQ(
		pmemfile_linkat(pfp, dir, "file", PMEMFILE_AT_CWD, "file1", 0),
		0);
	ASSERT_EQ(pmemfile_linkat(pfp, file, "", PMEMFILE_AT_CWD, "file2",
				  PMEMFILE_AT_EMPTY_PATH),
		  0);

	ASSERT_EQ(pmemfile_unlinkat(pfp, dir, "file", 0), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_mkdirat(pfp, dir, "dir2", 0), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_fchmod(pfp, file, PMEMFILE_S_IRWXU), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_fchmodat(pfp, dir, "file", PMEMFILE_S_IRWXU, 0), 0);

	ASSERT_EQ(pmemfile_fchown(pfp, file, 0, 0), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_fchownat(pfp, dir, "file", 0, 0, 0), 0);

	ASSERT_EQ(
		pmemfile_fchownat(pfp, file, "", 0, 0, PMEMFILE_AT_EMPTY_PATH),
		0);

	ASSERT_EQ(pmemfile_faccessat(pfp, dir, "file", PMEMFILE_W_OK, 0), 0);

	ASSERT_EQ(pmemfile_ftruncate(pfp, file, 0), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_fallocate(pfp, file, 0, 0, 1), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_symlinkat(pfp, "/file1", dir, "fileXXX"), -1);
	EXPECT_EQ(errno, EACCES);

	ssize_t r = pmemfile_readlinkat(pfp, dir, "symlink", buf, sizeof(buf));
	EXPECT_GT(r, 0);
	if (r > 0)
		EXPECT_EQ((size_t)r, strlen("/dir/file"));

	EXPECT_EQ(pmemfile_fcntl(pfp, dir, PMEMFILE_F_GETFL), PMEMFILE_O_PATH);
	EXPECT_EQ(pmemfile_fcntl(pfp, file, PMEMFILE_F_GETFL), PMEMFILE_O_PATH);

	EXPECT_EQ(pmemfile_fcntl(pfp, file, PMEMFILE_F_SETLK), -1);
	EXPECT_EQ(errno, EBADF);

	EXPECT_EQ(pmemfile_fcntl(pfp, file, PMEMFILE_F_UNLCK), -1);
	EXPECT_EQ(errno, EBADF);

	ASSERT_EQ(pmemfile_fchdir(pfp, dir), 0);
	ASSERT_EQ(pmemfile_access(pfp, "file", PMEMFILE_R_OK), 0);

	ASSERT_EQ(pmemfile_chdir(pfp, ".."), 0);

	pmemfile_close(pfp, dir);
	pmemfile_close(pfp, file);

	ASSERT_EQ(pmemfile_chmod(pfp, "/dir", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/symlink"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file2"), 0);
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
