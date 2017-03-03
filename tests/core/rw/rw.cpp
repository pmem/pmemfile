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
 * rw.cpp -- unit test for pmemfile_read & pmemfile_write
 */

#include "pmemfile_test.hpp"

static unsigned env_block_size;

class rw : public pmemfile_test
{
public:
	rw() : pmemfile_test(256 * 1024 * 1024) {}
};

TEST_F(rw, 1)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
			PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY, 0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	EXPECT_TRUE(test_compare_dirs(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 0, "file1"},
	    {}}));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 0));

	const char *data = "Marcin S";
	char data2[4096];
	char bufFF[4096], buf00[4096];
	size_t len = strlen(data) + 1;
	memset(bufFF, 0xff, sizeof(bufFF));
	memset(buf00, 0x00, sizeof(buf00));

	ssize_t written = pmemfile_write(pfp, f, data, len);
	ASSERT_EQ(written, (ssize_t)len) << COND_ERROR(written);

	EXPECT_TRUE(test_compare_dirs(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 9, "file1"},
	    {}}));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 1));

	/* try to read write-only file */
	ssize_t r = pmemfile_read(pfp, f, data2, len);
	ASSERT_EQ(r, -1);
	EXPECT_EQ(errno, EBADF);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* read only what we wrote and check nothing else was read */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, len);
	ASSERT_EQ(r, (ssize_t)len) << COND_ERROR(r);
	ASSERT_EQ(memcmp(data, data2, len), 0);
	ASSERT_EQ(memcmp(data2 + len, bufFF, sizeof(data2) - len), 0);

	/* try to write to read-only file */
	written = pmemfile_write(pfp, f, data, len);
	ASSERT_EQ(written, -1);
	EXPECT_EQ(errno, EBADF);

	memset(data2, 0, sizeof(data2));
	/* read from end of file */
	r = pmemfile_read(pfp, f, data2, len);
	ASSERT_EQ(r, 0);
	pmemfile_close(pfp, f);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 1));


	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* read as much as possible and check that we read only what we wrote */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, sizeof(data2));
	ASSERT_EQ(r, (ssize_t)len);
	ASSERT_EQ(memcmp(data, data2, len), 0);
	ASSERT_EQ(memcmp(data2 + len, bufFF, sizeof(data2) - len), 0);

	pmemfile_close(pfp, f);


	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* partial read */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, 5);
	ASSERT_EQ(r, 5) << COND_ERROR(r);
	ASSERT_EQ(memcmp(data, data2, 5), 0);
	ASSERT_EQ(memcmp(data2 + 5, bufFF, sizeof(data2) - 5), 0);

	/* another partial read till the end of file */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, 15);
	ASSERT_EQ(r, 4) << COND_ERROR(r);
	ASSERT_EQ(memcmp(data + 5, data2, 4), 0);
	ASSERT_EQ(memcmp(data2 + 4, bufFF, sizeof(data2) - 4), 0);

	pmemfile_close(pfp, f);


	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDWR);
	ASSERT_NE(f, nullptr) << strerror(errno);

	written = pmemfile_write(pfp, f, "pmem", 4);
	ASSERT_EQ(written, 4) << COND_ERROR(written);

	/* validate that write and read use the same offset */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, sizeof(data2));
	ASSERT_EQ(r, 5) << COND_ERROR(r);
	ASSERT_EQ(memcmp(data + 4, data2, 5), 0);
	ASSERT_EQ(memcmp(data2 + 5, bufFF, sizeof(data2) - 5), 0);

	pmemfile_close(pfp, f);


	EXPECT_TRUE(test_compare_dirs(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 9, "file1"},
	    {}}));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 1));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDWR);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* check that what we wrote previously is still there */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, sizeof(data2));
	ASSERT_EQ(r, 9) << COND_ERROR(r);
	ASSERT_EQ(memcmp("pmem", data2, 4), 0);
	ASSERT_EQ(memcmp(data + 4, data2 + 4, 5), 0);
	ASSERT_EQ(memcmp(data2 + 9, bufFF, sizeof(data2) - 9), 0);

	pmemfile_close(pfp, f);

	/* validate SEEK_CUR */
	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDWR);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 0);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 3, PMEMFILE_SEEK_CUR), 3);

	/* check that after "seek" "read" reads correct data */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, sizeof(data2));
	ASSERT_EQ(r, 6) << COND_ERROR(r);
	ASSERT_EQ(memcmp("min S\0", data2, 6), 0);
	ASSERT_EQ(memcmp(data2 + 6, bufFF, sizeof(data2) - 6), 0);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 9);
	ASSERT_EQ(pmemfile_lseek(pfp, f, -7, PMEMFILE_SEEK_CUR), 2);

	/* check that seeking backward works */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, sizeof(data2));
	ASSERT_EQ(r, 7) << COND_ERROR(r);
	ASSERT_EQ(memcmp("emin S\0", data2, 7), 0);
	ASSERT_EQ(memcmp(data2 + 7, bufFF, sizeof(data2) - 7), 0);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 9);


	ASSERT_EQ(pmemfile_lseek(pfp, f, -3, PMEMFILE_SEEK_END), 6);

	/* again, seeking backward works */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, sizeof(data2));
	ASSERT_EQ(r, 3) << COND_ERROR(r);
	ASSERT_EQ(memcmp(" S\0", data2, 3), 0);
	ASSERT_EQ(memcmp(data2 + 3, bufFF, sizeof(data2) - 3), 0);

	/* check that writing past the end of file works */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 9);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 100, PMEMFILE_SEEK_END), 9 + 100);
	ASSERT_EQ(pmemfile_write(pfp, f, "XYZ\0", 4), 4);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 9 + 100 + 4);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET), 0);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 1));

	/* validate the whole file contents */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, sizeof(data2));
	ASSERT_EQ(r, 9 + 100 + 4) << COND_ERROR(r);
	ASSERT_EQ(memcmp("pmemin S\0", data2, 9), 0);
	ASSERT_EQ(memcmp(data2 + 9, buf00, 100), 0);
	ASSERT_EQ(memcmp("XYZ\0", data2 + 9 + 100, 4), 0);
	ASSERT_EQ(memcmp(data2 + 9 + 100 + 4, bufFF,
			sizeof(data2) - 9 - 100 - 4), 0);

	/* write 4k past the end of file and check the hole is empty */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 9 + 100 + 4);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 4096, PMEMFILE_SEEK_END),
			9 + 100 + 4 + 4096);
	ASSERT_EQ(pmemfile_write(pfp, f, "NEXT BLOCK\0", 11), 11);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 9 + 100 + 4, PMEMFILE_SEEK_SET),
			9 + 100 + 4);
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, 4096);
	ASSERT_EQ(r, 4096) << COND_ERROR(r);
	ASSERT_EQ(memcmp(data2, buf00, 4096), 0);

	pmemfile_close(pfp, f);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0,
			(env_block_size == 4096) ? 2 : 1));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	/* check read after EOF returns 0 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 8192, PMEMFILE_SEEK_SET), 8192);
	r = pmemfile_read(pfp, f, data2, 4096);
	ASSERT_EQ(r, 0) << COND_ERROR(r);

	pmemfile_close(pfp, f);


	EXPECT_TRUE(test_compare_dirs(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 4220, "file1"},
	    {}}));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0,
			(env_block_size == 4096) ? 2 : 1));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 1, 0, 0, 1, 0));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_EXCL |
			PMEMFILE_O_RDWR, 0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* check that writing slightly bigger files and seeking in them works */
	ASSERT_EQ(pmemfile_write(pfp, f, buf00, 4096), 4096);
	ASSERT_EQ(test_pmemfile_file_size(pfp, f), 4096);

	ASSERT_EQ(pmemfile_write(pfp, f, bufFF, 4096), 4096);
	ASSERT_EQ(test_pmemfile_file_size(pfp, f), 8192);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 8192);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 4096, PMEMFILE_SEEK_SET), 4096);
	ASSERT_EQ(test_pmemfile_file_size(pfp, f), 8192);

	r = pmemfile_read(pfp, f, data2, 4096);
	ASSERT_EQ(r, 4096) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_file_size(pfp, f), 8192);

	pmemfile_close(pfp, f);

	EXPECT_TRUE(test_compare_dirs(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 8192, "file1"},
	    {}}));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 1,
			(env_block_size == 4096) ? 2 : 1));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, 2)
{
	/* write 800MB of random data and read it back */
	unsigned char buf00[128], bufFF[128], bufd[4096 * 4], buftmp[4096 * 4];

	memset(buf00, 0x00, sizeof(buf00));
	memset(bufFF, 0xFF, sizeof(bufFF));

	for (size_t i = 0; i < sizeof(bufd); ++i)
		bufd[i] = (unsigned char)(rand() % 255);

	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
			PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY,
			0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

#define LEN (sizeof(bufd) - 1000)
#define LOOPS ((200 * 1024 * 1024) / LEN)
	for (size_t i = 0; i < LOOPS; ++i) {
		ssize_t written = pmemfile_write(pfp, f, bufd, LEN);
		ASSERT_EQ(written, (ssize_t)LEN) << COND_ERROR(written);
	}

	pmemfile_close(pfp, f);

	EXPECT_TRUE(test_compare_dirs(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 209714688, "file1"},
	    {}}));

	if (env_block_size == 4096)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0x32c, 0,
				51200));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 10, 0, 633));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ssize_t r;

	for (size_t i = 0; i < LOOPS; ++i) {
		memset(buftmp, 0, sizeof(buftmp));
		r = pmemfile_read(pfp, f, buftmp, LEN);
		ASSERT_EQ(r, (ssize_t)LEN) << COND_ERROR(r);
		ASSERT_EQ(memcmp(buftmp, bufd, LEN), 0);
	}
