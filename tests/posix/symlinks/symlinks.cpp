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
 * symlinks.cpp -- unit test for pmemfile_symlink and co
 */
#include "pmemfile_test.hpp"

class symlinks : public pmemfile_test {
public:
	symlinks() : pmemfile_test()
	{
	}
};

static bool
test_pmemfile_readlink(PMEMfilepool *pfp, const char *pathname,
		       const char *expected)
{
	static char readlink_buf[PMEMFILE_PATH_MAX];

	pmemfile_ssize_t ret = pmemfile_readlink(pfp, pathname, readlink_buf,
						 sizeof(readlink_buf) - 1);
	EXPECT_GT(ret, 0) << pathname << " " << errno << " " << strerror(errno);
	if (ret <= 0)
		return false;
	EXPECT_LT((size_t)ret, sizeof(readlink_buf) - 1);
	if ((size_t)ret >= sizeof(readlink_buf) - 1)
		return false;

	readlink_buf[ret] = 0;

	if (strcmp(readlink_buf, expected) != 0) {
		ADD_FAILURE() << readlink_buf << " " << expected << " "
			      << pathname;
		return false;
	}

	return true;
}

static bool
test_pmemfile_readlinkat(PMEMfilepool *pfp, const char *dirpath,
			 const char *pathname, const char *expected)
{
	static char readlink_buf[PMEMFILE_PATH_MAX];
	PMEMfile *dir = pmemfile_open(pfp, dirpath, PMEMFILE_O_DIRECTORY);
	if (!dir) {
		ADD_FAILURE() << pathname << " " << strerror(errno);
		return false;
	}

	pmemfile_ssize_t ret = pmemfile_readlinkat(
		pfp, dir, pathname, readlink_buf, sizeof(readlink_buf) - 1);
	EXPECT_GT(ret, 0) << pathname << " " << errno << " " << strerror(errno);
	pmemfile_close(pfp, dir);
	if (ret <= 0)
		return false;
	EXPECT_LT((size_t)ret, sizeof(readlink_buf) - 1);
	if ((size_t)ret >= sizeof(readlink_buf) - 1)
		return false;

	readlink_buf[ret] = 0;

	if (strcmp(readlink_buf, expected) != 0) {
		ADD_FAILURE() << readlink_buf << " " << expected << " "
			      << pathname;
		return false;
	}

	return true;
}

TEST_F(symlinks, 0)
{
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file1", 0, 0644));

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0755), 0);

	ASSERT_EQ(pmemfile_symlink(pfp, "/file1", "/dir/sym1-exists"), 0);
	ASSERT_TRUE(test_pmemfile_readlink(pfp, "/dir/sym1-exists", "/file1"));
	ASSERT_TRUE(
		test_pmemfile_readlinkat(pfp, "/dir", "sym1-exists", "/file1"));
	ASSERT_TRUE(test_pmemfile_readlinkat(pfp, "/", "dir/sym1-exists",
					     "/file1"));

	ASSERT_EQ(pmemfile_symlink(pfp, "/file2", "/dir/sym2-not_exists"), 0);
	ASSERT_TRUE(
		test_pmemfile_readlink(pfp, "/dir/sym2-not_exists", "/file2"));
	ASSERT_TRUE(test_pmemfile_readlinkat(pfp, "/dir", "sym2-not_exists",
					     "/file2"));

	ASSERT_EQ(
		pmemfile_symlink(pfp, "../file1", "/dir/sym3-exists-relative"),
		0);
	ASSERT_TRUE(test_pmemfile_readlink(pfp, "/dir/sym3-exists-relative",
					   "../file1"));
	ASSERT_TRUE(test_pmemfile_readlinkat(
		pfp, "/dir", "sym3-exists-relative", "../file1"));

	ASSERT_EQ(pmemfile_symlink(pfp, "../file2",
				   "/dir/sym4-not_exists-relative"),
		  0);
	ASSERT_TRUE(test_pmemfile_readlink(pfp, "/dir/sym4-not_exists-relative",
					   "../file2"));
	ASSERT_TRUE(test_pmemfile_readlinkat(
		pfp, "/dir", "sym4-not_exists-relative", "../file2"));

#ifdef FAULT_INJECTION
	pmemfile_inject_fault_at(PF_MALLOC, 1, "copy_cred");
	ASSERT_FALSE(test_pmemfile_readlinkat(
		pfp, "/dir", "sym4-not_exists-relative", "../file2"));
	EXPECT_EQ(errno, ENOMEM);
