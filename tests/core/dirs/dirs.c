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
 * dirs.c -- unit test for directories
 */

#include "pmemfile_test.h"
#include "unittest.h"

static const char *
timespec_to_str(const struct timespec *t)
{
	char *s = asctime(localtime(&t->tv_sec));
	s[strlen(s) - 1] = 0;
	return s;
}

static void
dump_stat(struct stat *st, const char *path)
{
	UT_OUT("path:       %s", path);
	UT_OUT("st_dev:     0x%lx", st->st_dev);
	UT_OUT("st_ino:     %ld", st->st_ino);
	UT_OUT("st_mode:    0%o", st->st_mode);
	UT_OUT("st_nlink:   %lu", st->st_nlink);
	UT_OUT("st_uid:     %u", st->st_uid);
	UT_OUT("st_gid:     %u", st->st_gid);
	UT_OUT("st_rdev:    0x%lx", st->st_rdev);
	UT_OUT("st_size:    %ld", st->st_size);
	UT_OUT("st_blksize: %ld", st->st_blksize);
	UT_OUT("st_blocks:  %ld", st->st_blocks);
	UT_OUT("st_atim:    %ld.%.9ld, %s", st->st_atim.tv_sec,
			st->st_atim.tv_nsec, timespec_to_str(&st->st_atim));
	UT_OUT("st_mtim:    %ld.%.9ld, %s", st->st_mtim.tv_sec,
			st->st_mtim.tv_nsec, timespec_to_str(&st->st_mtim));
	UT_OUT("st_ctim:    %ld.%.9ld, %s", st->st_ctim.tv_sec,
			st->st_ctim.tv_nsec, timespec_to_str(&st->st_ctim));
	UT_OUT("---");
}

struct linux_dirent64 {
	uint64_t	d_ino;
	uint64_t	d_off;
	unsigned short	d_reclen;
	unsigned char	d_type;
	char		d_name[];
};

static void
list_files(PMEMfilepool *pfp, const char *dir, int expected_files,
		int just_count, const char *name)
{
	UT_OUT("\"%s\" start", name);
	PMEMfile *f = PMEMFILE_OPEN(pfp, dir, O_DIRECTORY | O_RDONLY);

	char buf[32 * 1024];
	char path[PATH_MAX];
	struct linux_dirent64 *d = (void *)buf;
	int r = pmemfile_getdents64(pfp, f, (void *)buf, sizeof(buf));
	int num_files = 0;

	while ((uintptr_t)d < (uintptr_t)&buf[r]) {
		num_files++;
		if (!just_count) {
			UT_OUT("ino: 0x%lx, off: 0x%lx, len: %d, type: %d, "
					"name: \"%s\"",
					d->d_ino, d->d_off, d->d_reclen,
					d->d_type, d->d_name);
			sprintf(path, "/%s/%s", dir, d->d_name);

			struct stat st;
			PMEMFILE_STAT(pfp, path, &st);
			dump_stat(&st, path);
		}
		d = (void *)(((char *)d) + d->d_reclen);
	}

	PMEMFILE_CLOSE(pfp, f);

	UT_OUT("\"%s\" end", name);
	UT_ASSERTeq(num_files, expected_files);
}

static void
test0(PMEMfilepool *pfp)
{
	PMEMFILE_CREATE(pfp, "/file", O_EXCL, 0644);

	PMEMfile *f;

	f = PMEMFILE_OPEN(pfp, "//file", 0);
	PMEMFILE_CLOSE(pfp, f);

	f = PMEMFILE_OPEN(pfp, "/../file", 0);
	PMEMFILE_CLOSE(pfp, f);

	f = PMEMFILE_OPEN(pfp, "/../../file", 0);
	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_UNLINK(pfp, "/file");


	PMEMFILE_MKDIR(pfp, "/dir////", 0755);
	list_files(pfp, "/", 3, 0, ". .. dir");
	list_files(pfp, "/dir", 2, 0, ". ..");
	PMEMFILE_CREATE(pfp, "/dir//../dir/.//file", O_EXCL, 0644);
	list_files(pfp, "/dir", 3, 0, ". .. file");

	f = PMEMFILE_OPEN(pfp, "/dir/file", 0);
	PMEMFILE_CLOSE(pfp, f);

	f = PMEMFILE_OPEN(pfp, "/dir/../dir////file", 0);
	PMEMFILE_CLOSE(pfp, f);

	f = pmemfile_open(pfp, "/dir/file/file", O_RDONLY);
	UT_ASSERTeq(f, NULL);
	UT_ASSERTeq(errno, ENOTDIR);

	f = pmemfile_open(pfp, "/dir/file/file", O_RDONLY | O_CREAT);
	UT_ASSERTeq(f, NULL);
	UT_ASSERTeq(errno, ENOTDIR);

	f = pmemfile_open(pfp, "/dir/file/file", O_RDONLY | O_CREAT | O_EXCL);
	UT_ASSERTeq(f, NULL);
	UT_ASSERTeq(errno, ENOTDIR);

	/* file is not a directory */
	errno = 0;
	f = pmemfile_open(pfp, "/dir/file/", O_RDONLY);
	UT_ASSERTeq(f, NULL);
	UT_ASSERTeq(errno, ENOTDIR);

	PMEMFILE_UNLINK(pfp, "/dir//file");
	PMEMFILE_RMDIR(pfp, "/dir//////");
}