#undef LEN
	r = pmemfile_read(pfp, f, buftmp, 1023);
	ASSERT_EQ(r, 0) << COND_ERROR(r);

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, trunc)
{
	/* check that O_TRUNC works */
	char bufFF[128], bufDD[128], buftmp[128];

	memset(bufFF, 0xFF, sizeof(bufFF));
	memset(bufDD, 0xDD, sizeof(bufDD));

	PMEMfile *f1 = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
			PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY, 0644);
	PMEMfile *f2 = pmemfile_open(pfp, "/file2", PMEMFILE_O_CREAT |
			PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY, 0644);
	ASSERT_NE(f1, nullptr) << strerror(errno);
	ASSERT_NE(f2, nullptr) << strerror(errno);

	for (int i = 0; i < 100; ++i) {
		ASSERT_EQ(pmemfile_write(pfp, f1, bufFF, 128), 128);
		ASSERT_EQ(pmemfile_write(pfp, f1, bufDD, 128), 128);

		ASSERT_EQ(pmemfile_write(pfp, f2, bufFF, 128), 128);
		ASSERT_EQ(pmemfile_write(pfp, f2, bufDD, 128), 128);
	}

	pmemfile_close(pfp, f1);
	pmemfile_close(pfp, f2);

	EXPECT_TRUE(test_compare_dirs(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 25600, "file1"},
	    {0100644, 1, 25600, "file2"},
	    {}}));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 3, 0, 0, 0,
			(env_block_size == 4096) ? 14 : 4));

	f1 = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDWR | PMEMFILE_O_TRUNC,
			0);
	ASSERT_NE(f1, nullptr) << strerror(errno);

	f2 = pmemfile_open(pfp, "/file2", PMEMFILE_O_RDWR | PMEMFILE_O_TRUNC,
			0);
	ASSERT_NE(f2, nullptr) << strerror(errno);

	ssize_t r = pmemfile_read(pfp, f1, buftmp, 128);
	ASSERT_EQ(r, 0) << COND_ERROR(r);

	ASSERT_EQ(pmemfile_write(pfp, f2, bufDD, 128), 128);

	pmemfile_close(pfp, f1);
	pmemfile_close(pfp, f2);

	EXPECT_TRUE(test_compare_dirs(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 0, "file1"},
	    {0100644, 1, 128, "file2"},
	    {}}));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 3, 0, 0, 0, 1));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file2"), 0);
}