#endif

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 3, 4008, "."},
						    {040777, 3, 4008, ".."},
						    {0100644, 1, 0, "file1"},
						    {040755, 2, 4008, "dir"},
					    }));

	EXPECT_TRUE(test_compare_dirs(
		pfp, "/dir",
		std::vector<pmemfile_ls>{
			{040755, 2, 4008, "."},
			{040777, 3, 4008, ".."},
			{0120777, 1, 6, "sym1-exists", "/file1"},
			{0120777, 1, 6, "sym2-not_exists", "/file2"},
			{0120777, 1, 8, "sym3-exists-relative", "../file1"},
			{0120777, 1, 8, "sym4-not_exists-relative", "../file2"},
		}));

	pmemfile_ssize_t ret;

	ret = pmemfile_symlink(pfp, "whatever", "/not-exisiting-dir/xxx");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOENT);

	ret = pmemfile_symlink(pfp, "whatever", "/file1/xxx");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTDIR);

	ret = pmemfile_symlink(pfp, "whatever", "/dir/sym1-exists");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, EEXIST);

	char tmp[4096];
	memset(tmp, '0', 4095);
	tmp[4095] = 0;

	ret = pmemfile_symlink(pfp, tmp, "/dir/lalala");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENAMETOOLONG);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/deleted-dir", 0755), 0);
	PMEMfile *deleted_dir =
		pmemfile_open(pfp, "/deleted-dir", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(deleted_dir, nullptr) << strerror(errno);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/deleted-dir"), 0);

	ret = pmemfile_symlinkat(pfp, "whatever", deleted_dir, "lalala");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOENT);
	pmemfile_close(pfp, deleted_dir);

	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ret = pmemfile_symlinkat(pfp, "whatever", f, "lalala");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTDIR);

	errno = 0;
	ASSERT_EQ(pmemfile_symlinkat(pfp, "whatever", NULL, "lalala"), -1);
	EXPECT_EQ(errno, EFAULT);

	ASSERT_EQ(
		pmemfile_symlinkat(pfp, "whatever", PMEMFILE_AT_CWD, "cwd-sym"),
		0);
	ASSERT_EQ(pmemfile_unlink(pfp, "cwd-sym"), 0);

	char buf[PMEMFILE_PATH_MAX];

	ret = pmemfile_readlink(pfp, "/not-existing-dir/xxx", buf,
				PMEMFILE_PATH_MAX);
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOENT);

	ret = pmemfile_readlink(pfp, "/file1/xxx", buf, PMEMFILE_PATH_MAX);
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTDIR);

	ret = pmemfile_readlink(pfp, "/file1", buf, PMEMFILE_PATH_MAX);
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, EINVAL);

	ret = pmemfile_readlinkat(pfp, f, "lalala", buf, PMEMFILE_PATH_MAX);
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTDIR);

	ret = pmemfile_readlink(pfp, "/dir/sym1-exists/", buf,
				PMEMFILE_PATH_MAX);
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTDIR);

	errno = 0;
	ASSERT_EQ(pmemfile_readlink(pfp, NULL, buf, PMEMFILE_PATH_MAX), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_readlink(NULL, "/dir/sym1-exists", buf,
				    PMEMFILE_PATH_MAX),
		  -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_readlink(pfp, "/dir/sym1-notexists", buf,
				    PMEMFILE_PATH_MAX),
		  -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_readlinkat(pfp, NULL, "dir/sym1-exists", buf,
				      PMEMFILE_PATH_MAX),
		  -1);
	EXPECT_EQ(errno, EFAULT);

	ASSERT_EQ(pmemfile_readlinkat(pfp, PMEMFILE_AT_CWD, "dir/sym1-exists",
				      buf, 2),
		  2);

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/sym1-exists"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/sym2-not_exists"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/sym3-exists-relative"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/sym4-not_exists-relative"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
}

static bool
test_symlink_valid(PMEMfilepool *pfp, const char *path)
{
	char buf[4096];

	memset(buf, 0, sizeof(buf));
	PMEMfile *file = pmemfile_open(pfp, path, PMEMFILE_O_RDONLY);
	EXPECT_NE(file, nullptr) << strerror(errno);
	if (!file)
		return false;

	pmemfile_ssize_t r = pmemfile_read(pfp, file, buf, sizeof(buf));
	if (r != 7) {
		ADD_FAILURE() << r << " " << COND_ERROR(r);
		return false;
	}

	pmemfile_close(pfp, file);

	if (memcmp(buf, "qwerty\n", 7) != 0) {
		ADD_FAILURE() << std::string(buf, 0, 7);
		return false;
	}

	return true;
}

