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
 * permissions.cpp -- unit test for pmemfile_chmod, chown & co
 */
#include "pmemfile_test.hpp"
#include <cstring>

class permissions : public pmemfile_test {
public:
	permissions() : pmemfile_test()
	{
	}
};

TEST_F(permissions, chmod)
{
	PMEMfile *f;
	struct stat statbuf;

	ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
						 PMEMFILE_S_IRGRP |
						 PMEMFILE_S_IROTH));
	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
			   PMEMFILE_S_IRGRP | PMEMFILE_S_IROTH));

	ASSERT_EQ(pmemfile_chmod(pfp, "/a_not_exists",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  -1);
	EXPECT_EQ(errno, ENOENT);

	/*
	 * Adding group or other permissions should not change anything
	 * WRT permission checks.
	 */
	for (auto m : {0, PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP,
		       PMEMFILE_S_IROTH | PMEMFILE_S_IWOTH}) {
		SCOPED_TRACE(m);

		/* chmod u+rw */
		ASSERT_EQ(pmemfile_chmod(pfp, "/aaa", PMEMFILE_S_IRUSR |
						 PMEMFILE_S_IWUSR | (mode_t)m),
			  0)
			<< strerror(errno);
		ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
		EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
			  (mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR | m));

		/* open rw */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
		ASSERT_NE(f, nullptr) << strerror(errno);
		pmemfile_close(pfp, f);

		/* open r */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDONLY);
		ASSERT_NE(f, nullptr) << strerror(errno);
		pmemfile_close(pfp, f);

		/* open w */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_WRONLY);
		ASSERT_NE(f, nullptr) << strerror(errno);
		pmemfile_close(pfp, f);
	}

	for (auto m : {0, PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP,
		       PMEMFILE_S_IROTH | PMEMFILE_S_IWOTH}) {
		SCOPED_TRACE(m);

		/* chmod u+r */
		ASSERT_EQ(pmemfile_chmod(pfp, "/aaa", S_IRUSR | (mode_t)m), 0)
			<< strerror(errno);
		ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
		EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
			  (mode_t)(PMEMFILE_S_IRUSR | m));

		/* open rw */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
		ASSERT_EQ(f, nullptr);
		EXPECT_EQ(errno, EACCES);

		/* open r */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDONLY);
		ASSERT_NE(f, nullptr) << strerror(errno);
		pmemfile_close(pfp, f);

		/* open w */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_WRONLY);
		ASSERT_EQ(f, nullptr);
		EXPECT_EQ(errno, EACCES);
	}

	for (auto m : {0, PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP,
		       PMEMFILE_S_IROTH | PMEMFILE_S_IWOTH}) {
		SCOPED_TRACE(m);

		/* chmod u+w */
		ASSERT_EQ(pmemfile_chmod(pfp, "/aaa",
					 PMEMFILE_S_IWUSR | (mode_t)m),
			  0)
			<< strerror(errno);
		ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
		EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
			  (mode_t)(PMEMFILE_S_IWUSR | m));

		/* open rw */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
		ASSERT_EQ(f, nullptr);
		EXPECT_EQ(errno, EACCES);

		/* open r */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDONLY);
		ASSERT_EQ(f, nullptr);
		EXPECT_EQ(errno, EACCES);

		/* open w */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_WRONLY);
		ASSERT_NE(f, nullptr) << strerror(errno);
		pmemfile_close(pfp, f);
	}

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);
}

TEST_F(permissions, symlink)
{
	struct stat statbuf;
	ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
						 PMEMFILE_S_IRGRP |
						 PMEMFILE_S_IROTH));

	ASSERT_EQ(pmemfile_symlink(pfp, "/aaa", "/bbb"), 0);

	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
			   PMEMFILE_S_IRGRP | PMEMFILE_S_IROTH));

	ASSERT_EQ(pmemfile_lstat(pfp, "/bbb", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)(PMEMFILE_S_IRWXU | PMEMFILE_S_IRWXG |
			   PMEMFILE_S_IRWXO));

	ASSERT_EQ(pmemfile_chmod(pfp, "/bbb",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  0);

	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR));

	ASSERT_EQ(pmemfile_lstat(pfp, "/bbb", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)(PMEMFILE_S_IRWXU | PMEMFILE_S_IRWXG |
			   PMEMFILE_S_IRWXO));
}

