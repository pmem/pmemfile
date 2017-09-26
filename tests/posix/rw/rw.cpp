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

#include <cstdint>
#include <sstream>

static unsigned env_block_size;

class rw : public pmemfile_test {
public:
	rw() : pmemfile_test(256 * 1024 * 1024)
	{
	}

protected:
	pmemfile_blkcnt_t
	stat_block_count(PMEMfile *f)
	{
		pmemfile_stat_t stat_buf;

		if (pmemfile_fstat(pfp, f, &stat_buf) != 0) {
			perror("stat_block_count");
			abort();
		}

		return stat_buf.st_blocks;
	}
};

TEST_F(rw, basic)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY,
				    0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4000, "."},
						    {040777, 2, 4000, ".."},
						    {0100644, 1, 0, "file1"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 0));

	const char *data = "Marcin S";
	char data2[4096];
	char bufFF[4096], buf00[4096];
	size_t len = strlen(data) + 1;
	memset(bufFF, 0xff, sizeof(bufFF));
	memset(buf00, 0x00, sizeof(buf00));

	errno = 0;
	ASSERT_EQ(pmemfile_write(pfp, NULL, data, len), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_write(NULL, f, data, len), -1);
	EXPECT_EQ(errno, EFAULT);

	pmemfile_ssize_t written = pmemfile_write(pfp, f, data, len);
	ASSERT_EQ(written, (pmemfile_ssize_t)len) << COND_ERROR(written);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4000, "."},
						    {040777, 2, 4000, ".."},
						    {0100644, 1, 9, "file1"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));

	errno = 0;
	ASSERT_EQ(pmemfile_read(pfp, NULL, data2, len), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_read(NULL, f, data2, len), -1);
	EXPECT_EQ(errno, EFAULT);

	/* try to read write-only file */
	pmemfile_ssize_t r = pmemfile_read(pfp, f, data2, len);
	ASSERT_EQ(r, -1);
	EXPECT_EQ(errno, EBADF);
	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* read only what we wrote and check nothing else was read */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, len);
	ASSERT_EQ(r, (pmemfile_ssize_t)len) << COND_ERROR(r);
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

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* read as much as possible and check that we read only what we wrote */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, sizeof(data2));
	ASSERT_EQ(r, (pmemfile_ssize_t)len);
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
						    {040777, 2, 4000, "."},
						    {040777, 2, 4000, ".."},
						    {0100644, 1, 9, "file1"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDWR);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* check that what we wrote previously is still there */
	memset(data2, 0xff, sizeof(data2));
	r = pmemfile_read(pfp, f, data2, sizeof(data2));
	ASSERT_EQ(r, 9) << COND_ERROR(r);
	ASSERT_EQ(memcmp("pmem", data2, 4), 0);
	ASSERT_EQ(memcmp(data + 4, data2 + 4, 5), 0);
	ASSERT_EQ(memcmp(data2 + 9, bufFF, sizeof(data2) - 9), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, NULL, 0, PMEMFILE_SEEK_CUR), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(NULL, f, 0, PMEMFILE_SEEK_CUR), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, -1), -1);
	EXPECT_EQ(errno, EINVAL);

	pmemfile_close(pfp, f);

	/* validate SEEK_CUR */
	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDWR);
	ASSERT_NE(f, nullptr) << strerror(errno);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 0);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 3, PMEMFILE_SEEK_CUR), 3);

	/* validate some lseek argument checking */
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, -0x1000, PMEMFILE_SEEK_CUR), -1);
	ASSERT_EQ(errno, EINVAL);
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, -1, PMEMFILE_SEEK_SET), -1);
	ASSERT_EQ(errno, EINVAL);
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, INT64_MAX, PMEMFILE_SEEK_CUR), -1);
	ASSERT_EQ(errno, EINVAL);
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, INT64_MAX - 1, PMEMFILE_SEEK_CUR), -1);
	ASSERT_EQ(errno, EINVAL);
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, INT64_MAX - 1, PMEMFILE_SEEK_END), -1);
	ASSERT_EQ(errno, EINVAL);
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, INT64_MIN, PMEMFILE_SEEK_END), -1);
	ASSERT_EQ(errno, EINVAL);

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

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));

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
		pfp, root_count() + 1, 0, 0, (env_block_size == 4096) ? 2 : 1));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	/* check read after EOF returns 0 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 8192, PMEMFILE_SEEK_SET), 8192);
	r = pmemfile_read(pfp, f, data2, 4096);
	ASSERT_EQ(r, 0) << COND_ERROR(r);

	pmemfile_close(pfp, f);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4000, "."},
						    {040777, 2, 4000, ".."},
						    {0100644, 1, 4220, "file1"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 4096) ? 2 : 1));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count(), 0, 0, 0));

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
						    {040777, 2, 4000, "."},
						    {040777, 2, 4000, ".."},
						    {0100644, 1, 8192, "file1"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 4096) ? 2 : 1));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, huge_file)
{
	/* write 200MB of random data and read it back */
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
		pmemfile_ssize_t written = pmemfile_write(pfp, f, bufd, LEN);
		ASSERT_EQ(written, (pmemfile_ssize_t)LEN)
			<< COND_ERROR(written);
	}

	pmemfile_close(pfp, f);

	EXPECT_TRUE(test_compare_dirs(pfp, "/",
				      std::vector<pmemfile_ls>{
					      {040777, 2, 4000, "."},
					      {040777, 2, 4000, ".."},
					      {0100644, 1, 209714688, "file1"},
				      }));

	if (env_block_size == 0x4000)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1,
						      0, 203, 12800));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1,
						      0, 12, 800));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);
	pmemfile_ssize_t r;

	for (size_t i = 0; i < LOOPS; ++i) {
		memset(buftmp, 0, sizeof(buftmp));
		r = pmemfile_read(pfp, f, buftmp, LEN);
		ASSERT_EQ(r, (pmemfile_ssize_t)LEN) << COND_ERROR(r);
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
					      {040777, 2, 4000, "."},
					      {040777, 2, 4000, ".."},
					      {0100644, 1, 25600, "file1"},
					      {0100644, 1, 25600, "file2"},
				      }));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 2, 0, 0, 4));

	f1 = pmemfile_open(pfp, "/file1", PMEMFILE_O_RDWR | PMEMFILE_O_TRUNC,
			   0);
	ASSERT_NE(f1, nullptr) << strerror(errno);

	f2 = pmemfile_open(pfp, "/file2", PMEMFILE_O_RDWR | PMEMFILE_O_TRUNC,
			   0);
	ASSERT_NE(f2, nullptr) << strerror(errno);

	pmemfile_ssize_t r = pmemfile_read(pfp, f1, buftmp, 128);
	ASSERT_EQ(r, 0) << COND_ERROR(r);

	ASSERT_EQ(pmemfile_write(pfp, f2, bufDD, 128), 128);

	pmemfile_close(pfp, f1);
	pmemfile_close(pfp, f2);

	EXPECT_TRUE(
		test_compare_dirs(pfp, "/", std::vector<pmemfile_ls>{
						    {040777, 2, 4000, "."},
						    {040777, 2, 4000, ".."},
						    {0100644, 1, 0, "file1"},
						    {0100644, 1, 128, "file2"},
					    }));

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 2, 0, 0, 1));

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file2"), 0);
}