static void
test1(PMEMfilepool *pfp)
{
	PMEMfile *f;
	char buf[1001];
	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {}});
	memset(buf, 0xff, sizeof(buf));
	UT_OUT("test1");

	for (int i = 0; i < 100; ++i) {
		sprintf(buf, "/file%04d", i);

		f = PMEMFILE_OPEN(pfp, buf, O_CREAT | O_EXCL | O_WRONLY, 0644);

		PMEMFILE_WRITE(pfp, f, buf, i, i);

		PMEMFILE_CLOSE(pfp, f);

		list_files(pfp, "/", i + 1 + 2, 0, "test1: after one iter");
	}

	for (int i = 0; i < 100; ++i) {
		sprintf(buf, "/file%04d", i);

		PMEMFILE_UNLINK(pfp, buf);
	}
}

static void
test2(PMEMfilepool *pfp)
{
	char buf[1001];
	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 2, 32680, "."},
	    {040777, 2, 32680, ".."},
	    {}});
	UT_OUT("test2");

	for (int i = 0; i < 100; ++i) {
		sprintf(buf, "/dir%04d", i);

		PMEMFILE_MKDIR(pfp, buf, 0755);

		list_files(pfp, "/", i + 1 + 2, 0, "test2: after one iter");
	}

	list_files(pfp, "/", 100 + 2, 1, "test2: after loop");
	PMEMFILE_MKDIR(pfp, "/dir0007/another_directory", 0755);

	errno = 0;
	UT_ASSERTeq(pmemfile_mkdir(pfp, "/", 0755), -1);
	UT_ASSERTeq(errno, EEXIST);

	errno = 0;
	UT_ASSERTeq(pmemfile_mkdir(pfp, "/dir0007", 0755), -1);
	UT_ASSERTeq(errno, EEXIST);

	errno = 0;
	UT_ASSERTeq(pmemfile_mkdir(pfp, "/dir2333/aaaa", 0755), -1);
	UT_ASSERTeq(errno, ENOENT);

	list_files(pfp, "/", 100 + 2, 1, "test2: after2");

	PMEMFILE_CREATE(pfp, "/file", O_EXCL, 0644);

	errno = 0;
	UT_ASSERTeq(pmemfile_mkdir(pfp, "/file/aaaa", 0755), -1);
	UT_ASSERTeq(errno, ENOTDIR);

	PMEMFILE_UNLINK(pfp, "/file");

	list_files(pfp, "/", 100 + 2, 1, "test2: after3");


	errno = 0;
	UT_ASSERTeq(pmemfile_rmdir(pfp, "/dir0100"), -1);
	UT_ASSERTeq(errno, ENOENT);

	errno = 0;
	UT_ASSERTeq(pmemfile_rmdir(pfp, "/dir0099/inside"), -1);
	UT_ASSERTeq(errno, ENOENT);


	PMEMFILE_CREATE(pfp, "/file", O_EXCL, 0644);

	errno = 0;
	UT_ASSERTeq(pmemfile_rmdir(pfp, "/file"), -1);
	UT_ASSERTeq(errno, ENOTDIR);

	errno = 0;
	UT_ASSERTeq(pmemfile_mkdir(pfp, "/file/", 0755), -1);
	UT_ASSERTeq(errno, EEXIST);

	errno = 0;
	UT_ASSERTeq(pmemfile_rmdir(pfp, "/file/"), -1);
	UT_ASSERTeq(errno, ENOTDIR);


	PMEMFILE_UNLINK(pfp, "/file");


	errno = 0;
	UT_ASSERTeq(pmemfile_unlink(pfp, "/dir0000"), -1);
	UT_ASSERTeq(errno, EISDIR);


	errno = 0;
	UT_ASSERTeq(pmemfile_rmdir(pfp, "/dir0007"), -1);
	UT_ASSERTeq(errno, ENOTEMPTY);

	PMEMFILE_RMDIR(pfp, "/dir0007/another_directory");

	for (int i = 0; i < 100; ++i) {
		sprintf(buf, "/dir%04d", i);

		PMEMFILE_RMDIR(pfp, buf);
	}
}

