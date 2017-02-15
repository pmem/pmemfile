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
 * file_core_openp.c -- unit test for pmemfile_open_parent
 */

#include "pmemfile_test.h"
#include "unittest.h"

static void
check_path(PMEMfilepool *pfp, int stop_at_root, const char *path,
		const char *parent, const char *child)
{
	char tmp_path[PATH_MAX], dir_path[PATH_MAX];

	strncpy(tmp_path, path, PATH_MAX);
	tmp_path[PATH_MAX - 1] = 0;

	PMEMfile *f = pmemfile_open_parent(pfp, PMEMFILE_AT_CWD, tmp_path,
		PATH_MAX, stop_at_root ? PMEMFILE_OPEN_PARENT_STOP_AT_ROOT : 0);
	UT_ASSERTne(f, NULL);

	char *dir_path2 = pmemfile_get_dir_path(pfp, f, dir_path, PATH_MAX);
	UT_ASSERTeq(dir_path2, dir_path);

	if (strcmp(dir_path, parent) != 0)
		UT_FATAL("parent: %s != %s", dir_path, parent);

	if (strcmp(tmp_path, child) != 0)
		UT_FATAL("child: %s != %s", tmp_path, child);

	PMEMFILE_CLOSE(pfp, f);
}

static void
test0(PMEMfilepool *pfp)
{
	PMEMFILE_MKDIR(pfp, "/dir1", 0777);
	PMEMFILE_MKDIR(pfp, "/dir2", 0777);
	PMEMFILE_MKDIR(pfp, "/dir1/dir3", 0777);
	PMEMFILE_MKDIR(pfp, "/dir1/dir3/dir4", 0777);
	PMEMFILE_CREATE(pfp, "/file1", O_EXCL, 0644);
	PMEMFILE_CREATE(pfp, "/dir2/file2", O_EXCL, 0644);
	PMEMFILE_CREATE(pfp, "/dir1/dir3/file3", O_EXCL, 0644);
	PMEMFILE_CREATE(pfp, "/dir1/dir3/dir4/file4", O_EXCL, 0644);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 4, 4008, "."},
	    {040777, 4, 4008, ".."},
	    {040777, 3, 4008, "dir1"},
	    {040777, 2, 4008, "dir2"},
	    {0100644, 1, 0, "file1"},
	    {}});

	PMEMFILE_LIST_FILES(pfp, "/dir1", (const struct pmemfile_ls[]) {
	    {040777, 3, 4008, "."},
	    {040777, 4, 4008, ".."},
	    {040777, 3, 4008, "dir3"},
	    {}});

	PMEMFILE_LIST_FILES(pfp, "/dir1/dir3", (const struct pmemfile_ls[]) {
	    {040777, 3, 4008, "."},
	    {040777, 3, 4008, ".."},
	    {040777, 2, 4008, "dir4"},
	    {0100644, 1, 0, "file3"},
	    {}});

	PMEMFILE_LIST_FILES(pfp, "/dir1/dir3/dir4",
	    (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 3, 4008, ".."},
	    {0100644, 1, 0, "file4"},
	    {}});

	PMEMFILE_LIST_FILES(pfp, "/dir2", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 4, 4008, ".."},
	    {0100644, 1, 0, "file2"},
	    {}});

	check_path(pfp, 0, "dir1", "/", "dir1");
	check_path(pfp, 0, "dir1/", "/", "dir1/");
	check_path(pfp, 0, "/dir1", "/", "dir1");
	check_path(pfp, 1, "/dir1", "/", "dir1");

	check_path(pfp, 0, "dir2", "/", "dir2");
	check_path(pfp, 0, "dir2/", "/", "dir2/");
	check_path(pfp, 0, "/dir2", "/", "dir2");
	check_path(pfp, 1, "/dir2", "/", "dir2");

	check_path(pfp, 0, "dir1/dir3", "/dir1", "dir3");
	check_path(pfp, 0, "dir1/dir3/", "/dir1", "dir3/");
	check_path(pfp, 0, "dir1//dir3", "/dir1", "dir3");
	check_path(pfp, 0, "/dir1/dir3", "/dir1", "dir3");
	check_path(pfp, 1, "/dir1/dir3", "/dir1", "dir3");

	check_path(pfp, 0, "dir1/dir3/dir4", "/dir1/dir3", "dir4");
	check_path(pfp, 0, "dir1/not_exists/dir4", "/dir1", "not_exists/dir4");

	check_path(pfp, 0, "dir1/dir3/../", "/dir1/dir3", "../");

	check_path(pfp, 0, "/dir1/../../dir2", "/", "dir2");
	check_path(pfp, 0, "dir1/../../dir2", "/", "dir2");
	check_path(pfp, 0, "/dir1/../dir2/../../dir2", "/", "dir2");
	check_path(pfp, 0, "../dir1", "/", "dir1");
	check_path(pfp, 0, "./dir1/../../dir1", "/", "dir1");

	check_path(pfp, 1, "/dir1/../../dir2", "/", "../dir2");
	check_path(pfp, 1, "dir1/../../dir2", "/", "../dir2");
	check_path(pfp, 1, "/dir1/../dir2/../../dir2", "/", "../dir2");
	check_path(pfp, 1, "../dir1", "/", "../dir1");
	check_path(pfp, 1, "./dir1/../../dir1", "/", "../dir1");

	PMEMFILE_UNLINK(pfp, "/dir1/dir3/dir4/file4");
	PMEMFILE_UNLINK(pfp, "/dir1/dir3/file3");
	PMEMFILE_UNLINK(pfp, "/dir2/file2");
	PMEMFILE_UNLINK(pfp, "/file1");

	PMEMFILE_RMDIR(pfp, "/dir1/dir3/dir4");
	PMEMFILE_RMDIR(pfp, "/dir1/dir3");
	PMEMFILE_RMDIR(pfp, "/dir1");
	PMEMFILE_RMDIR(pfp, "/dir2");
}

int
main(int argc, char *argv[])
{
	START(argc, argv, "file_core_openp");

	if (argc < 2)
		UT_FATAL("usage: %s file-name", argv[0]);

	const char *path = argv[1];

	PMEMfilepool *pfp = PMEMFILE_MKFS(path);

	test0(pfp);

	pmemfile_pool_close(pfp);

	DONE(NULL);
}
