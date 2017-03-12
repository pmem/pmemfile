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
		  (pmemfile_mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
				    PMEMFILE_S_IRGRP | PMEMFILE_S_IROTH));

	errno = 0;
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
						 PMEMFILE_S_IWUSR |
						 (pmemfile_mode_t)m),
			  0)
			<< strerror(errno);
		ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
		EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
			  (pmemfile_mode_t)(PMEMFILE_S_IRUSR |
					    PMEMFILE_S_IWUSR | m));

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
		ASSERT_EQ(pmemfile_chmod(pfp, "/aaa",
					 PMEMFILE_S_IRUSR | (pmemfile_mode_t)m),
			  0)
			<< strerror(errno);
		ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
		EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
			  (pmemfile_mode_t)(PMEMFILE_S_IRUSR | m));

		/* open rw */
		errno = 0;
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
		ASSERT_EQ(f, nullptr);
		EXPECT_EQ(errno, EACCES);

		/* open r */
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDONLY);
		ASSERT_NE(f, nullptr) << strerror(errno);
		pmemfile_close(pfp, f);

		/* open w */
		errno = 0;
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_WRONLY);
		ASSERT_EQ(f, nullptr);
		EXPECT_EQ(errno, EACCES);
	}

	for (auto m : {0, PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP,
		       PMEMFILE_S_IROTH | PMEMFILE_S_IWOTH}) {
		SCOPED_TRACE(m);

		/* chmod u+w */
		ASSERT_EQ(pmemfile_chmod(pfp, "/aaa",
					 PMEMFILE_S_IWUSR | (pmemfile_mode_t)m),
			  0)
			<< strerror(errno);
		ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
		EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
			  (pmemfile_mode_t)(PMEMFILE_S_IWUSR | m));

		/* open rw */
		errno = 0;
		f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
		ASSERT_EQ(f, nullptr);
		EXPECT_EQ(errno, EACCES);

		/* open r */
		errno = 0;
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
		  (pmemfile_mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
				    PMEMFILE_S_IRGRP | PMEMFILE_S_IROTH));

	ASSERT_EQ(pmemfile_lstat(pfp, "/bbb", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (pmemfile_mode_t)(PMEMFILE_S_IRWXU | PMEMFILE_S_IRWXG |
				    PMEMFILE_S_IRWXO));

	ASSERT_EQ(pmemfile_chmod(pfp, "/bbb",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  0);

	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (pmemfile_mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR));

	ASSERT_EQ(pmemfile_lstat(pfp, "/bbb", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (pmemfile_mode_t)(PMEMFILE_S_IRWXU | PMEMFILE_S_IRWXG |
				    PMEMFILE_S_IRWXO));

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/bbb"), 0);
}

#define TEST_EUID ((pmemfile_uid_t)1000)
#define TEST_EGID ((pmemfile_uid_t)2000)

#define TEST_FSUID ((pmemfile_uid_t)5000)
#define TEST_FSGID ((pmemfile_gid_t)6000)

#define TEST_SUPP_GID ((pmemfile_uid_t)3000)

TEST_F(permissions, reuid_regid_fsuid_fsgid_getgroups_setgroups)
{
	PMEMfile *f;
	struct stat statbuf;
	int prev_uid;
	int prev_gid;

	ASSERT_EQ(pmemfile_setreuid(pfp, TEST_EUID, TEST_EUID), 0);
	ASSERT_EQ(pmemfile_setregid(pfp, TEST_EGID, TEST_EGID), 0);

	prev_uid = pmemfile_setfsuid(pfp, TEST_FSUID);
	ASSERT_GE(prev_uid, 0) << strerror(errno);
	ASSERT_EQ((pmemfile_uid_t)prev_uid, TEST_EUID);

	prev_gid = pmemfile_setfsgid(pfp, TEST_FSGID);
	ASSERT_GE(prev_gid, 0) << strerror(errno);
	ASSERT_EQ((pmemfile_uid_t)prev_gid, TEST_EGID);

	prev_uid = pmemfile_setfsuid(pfp, TEST_EUID);
	ASSERT_GE(prev_uid, 0) << strerror(errno);
	ASSERT_EQ((pmemfile_uid_t)prev_uid, TEST_FSUID);

	prev_gid = pmemfile_setfsgid(pfp, TEST_EGID);
	ASSERT_GE(prev_gid, 0) << strerror(errno);
	ASSERT_EQ((pmemfile_uid_t)prev_gid, TEST_FSGID);

	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/aaa", PMEMFILE_O_EXCL, PMEMFILE_S_IRUSR |
			PMEMFILE_S_IWUSR | PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP |
			PMEMFILE_S_IROTH));

	prev_uid = pmemfile_setfsuid(pfp, TEST_FSUID);
	ASSERT_GE(prev_uid, 0) << strerror(errno);
	ASSERT_EQ((pmemfile_uid_t)prev_uid, TEST_EUID);

	prev_gid = pmemfile_setfsgid(pfp, TEST_FSGID);
	ASSERT_GE(prev_gid, 0) << strerror(errno);
	ASSERT_EQ((pmemfile_uid_t)prev_gid, TEST_EGID);

	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_uid, TEST_EUID);
	EXPECT_EQ(statbuf.st_gid, TEST_EGID);

	errno = 0;
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

	pmemfile_gid_t l0[1] = {TEST_SUPP_GID};
	ASSERT_EQ(pmemfile_setgroups(pfp, 1, l0), 0) << strerror(errno);

	/* open rw */
	f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
	ASSERT_EQ(f, nullptr);

	pmemfile_gid_t l1[2] = {TEST_EGID, TEST_SUPP_GID};
	ASSERT_EQ(pmemfile_setgroups(pfp, 2, l1), 0) << strerror(errno);

	/* open rw */
	f = pmemfile_open(pfp, "/aaa", PMEMFILE_O_RDWR);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_close(pfp, f);

	pmemfile_gid_t l2[2] = {0, 0};
	errno = 0;
	ASSERT_EQ(pmemfile_getgroups(pfp, 0, l2), -1);
	EXPECT_EQ(errno, EINVAL);

	errno = 0;
	ASSERT_EQ(pmemfile_getgroups(pfp, 1, l2), -1);
	EXPECT_EQ(errno, EINVAL);

	EXPECT_EQ(l2[0], (pmemfile_gid_t)0);
	EXPECT_EQ(l2[1], (pmemfile_gid_t)0);

	ASSERT_EQ(pmemfile_getgroups(pfp, 2, l2), 2);
	EXPECT_EQ(l2[0], TEST_EGID);
	EXPECT_EQ(l2[1], TEST_SUPP_GID);

	memset(l2, 0, sizeof(l2));
	ASSERT_EQ(pmemfile_getgroups(pfp, 3, l2), 2);

	EXPECT_EQ(l2[0], TEST_EGID);
	EXPECT_EQ(l2[1], TEST_SUPP_GID);

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);
}

