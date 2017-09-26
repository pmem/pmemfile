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
 * basic.cpp -- unit test for pmemfile_*
 */
#include "pmemfile_test.hpp"

class basic : public pmemfile_test {
public:
	basic() : pmemfile_test()
	{
	}
};

TEST_F(basic, open_create_close)
{
	PMEMfile *f1, *f2, *dir;

	/* NULL file name */
	errno = 0;
	f1 = pmemfile_open(pfp, NULL, PMEMFILE_O_CREAT, 0777);
	ASSERT_EQ(f1, nullptr);
	EXPECT_EQ(errno, ENOENT);

	/* file does not exist */
	errno = 0;
	f1 = pmemfile_open(pfp, "/aaa", 0);
	ASSERT_EQ(f1, nullptr);
	EXPECT_EQ(errno, ENOENT);

	/* successful create */
	f1 = pmemfile_open(pfp, "/aaa", PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
			   0777);
	ASSERT_NE(f1, nullptr);

	pmemfile_close(pfp, f1);

	/* file already exists */
	errno = 0;
	f1 = pmemfile_open(pfp, "/aaa", PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
			   0777);
	ASSERT_EQ(f1, nullptr);
	EXPECT_EQ(errno, EEXIST);

	/* too long name */
	errno = 0;
	f1 = pmemfile_open(pfp,
			   "/"
			   "12345678901234567890123456789012345678901234567890"
			   "12345678901234567890123456789012345678901234567890"
			   "12345678901234567890123456789012345678901234567890"
			   "12345678901234567890123456789012345678901234567890"
			   "12345678901234567890123456789012345678901234567890"
			   "123456",
			   PMEMFILE_O_CREAT | PMEMFILE_O_EXCL, 0777);
	ASSERT_EQ(f1, nullptr);
	EXPECT_EQ(errno, ENAMETOOLONG);

	/* too long name in directory name */
	errno = 0;
	f1 = pmemfile_open(pfp,
			   "/"
			   "12345678901234567890123456789012345678901234567890"
			   "12345678901234567890123456789012345678901234567890"
			   "12345678901234567890123456789012345678901234567890"
			   "12345678901234567890123456789012345678901234567890"
			   "12345678901234567890123456789012345678901234567890"
			   "123456/aaaa",
			   PMEMFILE_O_CREAT | PMEMFILE_O_EXCL, 0777);
	ASSERT_EQ(f1, nullptr);
	EXPECT_EQ(errno, ENAMETOOLONG);

	/* file does not exist */
	errno = 0;
	f2 = pmemfile_open(pfp, "/bbb", 0);
	ASSERT_EQ(f2, nullptr);
	EXPECT_EQ(errno, ENOENT);

	/* successful create */
	f2 = pmemfile_open(pfp, "/bbb", PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
			   0777);
	ASSERT_NE(f2, nullptr);

	/* successful open */
	f1 = pmemfile_open(pfp, "/aaa", 0);
	ASSERT_NE(f1, nullptr);

	pmemfile_close(pfp, f2);

	pmemfile_close(pfp, f1);

#ifdef FAULT_INJECTION
	pmemfile_gid_t groups[1] = {1002};
	ASSERT_EQ(pmemfile_setgroups(pfp, 1, groups), 0);
	pmemfile_inject_fault_at(PF_MALLOC, 1, "copy_cred");
	errno = 0;
	ASSERT_EQ(pmemfile_create(pfp, "/fileXXX", 0644), nullptr);
	EXPECT_EQ(errno, ENOMEM);
#endif

	EXPECT_TRUE(test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
							{040777, 2, 4000, "."},
							{040777, 2, 4000, ".."},
							{0100777, 1, 0, "aaa"},
							{0100777, 1, 0, "bbb"},
						}));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 2, 0, 0, 0));

	pmemfile_pool_close(pfp);

	pfp = pmemfile_pool_open(path.c_str());
	ASSERT_NE(pfp, nullptr) << strerror(errno);

	EXPECT_TRUE(test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
							{040777, 2, 4000, "."},
							{040777, 2, 4000, ".."},
							{0100777, 1, 0, "aaa"},
							{0100777, 1, 0, "bbb"},
						}));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 2, 0, 0, 0));

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/bbb"), 0);

	/* make directory */
	errno = 0;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0777), 0);

	/* successful open directory */
	dir = pmemfile_open(pfp, "/dir",
			    PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	ASSERT_NE(dir, nullptr);
	pmemfile_close(pfp, dir);

	/* wrong flags passed */
	dir = pmemfile_open(pfp, "/dir",
			    PMEMFILE_O_DIRECTORY | PMEMFILE_O_WRONLY);
	ASSERT_EQ(dir, nullptr);
	ASSERT_EQ(errno, EISDIR);

	errno = 0;
	dir = pmemfile_open(pfp, "/dir",
			    PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDWR);
	ASSERT_EQ(dir, nullptr);
	ASSERT_EQ(errno, EISDIR);

	errno = 0;
	/*
	 * O_PATH narrows flags to defined set, so we have to unset it to
	 * test the behavior for unknown flags
	*/
	f1 = pmemfile_open(pfp, "path", -1 & ~PMEMFILE_O_PATH);
	ASSERT_EQ(f1, nullptr);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);
}

