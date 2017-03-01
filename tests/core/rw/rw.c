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
 * rw.c -- unit test for pmemfile_read & pmemfile_write
 */

#include <stdlib.h>
#include <string.h>

#include "pmemfile_test.h"

static unsigned env_block_size;

static void
test1(PMEMfilepool *pfp)
{
	PMEMfile *f = PMEMFILE_OPEN(pfp, "/file1", O_CREAT | O_EXCL | O_WRONLY,
			0644);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 0, "file1"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = 0});

	const char *data = "Marcin S";
	char data2[4096];
	char bufFF[4096], buf00[4096];
	size_t len = strlen(data) + 1;
	memset(bufFF, 0xff, sizeof(bufFF));
	memset(buf00, 0x00, sizeof(buf00));

	PMEMFILE_WRITE(pfp, f, data, len, (ssize_t)len);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 9, "file1"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = 1});

	/* try to read write-only file */
	PMEMFILE_READ(pfp, f, data2, len, -1, EBADF);
	PMEMFILE_CLOSE(pfp, f);

	f = PMEMFILE_OPEN(pfp, "/file1", O_RDONLY);

	/* read only what we wrote and check nothing else was read */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, len, (ssize_t)len);
	UT_ASSERTeq(memcmp(data, data2, len), 0);
	UT_ASSERTeq(memcmp(data2 + len, bufFF, sizeof(data2) - len), 0);

	/* try to write to read-only file */
	PMEMFILE_WRITE(pfp, f, data, len, -1, EBADF);

	memset(data2, 0, sizeof(data2));
	/* read from end of file */
	PMEMFILE_READ(pfp, f, data2, len, 0);
	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = 1});


	f = PMEMFILE_OPEN(pfp, "/file1", O_RDONLY);

	/* read as much as possible and check that we read only what we wrote */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, sizeof(data2), (ssize_t)len);
	UT_ASSERTeq(memcmp(data, data2, len), 0);
	UT_ASSERTeq(memcmp(data2 + len, bufFF, sizeof(data2) - len), 0);

	PMEMFILE_CLOSE(pfp, f);


	f = PMEMFILE_OPEN(pfp, "/file1", O_RDONLY);

	/* partial read */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, 5, 5);
	UT_ASSERTeq(memcmp(data, data2, 5), 0);
	UT_ASSERTeq(memcmp(data2 + 5, bufFF, sizeof(data2) - 5), 0);

	/* another partial read till the end of file */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, 15, 4);
	UT_ASSERTeq(memcmp(data + 5, data2, 4), 0);
	UT_ASSERTeq(memcmp(data2 + 4, bufFF, sizeof(data2) - 4), 0);

	PMEMFILE_CLOSE(pfp, f);


	f = PMEMFILE_OPEN(pfp, "/file1", O_RDWR);

	PMEMFILE_WRITE(pfp, f, "pmem", 4, 4);

	/* validate that write and read use the same offset */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, sizeof(data2), 5);
	UT_ASSERTeq(memcmp(data + 4, data2, 5), 0);
	UT_ASSERTeq(memcmp(data2 + 5, bufFF, sizeof(data2) - 5), 0);

	PMEMFILE_CLOSE(pfp, f);


	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 9, "file1"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = 1});

	f = PMEMFILE_OPEN(pfp, "/file1", O_RDWR);

	/* check that what we wrote previously is still there */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, sizeof(data2), 9);
	UT_ASSERTeq(memcmp("pmem", data2, 4), 0);
	UT_ASSERTeq(memcmp(data + 4, data2 + 4, 5), 0);
	UT_ASSERTeq(memcmp(data2 + 9, bufFF, sizeof(data2) - 9), 0);

	PMEMFILE_CLOSE(pfp, f);

	/* validate SEEK_CUR */
	f = PMEMFILE_OPEN(pfp, "/file1", O_RDWR);
	PMEMFILE_LSEEK(pfp, f, 0, SEEK_CUR, 0);
	PMEMFILE_LSEEK(pfp, f, 3, SEEK_CUR, 3);

	/* check that after "seek" "read" reads correct data */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, sizeof(data2), 6);
	UT_ASSERTeq(memcmp("min S\0", data2, 6), 0);
	UT_ASSERTeq(memcmp(data2 + 6, bufFF, sizeof(data2) - 6), 0);

	PMEMFILE_LSEEK(pfp, f, 0, SEEK_CUR, 9);
	PMEMFILE_LSEEK(pfp, f, -7, SEEK_CUR, 2);

	/* check that seeking backward works */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, sizeof(data2), 7);
	UT_ASSERTeq(memcmp("emin S\0", data2, 7), 0);
	UT_ASSERTeq(memcmp(data2 + 7, bufFF, sizeof(data2) - 7), 0);

	PMEMFILE_LSEEK(pfp, f, 0, SEEK_CUR, 9);


	PMEMFILE_LSEEK(pfp, f, -3, SEEK_END, 6);

	/* again, seeking backward works */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, sizeof(data2), 3);
	UT_ASSERTeq(memcmp(" S\0", data2, 3), 0);
	UT_ASSERTeq(memcmp(data2 + 3, bufFF, sizeof(data2) - 3), 0);

	/* check that writing past the end of file works */
	PMEMFILE_LSEEK(pfp, f, 0, SEEK_CUR, 9);
	PMEMFILE_LSEEK(pfp, f, 100, SEEK_END, 9 + 100);
	PMEMFILE_WRITE(pfp, f, "XYZ\0", 4, 4);
	PMEMFILE_LSEEK(pfp, f, 0, SEEK_CUR, 9 + 100 + 4);
	PMEMFILE_LSEEK(pfp, f, 0, SEEK_SET, 0);

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = 1});

	/* validate the whole file contents */
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, sizeof(data2), 9 + 100 + 4);
	UT_ASSERTeq(memcmp("pmemin S\0", data2, 9), 0);
	UT_ASSERTeq(memcmp(data2 + 9, buf00, 100), 0);
	UT_ASSERTeq(memcmp("XYZ\0", data2 + 9 + 100, 4), 0);
	UT_ASSERTeq(memcmp(data2 + 9 + 100 + 4, bufFF,
			sizeof(data2) - 9 - 100 - 4), 0);

	/* write 4k past the end of file and check the hole is empty */
	PMEMFILE_LSEEK(pfp, f, 0, SEEK_CUR, 9 + 100 + 4);
	PMEMFILE_LSEEK(pfp, f, 4096, SEEK_END, 9 + 100 + 4 + 4096);
	PMEMFILE_WRITE(pfp, f, "NEXT BLOCK\0", 11, 11);
	PMEMFILE_LSEEK(pfp, f, 9 + 100 + 4, SEEK_SET, 9 + 100 + 4);
	memset(data2, 0xff, sizeof(data2));
	PMEMFILE_READ(pfp, f, data2, 4096, 4096);
	UT_ASSERTeq(memcmp(data2, buf00, 4096), 0);

	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = (env_block_size == 4096) ? 2 : 1});

	f = PMEMFILE_OPEN(pfp, "/file1", O_RDONLY);
	/* check read after EOF returns 0 */
	PMEMFILE_LSEEK(pfp, f, 8192, SEEK_SET, 8192);
	PMEMFILE_READ(pfp, f, data2, 4096, 0);

	PMEMFILE_CLOSE(pfp, f);


	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 4220, "file1"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = (env_block_size == 4096) ? 2 : 1});

	PMEMFILE_UNLINK(pfp, "/file1");

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});

	f = PMEMFILE_OPEN(pfp, "/file1", O_CREAT | O_EXCL | O_RDWR, 0644);

	/* check that writing slightly bigger files and seeking in them works */
	PMEMFILE_WRITE(pfp, f, buf00, 4096, 4096);
	PMEMFILE_FILE_SIZE(pfp, f, 4096);

	PMEMFILE_WRITE(pfp, f, bufFF, 4096, 4096);
	PMEMFILE_FILE_SIZE(pfp, f, 8192);

	PMEMFILE_LSEEK(pfp, f, 0, SEEK_CUR, 8192);
	PMEMFILE_LSEEK(pfp, f, 4096, SEEK_SET, 4096);
	PMEMFILE_FILE_SIZE(pfp, f, 8192);

	PMEMFILE_READ(pfp, f, data2, 4096, 4096);
	PMEMFILE_FILE_SIZE(pfp, f, 8192);

	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 8192, "file1"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = (env_block_size == 4096) ? 2 : 1});

	PMEMFILE_UNLINK(pfp, "/file1");
}