TEST_F(permissions, chmod_and_cap)
{
	ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));

	ASSERT_EQ(pmemfile_setfsuid(pfp, 1000), 0);

	ASSERT_EQ(pmemfile_chmod(pfp, "/aaa", PMEMFILE_S_IRUSR), -1);
	EXPECT_EQ(errno, EPERM);

	ASSERT_EQ(pmemfile_setcap(pfp, PMEMFILE_CAP_FOWNER), 0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_chmod(pfp, "/aaa", PMEMFILE_S_IRUSR), 0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_clrcap(pfp, PMEMFILE_CAP_FOWNER), 0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);
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
		  (pmemfile_mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
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
		  (pmemfile_mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
				    PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP |
				    PMEMFILE_S_IROTH));
	pmemfile_close(pfp, f);

	memset(&statbuf, 0, sizeof(statbuf));
	ASSERT_EQ(pmemfile_stat(pfp, "/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (pmemfile_mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
				    PMEMFILE_S_IRGRP | PMEMFILE_S_IWGRP |
				    PMEMFILE_S_IROTH));

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);
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
		  (pmemfile_mode_t)(PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR |
				    PMEMFILE_S_IRGRP | PMEMFILE_S_IROTH));

	dir = pmemfile_open(pfp, "/dir", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(dir, nullptr) << strerror(errno);

	errno = 0;
	ASSERT_EQ(pmemfile_fchmodat(pfp, dir, "a", PMEMFILE_ACCESSPERMS, 0),
		  -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_EQ(pmemfile_fchmodat(pfp, dir, "aaa", PMEMFILE_ACCESSPERMS, 0),
		  0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_stat(pfp, "/dir/aaa", &statbuf), 0);
	EXPECT_EQ(statbuf.st_mode & PMEMFILE_ALLPERMS,
		  (pmemfile_mode_t)PMEMFILE_ACCESSPERMS);

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
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rwx/dir_--x", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rwx/dir_r--", PMEMFILE_S_IRUSR), 0);
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/dir_rwx/dir_--x/file", PMEMFILE_O_EXCL,
		PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR | PMEMFILE_S_IRGRP |
			PMEMFILE_S_IROTH));
	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_rwx/dir_--x", PMEMFILE_S_IXUSR), 0);

	ASSERT_EQ(pmemfile_chdir(pfp, "/dir_rwx"), 0);
	ASSERT_EQ(pmemfile_chdir(pfp, "/"), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_chdir(pfp, "/dir_rw-"), -1);
	EXPECT_EQ(errno, EACCES);

	file = pmemfile_open(pfp, "/dir_rwx/dir_--x/file", PMEMFILE_O_RDONLY);
	ASSERT_NE(file, nullptr) << strerror(errno);
	pmemfile_close(pfp, file);

	errno = 0;
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

	errno = 0;
	file = pmemfile_open(pfp, "/dir_rwx/dir_r--/..",
			     PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	ASSERT_EQ(file, nullptr);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_chmod(pfp, "/dir_rwx/dir_--x", PMEMFILE_S_IRWXU), 0);
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

	errno = 0;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-/dir", PMEMFILE_S_IRWXU), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-/dir", PMEMFILE_S_IRWXU), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
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

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-/dir"), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-/dir"), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x/dir"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx/dir"), 0) << strerror(errno);

	errno = 0;
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