TEST_F(rw, ftruncate)
{
	char buf[0x1000];
	char bufFF[sizeof(buf)];
	PMEMfile *f;
	pmemfile_ssize_t r;

	memset(bufFF, 0xff, sizeof(bufFF));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_RDWR, 0);
	ASSERT_NE(f, nullptr) << strerror(errno);

	errno = 0;
	ASSERT_EQ(pmemfile_ftruncate(pfp, NULL, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_ftruncate(NULL, f, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_ftruncate(pfp, f, -1), -1);
	EXPECT_EQ(errno, EINVAL);

	r = pmemfile_ftruncate(pfp, f, 1024);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 1024);
	r = pmemfile_ftruncate(pfp, f, 40960);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 40960);

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 3 : 2));

	r = pmemfile_ftruncate(pfp, f, 0);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 0));

	static const pmemfile_ssize_t large = 0x100000;

	r = pmemfile_ftruncate(pfp, f, large / 32);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), (large / 32));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 2 : 1));

	r = pmemfile_ftruncate(pfp, f, large + 4);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), large + 4);

	static constexpr char data0[] = "testtest";
	static constexpr pmemfile_ssize_t l0 = sizeof(data0) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_write(pfp, f, data0, l0), l0);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), large + l0);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, bufFF, sizeof(buf)), 0);

	static constexpr char data1[] = "\0\0\0testtest";
	static constexpr pmemfile_ssize_t l1 = sizeof(data1) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_lseek(pfp, f, -3, PMEMFILE_SEEK_CUR), large - 3);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, l1) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, data1, l1), 0);
	ASSERT_EQ(memcmp(buf + l1, bufFF, sizeof(buf) - l1), 0);

	if (env_block_size == 0x4000)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 1, 65));
	else {
		// 256K + 2 * 2M because of overallocate
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 3));
	}

	static constexpr char data2[] = "\0\0\0te";
	static constexpr pmemfile_ssize_t l2 = sizeof(data2) - 1;

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

	if (env_block_size == 0x4000)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 1, 65));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 2));

	static constexpr char data3[] = "\0\0\0te\0\0\0\0\0\0";
	static constexpr pmemfile_ssize_t l3 = sizeof(data3) - 1;

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

	if (env_block_size == 0x4000)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 1, 65));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 2));

	r = pmemfile_ftruncate(pfp, f, 0x100);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x100);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0777), 0);
	f = pmemfile_open(pfp, "/dir", PMEMFILE_O_DIRECTORY, 0);
	ASSERT_NE(f, nullptr);

	errno = 0;
	ASSERT_EQ(pmemfile_ftruncate(pfp, f, 0), -1);
	EXPECT_EQ(errno, EINVAL);

	pmemfile_close(pfp, f);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);

	errno = 0;
	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_RDONLY,
			  0);
	ASSERT_NE(f, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_ftruncate(pfp, f, 4), -1);
	EXPECT_EQ(errno, EINVAL);

	pmemfile_close(pfp, f);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, truncate)
{
	char buf[0x1000];
	char bufFF[sizeof(buf)];
	PMEMfile *f;
	pmemfile_ssize_t r;

	memset(bufFF, 0xff, sizeof(bufFF));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_RDWR,
			  PMEMFILE_S_IRWXU);
	ASSERT_NE(f, nullptr) << strerror(errno);

	errno = 0;
	ASSERT_EQ(pmemfile_truncate(pfp, NULL, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_truncate(NULL, "/file1", 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_truncate(pfp, "/file1", -1), -1);
	EXPECT_EQ(errno, EINVAL);

	errno = 0;
	ASSERT_EQ(pmemfile_truncate(pfp, "/file-not-exists", 0), -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir", 0777), 0);
	errno = 0;
	ASSERT_EQ(pmemfile_truncate(pfp, "/dir", 0), -1);
	EXPECT_EQ(errno, EISDIR);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir"), 0);

	r = pmemfile_truncate(pfp, "file1", 1024);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 1024);
	r = pmemfile_truncate(pfp, "/file1", 40960);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 40960);

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 3 : 2));

	r = pmemfile_truncate(pfp, "/file1", 0);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 0));

	static const pmemfile_ssize_t large = 0x100000;

	r = pmemfile_truncate(pfp, "/file1", large / 32);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), (large / 32));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 2 : 1));