static bool
test_symlink_to_dir_valid(PMEMfilepool *pfp, const char *path)
{
	PMEMfile *file = pmemfile_open(pfp, path, PMEMFILE_O_RDONLY);
	EXPECT_NE(file, nullptr) << strerror(errno);
	if (!file)
		return false;
	pmemfile_close(pfp, file);

	file = pmemfile_open(pfp, path,
			     PMEMFILE_O_RDONLY | PMEMFILE_O_NOFOLLOW);
	EXPECT_EQ(file, nullptr);
	if (file)
		return false;

	EXPECT_EQ(errno, ELOOP);
	if (errno != ELOOP)
		return false;

#if 0
	char buf[1];
	file = pmemfile_open(pfp, path, PMEMFILE_O_RDONLY |
			PMEMFILE_O_NOFOLLOW | PMEMFILE_O_PATH);
	EXPECT_NE(file, nullptr) << strerror(errno);
	if (!file)
		return false;
	pmemfile_ssize_t r = pmemfile_read(pfp, file, buf, sizeof(buf), -1, EBADF);
	if (r != -1)
		return false;
	if (errno != EBADF)
		return false;
	pmemfile_close(pfp, file);
#endif

	return true;
}

static bool
test_symlink_invalid(PMEMfilepool *pfp, const char *path)
{
	PMEMfile *file = pmemfile_open(pfp, path, PMEMFILE_O_RDONLY);
	EXPECT_EQ(file, nullptr);
	EXPECT_EQ(errno, ENOENT);

	return file == nullptr && errno == ENOENT;
}

static bool
test_symlink_loop(PMEMfilepool *pfp, const char *path)
{
	PMEMfile *file = pmemfile_open(pfp, path, PMEMFILE_O_RDONLY);
	EXPECT_EQ(file, nullptr);
	EXPECT_EQ(errno, ELOOP);

	return file == nullptr && errno == ELOOP;
}