static void
test2(PMEMfilepool *pfp)
{
	/* write 800MB of random data and read it back */
	unsigned char buf00[128], bufFF[128], bufd[4096 * 4], buftmp[4096 * 4];

	memset(buf00, 0x00, sizeof(buf00));
	memset(bufFF, 0xFF, sizeof(bufFF));

	for (size_t i = 0; i < sizeof(bufd); ++i)
		bufd[i] = (unsigned char)(rand() % 255);

	PMEMfile *f = PMEMFILE_OPEN(pfp, "/file1", O_CREAT | O_EXCL | O_WRONLY,
			0644);

#define LEN (sizeof(bufd) - 1000)
#define LOOPS ((200 * 1024 * 1024) / LEN)
	for (size_t i = 0; i < LOOPS; ++i)
		PMEMFILE_WRITE(pfp, f, bufd, LEN, (ssize_t)LEN);

	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 209714688, "file1"},
	    {}});

	if (env_block_size == 4096)
		PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
			.inodes = 2,
			.dirs = 0,
			.block_arrays = 609,
			.inode_arrays = 1,
			.blocks = 51200});
	else
		PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
			.inodes = 2,
			.dirs = 0,
			.block_arrays = 7,
			.inode_arrays = 1,
			.blocks = 633});

	f = PMEMFILE_OPEN(pfp, "/file1", O_RDONLY);

	for (size_t i = 0; i < LOOPS; ++i) {
		memset(buftmp, 0, sizeof(buftmp));
		PMEMFILE_READ(pfp, f, buftmp, LEN, (ssize_t)LEN);
		if (memcmp(buftmp, bufd, LEN) != 0)
			UT_ASSERT(0);
	}