#ifdef FAULT_INJECTION
	pmemfile_gid_t groups[1] = {1002};
	ASSERT_EQ(pmemfile_setgroups(pfp, 1, groups), 0);
	pmemfile_inject_fault_at(PF_MALLOC, 1, "copy_cred");
	errno = 0;
	ASSERT_EQ(pmemfile_truncate(pfp, "/file1", large + 4), -1);
	EXPECT_EQ(errno, ENOMEM);
#endif

	r = pmemfile_truncate(pfp, "/file1", large + 4);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), large + 4);

	static constexpr char data0[] = "testtest";
	static constexpr pmemfile_ssize_t l0 = sizeof(data0) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_write(pfp, f, data0, l0), l0);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), large + l0);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, bufFF, sizeof(buf)), 0);

	static constexpr char data1[] = "\0\0\0testtest";
	static constexpr pmemfile_ssize_t l1 = sizeof(data1) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, large, PMEMFILE_SEEK_SET), large);
	ASSERT_EQ(pmemfile_lseek(pfp, f, -3, PMEMFILE_SEEK_CUR), large - 3);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 8192);
	ASSERT_EQ(r, l1) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, data1, l1), 0);
	ASSERT_EQ(memcmp(buf + l1, bufFF, sizeof(buf) - l1), 0);

	if (env_block_size == 0x4000)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 1, 65));
	else {
		// 256K + 2 * 2M because of overallocate
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 3));
	}

	static constexpr char data2[] = "\0\0\0te";
	static constexpr pmemfile_ssize_t l2 = sizeof(data2) - 1;

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

	if (env_block_size == 0x4000)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 1, 65));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 2));

	static constexpr char data3[] = "\0\0\0te\0\0\0\0\0\0";
	static constexpr pmemfile_ssize_t l3 = sizeof(data3) - 1;

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

	if (env_block_size == 0x4000)
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 1, 65));
	else
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 2));

	r = pmemfile_truncate(pfp, "/file1", 0x100);
	ASSERT_EQ(r, 0) << COND_ERROR(r);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x100);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, fallocate)
{
	char buf[0x1000];
	char buf00[sizeof(buf)];
	char bufFF[sizeof(buf)];
	PMEMfile *f;
	pmemfile_ssize_t r;

	memset(buf00, 0x00, sizeof(buf00));
	memset(bufFF, 0xff, sizeof(bufFF));

	f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_RDWR,
			  PMEMFILE_S_IRWXU);
	ASSERT_NE(f, nullptr) << strerror(errno);

	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 0));

	/* Allocate a range, file size is expected to remain zero */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_KEEP_SIZE, 0x4000,
			       0x40000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0);

	/*
	 * Allocated a 256K range, expecting a 2 large blocks, or 16 pieces
	 * of 16K blocks
	 */
	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (0x40000 / 512));

	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 16 : 2));

	/*
	 * Allocate the same range, file size is expected to change,
	 * but no new allocation should happen.
	 */
	r = pmemfile_fallocate(pfp, f, 0, 0x4000, 0x40000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x4000 + 0x40000);

	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (0x40000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 16 : 2));

	/*
	 * Now remove an interval, that overlaps with the previously
	 * allocated interval.
	 * This should be rounded to the interval: [0x4000, 0x10000) - thus
	 * removing 3 pieces of 16K blocks, or just zeroing out some data.
	 */

	/* But first make sure it is not allowed without the KEEP_SIZE flag */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_PUNCH_HOLE, 0x0007,
			       0x11230);

	ASSERT_EQ(r, -1);
	ASSERT_EQ(errno, EOPNOTSUPP);

	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				       PMEMFILE_FALLOC_FL_KEEP_SIZE,
			       0x0007, 0x11230);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x4000 + 0x40000);

	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (13 * 0x4000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 13 : 2));

	/*
	 * Writing some bytes -- this should allocate two new blocks when
	 * the block size is fixed to 16K bytes.
	 */
	static constexpr char data0[] = "testing testy tested tests";
	static constexpr pmemfile_ssize_t l0 = sizeof(data0) - 1;

	ASSERT_EQ(pmemfile_lseek(pfp, f, 2 * 16384 - 2, PMEMFILE_SEEK_SET),
		  2 * 16384 - 2);
	ASSERT_EQ(pmemfile_write(pfp, f, data0, l0), l0);

	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (15 * 0x4000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 13 + 2 : 2));

	/*
	 * Try to read the test data, there should be zeroes around it.
	 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 2 * 16384 - 3, PMEMFILE_SEEK_SET),
		  2 * 16384 - 3);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, sizeof(buf));
	ASSERT_EQ(r, (pmemfile_ssize_t)sizeof(buf)) << COND_ERROR(r);
	ASSERT_EQ(buf[0], '\0');
	ASSERT_EQ(memcmp(buf + 1, data0, l0), 0);
	ASSERT_EQ(memcmp(buf + 1 + l0, buf00, sizeof(buf) - 1 - l0), 0);

	/*
	 * Punch a hole at [0x7fff, 0x14000) interval, which should be
	 * internally translated to the [0x8000, 0x10000) interval.
	 * Bytes at 0x7fff and [0x8000, 0x14000) should be zeroed.
	 * With fix 16K blocksize, this should remove one of the previously
	 * allocated blocks.
	 */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				       PMEMFILE_FALLOC_FL_KEEP_SIZE,
			       0x7fff, 0xC000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x4000 + 0x40000);

	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (14 * 0x4000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 13 + 1 : 2));

	/*
	 * Try to read the test data, there should be only the first character
	 * left at 0x7fff - the hole is expected to start at the 0x8000 offset
	 * and 0x7fff is set to 0.
	 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0x7ffd, PMEMFILE_SEEK_SET), 0x7ffd);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, sizeof(buf));
	ASSERT_EQ(r, (pmemfile_ssize_t)sizeof(buf)) << COND_ERROR(r);
	ASSERT_EQ(buf[0], '\0');
	ASSERT_EQ(buf[1], data0[0]);
	ASSERT_EQ(buf[2], '\0');
	ASSERT_EQ(memcmp(buf + 1 + 2, buf00, sizeof(buf) - 1 - 2), 0);

	/*
	 * Allocate an interval well beyond current file size.
	 * The file has 14 pieces of 16K blocks (or 2 large blocks)
	 * before this operation.
	 * This is expected to allocate at least one new block, or in the
	 * case of 16K fixed size blocks, 4 new 16K blocks.
	 * Thus, the result should be 14 + 4 blocks, or 2 blocks.
	 */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_KEEP_SIZE, 0x400000,
			       0x10000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x4000 + 0x40000);
	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (18 * 0x4000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 14 + 4 : 2 + 1));

	/*
	 * So, the file size should remain as it was.
	 * By the way, ftruncate is expected to remove such extra blocks
	 * over file size, even though it does not have to alter file size.
	 * The block counts are expected to be 14 or 1 again.
	 */
	r = pmemfile_ftruncate(pfp, f, 0x4000 + 0x40000);
	ASSERT_EQ(r, 0);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0x4000 + 0x40000);
	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (14 * 0x4000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? 14 : 1));

	/*
	 * Allocate the same new blocks beyond current file size again.
	 * Altering the file size as well this time.
	 */
	/* Remember the expected size and block counts at this point */
	static constexpr pmemfile_ssize_t size = 0x400000 + 0x10000;
	static constexpr unsigned bc_4k = 14 + 4;
	static constexpr unsigned bc = 2;
	r = pmemfile_fallocate(pfp, f, 0, 0x400000, 0x10000);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (bc_4k * 0x4000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? bc_4k : bc));

	/*
	 * There should be a hole somewhere between offsets 0x40000 and
	 * 0x400000.
	 * Most likely there is a hole right before 0x400000, so punching a hole
	 * there should be no-op (block counts are expected to remain the same).
	 */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				       PMEMFILE_FALLOC_FL_KEEP_SIZE,
			       0x300000, 0x2234);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (bc_4k * 0x4000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(
		pfp, root_count() + 1, 0, 0, (env_block_size == 0x4000) ? bc_4k : bc));

	/*
	 * How about allocating a lot of single byte intervals?
	 */
	for (pmemfile_ssize_t offset = 77; offset < size; offset += 0x4000) {
		r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_KEEP_SIZE,
				       offset, 1);
		ASSERT_EQ(r, 0) << strerror(errno);
	}

	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	if (env_block_size == 0x4000) {
		ASSERT_EQ(stat_block_count(f), (size / 512));
		EXPECT_TRUE(
			test_pmemfile_stats_match(pfp, root_count() + 1, 0, 4, size / 0x4000));
	}

	/*
	 * Deallocate most of the blocks, leaving only 16K in
	 * at offset 0x13000.
	 */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				       PMEMFILE_FALLOC_FL_KEEP_SIZE,
			       0, 0x13000);
	ASSERT_EQ(r, 0) << strerror(errno);

	/*
	 * This also tests punching a hole that reaches beyond the last block.
	 */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				       PMEMFILE_FALLOC_FL_KEEP_SIZE,
			       0x14000, INT64_C(0x10000000));
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	if (env_block_size == 0x4000)
		ASSERT_EQ(stat_block_count(f), (0x4000 / 512));
	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));

	/*
	 * Remove that one block left.
	 */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				       PMEMFILE_FALLOC_FL_KEEP_SIZE,
			       0, INT64_C(0x10000000));
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	ASSERT_EQ(stat_block_count(f), 0);
	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 0));

	/*
	 * Punching a hole in a file with no blocks should no
	 * be a problem either.
	 */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				       PMEMFILE_FALLOC_FL_KEEP_SIZE,
			       1, INT64_C(0x1000000));
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);
	ASSERT_EQ(stat_block_count(f), 0);
	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 0));

	r = pmemfile_posix_fallocate(pfp, f, size - 1, 2);
	ASSERT_EQ(r, 0) << strerror(errno);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size + 1);
	if (env_block_size == 0x4000) {
		ASSERT_EQ(stat_block_count(f), (2 * 0x4000 / 512));
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 2));
	} else {
		EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));
	}

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