TEST_F(permissions, link)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-", PMEMFILE_S_IWUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_--x", PMEMFILE_S_IXUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-wx",
				 PMEMFILE_S_IWUSR | PMEMFILE_S_IXUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_r-x",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IXUSR),
		  0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));

	errno = 0;
	ASSERT_EQ(pmemfile_link(pfp, "/aaa", "/dir_rw-/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_link(pfp, "/aaa", "/dir_-w-/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_link(pfp, "/aaa", "/dir_--x/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_link(pfp, "/aaa", "/dir_-wx/aaa"), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_link(pfp, "/aaa", "/dir_r-x/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_-wx/aaa"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_r-x"), 0);
}

TEST_F(permissions, symlink2)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-", PMEMFILE_S_IWUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_--x", PMEMFILE_S_IXUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-wx",
				 PMEMFILE_S_IWUSR | PMEMFILE_S_IXUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_r-x",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IXUSR),
		  0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));

	errno = 0;
	ASSERT_EQ(pmemfile_symlink(pfp, "/aaa", "/dir_rw-/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_symlink(pfp, "/aaa", "/dir_-w-/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_symlink(pfp, "/aaa", "/dir_--x/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_symlink(pfp, "/aaa", "/dir_-wx/aaa"), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_symlink(pfp, "/aaa", "/dir_r-x/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_-wx/aaa"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_r-x"), 0);
}

TEST_F(permissions, create)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-", PMEMFILE_S_IWUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_--x", PMEMFILE_S_IXUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-wx",
				 PMEMFILE_S_IWUSR | PMEMFILE_S_IXUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_r-x",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IXUSR),
		  0);

	errno = 0;
	ASSERT_EQ(pmemfile_open(pfp, "/dir_rw-/aaa",
				PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
				PMEMFILE_S_IRWXU),
		  nullptr);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_open(pfp, "/dir_-w-/aaa",
				PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
				PMEMFILE_S_IRWXU),
		  nullptr);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_open(pfp, "/dir_--x/aaa",
				PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
				PMEMFILE_S_IRWXU),
		  nullptr);
	EXPECT_EQ(errno, EACCES);

	PMEMfile *file = pmemfile_open(pfp, "/dir_-wx/aaa",
				       PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
				       PMEMFILE_S_IRWXU);
	ASSERT_NE(file, nullptr) << strerror(errno);
	pmemfile_close(pfp, file);

	errno = 0;
	ASSERT_EQ(pmemfile_open(pfp, "/dir_r-x/aaa",
				PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
				PMEMFILE_S_IRWXU),
		  nullptr);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_-wx/aaa"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_r-x"), 0);
}

TEST_F(permissions, unlink)
{
	/* create directories with all permissions */
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_--x", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-wx", PMEMFILE_S_IRWXU), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_r-x", PMEMFILE_S_IRWXU), 0);

	/* create files */
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir_rw-/file", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir_-w-/file", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir_--x/file", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir_-wx/file", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir_r-x/file", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));

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

	errno = 0;
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_rw-/file"), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_-w-/file"), -1);
	EXPECT_EQ(errno, EACCES);

	errno = 0;
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_--x/file"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_-wx/file"), 0) << strerror(errno);

	errno = 0;
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_r-x/file"), -1);
	EXPECT_EQ(errno, EACCES);

	/* test done */

	/* chmod directories, so we can remove files */
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

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_rw-/file"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_-w-/file"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_--x/file"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_-wx/file"), -1);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir_r-x/file"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_r-x"), 0);
}

TEST_F(permissions, rename)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_rw-",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IWUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-w-", PMEMFILE_S_IWUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_--x", PMEMFILE_S_IXUSR), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_-wx",
				 PMEMFILE_S_IWUSR | PMEMFILE_S_IXUSR),
		  0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir_r-x",
				 PMEMFILE_S_IRUSR | PMEMFILE_S_IXUSR),
		  0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));

	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_rw-/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_-w-/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_--x/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_-wx/aaa"), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_rename(pfp, "/dir_-wx/aaa", "/aaa"), 0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_r-x/aaa"), -1);
	EXPECT_EQ(errno, EACCES);

/* XXX: rename implementation is not fully functional */
#if 0
	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_rw-"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_-w-"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_--x"), -1);
	EXPECT_EQ(errno, EACCES);

	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_-wx"), 0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_rename(pfp, "/dir_-wx", "/aaa"), 0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_rename(pfp, "/aaa", "/dir_r-x"), -1);
	EXPECT_EQ(errno, EACCES);
#endif

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_rw-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-w-"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_--x"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_-wx"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir_r-x"), 0);
}

