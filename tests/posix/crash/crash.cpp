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
 * crash.cpp -- unit test for pmemfile_*
 */

#include "pmemfile_test.hpp"

static PMEMfilepool *
create_pool(const char *path)
{
	char tmp[PMEMFILE_PATH_MAX];
	sprintf(tmp, "%s/pool", path);
	return pmemfile_pool_create(tmp, 8 * 1024 * 1024,
				    PMEMFILE_S_IWUSR | PMEMFILE_S_IRUSR);
}

static PMEMfilepool *
open_pool(const char *path)
{
	char tmp[PMEMFILE_PATH_MAX];
	sprintf(tmp, "%s/pool", path);
	return pmemfile_pool_open(tmp);
}

static const char *path;
static const char *op;

TEST(crash, 0)
{
	if (strcmp(op, "prep") == 0) {
		PMEMfilepool *pfp = create_pool(path);
		ASSERT_NE(pfp, nullptr) << strerror(errno);

		ASSERT_TRUE(test_pmemfile_create(pfp, "/aaa", PMEMFILE_O_EXCL,
						 0644));
		ASSERT_TRUE(test_pmemfile_create(pfp, "/bbb", PMEMFILE_O_EXCL,
						 0644));

		pmemfile_pool_close(pfp);
	} else if (strcmp(op, "crash1") == 0) {
		PMEMfilepool *pfp = open_pool(path);
		ASSERT_NE(pfp, nullptr) << strerror(errno);

		ASSERT_NE(pmemfile_open(pfp, "/aaa", 0), nullptr);

		exit(0);
	} else if (strcmp(op, "crash2") == 0) {
		PMEMfilepool *pfp = open_pool(path);
		ASSERT_NE(pfp, nullptr) << strerror(errno);

		ASSERT_NE(pmemfile_open(pfp, "/aaa", 0), nullptr);
		ASSERT_EQ(pmemfile_unlink(pfp, "/aaa"), 0);

		exit(0);
	} else if (strcmp(op, "openclose1") == 0 ||
		   strcmp(op, "openclose2") == 0) {
		PMEMfilepool *pfp = open_pool(path);
		ASSERT_NE(pfp, nullptr) << strerror(errno);

		EXPECT_TRUE(test_compare_dirs(pfp, "/",
					      std::vector<pmemfile_ls>{
						      {040777, 2, 4008, "."},
						      {040777, 2, 4008, ".."},
						      {0100644, 1, 0, "aaa"},
						      {0100644, 1, 0, "bbb"},
					      }));

		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 3, 0, 0, 0));

		pmemfile_pool_close(pfp);
	} else if (strcmp(op, "openclose3") == 0) {
		PMEMfilepool *pfp = open_pool(path);
		ASSERT_NE(pfp, nullptr) << strerror(errno);

		EXPECT_TRUE(test_compare_dirs(pfp, "/",
					      std::vector<pmemfile_ls>{
						      {040777, 2, 4008, "."},
						      {040777, 2, 4008, ".."},
						      {0100644, 1, 0, "bbb"},
					      }));

		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0));

		pmemfile_pool_close(pfp);
	} else
		ASSERT_TRUE(0);
}

int
main(int argc, char *argv[])
{
	char *is_pmemfile_pop_str = std::getenv("LIBPMEMFILE_POP");
	is_pmemfile_pop = is_pmemfile_pop_str != nullptr &&
		strtol(is_pmemfile_pop_str, nullptr, 10);

	START();

	if (argc < 3) {
		fprintf(stderr, "usage: %s path op", argv[0]);
		exit(1);
	}

	path = argv[1];
	op = argv[2];

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