TEST_F(rw, sparse_files_using_lseek)
{
	pmemfile_ssize_t size;
	pmemfile_ssize_t r;
	pmemfile_ssize_t hole;
	pmemfile_ssize_t hole_end;
	unsigned char buf[2 * 16383];
	PMEMfile *f;

	f = pmemfile_open(pfp, "/", PMEMFILE_O_DIRECTORY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/*
	 * SEEK_DATA - Directory does not have holes, so if offset passed is
	 * smaller than end offset, then it should be returned.
	 *
	 * Current directory should contain 2 dirents: '.' and '..'
	 */
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, -1, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, EINVAL);
	EXPECT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_DATA), 0);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_DATA), 1);

	/* get last offset in directory */
	pmemfile_off_t end = pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_END);

	ASSERT_EQ(pmemfile_lseek(pfp, f, end - 1, PMEMFILE_SEEK_DATA), end - 1);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, end + 1, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	/*
	 * SEEK_HOLE - if passed offset is smaller than end offset,
	 * the end offset should be returned
	 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, -1, PMEMFILE_SEEK_HOLE), end);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_HOLE), end);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_HOLE), end);
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, end, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file1",
			  PMEMFILE_O_CREAT | PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
			  0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/*
	 * Seeking to data, or to hole should fail with offset
	 * equal to file size
	 */
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	/* Seeking to data, or to hole should fail with negative offset */
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, -1, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, -1, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	/*
	 * Seeking to hole, or to data should fail with offset
	 * greater than the file size.
	 */
	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	/* creating a sparse file using seek + write */
	size = 16384 + 5;
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16384, PMEMFILE_SEEK_SET), 16384);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_write(pfp, f, "test", 5), 5);
	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), size);

	/* Expecting a 16K hole followed by a single block containing the data
	 */
	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));

	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_HOLE), 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_HOLE), 0);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16383, PMEMFILE_SEEK_HOLE), 16383);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16384, PMEMFILE_SEEK_HOLE), size);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16385, PMEMFILE_SEEK_HOLE), size);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_DATA), 16384);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_DATA), 16384);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16382, PMEMFILE_SEEK_DATA), 16384);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16383, PMEMFILE_SEEK_DATA), 16384);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16384, PMEMFILE_SEEK_DATA), 16384);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16385, PMEMFILE_SEEK_DATA), 16385);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16386, PMEMFILE_SEEK_DATA), 16386);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	/* Read the whole file */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET), 0);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 2 * 16384);
	ASSERT_EQ(r, 16384 + 5) << COND_ERROR(r);

	/* The 16K hole at the beginning should read as zero */
	ASSERT_EQ(is_zeroed(buf, 16384), 1);
	ASSERT_EQ(memcmp(buf + 16384, "test", 5), 0);
	ASSERT_EQ(buf[16384 + 5], 0xff);

	/* Fill the whole */
	/*
	 * After this write, expecting a 16K block at the beginning of the
	 * file, with the old block following it immediately.
	 * Thus, no holes left in the file.
	 */
	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 1));
	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, SEEK_SET), 1);
	ASSERT_EQ(pmemfile_write(pfp, f, "test", 5), 5);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, SEEK_SET), 0);
	memset(buf, 0xff, sizeof(buf));
	r = pmemfile_read(pfp, f, buf, 2 * 16384);
	ASSERT_EQ(r, 16384 + 5) << COND_ERROR(r);
	ASSERT_EQ(buf[0], 0);
	ASSERT_EQ(memcmp(buf + 1, "test", 5), 0);
	ASSERT_EQ(is_zeroed(buf + 6, 16384 - 6), 1);
	ASSERT_EQ(memcmp(buf + 16384, "test", 5), 0);
	EXPECT_TRUE(test_pmemfile_stats_match(pfp, root_count() + 1, 0, 0, 2));

	/*
	 * Now that there are no holes, seeking to data should simply just set
	 * the offset to the given argument, and seeking to hole should seek to
	 * the end of the file.
	 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_HOLE), size);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_HOLE), size);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16383, PMEMFILE_SEEK_HOLE), size);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16384, PMEMFILE_SEEK_HOLE), size);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16385, PMEMFILE_SEEK_HOLE), size);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_DATA), 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_DATA), 0);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16382, PMEMFILE_SEEK_DATA), 16382);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16383, PMEMFILE_SEEK_DATA), 16383);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16384, PMEMFILE_SEEK_DATA), 16384);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16385, PMEMFILE_SEEK_DATA), 16385);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16386, PMEMFILE_SEEK_DATA), 16386);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	/*
	 * The following tests would become too complicated without assuming
	 * a fixed block size. Punching holes in files might zero out some
	 * parts of some blocks, and deallocate some other blocks, while
	 * seeking to hole/data only seeks to block boundaries.
	 */
	if (env_block_size != 0x4000)
		goto end;

	/*
	 * Making a hole at the end of the file. The only interface in the API
	 * that can achieve this is fallocate.
	 */
	size = 0x40000;
	hole = size / 2; /* The hole starts at this offset */
	r = pmemfile_ftruncate(pfp, f, size);
	ASSERT_EQ(r, 0) << strerror(errno);
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				       PMEMFILE_FALLOC_FL_KEEP_SIZE,
			       hole, size);
	ASSERT_EQ(r, 0) << strerror(errno);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16383, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole - 1, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole + 1, PMEMFILE_SEEK_HOLE),
		  hole + 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, size - 1, PMEMFILE_SEEK_HOLE),
		  size - 1);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_DATA), 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_DATA), 0);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16382, PMEMFILE_SEEK_DATA), 16382);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole - 1, PMEMFILE_SEEK_DATA),
		  hole - 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole, PMEMFILE_SEEK_DATA), size);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole + 1, PMEMFILE_SEEK_DATA), size);
	ASSERT_EQ(pmemfile_lseek(pfp, f, size - 1, PMEMFILE_SEEK_DATA), size);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	/*
	 * Now try the same thing, but with some blocks allocated at offset
	 * greater than file size. Seeking should always ignore such data,
	 * and not seek further than the file size.
	 */
	r = pmemfile_fallocate(pfp, f, PMEMFILE_FALLOC_FL_KEEP_SIZE, 4 * size,
			       0x2000);
	ASSERT_EQ(r, 0) << strerror(errno);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16384, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole - 1, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole + 1, PMEMFILE_SEEK_HOLE),
		  hole + 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, size - 1, PMEMFILE_SEEK_HOLE),
		  size - 1);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_DATA), 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_DATA), 0);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16382, PMEMFILE_SEEK_DATA), 16382);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole - 1, PMEMFILE_SEEK_DATA),
		  hole - 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole, PMEMFILE_SEEK_DATA), size);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole + 1, PMEMFILE_SEEK_DATA), size);
	ASSERT_EQ(pmemfile_lseek(pfp, f, size - 1, PMEMFILE_SEEK_DATA), size);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	/*
	 * Increasing file size, to include to new blocks previously allocated.
	 * This time, there is a hole in the middle of the file.
	 */
	r = pmemfile_fallocate(pfp, f, 0, 4 * size, 0x1000);
	ASSERT_EQ(r, 0) << strerror(errno);

	hole_end = 4 * size;
	size = 4 * size + 0x1000;

	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16382, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole - 1, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole, PMEMFILE_SEEK_HOLE), hole);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole + 1, PMEMFILE_SEEK_HOLE),
		  hole + 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, size - 1, PMEMFILE_SEEK_HOLE), size);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_HOLE), -1);
	EXPECT_EQ(errno, ENXIO);

	ASSERT_EQ(pmemfile_lseek(pfp, f, hole_end - 1, PMEMFILE_SEEK_HOLE),
		  hole_end - 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole_end, PMEMFILE_SEEK_HOLE), size);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_DATA), 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_DATA), 0);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 16382, PMEMFILE_SEEK_DATA), 16382);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole - 1, PMEMFILE_SEEK_DATA),
		  hole - 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole, PMEMFILE_SEEK_DATA), hole_end);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole + 1, PMEMFILE_SEEK_DATA),
		  hole_end);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole - 1, PMEMFILE_SEEK_DATA),
		  hole - 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole_end, PMEMFILE_SEEK_DATA),
		  hole_end);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole_end + 1, PMEMFILE_SEEK_DATA),
		  hole_end + 1);
	ASSERT_EQ(pmemfile_lseek(pfp, f, hole_end - 1, PMEMFILE_SEEK_DATA),
		  hole_end);
	ASSERT_EQ(pmemfile_lseek(pfp, f, size - 1, PMEMFILE_SEEK_DATA),
		  size - 1);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

	errno = 0;
	ASSERT_EQ(pmemfile_lseek(pfp, f, size + 1, PMEMFILE_SEEK_DATA), -1);
	EXPECT_EQ(errno, ENXIO);

