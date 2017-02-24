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
 * basic.c -- unit test for pmemfile_*
 */
#define _GNU_SOURCE
#include "pmemfile_test.h"

static PMEMfilepool *
create_pool(const char *path)
{
	PMEMfilepool *pfp = pmemfile_mkfs(path, 8 * 1024 * 1024,
			S_IWUSR | S_IRUSR);
	if (!pfp)
		UT_FATAL("!pmemfile_mkfs: %s", path);
	return pfp;
}

static PMEMfilepool *
open_pool(const char *path)
{
	PMEMfilepool *pfp = pmemfile_pool_open(path);
	if (!pfp)
		UT_FATAL("!pmemfile_pool_open %s", path);
	return pfp;
}

static void
test_open_create_close(PMEMfilepool *pfp)
{
	PMEMfile *f1, *f2;

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = 0});

	/* NULL file name */
	errno = 0;
	f1 = pmemfile_open(pfp, NULL, O_CREAT, 0777);
	UT_ASSERTeq(f1, NULL);
	UT_ASSERTeq(errno, ENOENT);

	/* file does not exist */
	errno = 0;
	f1 = pmemfile_open(pfp, "/aaa", 0);
	UT_ASSERTeq(f1, NULL);
	UT_ASSERTeq(errno, ENOENT);

	/* successful create */
	f1 = pmemfile_open(pfp, "/aaa", O_CREAT | O_EXCL, 0777);
	UT_ASSERTne(f1, NULL);

	pmemfile_close(pfp, f1);

	/* file already exists */
	errno = 0;
	f1 = pmemfile_open(pfp, "/aaa", O_CREAT | O_EXCL, 0777);
	UT_ASSERTeq(f1, NULL);
	UT_ASSERTeq(errno, EEXIST);

	/* too long name */
	errno = 0;
	f1 = pmemfile_open(pfp, "/"
		"12345678901234567890123456789012345678901234567890"
		"12345678901234567890123456789012345678901234567890"
		"12345678901234567890123456789012345678901234567890"
		"12345678901234567890123456789012345678901234567890"
		"12345678901234567890123456789012345678901234567890"
		"123456", O_CREAT | O_EXCL, 0777);
	UT_ASSERTeq(f1, NULL);
	UT_ASSERTeq(errno, ENAMETOOLONG);

	/* file does not exist */
	errno = 0;
	f2 = pmemfile_open(pfp, "/bbb", 0);
	UT_ASSERTeq(f2, NULL);
	UT_ASSERTeq(errno, ENOENT);

	/* successful create */
	f2 = pmemfile_open(pfp, "/bbb", O_CREAT | O_EXCL, 0777);
	UT_ASSERTne(f2, NULL);



	/* successful open */
	f1 = pmemfile_open(pfp, "/aaa", 0);
	UT_ASSERTne(f1, NULL);

	pmemfile_close(pfp, f2);

	pmemfile_close(pfp, f1);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 1, 0, "aaa"},
	    {0100777, 1, 0, "bbb"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 3,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = 0});

	pmemfile_pool_close(pfp);
}

/*
 * At this point (after test_open_create_close) these files should exist in
 * root:
 * - .
 * - ..
 * - aaa
 * - bbb
 */

static void
test_open_close(const char *path)
{
	PMEMfilepool *pfp = open_pool(path);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 1, 0, "aaa"},
	    {0100777, 1, 0, "bbb"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 3,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 0,
		.blocks = 0});

	pmemfile_pool_close(pfp);
}

static void
test_link(const char *path)
{
	PMEMfilepool *pfp = open_pool(path);

	int ret;

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 1, 0, "aaa"},
	    {0100777, 1, 0, "bbb"},
	    {}});

	/* successful link */
	PMEMFILE_LINK(pfp, "/aaa", "/aaa.link");

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 2, 0, "aaa"},
	    {0100777, 1, 0, "bbb"},
	    {0100777, 2, 0, "aaa.link"},
	    {}});

	/* destination already exists */
	errno = 0;
	ret = pmemfile_link(pfp, "/aaa", "/aaa.link");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, EEXIST);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 2, 0, "aaa"},
	    {0100777, 1, 0, "bbb"},
	    {0100777, 2, 0, "aaa.link"},
	    {}});

	/* source does not exist */
	errno = 0;
	ret = pmemfile_link(pfp, "/aaaaaaaaaaaa", "/aaa.linkXXX");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, ENOENT);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 2, 0, "aaa"},
	    {0100777, 1, 0, "bbb"},
	    {0100777, 2, 0, "aaa.link"},
	    {}});

	/* successful link from link */
	PMEMFILE_LINK(pfp, "/aaa.link", "/aaa2.link");

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 3, 0, "aaa"},
	    {0100777, 1, 0, "bbb"},
	    {0100777, 3, 0, "aaa.link"},
	    {0100777, 3, 0, "aaa2.link"},
	    {}});

	/* another successful link */
	PMEMFILE_LINK(pfp, "/bbb", "/bbb2.link");

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 3, 0, "aaa"},
	    {0100777, 2, 0, "bbb"},
	    {0100777, 3, 0, "aaa.link"},
	    {0100777, 3, 0, "aaa2.link"},
	    {0100777, 2, 0, "bbb2.link"},
	    {}});

	PMEMFILE_MKDIR(pfp, "/dir", 0777);
	/* destination already exists as directory */
	errno = 0;
	ret = pmemfile_link(pfp, "/aaa", "/dir");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, EEXIST);

	errno = 0;
	ret = pmemfile_link(pfp, "/dir", "/dir2");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, EPERM);

	errno = 0;
	ret = pmemfile_link(pfp, "/aaa/bbb", "/file");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, ENOTDIR);

	errno = 0;
	ret = pmemfile_link(pfp, "/bbb", "/aaa/ccc");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, ENOTDIR);

	errno = 0;
	ret = pmemfile_link(pfp, "/dir/aaaa", "/bbbb");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, ENOENT);

	errno = 0;
	ret = pmemfile_link(pfp, "/aaa/", "/bbbb");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, ENOTDIR);

	errno = 0;
	ret = pmemfile_link(pfp, "/aaa", "/"
		"12345678901234567890123456789012345678901234567890"
		"12345678901234567890123456789012345678901234567890"
		"12345678901234567890123456789012345678901234567890"
		"12345678901234567890123456789012345678901234567890"
		"12345678901234567890123456789012345678901234567890"
		"123456");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, ENAMETOOLONG);

	PMEMFILE_RMDIR(pfp, "/dir");

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 3, 0, "aaa"},
	    {0100777, 2, 0, "bbb"},
	    {0100777, 3, 0, "aaa.link"},
	    {0100777, 3, 0, "aaa2.link"},
	    {0100777, 2, 0, "bbb2.link"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 3,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});

	pmemfile_pool_close(pfp);
}

