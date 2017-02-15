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
 * file_core_crash.c -- unit test for pmemfile_*
 */

#include "unittest.h"
#include "pmemfile_test.h"

static PMEMfilepool *
create_pool(const char *path)
{
	PMEMfilepool *pfp = pmemfile_mkfs(path, PMEMOBJ_MIN_POOL,
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

int
main(int argc, char *argv[])
{
	START(argc, argv, "file_core_crash %s", argc >= 3 ? argv[2] : "");

	if (argc < 3)
		UT_FATAL("usage: %s file-name op", argv[0]);

	const char *path = argv[1];
	PMEMfilepool *pfp;

	if (strcmp(argv[2], "prep") == 0) {
		pfp = create_pool(path);

		PMEMFILE_CREATE(pfp, "/aaa", O_CREAT | O_EXCL, 0644);
		PMEMFILE_CREATE(pfp, "/bbb", O_CREAT | O_EXCL, 0644);

		pmemfile_pool_close(pfp);
	} else if (strcmp(argv[2], "crash1") == 0) {
		pfp = open_pool(path);

		PMEMFILE_OPEN(pfp, "/aaa", 0);

		exit(0);
	} else if (strcmp(argv[2], "crash2") == 0) {
		pfp = open_pool(path);

		PMEMFILE_OPEN(pfp, "/aaa", 0);
		PMEMFILE_UNLINK(pfp, "/aaa");

		exit(0);
	} else if (strcmp(argv[2], "openclose1") == 0 ||
	    strcmp(argv[2], "openclose2") == 0) {
		pfp = open_pool(path);

		PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
		    {040777, 2, 4008, "."},
		    {040777, 2, 4008, ".."},
		    {0100644, 1, 0, "aaa"},
		    {0100644, 1, 0, "bbb"},
		    {}});

		PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
			.inodes = 3,
			.dirs = 0,
			.block_arrays = 0,
			.inode_arrays = 0,
			.blocks = 0});

		pmemfile_pool_close(pfp);
	} else if (strcmp(argv[2], "openclose3") == 0) {
		pfp = open_pool(path);

		PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
		    {040777, 2, 4008, "."},
		    {040777, 2, 4008, ".."},
		    {0100644, 1, 0, "bbb"},
		    {}});

		PMEMFILE_STATS(pfp, (const struct pmemfile_stats) {
			.inodes = 2,
			.dirs = 0,
			.block_arrays = 0,
			.inode_arrays = 1,
			.blocks = 0});

		pmemfile_pool_close(pfp);
	} else
		UT_ASSERT(0);

	DONE(NULL);
}