static void
test3(PMEMfilepool *pfp)
{
	UT_OUT("test3");

	PMEMFILE_MKDIR(pfp, "/dir1", 0755);
	PMEMFILE_CREATE(pfp, "/dir1/file", O_EXCL, 0644);

	errno = 0;
	UT_ASSERTeq(pmemfile_rmdir(pfp, "/dir1"), -1);
	UT_ASSERTeq(errno, ENOTEMPTY);

	PMEMFILE_UNLINK(pfp, "/dir1/file");

	PMEMFILE_MKDIR(pfp, "/dir1/dir2", 0755);

	errno = 0;
	UT_ASSERTeq(pmemfile_rmdir(pfp, "/dir1"), -1);
	UT_ASSERTeq(errno, ENOTEMPTY);

	PMEMFILE_RMDIR(pfp, "/dir1/dir2");

	PMEMFILE_RMDIR(pfp, "/dir1");
}

static void
test4(PMEMfilepool *pfp)
{
	UT_OUT("test4");
	char buf[PATH_MAX];

	PMEMFILE_MKDIR(pfp, "/dir1", 0755);
	PMEMFILE_MKDIR(pfp, "/dir1/dir2", 0755);
	PMEMFILE_MKDIR(pfp, "/dir1/dir2/dir3", 0755);

	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/");

	PMEMFILE_CHDIR(pfp, "/dir1");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/dir1");

	PMEMFILE_CHDIR(pfp, "/dir1/dir2");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/dir1/dir2");

	PMEMFILE_CHDIR(pfp, "/dir1/dir2/dir3");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/dir1/dir2/dir3");


	PMEMFILE_CHDIR(pfp, "..");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/dir1/dir2");

	PMEMFILE_CHDIR(pfp, "..");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/dir1");

	PMEMFILE_CHDIR(pfp, "..");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/");

	PMEMFILE_CHDIR(pfp, "..");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/");


	PMEMFILE_CHDIR(pfp, "dir1/..");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/");


	PMEMFILE_CHDIR(pfp, "dir1");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/dir1");

	PMEMFILE_CHDIR(pfp, "dir2");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/dir1/dir2");

	PMEMFILE_CHDIR(pfp, "dir3");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/dir1/dir2/dir3");

	PMEMFILE_CHDIR(pfp, ".");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/dir1/dir2/dir3");

	PMEMFILE_RMDIR(pfp, "/dir1/dir2/dir3");
	PMEMFILE_RMDIR(pfp, "/dir1/dir2");
	PMEMFILE_RMDIR(pfp, "/dir1");

	errno = 0;
	UT_ASSERTeq(pmemfile_getcwd(pfp, buf, sizeof(buf)), NULL);
	UT_ASSERTeq(errno, ENOENT);

	PMEMFILE_CHDIR(pfp, "..");
	UT_ASSERTeq(pmemfile_getcwd(pfp, buf, sizeof(buf)), NULL);

	PMEMFILE_CHDIR(pfp, "..");
	UT_ASSERTeq(pmemfile_getcwd(pfp, buf, sizeof(buf)), NULL);

	PMEMFILE_CHDIR(pfp, "..");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/");


	PMEMFILE_CHDIR(pfp, ".");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/");

	PMEMFILE_CHDIR(pfp, "./././././");
	PMEMFILE_GETCWD(pfp, buf, sizeof(buf), "/");

	errno = 0;
	UT_ASSERTeq(pmemfile_chdir(pfp, "dir1/../"), -1);
	UT_ASSERTeq(errno, ENOENT);

	PMEMFILE_CREATE(pfp, "/file", 0, 0777);
	errno = 0;
	UT_ASSERTeq(pmemfile_chdir(pfp, "file"), -1);
	UT_ASSERTeq(errno, ENOTDIR);

	errno = 0;
	UT_ASSERTeq(pmemfile_chdir(pfp, "file/file"), -1);
	UT_ASSERTeq(errno, ENOTDIR);

	PMEMFILE_UNLINK(pfp, "/file");


	PMEMFILE_MKDIR(pfp, "/dir1", 0755);
	PMEMfile *f = PMEMFILE_OPEN(pfp, "dir1", O_DIRECTORY);
	PMEMFILE_FCHDIR(pfp, f);
	PMEMFILE_CLOSE(pfp, f);

	errno = 0;
	UT_ASSERTeq(pmemfile_getcwd(pfp, buf, 0), NULL);
	UT_ASSERTeq(errno, EINVAL);

	char *t;

	t = PMEMFILE_GETCWD(pfp, NULL, 0, "/dir1");
	free(t);

	t = PMEMFILE_GETCWD(pfp, NULL, 10, "/dir1");
	free(t);

	t = pmemfile_getcwd(pfp, NULL, 2);
	UT_ASSERTeq(t, NULL);
	UT_ASSERTeq(errno, ERANGE);

	for (int i = 1; i < strlen("/dir1") + 1; ++i) {
		errno = 0;
		UT_ASSERTeq(pmemfile_getcwd(pfp, buf, i), NULL);
		UT_ASSERTeq(errno, ERANGE);
	}
	PMEMFILE_GETCWD(pfp, buf, strlen("/dir1") + 1, "/dir1");

	PMEMFILE_RMDIR(pfp, "/dir1");
}