static bool
test_chown(PMEMfilepool *pfp, const char *pathname, pmemfile_uid_t owner,
	   pmemfile_gid_t group, int error)
{
	int r = pmemfile_chown(pfp, pathname, owner, group);
	if (error) {
		EXPECT_EQ(r, -1);
		EXPECT_EQ(errno, error);
		if (r != -1 || errno != error)
			return false;
	} else {
		EXPECT_EQ(r, 0) << strerror(errno);
		if (r != 0)
			return false;

		struct stat s;
		memset(&s, 0, sizeof(s));
		if (pmemfile_stat(pfp, pathname, &s)) {
			ADD_FAILURE() << "stat failed " << strerror(errno);
			return false;
		}

		if (owner != (pmemfile_uid_t)-1) {
			EXPECT_EQ(s.st_uid, owner);
			if (s.st_uid != owner)
				return false;
		}

		if (group != (pmemfile_gid_t)-1) {
			EXPECT_EQ(s.st_gid, group);
			if (s.st_gid != group)
				return false;
		}
	}

	return true;
}

TEST_F(permissions, chown)
{
	struct stat s;

	ASSERT_TRUE(test_pmemfile_create(pfp, "/file", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file0", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));

	/* ruid=euid=fsuid=0, rgid=egid=fsgid=0 */

	ASSERT_TRUE(test_chown(pfp, "/file", 0, 0, 0));
	ASSERT_TRUE(test_chown(pfp, "/file", (pmemfile_uid_t)-1, 0, 0));
	ASSERT_TRUE(test_chown(pfp, "/file", 0, (pmemfile_gid_t)-1, 0));
	ASSERT_TRUE(test_chown(pfp, "/file", (pmemfile_uid_t)-1,
			       (pmemfile_gid_t)-1, 0));

	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 0, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", 0, 1001, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1001, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", (pmemfile_uid_t)-1, 1001, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1002, EPERM));

	ASSERT_EQ(pmemfile_setreuid(pfp, 1000, 1000), 0);

	/* ruid=euid=fsuid=1000, rgid=egid=fsgid=0 */
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 0, EPERM));

	ASSERT_EQ(pmemfile_setcap(pfp, PMEMFILE_CAP_CHOWN), 0)
		<< strerror(errno);

	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 0, 0));

	ASSERT_EQ(pmemfile_clrcap(pfp, PMEMFILE_CAP_CHOWN), 0)
		<< strerror(errno);

	ASSERT_TRUE(test_chown(pfp, "/file", 0, 1001, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1000, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1001, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", (pmemfile_uid_t)-1, 1001, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1002, EPERM));

	ASSERT_EQ(pmemfile_setfsgid(pfp, 1001), 0);

	/* ruid=euid=fsuid=1000, rgid=egid=0, fsgid=1001 */

	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 0, 0));
	ASSERT_TRUE(test_chown(pfp, "/file", 0, 1001, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1000, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1001, 0));
	ASSERT_TRUE(test_chown(pfp, "/file", (pmemfile_uid_t)-1, 1001, 0));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1002, EPERM));

	pmemfile_gid_t groups[1] = {1002};
	ASSERT_EQ(pmemfile_setgroups(pfp, 1, groups), 0);

	/* ruid=euid=fsuid=1000, rgid=egid=0, fsgid=1001, gids=1002 */

	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1003, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1002, 0));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1001, 0));
	ASSERT_TRUE(test_chown(pfp, "/file", 1000, 1000, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file0", (pmemfile_uid_t)-1, 1001, EPERM));
	ASSERT_TRUE(test_chown(pfp, "/file0", (pmemfile_uid_t)-1, 1002, EPERM));

	ASSERT_EQ(pmemfile_symlink(pfp, "/file", "/symlink"), 0)
		<< strerror(errno);

	memset(&s, 0, sizeof(s));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &s), 0);
	ASSERT_EQ(s.st_gid, (pmemfile_gid_t)1001);

	memset(&s, 0, sizeof(s));
	ASSERT_EQ(pmemfile_lstat(pfp, "/symlink", &s), 0);
	ASSERT_EQ(s.st_gid, (pmemfile_gid_t)0);

	ASSERT_EQ(pmemfile_chown(pfp, "/symlink", (pmemfile_uid_t)-1, 1002), 0)
		<< strerror(errno);

	memset(&s, 0, sizeof(s));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &s), 0);
	ASSERT_EQ(s.st_gid, (pmemfile_gid_t)1002);

	memset(&s, 0, sizeof(s));
	ASSERT_EQ(pmemfile_lstat(pfp, "/symlink", &s), 0);
	ASSERT_EQ(s.st_gid, (pmemfile_gid_t)0);

	ASSERT_EQ(pmemfile_unlink(pfp, "/symlink"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file0"), 0);
}

static bool
test_fchown(PMEMfilepool *pfp, PMEMfile *f, pmemfile_uid_t owner,
	    pmemfile_gid_t group, int error)
{
	int r = pmemfile_fchown(pfp, f, owner, group);
	if (error) {
		EXPECT_EQ(r, -1);
		EXPECT_EQ(errno, error);
		if (r != -1 || errno != error)
			return false;
	} else {
		EXPECT_EQ(r, 0) << strerror(errno);
		if (r != 0)
			return false;

		struct stat s;
		memset(&s, 0, sizeof(s));
		if (pmemfile_fstat(pfp, f, &s)) {
			ADD_FAILURE() << "stat failed " << strerror(errno);
			return false;
		}

		if (owner != (pmemfile_uid_t)-1) {
			EXPECT_EQ(s.st_uid, owner);
			if (s.st_uid != owner)
				return false;
		}

		if (group != (pmemfile_gid_t)-1) {
			EXPECT_EQ(s.st_gid, group);
			if (s.st_gid != group)
				return false;
		}
	}

	return true;
}

TEST_F(permissions, fchown)
{
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));

	PMEMfile *f = pmemfile_open(pfp, "/file", PMEMFILE_O_RDONLY);

	/* ruid=euid=fsuid=0, rgid=egid=fsgid=0 */

	ASSERT_TRUE(test_fchown(pfp, f, 0, 0, 0));
	ASSERT_TRUE(test_fchown(pfp, f, (pmemfile_uid_t)-1, 0, 0));
	ASSERT_TRUE(test_fchown(pfp, f, 0, (pmemfile_gid_t)-1, 0));
	ASSERT_TRUE(
		test_fchown(pfp, f, (pmemfile_uid_t)-1, (pmemfile_gid_t)-1, 0));

	ASSERT_TRUE(test_fchown(pfp, f, 1000, 0, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, 0, 1001, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1001, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, (pmemfile_uid_t)-1, 1001, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1002, EPERM));

	ASSERT_TRUE(test_fchown(pfp, f, 1000, 0, EPERM));

	ASSERT_EQ(pmemfile_setfsuid(pfp, 1000), 0);

	/* ruid=euid=0 fsuid=1000, rgid=egid=fsgid=0 */
	ASSERT_EQ(pmemfile_setcap(pfp, PMEMFILE_CAP_CHOWN), 0)
		<< strerror(errno);

	ASSERT_TRUE(test_fchown(pfp, f, 1000, 0, 0));

	ASSERT_EQ(pmemfile_clrcap(pfp, PMEMFILE_CAP_CHOWN), 0)
		<< strerror(errno);

	ASSERT_TRUE(test_fchown(pfp, f, 0, 1001, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1000, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1001, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, (pmemfile_uid_t)-1, 1001, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1002, EPERM));

	ASSERT_EQ(pmemfile_setfsgid(pfp, 1001), 0);

	/* ruid=euid=0 fsuid=1000, rgid=egid=0 fsgid=1001 */

	ASSERT_TRUE(test_fchown(pfp, f, 1000, 0, 0));
	ASSERT_TRUE(test_fchown(pfp, f, 0, 1001, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1000, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1001, 0));
	ASSERT_TRUE(test_fchown(pfp, f, (pmemfile_uid_t)-1, 1001, 0));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1002, EPERM));

	pmemfile_gid_t groups[1] = {1002};
	ASSERT_EQ(pmemfile_setgroups(pfp, 1, groups), 0);

	/* ruid=euid=0 fsuid=1000, rgid=egid=0 fsgid=1001, gids=1002 */

	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1003, EPERM));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1002, 0));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1001, 0));
	ASSERT_TRUE(test_fchown(pfp, f, 1000, 1000, EPERM));

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
}

