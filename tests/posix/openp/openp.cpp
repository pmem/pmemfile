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
 * openp.cpp -- unit test for pmemfile_open_parent
 */

#include "pmemfile_test.hpp"

class openp : public pmemfile_test {
public:
	openp() : pmemfile_test()
	{
	}
};

static bool
check_path(PMEMfilepool *pfp, int stop_at_root, const char *path,
	   const char *parent, const char *child)
{
	char tmp_path[PMEMFILE_PATH_MAX], dir_path[PMEMFILE_PATH_MAX];

	strncpy(tmp_path, path, PMEMFILE_PATH_MAX);
	tmp_path[PMEMFILE_PATH_MAX - 1] = 0;

	PMEMfile *f = pmemfile_open_parent(
		pfp, PMEMFILE_AT_CWD, tmp_path, PMEMFILE_PATH_MAX,
		stop_at_root ? PMEMFILE_OPEN_PARENT_STOP_AT_ROOT : 0);
	if (!f) {
		ADD_FAILURE() << path << " " << strerror(errno);
		return false;
	}

	char *dir_path2 =
		pmemfile_get_dir_path(pfp, f, dir_path, PMEMFILE_PATH_MAX);
	if (dir_path2 != dir_path) {
		ADD_FAILURE() << dir_path << " " << dir_path2;
		pmemfile_close(pfp, f);
		return false;
	}
	pmemfile_close(pfp, f);

	if (strcmp(dir_path, parent) != 0) {
		ADD_FAILURE() << "parent" << dir_path << " != " << parent;
		return false;
	}

	if (strcmp(tmp_path, child) != 0) {
		ADD_FAILURE() << "child" << tmp_path << " != " << child;
		return false;
	}

	return true;
}

TEST_F(openp, 0)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0777), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2", 0777), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1/dir3", 0777), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1/dir3/dir4", 0777), 0);
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file1", PMEMFILE_O_EXCL, 0644));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir2/file2", PMEMFILE_O_EXCL,
					 0644));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir1/dir3/file3",
					 PMEMFILE_O_EXCL, 0644));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir1/dir3/dir4/file4",
					 PMEMFILE_O_EXCL, 0644));

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 4, 8192, "."},
						    {040777, 4, 8192, ".."},
						    {040777, 3, 8192, "dir1"},
						    {040777, 2, 8192, "dir2"},
						    {0100644, 1, 0, "file1"},
					    }));

	EXPECT_TRUE(test_compare_dirs(pfp, "/dir1",
				      std::vector<pmemfile_ls>{
					      {040777, 3, 8192, "."},
					      {040777, 4, 8192, ".."},
					      {040777, 3, 8192, "dir3"},
				      }));

	EXPECT_TRUE(test_compare_dirs(pfp, "/dir1/dir3",
				      std::vector<pmemfile_ls>{
					      {040777, 3, 8192, "."},
					      {040777, 3, 8192, ".."},
					      {040777, 2, 8192, "dir4"},
					      {0100644, 1, 0, "file3"},
				      }));

	EXPECT_TRUE(test_compare_dirs(pfp, "/dir1/dir3/dir4",
				      std::vector<pmemfile_ls>{
					      {040777, 2, 8192, "."},
					      {040777, 3, 8192, ".."},
					      {0100644, 1, 0, "file4"},
				      }));

	EXPECT_TRUE(test_compare_dirs(pfp, "/dir2",
				      std::vector<pmemfile_ls>{
					      {040777, 2, 8192, "."},
					      {040777, 4, 8192, ".."},
					      {0100644, 1, 0, "file2"},
				      }));

	EXPECT_TRUE(check_path(pfp, 0, "dir1", "/", "dir1"));
	EXPECT_TRUE(check_path(pfp, 0, "dir1/", "/", "dir1/"));
	EXPECT_TRUE(check_path(pfp, 0, "/dir1", "/", "dir1"));
	EXPECT_TRUE(check_path(pfp, 1, "/dir1", "/", "dir1"));

	EXPECT_TRUE(check_path(pfp, 0, "dir2", "/", "dir2"));
	EXPECT_TRUE(check_path(pfp, 0, "dir2/", "/", "dir2/"));
	EXPECT_TRUE(check_path(pfp, 0, "/dir2", "/", "dir2"));
	EXPECT_TRUE(check_path(pfp, 1, "/dir2", "/", "dir2"));

	EXPECT_TRUE(check_path(pfp, 0, "dir1/dir3", "/dir1", "dir3"));
	EXPECT_TRUE(check_path(pfp, 0, "dir1/dir3/", "/dir1", "dir3/"));
	EXPECT_TRUE(check_path(pfp, 0, "dir1//dir3", "/dir1", "dir3"));
	EXPECT_TRUE(check_path(pfp, 0, "/dir1/dir3", "/dir1", "dir3"));
	EXPECT_TRUE(check_path(pfp, 1, "/dir1/dir3", "/dir1", "dir3"));

	EXPECT_TRUE(check_path(pfp, 0, "dir1/dir3/dir4", "/dir1/dir3", "dir4"));
	EXPECT_TRUE(check_path(pfp, 0, "dir1/not_exists/dir4", "/dir1",
			       "not_exists/dir4"));

	EXPECT_TRUE(check_path(pfp, 0, "dir1/dir3/../", "/dir1/dir3", "../"));

	EXPECT_TRUE(check_path(pfp, 0, "/dir1/../../dir2", "/", "dir2"));
	EXPECT_TRUE(check_path(pfp, 0, "dir1/../../dir2", "/", "dir2"));
	EXPECT_TRUE(
		check_path(pfp, 0, "/dir1/../dir2/../../dir2", "/", "dir2"));
	EXPECT_TRUE(check_path(pfp, 0, "../dir1", "/", "dir1"));
	EXPECT_TRUE(check_path(pfp, 0, "./dir1/../../dir1", "/", "dir1"));

	EXPECT_TRUE(check_path(pfp, 1, "/dir1/../../dir2", "/", "../dir2"));
	EXPECT_TRUE(check_path(pfp, 1, "dir1/../../dir2", "/", "../dir2"));
	EXPECT_TRUE(
		check_path(pfp, 1, "/dir1/../dir2/../../dir2", "/", "../dir2"));
	EXPECT_TRUE(check_path(pfp, 1, "../dir1", "/", "../dir1"));
	EXPECT_TRUE(check_path(pfp, 1, "./dir1/../../dir1", "/", "../dir1"));

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/dir3/dir4/file4"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/dir3/file3"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir2/file2"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1/dir3/dir4"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1/dir3"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir2"), 0);
}

#ifdef FAULT_INJECTION
TEST_F(openp, copy_cred)
{
	pmemfile_gid_t groups[1] = {1002};
	ASSERT_EQ(pmemfile_setgroups(pfp, 1, groups), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0777), 0);

	char path[PMEMFILE_PATH_MAX] = "dir";
	pmemfile_inject_fault_at(PF_MALLOC, 1, "copy_cred");
	errno = 0;
	ASSERT_EQ(pmemfile_open_parent(pfp, PMEMFILE_AT_CWD, path,
				       PMEMFILE_PATH_MAX, 0),
		  nullptr);
	EXPECT_EQ(errno, ENOMEM);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
}
#endif

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