static void
test5(PMEMfilepool *pfp)
{
	UT_OUT("test5");
	struct stat stat;

	PMEMFILE_MKDIR(pfp, "/dir1", 0755);
	PMEMFILE_CHDIR(pfp, "/dir1");

	PMEMFILE_CREATE(pfp, "../file1", 0, 0755);
	PMEMFILE_CREATE(pfp, "file2", 0, 0755);
	PMEMFILE_UNLINK(pfp, "file2");
	PMEMFILE_LINK(pfp, "../file1", "file2");
	PMEMFILE_STAT(pfp, "file2", &stat);
	PMEMFILE_STAT(pfp, "../file1", &stat);
	PMEMFILE_LSTAT(pfp, "file2", &stat);
	PMEMFILE_LSTAT(pfp, "../file1", &stat);
	PMEMFILE_MKDIR(pfp, "../dir2", 0755);
	PMEMFILE_MKDIR(pfp, "dir3", 0755);
	PMEMFILE_STAT(pfp, "/dir2", &stat);
	PMEMFILE_STAT(pfp, "/dir1/dir3", &stat);

	UT_ASSERTeq(pmemfile_rmdir(pfp, "/dir1/dir3/.."), -1);
	UT_ASSERTeq(errno, ENOTEMPTY);

	UT_ASSERTeq(pmemfile_rmdir(pfp, "/dir1/dir3/."), -1);
	UT_ASSERTeq(errno, EINVAL);

	UT_ASSERTeq(pmemfile_rmdir(pfp, "/dir1/file2/file"), -1);
	UT_ASSERTeq(errno, ENOTDIR);

	PMEMFILE_RMDIR(pfp, "../dir2");
	PMEMFILE_RMDIR(pfp, "dir3");

	PMEMFILE_UNLINK(pfp, "/dir1/file2");
	PMEMFILE_UNLINK(pfp, "/file1");
	PMEMFILE_RMDIR(pfp, "/dir1");
	PMEMFILE_CHDIR(pfp, "/");
}
static void
test6(PMEMfilepool *pfp)
{
	UT_OUT("test6");
	// struct stat stat;

	PMEMFILE_MKDIR(pfp, "/dir1", 0755);
	PMEMFILE_MKDIR(pfp, "/dir2", 0755);

	PMEMFILE_CREATE(pfp, "/dir1/file1", 0, 0755);
	PMEMFILE_CREATE(pfp, "/dir2/file2", 0, 0755);
	PMEMFILE_CREATE(pfp, "/file3", 0, 0755);

	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 4, 32680, "."},
	    {040777, 4, 32680, ".."},
	    {040755, 2, 4008, "dir1"},
	    {040755, 2, 4008, "dir2"},
	    {0100755, 1, 0, "file3"},
	    {}});
	PMEMFILE_LIST_FILES(pfp, "/dir1", (const struct pmemfile_ls[]) {
	    {040755, 2, 4008, "."},
	    {040777, 4, 32680, ".."},
	    {0100755, 1, 0, "file1"},
	    {}});
	PMEMFILE_LIST_FILES(pfp, "/dir2", (const struct pmemfile_ls[]) {
	    {040755, 2, 4008, "."},
	    {040777, 4, 32680, ".."},
	    {0100755, 1, 0, "file2"},
	    {}});

	PMEMFILE_RENAME(pfp, "/file3", "/file4");
	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 4, 32680, "."},
	    {040777, 4, 32680, ".."},
	    {040755, 2, 4008, "dir1"},
	    {040755, 2, 4008, "dir2"},
	    {0100755, 1, 0, "file4"},
	    {}});
	PMEMFILE_RENAME(pfp, "/dir1/file1", "/dir1/file11");
	PMEMFILE_LIST_FILES(pfp, "/dir1", (const struct pmemfile_ls[]) {
	    {040755, 2, 4008, "."},
	    {040777, 4, 32680, ".."},
	    {0100755, 1, 0, "file11"},
	    {}});
	PMEMFILE_RENAME(pfp, "/dir2/file2", "/dir2/file22");
	PMEMFILE_LIST_FILES(pfp, "/dir2", (const struct pmemfile_ls[]) {
	    {040755, 2, 4008, "."},
	    {040777, 4, 32680, ".."},
	    {0100755, 1, 0, "file22"},
	    {}});

	PMEMFILE_RENAME(pfp, "/file4", "/dir2/file4");
	PMEMFILE_LIST_FILES(pfp, "/", (const struct pmemfile_ls[]) {
	    {040777, 4, 32680, "."},
	    {040777, 4, 32680, ".."},
	    {040755, 2, 4008, "dir1"},
	    {040755, 2, 4008, "dir2"},
	    {}});
	PMEMFILE_LIST_FILES(pfp, "/dir2", (const struct pmemfile_ls[]) {
	    {040755, 2, 4008, "."},
	    {040777, 4, 32680, ".."},
	    {0100755, 1, 0, "file4"},
	    {0100755, 1, 0, "file22"},
	    {}});
	PMEMFILE_RENAME(pfp, "/dir1/file11", "/dir2/file11");
	PMEMFILE_LIST_FILES(pfp, "/dir1", (const struct pmemfile_ls[]) {
	    {040755, 2, 4008, "."},
	    {040777, 4, 32680, ".."},
	    {}});
	PMEMFILE_LIST_FILES(pfp, "/dir2", (const struct pmemfile_ls[]) {
	    {040755, 2, 4008, "."},
	    {040777, 4, 32680, ".."},
	    {0100755, 1, 0, "file4"},
	    {0100755, 1, 0, "file22"},
	    {0100755, 1, 0, "file11"},
	    {}});
	PMEMFILE_RENAME(pfp, "/dir2/file11", "/dir2/file22");
	PMEMFILE_LIST_FILES(pfp, "/dir2", (const struct pmemfile_ls[]) {
	    {040755, 2, 4008, "."},
	    {040777, 4, 32680, ".."},
	    {0100755, 1, 0, "file4"},
	    {0100755, 1, 0, "file22"},
	    {}});

	PMEMFILE_UNLINK(pfp, "/dir2/file22");
	PMEMFILE_UNLINK(pfp, "/dir2/file4");

	PMEMFILE_RMDIR(pfp, "/dir2");
	PMEMFILE_RMDIR(pfp, "/dir1");

	int ret = pmemfile_rmdir(pfp, "/");
	UT_ASSERTeq(ret, -1);
	UT_ASSERTeq(errno, EBUSY);
}

int
main(int argc, char *argv[])
{
	if (argc < 2)
		UT_FATAL("usage: %s file-name", argv[0]);

	const char *path = argv[1];

	PMEMfilepool *pfp = PMEMFILE_MKFS(path);

	test0(pfp);
	list_files(pfp, "/", 2, 1, "after test0");
	test1(pfp);
	list_files(pfp, "/", 2, 1, "after test1");
	test2(pfp);
	list_files(pfp, "/", 2, 1, "after test2");
	test3(pfp);
	list_files(pfp, "/", 2, 1, "after test3");
	test4(pfp);
	list_files(pfp, "/", 2, 1, "after test4");
	test5(pfp);
	list_files(pfp, "/", 2, 1, "after test5");
	test6(pfp);
	list_files(pfp, "/", 2, 1, "after test6");

	pmemfile_pool_close(pfp);
}