end:
	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, failed_write)
{
	char buf[256];
	pmemfile_ssize_t r;

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
	EXPECT_EQ(errno, ENOSPC);

	ASSERT_EQ(test_pmemfile_path_size(pfp, "/file1"), 5);

	r = pmemfile_read(pfp, f, buf, 5);
	ASSERT_EQ(r, 5) << COND_ERROR(r);
	ASSERT_EQ(memcmp(buf, "test", 5), 0);

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, pwrite)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
				    0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	char buf[100];

	errno = 0;
	ASSERT_EQ(pmemfile_pwrite(pfp, NULL, buf, sizeof(buf), 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_pwrite(NULL, f, buf, sizeof(buf), 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_pwrite(pfp, f, NULL, sizeof(buf), 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_pwrite(pfp, f, buf, sizeof(buf), -1), -1);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 0);

	ASSERT_EQ(pmemfile_pwrite(pfp, f, "test1234567890", 14, 0), 14);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 0);

	ASSERT_EQ(pmemfile_write(pfp, f, "blabla", 6), 6);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 6);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET), 0);

	char buf0xff[100];
	memset(buf, 0xff, sizeof(buf));
	memset(buf0xff, 0xff, sizeof(buf0xff));

	ASSERT_EQ(pmemfile_read(pfp, f, buf, sizeof(buf)), 14);
	ASSERT_EQ(memcmp(buf, "blabla34567890", 14), 0);
	ASSERT_EQ(memcmp(buf0xff, buf + 14, sizeof(buf) - 14), 0);

	pmemfile_close(pfp, f);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, pread)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
				    0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 0);

	ASSERT_EQ(pmemfile_write(pfp, f, "test1234567890", 14), 14);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 14);

	ASSERT_EQ(pmemfile_write(pfp, f, "wxyz!@#$%^&*()", 14), 14);
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 28);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 14, PMEMFILE_SEEK_SET), 14);

	char buf[100];

	errno = 0;
	ASSERT_EQ(pmemfile_pread(pfp, NULL, buf, sizeof(buf), 10), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_pread(NULL, f, buf, sizeof(buf), 10), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_pread(pfp, f, NULL, sizeof(buf), 10), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_pread(pfp, f, buf, sizeof(buf), -1), -1);
	EXPECT_EQ(errno, EINVAL);

	char buf0xff[100];
	memset(buf, 0xff, sizeof(buf));
	memset(buf0xff, 0xff, sizeof(buf0xff));

	ASSERT_EQ(pmemfile_pread(pfp, f, buf, sizeof(buf), 10), 28 - 10);
	ASSERT_EQ(memcmp(buf, "7890wxyz!@#$%^&*()", 28 - 10), 0);
	ASSERT_EQ(memcmp(buf0xff, buf + 28 - 10, sizeof(buf) - (28 - 10)), 0);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 14);

	pmemfile_close(pfp, f);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

