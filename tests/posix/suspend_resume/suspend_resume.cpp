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
 * suspend_resume.cpp -- test for pmemfile_pool_[suspend|resume]
 */

#include "pmemfile_test.hpp"

#include <cinttypes>
#include <cstdio>

static const char *pool_path;

static PMEMfilepool *
create_pool()
{
	return pmemfile_pool_create(pool_path, 16 * 1024 * 1024,
				    PMEMFILE_S_IWUSR | PMEMFILE_S_IRUSR);
}

static bool
contains_two_ints(const char *buffer)
{
	uint64_t raw[2];
	if (sscanf(buffer, "0x%" SCNx64 ":0x%" SCNx64 "\n", raw, raw + 1) != 2)
		return false;

	return raw[0] != 0 && raw[1] != 0;
}

TEST(suspend_resume, 0)
{
	static const char *const paths0[] = {nullptr};
	static const char *const paths[] = {"dummy0", "dummy1", "dummy2",
					    nullptr};
	ssize_t r;

	PMEMfilepool *pfp = create_pool();
	ASSERT_NE(pfp, nullptr) << strerror(errno);

	errno = 0;
	r = pmemfile_pool_suspend(pfp, 1, paths, 1);
	ASSERT_EQ(r, -1);
	EXPECT_EQ(errno, EINVAL);

	errno = 0;
	r = pmemfile_pool_suspend(pfp, 1, paths0, 0);
	ASSERT_EQ(r, -1);
	EXPECT_EQ(errno, EINVAL);

	errno = 0;
	r = pmemfile_pool_suspend(pfp, 255, paths, 0);
	ASSERT_EQ(r, -1);
	EXPECT_EQ(errno, EINVAL);

	errno = 0;
	r = pmemfile_pool_suspend(pfp, 1, paths, 0);
	ASSERT_EQ(r, 0) << strerror(errno);

	errno = 0;
	r = pmemfile_pool_resume(pfp, pool_path, 1, paths, 0);
	ASSERT_EQ(r, 0) << strerror(errno);

	pmemfile_pool_close(pfp);

	pfp = pmemfile_pool_open(pool_path);
	ASSERT_NE(pfp, nullptr) << strerror(errno);

	PMEMfile *f0 = pmemfile_open(pfp, "/file0",
				     PMEMFILE_O_CREAT | PMEMFILE_O_EXCL, 0700);
	ASSERT_NE(f0, nullptr) << strerror(errno);
	PMEMfile *f1 = pmemfile_open(pfp, "/file1",
				     PMEMFILE_O_CREAT | PMEMFILE_O_EXCL, 0700);
	ASSERT_NE(f1, nullptr) << strerror(errno);

	errno = 0;
	r = pmemfile_pool_suspend(pfp, 1, paths, 0);
	ASSERT_EQ(r, 0) << strerror(errno);

	errno = 0;
	PMEMfilepool *pfp2 = pmemfile_pool_open(pool_path);
	ASSERT_NE(pfp2, nullptr) << strerror(errno);

	errno = 0;
	PMEMfile *root1 = pmemfile_open_root(pfp2, 1, 0);
	ASSERT_NE(root1, nullptr) << strerror(errno);

	errno = 0;
	PMEMfile *dummy1 =
		pmemfile_openat(pfp2, root1, "dummy1", PMEMFILE_O_RDONLY);
	ASSERT_NE(dummy1, nullptr) << strerror(errno);

	char buf[0x1000];

	r = pmemfile_read(pfp2, dummy1, buf, sizeof(buf));
	ASSERT_GE(r, 16) << strerror(errno);
	ASSERT_NE(r, 0x100);
	buf[r] = '\0';

	ASSERT_NE(strchr(buf, '\n'), nullptr);
	ASSERT_TRUE(contains_two_ints(buf));
	ASSERT_NE(strchr(strchr(buf, '\n') + 1, '\n'), nullptr);
	ASSERT_TRUE(contains_two_ints(strchr(buf, '\n') + 1));

	pmemfile_close(pfp2, dummy1);
	pmemfile_close(pfp2, root1);
	pmemfile_pool_close(pfp2);

	errno = 0;
	r = pmemfile_pool_resume(pfp, pool_path, 1, paths, 0);
	ASSERT_EQ(r, 0) << strerror(errno);

	pmemfile_close(pfp, f0);
	pmemfile_close(pfp, f1);

	pmemfile_pool_close(pfp);
}

int
main(int argc, char *argv[])
{
	START();

	if (argc < 2) {
		fprintf(stderr, "usage: %s path", argv[0]);
		exit(1);
	}

	static std::string pool_path_container =
		std::string(argv[1]) + std::string("/pool");
	pool_path = pool_path_container.c_str();

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