TEST_F(permissions, lchown)
{
	struct stat s;

	ASSERT_EQ(pmemfile_setreuid(pfp, 1000, 1000), 0);
	ASSERT_EQ(pmemfile_setregid(pfp, 1001, 1001), 0);

	pmemfile_gid_t groups[1] = {1002};
	ASSERT_EQ(pmemfile_setgroups(pfp, 1, groups), 0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/file", PMEMFILE_O_EXCL,
					 PMEMFILE_S_IRWXU));

	ASSERT_EQ(pmemfile_symlink(pfp, "/file", "/symlink"), 0)
		<< strerror(errno);

	memset(&s, 0, sizeof(s));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &s), 0);
	ASSERT_EQ(s.st_gid, (pmemfile_gid_t)1001);

	memset(&s, 0, sizeof(s));
	ASSERT_EQ(pmemfile_lstat(pfp, "/symlink", &s), 0);
	ASSERT_EQ(s.st_gid, (pmemfile_gid_t)1001);

	ASSERT_EQ(pmemfile_lchown(pfp, "/symlink", (pmemfile_uid_t)-1, 1002), 0)
		<< strerror(errno);

	memset(&s, 0, sizeof(s));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &s), 0);
	ASSERT_EQ(s.st_gid, (pmemfile_gid_t)1001);

	memset(&s, 0, sizeof(s));
	ASSERT_EQ(pmemfile_lstat(pfp, "/symlink", &s), 0);
	ASSERT_EQ(s.st_gid, (pmemfile_gid_t)1002);

	ASSERT_EQ(pmemfile_unlink(pfp, "/symlink"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
}

static bool
_test_acc(PMEMfilepool *pfp, const char *path, int mode, int error)
{
	int ret = pmemfile_access(pfp, path, mode);
	if (error) {
		EXPECT_EQ(ret, -1);
		EXPECT_EQ(errno, error);

		if (ret != -1)
			return false;
		if (errno != error)
			return false;
	} else {
		EXPECT_EQ(ret, 0) << strerror(errno);
		if (ret)
			return false;
	}

	return true;
}

static bool
test_acc(PMEMfilepool *pfp, const char *path, int mode, int error)
{
	bool r = _test_acc(pfp, path, mode, error);
	if (!r)
		return false;

	static char path2[PMEMFILE_PATH_MAX + 1];
	sprintf(path2, "%s_sym", path);
	return _test_acc(pfp, path2, mode, error);
}

TEST_F(permissions, access)
{
	ASSERT_EQ(pmemfile_setreuid(pfp, 1000, 1000), 0);
	ASSERT_EQ(pmemfile_setregid(pfp, 2000, 2000), 0);

	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/file_rwxr-x---", PMEMFILE_O_EXCL,
		PMEMFILE_S_IRWXU | PMEMFILE_S_IRGRP | PMEMFILE_S_IXGRP));
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/file_r---w---x", PMEMFILE_O_EXCL,
		PMEMFILE_S_IRUSR | PMEMFILE_S_IWGRP | PMEMFILE_S_IXOTH));
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/file_-w-r---w-", PMEMFILE_O_EXCL,
		PMEMFILE_S_IWUSR | PMEMFILE_S_IRGRP | PMEMFILE_S_IWOTH));
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/file_--x--xr--", PMEMFILE_O_EXCL,
		PMEMFILE_S_IXUSR | PMEMFILE_S_IXGRP | PMEMFILE_S_IROTH));
	ASSERT_EQ(
		pmemfile_symlink(pfp, "/file_rwxr-x---", "/file_rwxr-x---_sym"),
		0)
		<< strerror(errno);
	ASSERT_EQ(
		pmemfile_symlink(pfp, "/file_r---w---x", "/file_r---w---x_sym"),
		0)
		<< strerror(errno);
	ASSERT_EQ(
		pmemfile_symlink(pfp, "/file_-w-r---w-", "/file_-w-r---w-_sym"),
		0)
		<< strerror(errno);
	ASSERT_EQ(
		pmemfile_symlink(pfp, "/file_--x--xr--", "/file_--x--xr--_sym"),
		0)
		<< strerror(errno);

	EXPECT_TRUE(test_acc(pfp, "/fileX", PMEMFILE_F_OK, ENOENT));

	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_R_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_W_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_X_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0));

	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_R_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_W_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_X_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			     EACCES));

	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_R_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_W_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_X_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			     EACCES));

	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_R_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_W_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_X_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			     EACCES));

	ASSERT_EQ(pmemfile_seteuid(pfp, 1002), 0);
	ASSERT_EQ(pmemfile_setfsuid(pfp, 1001), 1002);
	/* changing euid or fsuid should not change anything for access */

	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_R_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_W_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_X_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0));

	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_R_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_W_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_X_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			     EACCES));

	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_R_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_W_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_X_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			     EACCES));

	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_R_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_W_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_X_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			     EACCES));

	ASSERT_EQ(pmemfile_setegid(pfp, 2002), 0);
	ASSERT_EQ(pmemfile_setfsgid(pfp, 2001), 2002);
	/* changing egid or fsgid should not change anything for access */

	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_R_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_W_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---", PMEMFILE_X_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_rwxr-x---",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0));

	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_R_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_W_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x", PMEMFILE_X_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_r---w---x",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			     EACCES));

	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_R_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_W_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-", PMEMFILE_X_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_-w-r---w-",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			     EACCES));

	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_R_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_W_OK, EACCES));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--", PMEMFILE_X_OK, 0));
	EXPECT_TRUE(test_acc(pfp, "/file_--x--xr--",
			     PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			     EACCES));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file_--x--xr--"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_--x--xr--_sym"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_-w-r---w-"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_-w-r---w-_sym"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_r---w---x"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_r---w---x_sym"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_rwxr-x---"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_rwxr-x---_sym"), 0);
}