static constexpr char poison_pattern = 0x66;

static constexpr char
fill_pattern(size_t i)
{
	return (char)(0xc0 + i);
}

static PMEMfile *
prepare_file(PMEMfilepool *pfp)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
				    0644);
	EXPECT_NE(f, nullptr) << strerror(errno);
	if (f == nullptr)
		return nullptr;

	char buf[10];
	for (size_t i = 0; i < 20; ++i) {
		memset(buf, fill_pattern(i), sizeof(buf));
		ssize_t ret = pmemfile_write(pfp, f, buf, sizeof(buf));
		EXPECT_GT(ret, 0);
		if (ret < 0)
			return nullptr;
		EXPECT_EQ((size_t)ret, sizeof(buf));
		if ((size_t)ret != sizeof(buf))
			return nullptr;
	}

	ssize_t r = pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET);
	EXPECT_EQ(r, 0);
	if (r)
		return nullptr;

	return f;
}

static std::string
dump_buf(char *buf, size_t len)
{
	std::stringstream ss;
	for (size_t i = 0; i < len; ++i)
		ss << " " << std::hex << (unsigned)(unsigned char)buf[i];
	return ss.str();
}

TEST_F(rw, readv)
{
	PMEMfile *f = prepare_file(pfp);
	ASSERT_NE(f, nullptr);

	constexpr size_t vec_size = 40;
	constexpr size_t arr_len = 5;
	char buf[10];
	char bufs[vec_size][arr_len];
	pmemfile_iovec_t vec[vec_size];

	for (size_t i = 0; i < vec_size; ++i) {
		memset(bufs[i], poison_pattern, arr_len);
		vec[i].iov_base = bufs[i];
		vec[i].iov_len = arr_len;
	}

	errno = 0;
	ASSERT_EQ(pmemfile_readv(pfp, NULL, vec, vec_size), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_readv(NULL, f, vec, vec_size), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_readv(pfp, f, NULL, vec_size), -1);
	EXPECT_EQ(errno, EFAULT);

	ASSERT_EQ(pmemfile_readv(pfp, f, vec, 0), 0);

	ssize_t ret = pmemfile_readv(pfp, f, vec, vec_size);
	ASSERT_GT(ret, 0);
	ASSERT_EQ((size_t)ret, vec_size * arr_len);

	for (size_t i = 0; i < vec_size; ++i) {
		memset(buf, fill_pattern(i / 2), arr_len);

		EXPECT_EQ(memcmp(bufs[i], buf, arr_len), 0)
			<< i << " expected:" << dump_buf(buf, arr_len)
			<< " got:" << dump_buf(bufs[i], arr_len);
	}

	pmemfile_close(pfp, f);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

TEST_F(rw, preadv)
{
	PMEMfile *f = prepare_file(pfp);
	ASSERT_NE(f, nullptr);

	constexpr size_t vec_size = 40;
	constexpr size_t arr_len = 5;
	char buf[10];
	char bufs[vec_size][arr_len];
	pmemfile_iovec_t vec[vec_size];

	for (size_t i = 0; i < vec_size; ++i) {
		memset(bufs[i], poison_pattern, arr_len);
		vec[i].iov_base = bufs[i];
		vec[i].iov_len = arr_len;
	}

	errno = 0;
	ASSERT_EQ(pmemfile_preadv(pfp, NULL, vec, vec_size, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_preadv(NULL, f, vec, vec_size, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_preadv(pfp, f, NULL, vec_size, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_preadv(pfp, f, vec, vec_size, -1), -1);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_EQ(pmemfile_preadv(pfp, f, vec, 0, 1), 0);

	ssize_t ret = pmemfile_preadv(pfp, f, vec, vec_size, 1);
	ASSERT_GT(ret, 0);
	ASSERT_EQ((size_t)ret, vec_size * arr_len - 1);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 0);

	for (size_t i = 0; i < vec_size; ++i) {
		size_t len = arr_len;

		memset(buf, fill_pattern(i / 2), len);
		if (i % 2 == 1) {
			buf[4] = fill_pattern((i + 1) / 2);
			/* last vector is shorter because of initial offset */
			if (i == vec_size - 1)
				len--;
		}

		EXPECT_EQ(memcmp(bufs[i], buf, len), 0)
			<< i << " expected:" << dump_buf(buf, len)
			<< " got:" << dump_buf(bufs[i], len);
	}

	pmemfile_close(pfp, f);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

static bool
test_writev(PMEMfilepool *pfp, const size_t vec_size, const size_t arr_len)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
				    0644);
	EXPECT_NE(f, nullptr) << strerror(errno);
	if (!f)
		return false;

	char bufs[vec_size][arr_len];
	pmemfile_iovec_t vec[vec_size];

	for (size_t i = 0; i < vec_size; ++i) {
		memset(bufs[i], fill_pattern(i), arr_len);
		vec[i].iov_base = bufs[i];
		vec[i].iov_len = arr_len;
	}

	errno = 0;
	ssize_t ret = pmemfile_writev(pfp, NULL, vec, (int)vec_size);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, EFAULT);
	if (ret != -1)
		return false;

	errno = 0;
	ret = pmemfile_writev(NULL, f, vec, (int)vec_size);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, EFAULT);
	if (ret != -1)
		return false;

	errno = 0;
	ret = pmemfile_writev(pfp, f, NULL, (int)vec_size);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, EFAULT);
	if (ret != -1)
		return false;

	ret = pmemfile_writev(pfp, f, vec, 0);
	EXPECT_EQ(ret, 0);
	if (ret != 0)
		return false;

	ret = pmemfile_writev(pfp, f, vec, (int)vec_size);
	EXPECT_GT(ret, 0);
	EXPECT_EQ((size_t)ret, vec_size * arr_len);
	if (ret <= 0 || (size_t)ret != vec_size * arr_len)
		return false;

	pmemfile_off_t off = pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET);
	EXPECT_EQ(off, 0);
	if (off)
		return false;

	char buf[vec_size * arr_len];
	memset(buf, poison_pattern, vec_size * arr_len);

	ret = pmemfile_read(pfp, f, buf, vec_size * arr_len);
	EXPECT_GT(ret, 0);
	EXPECT_EQ((size_t)ret, vec_size * arr_len);
	if (ret <= 0 || (size_t)ret != vec_size * arr_len)
		return false;

	int ok = 1;
	for (size_t i = 0; i < vec_size; ++i) {
		memset(bufs[0], fill_pattern(i), arr_len);

		int r = memcmp(bufs[0], buf + arr_len * i, arr_len);
		EXPECT_EQ(r, 0)
			<< i << " expected:" << dump_buf(bufs[0], arr_len)
			<< " got:" << dump_buf(buf + arr_len * i, arr_len);
		if (r)
			ok = 0;
	}
	if (!ok)
		return false;

	pmemfile_close(pfp, f);
	int unlinkret = pmemfile_unlink(pfp, "/file1");
	EXPECT_EQ(unlinkret, 0);
	if (unlinkret)
		return false;

	return true;
}