TEST_F(rw, o_append)
{
	/* check that O_APPEND works */
	char bufFF[128], bufDD[128];
	PMEMfile *f;

	memset(bufFF, 0xFF, sizeof(bufFF));
	memset(bufDD, 0xDD, sizeof(bufDD));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_EXCL |
			PMEMFILE_O_WRONLY | PMEMFILE_O_APPEND, 0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_write(pfp, f, bufFF, 128), 128);
	pmemfile_close(pfp, f);

	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 128);

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_WRONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_write(pfp, f, bufFF, 128), 128);
	pmemfile_close(pfp, f);

	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 128);

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_WRONLY | PMEMFILE_O_APPEND);
	ASSERT_NE(f, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_write(pfp, f, bufDD, 128), 128);
	pmemfile_close(pfp, f);

	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 256);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, sparse_files)
{
	unsigned char buf[8192];
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
			PMEMFILE_O_EXCL | PMEMFILE_O_RDWR, 0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 4096, PMEMFILE_SEEK_SET), 4096);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_write(pfp, f, "test", 5), 5);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 4096 + 5);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET), 0);
	memset(buf, 0xff, sizeof(buf));
	ssize_t r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, 4096 + 5) << COND_ERROR(r);

	ASSERT_EQ(is_zeroed(buf, 4096), 1);
	ASSERT_EQ(memcmp(buf + 4096, "test", 5), 0);
	ASSERT_EQ(buf[4096 + 5], 0xff);

	/* Partially fill the whole */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, SEEK_SET), 1);
	ASSERT_EQ(pmemfile_write(pfp, f, "test", 5), 5);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, SEEK_SET), 0);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, 4096 + 5) << COND_ERROR(r);
	ASSERT_EQ(buf[0], 0);
	ASSERT_EQ(memcmp(buf + 1, "test", 5), 0);
	ASSERT_EQ(is_zeroed(buf + 6, 4096 - 6), 1);
	ASSERT_EQ(memcmp(buf + 4096, "test", 5), 0);

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

int
main(int argc, char *argv[])
{
	START();

	if (argc < 2) {
		fprintf(stderr, "usage: %s global_path", argv[0]);
		exit(1);
	}

	const char *e = getenv("PMEMFILECORE_BLOCK_SIZE");

	if (e == NULL)
		env_block_size = 0;
	else if (strcmp(e, "4096") == 0)
		env_block_size = 4096;
	else {
		fprintf(stderr, "unexpected PMEMFILECORE_BLOCK_SIZE\n");
		exit(1);
	}

	global_path = argv[1];

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