TEST_F(basic, link)
{
	int ret;

	ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL, 0777));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/bbb", PMEMFILE_O_EXCL, 0777));

	EXPECT_TRUE(test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
							{040777, 2, 4000, "."},
							{040777, 2, 4000, ".."},
							{0100777, 1, 0, "aaa"},
							{0100777, 1, 0, "bbb"},
						}));

	errno = 0;
	ASSERT_EQ(pmemfile_link(pfp, NULL, "/aaa.link"), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_link(pfp, "/aaa", NULL), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_link(NULL, "/aaa", "/aaa.link"), -1);
	EXPECT_EQ(errno, EFAULT);

	/* successful link */
	ret = pmemfile_link(pfp, "/aaa", "/aaa.link");
	ASSERT_EQ(ret, 0) << strerror(errno);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4000, "."},
						    {040777, 2, 4000, ".."},
						    {0100777, 2, 0, "aaa"},
						    {0100777, 1, 0, "bbb"},
						    {0100777, 2, 0, "aaa.link"},
					    }));

	/* destination already exists */
	errno = 0;
	ret = pmemfile_link(pfp, "/aaa", "/aaa.link");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, EEXIST);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4000, "."},
						    {040777, 2, 4000, ".."},
						    {0100777, 2, 0, "aaa"},
						    {0100777, 1, 0, "bbb"},
						    {0100777, 2, 0, "aaa.link"},
					    }));

	/* source does not exist */
	errno = 0;
	ret = pmemfile_link(pfp, "/aaaaaaaaaaaa", "/aaa.linkXXX");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOENT);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4000, "."},
						    {040777, 2, 4000, ".."},
						    {0100777, 2, 0, "aaa"},
						    {0100777, 1, 0, "bbb"},
						    {0100777, 2, 0, "aaa.link"},
					    }));

	/* successful link from link */
	ret = pmemfile_link(pfp, "/aaa.link", "/aaa2.link");
	ASSERT_EQ(ret, 0) << strerror(errno);

	EXPECT_TRUE(test_compare_dirs(pfp, "/",
				      std::vector<pmemfile_ls>{
					      {040777, 2, 4000, "."},
					      {040777, 2, 4000, ".."},
					      {0100777, 3, 0, "aaa"},
					      {0100777, 1, 0, "bbb"},
					      {0100777, 3, 0, "aaa.link"},
					      {0100777, 3, 0, "aaa2.link"},
				      }));

	/* another successful link */
	ret = pmemfile_link(pfp, "/bbb", "/bbb2.link");
	ASSERT_EQ(ret, 0) << strerror(errno);

	EXPECT_TRUE(test_compare_dirs(pfp, "/",
				      std::vector<pmemfile_ls>{
					      {040777, 2, 4000, "."},
					      {040777, 2, 4000, ".."},
					      {0100777, 3, 0, "aaa"},
					      {0100777, 2, 0, "bbb"},
					      {0100777, 3, 0, "aaa.link"},
					      {0100777, 3, 0, "aaa2.link"},
					      {0100777, 2, 0, "bbb2.link"},
				      }));

	/* link from absolute to relative path */
	ret = pmemfile_link(pfp, "/bbb", "rel.link");
	ASSERT_EQ(ret, 0) << strerror(errno);
	ASSERT_EQ(pmemfile_unlink(pfp, "rel.link"), 0);

	ret = pmemfile_mkdir(pfp, "/dir", 0777);
	ASSERT_EQ(ret, 0) << strerror(errno);

	/* destination already exists as directory */
	errno = 0;
	ret = pmemfile_link(pfp, "/aaa", "/dir");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, EEXIST);

	errno = 0;
	ret = pmemfile_link(pfp, "/dir", "/dir2");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, EPERM);

	errno = 0;
	ret = pmemfile_link(pfp, "/aaa/bbb", "/file");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTDIR);

	errno = 0;
	ret = pmemfile_link(pfp, "/bbb", "/aaa/ccc");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTDIR);

	errno = 0;
	ret = pmemfile_link(pfp, "/dir/aaaa", "/bbbb");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ret = pmemfile_link(pfp, "/aaa/", "/bbbb");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTDIR);

	errno = 0;
	ret = pmemfile_link(pfp, "/aaa",
			    "/"
			    "12345678901234567890123456789012345678901234567890"
			    "12345678901234567890123456789012345678901234567890"
			    "12345678901234567890123456789012345678901234567890"
			    "12345678901234567890123456789012345678901234567890"
			    "12345678901234567890123456789012345678901234567890"
			    "123456");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENAMETOOLONG);

	ret = pmemfile_rmdir(pfp, "/dir");
	ASSERT_EQ(ret, 0) << strerror(errno);

	EXPECT_TRUE(test_compare_dirs(pfp, "/",
				      std::vector<pmemfile_ls>{
					      {040777, 2, 4000, "."},
					      {040777, 2, 4000, ".."},
					      {0100777, 3, 0, "aaa"},
					      {0100777, 2, 0, "bbb"},
					      {0100777, 3, 0, "aaa.link"},
					      {0100777, 3, 0, "aaa2.link"},
					      {0100777, 2, 0, "bbb2.link"},
				      }));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 2, 0, 0, 0));

	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/bbb"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa.link"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/aaa2.link"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/bbb2.link"), 0);
}