#define TEST_INITIAL_FSUID ((uid_t)0)
#define TEST_INITIAL_FSGID ((uid_t)0)
#define TEST_FSUID ((uid_t)1000)
#define TEST_FSGID ((uid_t)2000)
#define TEST_FSGID2 ((uid_t)3000)

TEST_F(permissions, fsuid_fsgid_getgroups_setgroups)
{
	PMEMfile *f;
	struct stat statbuf;
	int prev_uid;
	int prev_gid;

	prev_uid = pmemfile_setfsuid(pfp, TEST_FSUID);
	ASSERT_GE(prev_uid, 0) << strerror(errno);
	ASSERT_EQ((uid_t)prev_uid, TEST_INITIAL_FSUID);

	prev_gid = pmemfile_setfsgid(pfp, TEST_FSGID);
	ASSERT_GE(prev_gid, 0) << strerror(errno);
	ASSERT_EQ((uid_t)prev_gid, TEST_INITIAL_FSUID);

	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/aaa", PMEMFILE_O_EXCL, PMEMFILE_S_IRUSR |
			PMEMFILE_S_IWUSR | PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP |
			PMEMFILE_S_IROTH));

	prev_uid = pmemfile_setfsuid(pfp, TEST_INITIAL_FSUID);
	ASSERT_GE(prev_uid, 0) << strerror(errno);
	ASSERT_EQ((uid_t)prev_uid, TEST_FSUID);

	prev_gid = pmemfile_setfsgid(pfp, TEST_INITIAL_FSGID);
	ASSERT_GE(prev_gid, 0) << strerror(errno);
	ASSERT_EQ((uid_t)prev_gid, TEST_FSGID);

	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_uid, TEST_FSUID);
	EXPECT_EQ(statbuf.st_gid, TEST_FSGID);

	ASSERT_EQ(pmemfile_chmod(pfp, "/aaa", PMEMFILE_S_IRUSR), -1);
	EXPECT_EQ(errno, EPERM);

	/* open rw */
	f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
	ASSERT_EQ(f, nullptr);

	/* open r */
	f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	/* open w */
	f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_WRONLY);
	ASSERT_EQ(f, nullptr);

	gid_t l0[1] = {TEST_FSGID2};
	ASSERT_EQ(pmemfile_setgroups(pfp, 1, l0), 0) << strerror(errno);

	/* open rw */
	f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
	ASSERT_EQ(f, nullptr);

	gid_t l1[2] = {TEST_FSGID, TEST_FSGID2};
	ASSERT_EQ(pmemfile_setgroups(pfp, 2, l1), 0) << strerror(errno);

	/* open rw */
	f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	gid_t l2[2] = {0, 0};
	ASSERT_EQ(pmemfile_getgroups(pfp, 0, l2), -1);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_EQ(pmemfile_getgroups(pfp, 1, l2), -1);
	EXPECT_EQ(errno, EINVAL);

	EXPECT_EQ(l2[0], (gid_t)0);
	EXPECT_EQ(l2[1], (gid_t)0);

	ASSERT_EQ(pmemfile_getgroups(pfp, 2, l2), 2);
	EXPECT_EQ(l2[0], TEST_FSGID);
	EXPECT_EQ(l2[1], TEST_FSGID2);

	memset(l2, 0, sizeof(l2));
	ASSERT_EQ(pmemfile_getgroups(pfp, 3, l2), 2);
	EXPECT_EQ(l2[0], TEST_FSGID);
	EXPECT_EQ(l2[1], TEST_FSGID2);
}

TEST_F(permissions, fchmod)
{
	PMEMfile *f;
	struct stat statbuf;

	ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
						 PMEMFILE_S_IRGRP |
						 PMEMFILE_S_IROTH));
	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
			   PMEMFILE_S_IRGRP | PMEMFILE_S_IROTH));

	f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_fchmod(pfp, f, PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
					  PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP |
					  PMEMFILE_S_IROTH),
		  0);

	memset(&statbuf, 0, sizeof(statbuf));
	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
			   PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP |
			   PMEMFILE_S_IROTH));
	pmemfile_close(pfp, f);

	memset(&statbuf, 0, sizeof(statbuf));
	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
			   PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP |
			   PMEMFILE_S_IROTH));
}