#undef LEN
	PMEMFILE_READ(pfp, f, buftmp, 1023, 0);

	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_UNLINK(pfp, "/file1");
}

static void
test_trunc(PMEMfilepool *pfp)
{
	/* check that O_TRUNC works */
	char bufFF[128], bufDD[128], buftmp[128];

	memset(bufFF, 0xFF, sizeof(bufFF));
	memset(bufDD, 0xDD, sizeof(bufDD));

	PMEMfile *f1 = PMEMFILE_OPEN(pfp, "/file1", O_CREAT | O_EXCL | O_WRONLY,
			0644);
	PMEMfile *f2 = PMEMFILE_OPEN(pfp, "/file2", O_CREAT | O_EXCL | O_WRONLY,
			0644);

	for (int i = 0; i < 100; ++i) {
		PMEMFILE_WRITE(pfp, f1, bufFF, 128, 128);
		PMEMFILE_WRITE(pfp, f1, bufDD, 128, 128);

		PMEMFILE_WRITE(pfp, f2, bufFF, 128, 128);
		PMEMFILE_WRITE(pfp, f2, bufDD, 128, 128);
	}

	PMEMFILE_CLOSE(pfp, f1);
	PMEMFILE_CLOSE(pfp, f2);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 25600, "file1"},
	    {0100644, 1, 25600, "file2"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 3,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = (env_block_size == 4096) ? 14 : 4});

	f1 = PMEMFILE_OPEN(pfp, "/file1", O_RDWR | O_TRUNC, 0);

	f2 = PMEMFILE_OPEN(pfp, "/file2", O_RDWR | O_TRUNC, 0);

	PMEMFILE_READ(pfp, f1, buftmp, 128, 0);

	PMEMFILE_WRITE(pfp, f2, bufDD, 128, 128);

	PMEMFILE_CLOSE(pfp, f1);
	PMEMFILE_CLOSE(pfp, f2);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100644, 1, 0, "file1"},
	    {0100644, 1, 128, "file2"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 3,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 1});

	PMEMFILE_UNLINK(pfp, "/file1");

	PMEMFILE_UNLINK(pfp, "/file2");
}