TEST_F(basic, unlink)
{
	int ret;

	ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL, 0777));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/bbb", PMEMFILE_O_EXCL, 0777));

	ret = pmemfile_link(pfp, "/aaa", "/aaa.link");
	ASSERT_EQ(ret, 0) << strerror(errno);

	ret = pmemfile_link(pfp, "/aaa", "/aaa2.link");
	ASSERT_EQ(ret, 0) << strerror(errno);

	ret = pmemfile_link(pfp, "/bbb", "/bbb2.link");
	ASSERT_EQ(ret, 0) << strerror(errno);

	PMEMfile *f1;

	f1 = pmemfile_open(pfp, "/bbb2.link", 0);
	ASSERT_NE(f1, nullptr) << strerror(errno);
	pmemfile_close(pfp, f1);

	errno = 0;
	ASSERT_EQ(pmemfile_unlink(pfp, NULL), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_unlink(NULL, "/bbb2.link"), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ret = pmemfile_unlink(pfp, "/bbb2.link/");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTDIR);

	ret = pmemfile_unlink(pfp, "/bbb2.link");
	ASSERT_EQ(ret, 0) << strerror(errno);

	errno = 0;
	ret = pmemfile_unlink(pfp, "/bbb2.link");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	f1 = pmemfile_open(pfp, "/bbb2.link", 0);
	ASSERT_EQ(f1, nullptr);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ret = pmemfile_unlink(pfp, "/bbb.notexists");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOENT);

	f1 = pmemfile_open(pfp, "/bbb", 0);
	ASSERT_NE(f1, nullptr) << strerror(errno);

	ret = pmemfile_unlink(pfp, "/bbb");
	ASSERT_EQ(ret, 0) << strerror(errno);

	pmemfile_close(pfp, f1);

	errno = 0;
	f1 = pmemfile_open(pfp, "/bbb", 0);
	ASSERT_EQ(f1, nullptr);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ret = pmemfile_unlink(pfp, "/..");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, EISDIR);

	errno = 0;
	ret = pmemfile_unlink(pfp, "/.");
	ASSERT_EQ(ret, -1);
	EXPECT_EQ(errno, EISDIR);

	EXPECT_TRUE(test_compare_dirs(pfp, "/",
				      std::vector<pmemfile_ls>{
					      {040777, 2, 4000, "."},
					      {040777, 2, 4000, ".."},
					      {0100777, 3, 0, "aaa"},
					      {0100777, 3, 0, "aaa.link"},
					      {0100777, 3, 0, "aaa2.link"},
				      }));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 0));

	ret = pmemfile_unlink(pfp, "/aaa");
	ASSERT_EQ(ret, 0) << strerror(errno);

	ret = pmemfile_unlink(pfp, "/aaa.link");
	ASSERT_EQ(ret, 0) << strerror(errno);

	ret = pmemfile_unlink(pfp, "/aaa2.link");
	ASSERT_EQ(ret, 0) << strerror(errno);
}

TEST_F(basic, tmpfile)
{
	pmemfile_ssize_t written;

	PMEMfile *f = pmemfile_open(
		pfp, "/", PMEMFILE_O_TMPFILE | PMEMFILE_O_WRONLY, 0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	written = pmemfile_write(pfp, f, "qwerty", 6);
	ASSERT_EQ(written, 6) << COND_ERROR(written);

	ASSERT_TRUE(test_empty_dir(pfp, "/"));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));

	pmemfile_close(pfp, f);

	ASSERT_TRUE(test_empty_dir(pfp, "/"));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count(), 0, 0, 0));
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