TEST_F(permissions, fchmodat)
{
	PMEMfile *dir;
	struct stat statbuf;

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", PMEMFILE_S_IRWXU), 0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir/aaa", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
						 PMEMFILE_S_IRGRP |
						 PMEMFILE_S_IROTH));
	ASSERT_EQ(pmemfile_stat(pfp, "/dir/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
			   PMEMFILE_S_IRGRP | PMEMFILE_S_IROTH));

	dir = pmemfile_open(pfp, "/dir", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(dir, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_fchmodat(pfp, dir, "a", PMEMFILE_ACCESSPERMS, 0),
		  -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_EQ(pmemfile_fchmodat(pfp, dir, "aaa", PMEMFILE_ACCESSPERMS, 0),
		  0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_stat(pfp, "/dir/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (mode_t)PMEMFILE_ACCESSPERMS);

	pmemfile_close(pfp, dir);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/aaa"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir/"), 0);
}

TEST_F(permissions, dirs)
{
	PMEMfile *file;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rwx", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rwx/dir_--x", PMEMFILE_S_IXUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rwx/dir_r--", PMEMFILE_S_IRUSR), 0);
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/dir_rwx/dir_--x/file", PMEMFILE_O_EXCL,
		PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR | PMEMFILE_S_IRGRP |
			PMEMFILE_S_IROTH));

	ASSERT_EQ(pmemfile_chdir(pfp, "/dir_rwx"), 0);
	ASSERT_EQ(pmemfile_chdir(pfp, "/"), 0);

	ASSERT_EQ(pmemfile_chdir(pfp, "/dir_rw-"), -1);
	EXPECT_EQ(errno, EACCES);

	file = pmemfile_open(pfp, "/dir_rwx/dir_--x/file", PMEMFILE_O_RDONLY);
	ASSERT_NE(file, nullptr) << strerror(errno);
	pmemfile_close(pfp, file);

	file = pmemfile_open(pfp, "/dir_rwx/dir_--x",
			     PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	ASSERT_EQ(file, nullptr);
	EXPECT_EQ(errno, EACCES);

	/*
	 * Just to be sure opening the next path without going into
	 * non-executable directory works.
	 */
	file = pmemfile_open(pfp, "/dir_rwx",
			     PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	ASSERT_NE(file, nullptr) << strerror(errno);
	pmemfile_close(pfp, file);

	file = pmemfile_open(pfp, "/dir_rwx/dir_r--/..",
			     PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	ASSERT_EQ(file, nullptr);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_rwx/dir_--x/file"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rwx/dir_--x"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rwx/dir_r--"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rwx"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-"), 0);
}

TEST_F(permissions, mkdir)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-", PMEMFILE_S_IWUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_--x", PMEMFILE_S_IXUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-wx",
				 PMEMFILE_S_IWUSR | PMEMFILE_S_IXUSR),
		  0);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-/dir", PMEMFILE_S_IRWXU), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-/dir", PMEMFILE_S_IRWXU), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_--x/dir", PMEMFILE_S_IRWXU), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-wx/dir", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx/dir"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx"), 0);
}

TEST_F(permissions, rmdir)
{
	/* create directories with all permissions */
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_--x", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-wx", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_r-x", PMEMFILE_S_IRWXU), 0);

	/* create internal directories */
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-/dir", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-/dir", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_--x/dir", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-wx/dir", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_r-x/dir", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);

	/* chmod parent directories to what's in the name */
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_rw-",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_-w-", PMEMFILE_S_IWUSR), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_--x", PMEMFILE_S_IXUSR), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_-wx",
				 PMEMFILE_S_IWUSR | PMEMFILE_S_IXUSR),
		  0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_r-x",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IXUSR),
		  0)
		<< strerror(errno);

	/* setup done, now do the actual test */

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-/dir"), -1);
	EXPECT_EQ(errno, EACCES);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-/dir"), -1);
	EXPECT_EQ(errno, EACCES);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x/dir"), -1);
	EXPECT_EQ(errno, EACCES);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx/dir"), 0) << strerror(errno);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_r-x/dir"), -1);
	EXPECT_EQ(errno, EACCES);

	/* test done */

	/* chmod directories, so we can remove internal directories */
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_rw-", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_-w-", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_--x", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_-wx", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_r-x", PMEMFILE_S_IRWXU), 0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-/dir"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-/dir"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x/dir"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx/dir"), -1);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_r-x/dir"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_r-x"), 0);
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