TEST_F(symlinks, 1)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1/internal_dir", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2", 0755), 0);

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir1/internal_dir",
				   "/dir2/symlink_dir1"),
		  0);
	ASSERT_EQ(pmemfile_symlink(pfp, "../dir1/internal_dir",
				   "/dir2/symlink_dir2"),
		  0);

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir1/not_existing_dir",
				   "/dir2/symlink_dir3"),
		  0);
	ASSERT_EQ(pmemfile_symlink(pfp, "../not_existing_dir",
				   "/dir2/symlink_dir4"),
		  0);

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir2/symlink_dir1",
				   "/symlink_to_symlink_dir"),
		  0);

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir1", "/dir2/symlink_dir1/dir1"), 0);
	ASSERT_EQ(
		pmemfile_symlink(pfp, "/dir1/", "/dir2/symlink_dir1/dir1slash"),
		0);

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir1/loop", "/loop1"), 0);
	ASSERT_EQ(pmemfile_symlink(pfp, "/loop1", "/dir1/loop"), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_symlink(NULL, "/dir1/loop", "/loop1"), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_symlink(pfp, NULL, "/loop1"), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_symlink(pfp, "/dir1/loop", NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	PMEMfile *file =
		pmemfile_open(pfp, "/dir1/internal_dir/file",
			      PMEMFILE_O_CREAT | PMEMFILE_O_WRONLY, 0644);
	ASSERT_NE(file, nullptr) << strerror(errno);
	pmemfile_ssize_t written = pmemfile_write(pfp, file, "qwerty\n", 7);
	ASSERT_EQ(written, 7) << COND_ERROR(written);
	pmemfile_close(pfp, file);

	ASSERT_TRUE(test_symlink_valid(pfp, "/dir2/symlink_dir1/file"));
	ASSERT_TRUE(test_symlink_valid(pfp, "/dir2/symlink_dir2/file"));
	ASSERT_TRUE(test_symlink_valid(pfp, "/symlink_to_symlink_dir/file"));

	ASSERT_TRUE(test_symlink_to_dir_valid(pfp, "/dir2/symlink_dir1/dir1"));
	ASSERT_TRUE(
		test_symlink_to_dir_valid(pfp, "/dir2/symlink_dir1/dir1slash"));

	ASSERT_TRUE(test_symlink_invalid(pfp, "/dir2/symlink_dir3/file"));
	ASSERT_TRUE(test_symlink_invalid(pfp, "/dir2/symlink_dir4/file"));

	ASSERT_TRUE(test_symlink_loop(pfp, "/loop1/file"));

	ASSERT_EQ(pmemfile_unlink(pfp, "/symlink_to_symlink_dir"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/symlink_dir1/dir1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/symlink_dir1/dir1slash"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/symlink_dir4"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/symlink_dir3"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/symlink_dir2"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/symlink_dir1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/internal_dir/file"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/loop"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/loop1"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir2"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1/internal_dir"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
}

TEST_F(symlinks, 2)
{
	PMEMfile *file = pmemfile_open(
		pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_WRONLY, 0644);
	ASSERT_NE(file, nullptr) << strerror(errno);

	pmemfile_ssize_t written = pmemfile_write(pfp, file, "qwerty\n", 7);
	ASSERT_EQ(written, 7) << COND_ERROR(written);
	pmemfile_close(pfp, file);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0755), 0);

	ASSERT_EQ(pmemfile_symlink(pfp, "/file1", "/dir/sym1-exists"), 0);
	ASSERT_EQ(pmemfile_symlink(pfp, "/file2", "/dir/sym2-not_exists"), 0);
	ASSERT_EQ(
		pmemfile_symlink(pfp, "../file1", "/dir/sym3-exists-relative"),
		0);
	ASSERT_EQ(pmemfile_symlink(pfp, "../file2",
				   "/dir/sym4-not_exists-relative"),
		  0);

	char buf[4096];

	memset(buf, 0, sizeof(buf));
	file = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(file, nullptr) << strerror(errno);

	pmemfile_ssize_t r = pmemfile_read(pfp, file, buf, sizeof(buf));
	ASSERT_EQ(r, 7) << COND_ERROR(r);
	pmemfile_close(pfp, file);
	ASSERT_EQ(memcmp(buf, "qwerty\n", 7), 0);

	ASSERT_TRUE(test_symlink_valid(pfp, "/dir/sym1-exists"));
	ASSERT_TRUE(test_symlink_invalid(pfp, "/dir/sym2-not_exists"));

	ASSERT_TRUE(test_symlink_valid(pfp, "/dir/sym3-exists-relative"));
	ASSERT_TRUE(test_symlink_invalid(pfp, "/dir/sym4-not_exists-relative"));

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/sym1-exists"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/sym2-not_exists"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/sym3-exists-relative"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/sym4-not_exists-relative"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
}

TEST_F(symlinks, 3)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0777), 0);

	PMEMfile *file = pmemfile_open(
		pfp, "/file", PMEMFILE_O_CREAT | PMEMFILE_O_WRONLY, 0644);
	ASSERT_NE(file, nullptr) << strerror(errno);

	pmemfile_ssize_t written = pmemfile_write(pfp, file, "qwerty\n", 7);
	ASSERT_EQ(written, 7) << COND_ERROR(written);
	pmemfile_close(pfp, file);

	ASSERT_EQ(pmemfile_symlink(pfp, "/file", "/dir/symlink"), 0);

	ASSERT_EQ(pmemfile_link(pfp, "/dir/symlink", "/link_to_symlink"), 0);
	ASSERT_EQ(pmemfile_linkat(pfp, NULL, "/dir/symlink", NULL,
				  "/link_to_symlink2", 0),
		  0);
	ASSERT_EQ(pmemfile_linkat(pfp, NULL, "/dir/symlink", NULL,
				  "/link_to_underlying_file",
				  PMEMFILE_AT_SYMLINK_FOLLOW),
		  0);

	EXPECT_TRUE(test_compare_dirs(
		pfp, "/dir", std::vector<pmemfile_ls>{
				     {0040777, 2, 4008, "."},
				     {0040777, 3, 4008, ".."},
				     {0120777, 3, 5, "symlink", "/file"},
			     }));

	EXPECT_TRUE(test_compare_dirs(
		pfp, "/", std::vector<pmemfile_ls>{
				  {0040777, 3, 4008, "."},
				  {0040777, 3, 4008, ".."},
				  {0040777, 2, 4008, "dir"},
				  {0100644, 2, 7, "file"},
				  {0120777, 3, 5, "link_to_symlink", "/file"},
				  {0120777, 3, 5, "link_to_symlink2", "/file"},
				  {0100644, 2, 7, "link_to_underlying_file"},
			  }));

	ASSERT_EQ(pmemfile_unlink(pfp, "/link_to_underlying_file"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/link_to_symlink2"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/link_to_symlink"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/symlink"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
}

static bool
check_path(PMEMfilepool *pfp, int follow_symlink, const char *path,
	   const char *parent, const char *child)
{
	char tmp_path[PMEMFILE_PATH_MAX], dir_path[PMEMFILE_PATH_MAX];

	strncpy(tmp_path, path, PMEMFILE_PATH_MAX);
	tmp_path[PMEMFILE_PATH_MAX - 1] = 0;

	PMEMfile *f = pmemfile_open_parent(
		pfp, PMEMFILE_AT_CWD, tmp_path, PMEMFILE_PATH_MAX,
		follow_symlink ? PMEMFILE_OPEN_PARENT_SYMLINK_FOLLOW : 0);
	EXPECT_NE(f, nullptr) << strerror(errno);
	if (!f)
		return false;

	char *dir_path2 =
		pmemfile_get_dir_path(pfp, f, dir_path, PMEMFILE_PATH_MAX);
	EXPECT_EQ(dir_path2, dir_path);

	if (strcmp(dir_path, parent) != 0) {
		ADD_FAILURE() << dir_path << " " << parent;
		return false;
	}

	if (strcmp(tmp_path, child) != 0) {
		ADD_FAILURE() << tmp_path << " " << child;
		return false;
	}

	pmemfile_close(pfp, f);

	return true;
}

TEST_F(symlinks, 4)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0777), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2", 0777), 0);
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir2/file", 0, 0755));

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir2/file", "/dir1/symlink"), 0);

	ASSERT_TRUE(check_path(pfp, 0, "/dir1/symlink", "/dir1", "symlink"));
	ASSERT_TRUE(check_path(pfp, 1, "/dir1/symlink", "/dir2", "file"));

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/symlink"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/file"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir2"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
}