static bool
_test_eacc(PMEMfilepool *pfp, const char *path, int mode, int error)
{
	int ret = pmemfile_euidaccess(pfp, path, mode);
	if (error) {
		EXPECT_EQ(ret, -1);
		EXPECT_EQ(errno, error);

		if (ret != -1)
			return false;
		if (errno != error)
			return false;
	} else {
		EXPECT_EQ(ret, 0) << strerror(errno);
		if (ret)
			return false;
	}

	return true;
}

static bool
test_eacc(PMEMfilepool *pfp, const char *path, int mode, int error)
{
	bool r = _test_eacc(pfp, path, mode, error);
	if (!r)
		return false;

	static char path2[PMEMFILE_PATH_MAX + 1];
	sprintf(path2, "%s_sym", path);
	return _test_eacc(pfp, path2, mode, error);
}

TEST_F(permissions, euidaccess)
{
	ASSERT_EQ(pmemfile_setreuid(pfp, 1000, 1000), 0);
	ASSERT_EQ(pmemfile_setregid(pfp, 2000, 2000), 0);

	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/file_rwxr-x---", PMEMFILE_O_EXCL,
		PMEMFILE_S_IRWXU | PMEMFILE_S_IRGRP | PMEMFILE_S_IXGRP));
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/file_r---w---x", PMEMFILE_O_EXCL,
		PMEMFILE_S_IRUSR | PMEMFILE_S_IWGRP | PMEMFILE_S_IXOTH));
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/file_-w-r---w-", PMEMFILE_O_EXCL,
		PMEMFILE_S_IWUSR | PMEMFILE_S_IRGRP | PMEMFILE_S_IWOTH));
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/file_--x--xr--", PMEMFILE_O_EXCL,
		PMEMFILE_S_IXUSR | PMEMFILE_S_IXGRP | PMEMFILE_S_IROTH));
	ASSERT_EQ(
		pmemfile_symlink(pfp, "/file_rwxr-x---", "/file_rwxr-x---_sym"),
		0)
		<< strerror(errno);
	ASSERT_EQ(
		pmemfile_symlink(pfp, "/file_r---w---x", "/file_r---w---x_sym"),
		0)
		<< strerror(errno);
	ASSERT_EQ(
		pmemfile_symlink(pfp, "/file_-w-r---w-", "/file_-w-r---w-_sym"),
		0)
		<< strerror(errno);
	ASSERT_EQ(
		pmemfile_symlink(pfp, "/file_--x--xr--", "/file_--x--xr--_sym"),
		0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_seteuid(pfp, 1002), 0);

	EXPECT_TRUE(test_eacc(pfp, "/file_rwxr-x---", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_eacc(pfp, "/file_rwxr-x---", PMEMFILE_R_OK, 0));
	EXPECT_TRUE(test_eacc(pfp, "/file_rwxr-x---", PMEMFILE_W_OK, EACCES));
	EXPECT_TRUE(test_eacc(pfp, "/file_rwxr-x---", PMEMFILE_X_OK, 0));
	EXPECT_TRUE(test_eacc(pfp, "/file_rwxr-x---",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			      EACCES));

	EXPECT_TRUE(test_eacc(pfp, "/file_r---w---x", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_eacc(pfp, "/file_r---w---x", PMEMFILE_R_OK, EACCES));
	EXPECT_TRUE(test_eacc(pfp, "/file_r---w---x", PMEMFILE_W_OK, 0));
	EXPECT_TRUE(test_eacc(pfp, "/file_r---w---x", PMEMFILE_X_OK, EACCES));
	EXPECT_TRUE(test_eacc(pfp, "/file_r---w---x",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			      EACCES));

	EXPECT_TRUE(test_eacc(pfp, "/file_-w-r---w-", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_eacc(pfp, "/file_-w-r---w-", PMEMFILE_R_OK, 0));
	EXPECT_TRUE(test_eacc(pfp, "/file_-w-r---w-", PMEMFILE_W_OK, EACCES));
	EXPECT_TRUE(test_eacc(pfp, "/file_-w-r---w-", PMEMFILE_X_OK, EACCES));
	EXPECT_TRUE(test_eacc(pfp, "/file_-w-r---w-",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			      EACCES));

	EXPECT_TRUE(test_eacc(pfp, "/file_--x--xr--", PMEMFILE_F_OK, 0));
	EXPECT_TRUE(test_eacc(pfp, "/file_--x--xr--", PMEMFILE_R_OK, EACCES));
	EXPECT_TRUE(test_eacc(pfp, "/file_--x--xr--", PMEMFILE_W_OK, EACCES));
	EXPECT_TRUE(test_eacc(pfp, "/file_--x--xr--", PMEMFILE_X_OK, 0));
	EXPECT_TRUE(test_eacc(pfp, "/file_--x--xr--",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			      EACCES));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file_--x--xr--"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_--x--xr--_sym"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_-w-r---w-"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_-w-r---w-_sym"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_r---w---x"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_r---w---x_sym"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_rwxr-x---"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file_rwxr-x---_sym"), 0);
}

