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
 * fcntl.cpp -- unit test for pmemfile_fcntl
 */

#include "pmemfile_test.hpp"

class fcntl : public pmemfile_test {
public:
	fcntl() : pmemfile_test()
	{
	}
};

TEST_F(fcntl, fl)
{
	PMEMfile *f;
	int ret;

	f = pmemfile_open(pfp, "/file", PMEMFILE_O_CREAT | PMEMFILE_O_RDWR,
			  0755);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ret = pmemfile_fcntl(pfp, f, PMEMFILE_F_GETFL);
	EXPECT_EQ(ret, PMEMFILE_O_RDWR);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ret = pmemfile_fcntl(pfp, f, PMEMFILE_F_GETFL);
	EXPECT_EQ(ret, PMEMFILE_O_RDONLY);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file", PMEMFILE_O_WRONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ret = pmemfile_fcntl(pfp, f, PMEMFILE_F_GETFL);
	EXPECT_EQ(ret, PMEMFILE_O_WRONLY);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file", PMEMFILE_O_WRONLY | PMEMFILE_O_APPEND);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ret = pmemfile_fcntl(pfp, f, PMEMFILE_F_GETFL);
	EXPECT_EQ(ret, PMEMFILE_O_WRONLY | PMEMFILE_O_APPEND);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file", PMEMFILE_O_WRONLY | PMEMFILE_O_NOATIME);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ret = pmemfile_fcntl(pfp, f, PMEMFILE_F_GETFL);
	EXPECT_EQ(ret, PMEMFILE_O_WRONLY | PMEMFILE_O_NOATIME);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file", PMEMFILE_O_WRONLY | PMEMFILE_O_PATH);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ret = pmemfile_fcntl(pfp, f, PMEMFILE_F_GETFL);
	EXPECT_EQ(ret, PMEMFILE_O_PATH);
	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
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