TEST_F(symlinks, 5)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0777), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2", 0777), 0);

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir2", "/dir1/symlink"), 0);

	char buf[PMEMFILE_PATH_MAX];
	ASSERT_EQ(pmemfile_chdir(pfp, "/dir1/symlink"), 0);
	ASSERT_NE(pmemfile_getcwd(pfp, buf, sizeof(buf)), nullptr);
	ASSERT_STREQ(buf, "/dir2");

	ASSERT_EQ(pmemfile_chdir(pfp, "/"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/symlink"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir2"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
}

TEST_F(symlinks, 6)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0777), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2", 0777), 0);

	ASSERT_EQ(pmemfile_symlink(pfp, "/dir2", "/dir1/symlink"), 0);

	pmemfile_stat_t buf;
	ASSERT_EQ(pmemfile_stat(pfp, "/dir1/symlink", &buf), 0);
	ASSERT_EQ(PMEMFILE_S_ISLNK(buf.st_mode), 0);

	ASSERT_EQ(pmemfile_lstat(pfp, "/dir1/symlink", &buf), 0);
	ASSERT_EQ(PMEMFILE_S_ISLNK(buf.st_mode), 1);

	ASSERT_EQ(pmemfile_fstatat(pfp, NULL, "/dir1/symlink", &buf, 0), 0);
	ASSERT_EQ(PMEMFILE_S_ISLNK(buf.st_mode), 0);

	ASSERT_EQ(pmemfile_fstatat(pfp, NULL, "/dir1/symlink", &buf,
				   PMEMFILE_AT_SYMLINK_NOFOLLOW),
		  0);
	ASSERT_EQ(PMEMFILE_S_ISLNK(buf.st_mode), 1);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/symlink"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir2"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
}

TEST_F(symlinks, creat_excl)
{
	PMEMfile *file;
	pmemfile_stat_t buf;

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0777), 0);

#ifdef FAULT_INJECTION
	pmemfile_inject_fault_at(PF_MALLOC, 1, "copy_cred");
	ASSERT_EQ(pmemfile_symlink(pfp, "../file", "/dir/symlink"), -1);
	EXPECT_EQ(errno, ENOMEM);
#endif

	ASSERT_EQ(pmemfile_symlink(pfp, "../file", "/dir/symlink"), 0);

	file = pmemfile_open(pfp, "/dir/symlink",
			     PMEMFILE_O_CREAT | PMEMFILE_O_EXCL, 0644);
	ASSERT_EQ(file, nullptr);
	EXPECT_EQ(errno, EEXIST);

	ASSERT_EQ(pmemfile_stat(pfp, "/file", &buf), -1);
	EXPECT_EQ(errno, ENOENT);

	file = pmemfile_open(pfp, "/dir/symlink", PMEMFILE_O_CREAT, 0644);
	ASSERT_NE(file, nullptr) << strerror(errno);
	pmemfile_close(pfp, file);

	ASSERT_EQ(pmemfile_stat(pfp, "/file", &buf), 0);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/symlink"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
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