static bool
_test_facc(PMEMfilepool *pfp, PMEMfile *dir, const char *path, int mode,
	   int flags, int error)
{
	int ret = pmemfile_faccessat(pfp, dir, path, mode, flags);
	if (error) {
		EXPECT_EQ(ret, -1);
		EXPECT_EQ(errno, error);

		if (ret != -1)
			return false;
		if (errno != error)
			return false;
	} else {
		EXPECT_EQ(ret, 0) << strerror(errno);
		if (ret)
			return false;
	}

	return true;
}

static bool
test_facc(PMEMfilepool *pfp, PMEMfile *dir, const char *path, int mode,
	  int flags, int error)
{
	bool r = _test_facc(pfp, dir, path, mode, flags, error);
	if (!r)
		return false;

	static char path2[PMEMFILE_PATH_MAX + 1];
	sprintf(path2, "%s_sym", path);
	return _test_facc(pfp, dir, path2, mode, flags, error);
}

TEST_F(permissions, faccessat)
{
	ASSERT_EQ(pmemfile_setreuid(pfp, 1000, 1000), 0);
	ASSERT_EQ(pmemfile_setregid(pfp, 2000, 2000), 0);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", PMEMFILE_ACCESSPERMS), 0);
	PMEMfile *dir = pmemfile_open(pfp, "/dir", PMEMFILE_O_DIRECTORY);

	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/dir/file_rwxr-x---", PMEMFILE_O_EXCL,
		PMEMFILE_S_IRWXU | PMEMFILE_S_IRGRP | PMEMFILE_S_IXGRP));
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/dir/file_r---w---x", PMEMFILE_O_EXCL,
		PMEMFILE_S_IRUSR | PMEMFILE_S_IWGRP | PMEMFILE_S_IXOTH));
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/dir/file_-w-r---w-", PMEMFILE_O_EXCL,
		PMEMFILE_S_IWUSR | PMEMFILE_S_IRGRP | PMEMFILE_S_IWOTH));
	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/dir/file_--x--xr--", PMEMFILE_O_EXCL,
		PMEMFILE_S_IXUSR | PMEMFILE_S_IXGRP | PMEMFILE_S_IROTH));
	ASSERT_EQ(pmemfile_symlink(pfp, "/dir/file_rwxr-x---",
				   "/dir/file_rwxr-x---_sym"),
		  0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_symlink(pfp, "/dir/file_r---w---x",
				   "/dir/file_r---w---x_sym"),
		  0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_symlink(pfp, "/dir/file_-w-r---w-",
				   "/dir/file_-w-r---w-_sym"),
		  0)
		<< strerror(errno);
	ASSERT_EQ(pmemfile_symlink(pfp, "/dir/file_--x--xr--",
				   "/dir/file_--x--xr--_sym"),
		  0)
		<< strerror(errno);

	ASSERT_EQ(pmemfile_seteuid(pfp, 1002), 0);

	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---", PMEMFILE_F_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---", PMEMFILE_R_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---", PMEMFILE_W_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---", PMEMFILE_X_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0,
			      0));

	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---", PMEMFILE_F_OK,
			      PMEMFILE_AT_EACCESS, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---", PMEMFILE_R_OK,
			      PMEMFILE_AT_EACCESS, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---", PMEMFILE_W_OK,
			      PMEMFILE_AT_EACCESS, EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---", PMEMFILE_X_OK,
			      PMEMFILE_AT_EACCESS, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_rwxr-x---",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK,
			      PMEMFILE_AT_EACCESS, EACCES));

	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x", PMEMFILE_F_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x", PMEMFILE_R_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x", PMEMFILE_W_OK, 0,
			      EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x", PMEMFILE_X_OK, 0,
			      EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0,
			      EACCES));

	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x", PMEMFILE_F_OK,
			      PMEMFILE_AT_EACCESS, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x", PMEMFILE_R_OK,
			      PMEMFILE_AT_EACCESS, EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x", PMEMFILE_W_OK,
			      PMEMFILE_AT_EACCESS, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x", PMEMFILE_X_OK,
			      PMEMFILE_AT_EACCESS, EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_r---w---x",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0,
			      EACCES));

	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-", PMEMFILE_F_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-", PMEMFILE_R_OK, 0,
			      EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-", PMEMFILE_W_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-", PMEMFILE_X_OK, 0,
			      EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0,
			      EACCES));

	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-", PMEMFILE_F_OK,
			      PMEMFILE_AT_EACCESS, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-", PMEMFILE_R_OK,
			      PMEMFILE_AT_EACCESS, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-", PMEMFILE_W_OK,
			      PMEMFILE_AT_EACCESS, EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-", PMEMFILE_X_OK,
			      PMEMFILE_AT_EACCESS, EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_-w-r---w-",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0,
			      EACCES));

	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--", PMEMFILE_F_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--", PMEMFILE_R_OK, 0,
			      EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--", PMEMFILE_W_OK, 0,
			      EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--", PMEMFILE_X_OK, 0, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0,
			      EACCES));

	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--", PMEMFILE_F_OK,
			      PMEMFILE_AT_EACCESS, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--", PMEMFILE_R_OK,
			      PMEMFILE_AT_EACCESS, EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--", PMEMFILE_W_OK,
			      PMEMFILE_AT_EACCESS, EACCES));
	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--", PMEMFILE_X_OK,
			      PMEMFILE_AT_EACCESS, 0));
	EXPECT_TRUE(test_facc(pfp, dir, "file_--x--xr--",
			      PMEMFILE_R_OK | PMEMFILE_W_OK | PMEMFILE_X_OK, 0,
			      EACCES));

	pmemfile_close(pfp, dir);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file_--x--xr--"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file_--x--xr--_sym"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file_-w-r---w-"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file_-w-r---w-_sym"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file_r---w---x"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file_r---w---x_sym"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file_rwxr-x---"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir/file_rwxr-x---_sym"), 0);

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
