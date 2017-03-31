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

class rw : public pmemfile_test {
public:
	rw() : pmemfile_test(256 * 1024 * 1024)
	{
	}

protected:
	blkcnt_t
	stat_block_count(PMEMfile *f)
	{
		struct stat stat_buf;

		if (pmemfile_fstat(pfp, f, &stat_buf) != 0) {
			perror("stat_block_count");
			abort();
		}

		return stat_buf.st_blocks;
	}
};

TEST_F(rw, 1)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY,
				    0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4008, "."},
						    {040777, 2, 4008, ".."},
						    {0100644, 1, 0, "file1"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 0));

	const char *data = "Marcin S";
	char data2[4096];
	char bufFF[4096], buf00[4096];
	size_t len = strlen(data) + 1;
	memset(bufFF, 0xff, sizeof(bufFF));
	memset(buf00, 0x00, sizeof(buf00));

	ssize_t written = pmemfile_write(pfp, f, data, len);
	ASSERT_EQ(written, (ssize_t)len) << COND_ERROR(written);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4008, "."},
						    {040777, 2, 4008, ".."},
						    {0100644, 1, 9, "file1"},
					    }));

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

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4008, "."},
						    {040777, 2, 4008, ".."},
						    {0100644, 1, 9, "file1"},
					    }));

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
	ASSERT_EQ(
		memcmp(data2 + 9 + 100 + 4, bufFF, sizeof(data2) - 9 - 100 - 4),
		0);

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

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 2 : 1));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	/* check read after EOF returns 0 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 8192, PMEMFILE_SEEK_SET), 8192);
	r = pmemfile_read(pfp, f, data2, 4096);
	ASSERT_EQ(r, 0) << COND_ERROR(r);

	pmemfile_close(pfp, f);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4008, "."},
						    {040777, 2, 4008, ".."},
						    {0100644, 1, 4220, "file1"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 2 : 1));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 1, 0, 0, 1, 0));

	f = pmemfile_open(pfp, "/file1",
			  PMEMFILE_O_CREAT | PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
			  0644);
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

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4008, "."},
						    {040777, 2, 4008, ".."},
						    {0100644, 1, 8192, "file1"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 1, (env_block_size == 4096) ? 2 : 1));

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

	EXPECT_TRUE(test_compare_dirs(pfp, "/",
				      std::vector<pmemfile_ls>{
					      {040777, 2, 4008, "."},
					      {040777, 2, 4008, ".."},
					      {0100644, 1, 209714688, "file1"},
				      }));

	if (env_block_size == 4096)
		EXPECT_TRUE(
			test_pmemfile_stats_match(pfp, 2, 0, 0x32c, 0, 51200));
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

	PMEMfile *f1 = pmemfile_open(
		pfp, "/file1",
		PMEMFILE_O_CREAT | PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY, 0644);
	PMEMfile *f2 = pmemfile_open(
		pfp, "/file2",
		PMEMFILE_O_CREAT | PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY, 0644);
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

	EXPECT_TRUE(test_compare_dirs(pfp, "/",
				      std::vector<pmemfile_ls>{
					      {040777, 2, 4008, "."},
					      {040777, 2, 4008, ".."},
					      {0100644, 1, 25600, "file1"},
					      {0100644, 1, 25600, "file2"},
				      }));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 3, 0, 0, 0, (env_block_size == 4096) ? 14 : 4));

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

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4008, "."},
						    {040777, 2, 4008, ".."},
						    {0100644, 1, 0, "file1"},
						    {0100644, 1, 128, "file2"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 3, 0, 0, 0, 1));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file2"), 0);
}

TEST_F(rw, ftruncate)
{
	char buf[0x1000];
	char bufFF[sizeof(buf)];
	PMEMfile *f;
	ssize_t r;

	memset(bufFF, 0xff, sizeof(bufFF));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_RDWR, 0);
	ASSERT_NE(f, nullptr) << strerror(errno);

	r = pmemfile_ftruncate(pfp, f, 1024);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 1024);
	r = pmemfile_ftruncate(pfp, f, 10240);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 10240);

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 3 : 1));

	r = pmemfile_ftruncate(pfp, f, 0);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 0));

	static const ssize_t large = 0x100000;

	r = pmemfile_ftruncate(pfp, f, large / 32);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), (large / 32));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 8 : 1));

	r = pmemfile_ftruncate(pfp, f, large + 4);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), large + 4);

	static constexpr char data0[] = "testtest";
	static constexpr ssize_t l0 = sizeof(data0) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_write(pfp, f, data0, l0), l0);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), large + l0);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, bufFF, sizeof(buf)), 0);

	static constexpr char data1[] = "\0\0\0testtest";
	static constexpr ssize_t l1 = sizeof(data1) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_lseek(pfp, f, -3, PMEMFILE_SEEK_CUR), large - 3);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, l1) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, data1, l1), 0);
	ASSERT_EQ(memcmp(buf + l1, bufFF, sizeof(buf) - l1), 0);

	if (env_block_size == 4096)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 4, 0, 257));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 2));

	static constexpr char data2[] = "\0\0\0te";
	static constexpr ssize_t l2 = sizeof(data2) - 1;

	r = pmemfile_ftruncate(pfp, f, large + 2);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), (large + 2));
	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_lseek(pfp, f, -3, PMEMFILE_SEEK_CUR), large - 3);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, l2) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, data2, l2), 0);
	ASSERT_EQ(memcmp(buf + l2, bufFF, sizeof(buf) - l2), 0);

	if (env_block_size == 4096)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 4, 0, 257));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 2));

	static constexpr char data3[] = "\0\0\0te\0\0\0\0\0\0";
	static constexpr ssize_t l3 = sizeof(data3) - 1;

	r = pmemfile_ftruncate(pfp, f, large + 8);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), (large + 8));
	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_lseek(pfp, f, -3, PMEMFILE_SEEK_CUR), large - 3);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, l3) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, data3, l3), 0);
	ASSERT_EQ(memcmp(buf + l3, bufFF, sizeof(buf) - l3), 0);

	if (env_block_size == 4096)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 4, 0, 257));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 2));

	r = pmemfile_ftruncate(pfp, f, 0x100);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x100);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 1));

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, truncate)
{
	char buf[0x1000];
	char bufFF[sizeof(buf)];
	PMEMfile *f;
	ssize_t r;

	memset(bufFF, 0xff, sizeof(bufFF));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_RDWR,
			  PMEMFILE_S_IRWXU);
	ASSERT_NE(f, nullptr) << strerror(errno);

	r = pmemfile_truncate(pfp, "/file1", 1024);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 1024);
	r = pmemfile_truncate(pfp, "/file1", 10240);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 10240);

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 3 : 1));

	r = pmemfile_truncate(pfp, "/file1", 0);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 0));

	static const ssize_t large = 0x100000;

	r = pmemfile_truncate(pfp, "/file1", large / 32);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), (large / 32));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 8 : 1));

	r = pmemfile_truncate(pfp, "/file1", large + 4);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), large + 4);

	static constexpr char data0[] = "testtest";
	static constexpr ssize_t l0 = sizeof(data0) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_write(pfp, f, data0, l0), l0);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), large + l0);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, bufFF, sizeof(buf)), 0);

	static constexpr char data1[] = "\0\0\0testtest";
	static constexpr ssize_t l1 = sizeof(data1) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_lseek(pfp, f, -3, PMEMFILE_SEEK_CUR), large - 3);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, l1) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, data1, l1), 0);
	ASSERT_EQ(memcmp(buf + l1, bufFF, sizeof(buf) - l1), 0);

	if (env_block_size == 4096)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 4, 0, 257));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 2));

	static constexpr char data2[] = "\0\0\0te";
	static constexpr ssize_t l2 = sizeof(data2) - 1;

	r = pmemfile_truncate(pfp, "/file1", large + 2);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), (large + 2));
	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_lseek(pfp, f, -3, PMEMFILE_SEEK_CUR), large - 3);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, l2) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, data2, l2), 0);
	ASSERT_EQ(memcmp(buf + l2, bufFF, sizeof(buf) - l2), 0);

	if (env_block_size == 4096)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 4, 0, 257));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 2));

	static constexpr char data3[] = "\0\0\0te\0\0\0\0\0\0";
	static constexpr ssize_t l3 = sizeof(data3) - 1;

	r = pmemfile_truncate(pfp, "/file1", large + 8);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), (large + 8));
	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_lseek(pfp, f, -3, PMEMFILE_SEEK_CUR), large - 3);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, l3) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, data3, l3), 0);
	ASSERT_EQ(memcmp(buf + l3, bufFF, sizeof(buf) - l3), 0);

	if (env_block_size == 4096)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 4, 0, 257));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 2));

	r = pmemfile_truncate(pfp, "/file1", 0x100);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x100);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 1));

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, fallocate)
{
	char buf[0x1000];
	char buf00[sizeof(buf)];
	char bufFF[sizeof(buf)];
	PMEMfile *f;
	ssize_t r;

	memset(buf00, 0x00, sizeof(buf00));
	memset(bufFF, 0xff, sizeof(bufFF));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_RDWR,
			  PMEMFILE_S_IRWXU);
	ASSERT_NE(f, nullptr) << strerror(errno);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 0));

	/* Allocate a range, file size is expected to remain zero */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FL_KEEP_SIZE, 0x1000, 0x10000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0);

	/*
	 * Allocated a 64K range, expecting a large block, or 16 pieces
	 * of 4K blocks
	 */
	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (0x10000 / 512));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 16 : 1));

	/*
	 * Allocate the same range, file size is expected to change,
	 * but no new allocation should happen.
	 */
	r = pmemfile_fallocate(pfp, f, 0, 0x1000, 0x10000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x1000 + 0x10000);

	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (0x10000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 16 : 1));

	/*
	 * Now remove an interval, that overlaps with the previously
	 * allocated interval.
	 * This should be rounded to the interval: [0x1000, 0x4000) - thus
	 * removing 3 pieces of 4K blocks, or just zeroing out some data.
	 */

	/* But first make sure it is not allowed without the KEEP_SIZE flag */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FL_PUNCH_HOLE, 0x0007, 0x4123);
	ASSERT_EQ(r, -1);
	ASSERT_EQ(errno, EINVAL);

	r = pmemfile_fallocate(pfp, f,
			       PMEMFILE_FL_PUNCH_HOLE | PMEMFILE_FL_KEEP_SIZE,
			       0x0007, 0x4123);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x1000 + 0x10000);

	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (0xd000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 13 : 1));

	/*
	 * Writing some bytes -- this should allocate two new blocks when
	 * the block size is fixed to 4K bytes.
	 */
	static constexpr char data0[] = "testing testy tested tests";
	static constexpr ssize_t l0 = sizeof(data0) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0x1ffe, PMEMFILE_SEEK_SET), 0x1ffe);
	ASSERT_EQ(pmemfile_write(pfp, f, data0, l0), l0);

	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (0xf000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 13 + 2 : 1));

	/*
	 * Try to read the test data, there should be zeroes around it.
	 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0x1ffd, PMEMFILE_SEEK_SET), 0x1ffd);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, sizeof(buf));
	ASSERT_EQ(r, (ssize_t)sizeof(buf)) << COND_ERROR(r);
	ASSERT_EQ(buf[0], '\0');
	ASSERT_EQ(memcmp(buf + 1, data0, l0), 0);
	ASSERT_EQ(memcmp(buf + 1 + l0, buf00, sizeof(buf) - 1 - l0), 0);

	/*
	 * Punch a hole at [0x1fff, 0x4777) interval, which should be
	 * internally translated to the [0x2000, 0x4000) interval.
	 * With fix 4K blocksize, this should remove one of the previously
	 * allocated blocks.
	 */
	r = pmemfile_fallocate(pfp, f,
			       PMEMFILE_FL_PUNCH_HOLE | PMEMFILE_FL_KEEP_SIZE,
			       0x1fff, 0x3000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x1000 + 0x10000);

	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (0xe000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 13 + 1 : 1));

	/*
	 * Try to read the test data, there should be only the first two
	 * characters left at 0x1ffe and 0x1fff - the hole is expected to start
	 * at the 0x2000 offset.
	 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0x1ffd, PMEMFILE_SEEK_SET), 0x1ffd);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, sizeof(buf));
	ASSERT_EQ(r, (ssize_t)sizeof(buf)) << COND_ERROR(r);
	ASSERT_EQ(buf[0], '\0');
	ASSERT_EQ(memcmp(buf + 1, data0, 2), 0);
	ASSERT_EQ(memcmp(buf + 1 + 2, buf00, sizeof(buf) - 1 - 2), 0);

	/*
	 * Allocate an interval well beyond current filesize.
	 * The file has 14 pieces of 4K blocks (or one large block)
	 * before this operation.
	 * This is expected to allocate at least one new block, or in the
	 * case of 4K fixed size blocks, 4 new 4K blocks.
	 * Thus, the result should be 14 + 4 blocks, or 2 blocks.
	 */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FL_KEEP_SIZE, 0x80000, 0x4000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x1000 + 0x10000);
	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (0x12000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 14 + 4 : 1 + 1));

	/*
	 * So, the file size should remain as it was.
	 * By the way, ftruncate is expected to remove such extra blocks
	 * over file size, even though it does not have to alter file size.
	 * The block counts are expected to be 14 or 1 again.
	 */
	r = pmemfile_ftruncate(pfp, f, 0x1000 + 0x10000);
	ASSERT_EQ(r, 0);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x1000 + 0x10000);
	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (0xe000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? 14 : 1));

	/*
	 * Allocate the same new blocks beyond current file size again.
	 * Altering the file size as well this time.
	 */
	/* Remember the the expected size and block counts at this point */
	static constexpr ssize_t size = 0x80000 + 0x4000;
	static constexpr unsigned bc_4k = 14 + 4;
	static constexpr unsigned bc = 2;
	r = pmemfile_fallocate(pfp, f, 0, 0x80000, 0x4000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (bc_4k * 0x1000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? bc_4k : bc));

	/*
	 * There should be a hole somewhere between offsets 0x10000 and 0x80000.
	 * Most likely there is a hole right before 0x80000, so punching a hole
	 * there should be no-op (block counts are expected to remain the same).
	 */
	r = pmemfile_fallocate(pfp, f,
			       PMEMFILE_FL_PUNCH_HOLE | PMEMFILE_FL_KEEP_SIZE,
			       0x73000, 0x2234);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (bc_4k * 0x1000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, 2, 0, 0, 0, (env_block_size == 4096) ? bc_4k : bc));

	/*
	 * How about allocating a lot of single byte intervals?
	 */
	for (ssize_t offset = 77; offset < size; offset += 0x1000) {
		r = pmemfile_fallocate(pfp, f, PMEMFILE_FL_KEEP_SIZE, offset,
				       1);
		ASSERT_EQ(r, 0) << strerror(errno);
	}

	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	if (env_block_size == 0x1000) {
		ASSERT_EQ(stat_block_count(f), (size / 512));
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 2, 0,
						      size / 0x1000));
	}

	/*
	 * Deallocate most of the blocks, leaving only 4K in
	 * at offset 0x13000.
	 */
	r = pmemfile_fallocate(pfp, f,
			       PMEMFILE_FL_PUNCH_HOLE | PMEMFILE_FL_KEEP_SIZE,
			       0, 0x13000);
	ASSERT_EQ(r, 0) << strerror(errno);

	/*
	 * This also tests punching a hole that reaches beyond the last block.
	 */
	r = pmemfile_fallocate(pfp, f,
			       PMEMFILE_FL_PUNCH_HOLE | PMEMFILE_FL_KEEP_SIZE,
			       0x14000, INT64_C(0x10000000));
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	if (env_block_size == 0x1000)
		ASSERT_EQ(stat_block_count(f), (0x1000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 1));

	/*
	 * Remove that one block left.
	 */
	r = pmemfile_fallocate(pfp, f,
			       PMEMFILE_FL_PUNCH_HOLE | PMEMFILE_FL_KEEP_SIZE,
			       0, INT64_C(0x10000000));
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	ASSERT_EQ(stat_block_count(f), 0);
	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 0));

	/*
	 * Punching a hole in a file with no blocks should no
	 * be a problem either.
	 */
	r = pmemfile_fallocate(pfp, f,
			       PMEMFILE_FL_PUNCH_HOLE | PMEMFILE_FL_KEEP_SIZE,
			       1, INT64_C(0x1000000));
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	ASSERT_EQ(stat_block_count(f), 0);
	EXPECT_TRUE(test_pmemfile_stats_match(pfp, 2, 0, 0, 0, 0));

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, o_append)
{
	/* check that O_APPEND works */
	char bufFF[128], bufDD[128];
	PMEMfile *f;

	memset(bufFF, 0xFF, sizeof(bufFF));
	memset(bufDD, 0xDD, sizeof(bufDD));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_EXCL |
				  PMEMFILE_O_WRONLY | PMEMFILE_O_APPEND,
			  0644);
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
					    PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
				    0644);
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

TEST_F(rw, failed_write)
{
	char buf[256];
	ssize_t r;

	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
				    0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_write(pfp, f, "test", 5), 5);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET), 0);

	/*
	 * Writing from an uninitialized buffer.
	 * The write should fail during allocation in pmemfile, before ever
	 * accessing the contents of the buffer - since the requested write size
	 * is larger than the pool size.
	 */
	ASSERT_EQ(pmemfile_write(pfp, f, buf, 1024 * 1024 * 1024), -1);

	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 5);

	r = pmemfile_read(pfp, f, buf, 5);
	ASSERT_EQ(r, 5) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, "test", 5), 0);

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

	const char *e = getenv("PMEMFILE_BLOCK_SIZE");

	if (e == NULL)
		env_block_size = 0;
	else if (strcmp(e, "4096") == 0)
		env_block_size = 4096;
	else {
		fprintf(stderr, "unexpected PMEMFILE_BLOCK_SIZE\n");
		exit(1);
	}

	global_path = argv[1];

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