static void
test_o_append(PMEMfilepool *pfp)
{
	/* check that O_APPEND works */
	char bufFF[128], bufDD[128];
	PMEMfile *f;

	memset(bufFF, 0xFF, sizeof(bufFF));
	memset(bufDD, 0xDD, sizeof(bufDD));

	f = PMEMFILE_OPEN(pfp, "/file1", O_CREAT | O_EXCL | O_WRONLY | O_APPEND,
			0644);
	PMEMFILE_WRITE(pfp, f, bufFF, 128, 128);
	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_PATH_SIZE(pfp, "/file1", 128);

	f = PMEMFILE_OPEN(pfp, "/file1", O_WRONLY);
	PMEMFILE_WRITE(pfp, f, bufFF, 128, 128);
	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_PATH_SIZE(pfp, "/file1", 128);

	f = PMEMFILE_OPEN(pfp, "/file1", O_WRONLY | O_APPEND);
	PMEMFILE_WRITE(pfp, f, bufDD, 128, 128);
	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_PATH_SIZE(pfp, "/file1", 256);

	PMEMFILE_UNLINK(pfp, "/file1");
}

static void
test_sparse_files(PMEMfilepool *pfp)
{
	unsigned char buf[8192];
	PMEMfile *f = PMEMFILE_OPEN(pfp, "/file1",
			O_CREAT | O_EXCL | O_RDWR,
			0644);
	PMEMFILE_LSEEK(pfp, f, 4096, SEEK_SET, 4096);
	PMEMFILE_PATH_SIZE(pfp, "/file1", 0);
	PMEMFILE_WRITE(pfp, f, "test", 5, 5);
	PMEMFILE_PATH_SIZE(pfp, "/file1", 4096 + 5);

	PMEMFILE_LSEEK(pfp, f, 0, SEEK_SET, 0);
	memset(buf, 0xff, sizeof(buf));
	PMEMFILE_READ(pfp, f, buf, 8192, 4096 + 5);
	UT_ASSERTeq(is_zeroed(buf, 4096), 1);
	UT_ASSERTeq(memcmp(buf + 4096, "test", 5), 0);
	UT_ASSERTeq(buf[4096 + 5], 0xff);

	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_UNLINK(pfp, "/file1");
}

int
main(int argc, char *argv[])
{
	START();

	if (argc < 2)
		UT_FATAL("usage: %s file-name", argv[0]);

	const char *path = argv[1];
	const char *e = getenv("PMEMFILECORE_BLOCK_SIZE");

	if (e == NULL)
		env_block_size = 0;
	else if (strcmp(e, "4096") == 0)
		env_block_size = 4096;
	else
		UT_FATAL("unexpected PMEMFILECORE_BLOCK_SIZE");

	PMEMfilepool *pfp = PMEMFILE_MKFS(path);

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = 0});
	PMEMFILE_ASSERT_EMPTY_DIR(pfp, "/");

	test1(pfp);
	PMEMFILE_ASSERT_EMPTY_DIR(pfp, "/");
	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});

	test2(pfp);
	PMEMFILE_ASSERT_EMPTY_DIR(pfp, "/");
	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});

	test_trunc(pfp);
	PMEMFILE_ASSERT_EMPTY_DIR(pfp, "/");
	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});

	test_o_append(pfp);
	PMEMFILE_ASSERT_EMPTY_DIR(pfp, "/");
	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});

	test_sparse_files(pfp);
	PMEMFILE_ASSERT_EMPTY_DIR(pfp, "/");
	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});

	pmemfile_pool_close(pfp);
}