TEST_F(rw, writev)
{
	ASSERT_TRUE(test_writev(pfp, 40, 5));
	ASSERT_TRUE(test_writev(pfp, 10, 4096));
}

TEST_F(rw, pwritev)
{
	PMEMfile *f = pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT |
					    PMEMFILE_O_EXCL | PMEMFILE_O_RDWR,
				    0644);
	ASSERT_NE(f, nullptr) << strerror(errno);

	constexpr size_t vec_size = 40;
	constexpr size_t arr_len = 5;
	char bufs[vec_size][arr_len];
	pmemfile_iovec_t vec[vec_size];

	for (size_t i = 0; i < vec_size; ++i) {
		memset(bufs[i], fill_pattern(i), arr_len);
		vec[i].iov_base = bufs[i];
		vec[i].iov_len = arr_len;
	}

	errno = 0;
	ASSERT_EQ(pmemfile_pwritev(pfp, NULL, vec, vec_size, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_pwritev(NULL, f, vec, vec_size, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_pwritev(pfp, f, NULL, vec_size, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_pwritev(pfp, f, vec, vec_size, -1), -1);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_EQ(pmemfile_pwritev(pfp, f, vec, 0, 1), 0);

	ssize_t ret = pmemfile_pwritev(pfp, f, vec, vec_size, 1);
	ASSERT_GT(ret, 0);
	ASSERT_EQ((size_t)ret, vec_size * arr_len);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_CUR), 0);

	char buf[vec_size * arr_len + 1];
	memset(buf, poison_pattern, vec_size * arr_len + 1);

	ret = pmemfile_read(pfp, f, buf, vec_size * arr_len + 1);
	ASSERT_GT(ret, 0);
	ASSERT_EQ((size_t)ret, vec_size * arr_len + 1);

	EXPECT_EQ(buf[0], 0);

	for (size_t i = 0; i < vec_size; ++i) {
		memset(bufs[0], fill_pattern(i), arr_len);

		EXPECT_EQ(memcmp(bufs[0], buf + arr_len * i + 1, arr_len), 0)
			<< i << " expected:" << dump_buf(bufs[0], arr_len)
			<< " got:" << dump_buf(buf + arr_len * i + 1, arr_len);
	}

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
	else if (strcmp(e, "16384") == 0)
		env_block_size = 16384;
	else {
		fprintf(stderr, "unexpected PMEMFILE_BLOCK_SIZE\n");
		exit(1);
	}

	global_path = argv[1];

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