/*
 * At this point (after test_link) these files should exist in root:
 * - .
 * - ..
 * - aaa
 * - bbb
 * - aaa.link (hardlink to aaa)
 * - aaa2.link (hardlink to aaa)
 * - bbb2.link (hardlink to bbb)
 */

static void
test_unlink(const char *path)
{
	PMEMfilepool *pfp = open_pool(path);

	int ret;
	PMEMfile *f1;

	f1 = PMEMFILE_OPEN(pfp, "/bbb2.link", 0);
	PMEMFILE_CLOSE(pfp, f1);

	errno = 0;
	ret = pmemfile_unlink(pfp, "/bbb2.link/");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, ENOTDIR);

	PMEMFILE_UNLINK(pfp, "/bbb2.link");

	errno = 0;
	ret = pmemfile_unlink(pfp, "/bbb2.link");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, ENOENT);

	errno = 0;
	f1 = pmemfile_open(pfp, "/bbb2.link", 0);
	UT_ASSERTeq(f1, NULL);
	UT_ASSERTeq(errno, ENOENT);

	errno = 0;
	ret = pmemfile_unlink(pfp, "/bbb.notexists");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, ENOENT);


	f1 = PMEMFILE_OPEN(pfp, "/bbb", 0);
	PMEMFILE_UNLINK(pfp, "/bbb");
	PMEMFILE_CLOSE(pfp, f1);

	errno = 0;
	f1 = pmemfile_open(pfp, "/bbb", 0);
	UT_ASSERTeq(f1, NULL);
	UT_ASSERTeq(errno, ENOENT);

	errno = 0;
	ret = pmemfile_unlink(pfp, "/..");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, EISDIR);

	errno = 0;
	ret = pmemfile_unlink(pfp, "/.");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, EISDIR);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {0100777, 3, 0, "aaa"},
	    {0100777, 3, 0, "aaa.link"},
	    {0100777, 3, 0, "aaa2.link"},
	    {}});

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});

	PMEMFILE_UNLINK(pfp, "/aaa");
	PMEMFILE_UNLINK(pfp, "/aaa.link");
	PMEMFILE_UNLINK(pfp, "/aaa2.link");

	pmemfile_pool_close(pfp);
}

static void
test_tmpfile(const char *path)
{
	PMEMfilepool *pfp = open_pool(path);

	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});

	PMEMFILE_ASSERT_EMPTY_DIR(pfp, "/");

#ifdef O_TMPFILE
	PMEMfile *f = PMEMFILE_OPEN(pfp, "/", O_TMPFILE | O_WRONLY, 0644);
	PMEMFILE_WRITE(pfp, f, "qwerty", 6, 6);

	PMEMFILE_ASSERT_EMPTY_DIR(pfp, "/");
	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 2,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 1});

	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_ASSERT_EMPTY_DIR(pfp, "/");
#endif
	PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
		.inodes = 1,
		.dirs = 0,
		.block_arrays = 0,
		.inode_arrays = 1,
		.blocks = 0});


	pmemfile_pool_close(pfp);
}

/*
 * At this point (after test_unlink) these files should exist in root:
 * - .
 * - ..
 * - aaa
 * - aaa.link
 * - aaa2.link
 *
 * And these files should not exist:
 * - bbb
 * - bbb2.link
 */

int
main(int argc, char *argv[])
{
	START();

	if (argc < 2)
		UT_FATAL("usage: %s file-name", argv[0]);

	const char *path = argv[1];

	test_open_create_close(create_pool(path));

	/* open and close pool to test there are no inode leaks */
	test_open_close(path);

	test_link(path);

	test_unlink(path);

	test_tmpfile(path);
}
