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
 * getdents.cpp -- unit test for pmemfile_getdents & pmemfile_getdents64
 */

#include "pmemfile_test.hpp"

class getdents : public pmemfile_test {
public:
	getdents() : pmemfile_test()
	{
	}
};

static void
dump_linux_dirents(void *dirp, unsigned length)
{
	char *buf = (char *)dirp;
	for (unsigned i = 0; i < length;) {
		long ino = *(long *)&buf[i];
		T_OUT("d_ino.txt: 0x%016lx\n", ino);
		T_OUT("d_ino.bin:");
		for (unsigned j = 0; j < 8; ++j, ++i)
			T_OUT(" 0x%02hhx", buf[i]);
		T_OUT("\n");

		long off = *(long *)&buf[i];
		T_OUT("d_off.txt: 0x%016lx\n", off);
		T_OUT("d_off.bin:");
		for (unsigned j = 0; j < 8; ++j, ++i)
			T_OUT(" 0x%02hhx", buf[i]);
		T_OUT("\n");

		short int reclen = *(short *)&buf[i];
		T_OUT("d_reclen.txt: %hd\n", reclen);
		T_OUT("d_reclen.bin:");
		for (unsigned j = 0; j < 2; ++j, ++i)
			T_OUT(" 0x%02hhx", buf[i]);
		T_OUT("\n");

		T_OUT("d_name.txt: \"%s\"\n", buf + i);
		T_OUT("d_name.bin:");
		for (int j = 0; j < reclen - 8 - 8 - 2; ++j, ++i)
			T_OUT(" 0x%02hhx (%c)", buf[i],
			      isprint(buf[i]) ? buf[i] : '?');
		T_OUT("\n");
		T_OUT("-\n");
	}
	T_OUT("---\n");
}

static void
dump_linux_dirents64(void *dirp, unsigned length)
{
	char *buf = (char *)dirp;
	for (size_t i = 0; i < length;) {
		long ino = *(long *)&buf[i];
		T_OUT("d_ino.txt: 0x%016lx\n", ino);
		T_OUT("d_ino.bin:");
		for (int j = 0; j < 8; ++j, ++i)
			T_OUT(" 0x%02hhx", buf[i]);
		T_OUT("\n");

		long off = *(long *)&buf[i];
		T_OUT("d_off.txt: 0x%016lx\n", off);
		T_OUT("d_off.bin:");
		for (int j = 0; j < 8; ++j, ++i)
			T_OUT(" 0x%02hhx", buf[i]);
		T_OUT("\n");

		short int reclen = *(short *)&buf[i];
		T_OUT("d_reclen.txt: %hd\n", reclen);
		T_OUT("d_reclen.bin:");
		for (int j = 0; j < 2; ++j, ++i)
			T_OUT(" 0x%02hhx", buf[i]);
		T_OUT("\n");

		char type = *(char *)&buf[i];
		T_OUT("d_type.txt: %hhd\n", type);
		T_OUT("d_type.bin:");
		for (int j = 0; j < 1; ++j, ++i)
			T_OUT(" 0x%02hhx", buf[i]);
		T_OUT("\n");

		T_OUT("d_name.txt: \"%s\"\n", buf + i);
		T_OUT("d_name.bin:");
		for (int j = 0; j < reclen - 8 - 8 - 2 - 1; ++j, ++i)
			T_OUT(" 0x%02hhx (%c)", buf[i],
			      isprint(buf[i]) ? buf[i] : '?');
		T_OUT("\n");
		T_OUT("-\n");
	}

	T_OUT("---\n");
}

TEST_F(getdents, 1)
{
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file1", PMEMFILE_O_EXCL, 0644));

	ASSERT_TRUE(test_pmemfile_create(pfp, "/file2with_long_name",
					 PMEMFILE_O_EXCL, 0644));

	ASSERT_TRUE(test_pmemfile_create(
		pfp, "/file3with_very_long_name"
		     "_1234567890_1234567890_1234567890_1234567890"
		     "_1234567890_1234567890_1234567890_1234567890"
		     "_1234567890_1234567890_1234567890_1234567890"
		     "_1234567890_1234567890_1234567890_1234567890"
		     "_1234567890_1234567890_1234567890_1234567890"
		     "_qwertyuiop",
		PMEMFILE_O_EXCL, 0644));

	ASSERT_TRUE(test_pmemfile_create(pfp, "/file4", PMEMFILE_O_EXCL, 0644));

	PMEMfile *f = pmemfile_open(pfp, "/",
				    PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* 4 entries in directory and '.' '..' */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_END), 6);

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET), 0);

	char buf[32758];
	struct linux_dirent *dirents = (struct linux_dirent *)buf;
	struct linux_dirent64 *dirents64 = (struct linux_dirent64 *)buf;

	PMEMfile *regfile = pmemfile_open(pfp, "/file4", PMEMFILE_O_RDONLY);
	errno = 0;
	ASSERT_EQ(pmemfile_getdents(pfp, regfile, dirents, sizeof(buf)), -1);
	EXPECT_EQ(errno, ENOTDIR);
	pmemfile_close(pfp, regfile);

	errno = 0;
	ASSERT_EQ(pmemfile_getdents(pfp, f, NULL, sizeof(buf)), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_getdents(pfp, NULL, dirents, sizeof(buf)), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_getdents(NULL, f, dirents, sizeof(buf)), -1);
	EXPECT_EQ(errno, EFAULT);

	int r = pmemfile_getdents(pfp, f, dirents, sizeof(buf));
	ASSERT_GT(r, 0);

	dump_linux_dirents(buf, (unsigned)r);

	r = pmemfile_getdents(pfp, f, dirents, sizeof(buf));
	ASSERT_EQ(r, 0);

	pmemfile_off_t off = pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET);
	ASSERT_EQ(off, 0);

	r = pmemfile_getdents64(pfp, f, dirents64, sizeof(buf));
	ASSERT_GT(r, 0);

	dump_linux_dirents64(buf, (unsigned)r);

	r = pmemfile_getdents64(pfp, f, dirents64, sizeof(buf));
	ASSERT_EQ(r, 0);

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file2with_long_name"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp,
				  "/file3with_very_long_name"
				  "_1234567890_1234567890_1234567890_1234567890"
				  "_1234567890_1234567890_1234567890_1234567890"
				  "_1234567890_1234567890_1234567890_1234567890"
				  "_1234567890_1234567890_1234567890_1234567890"
				  "_1234567890_1234567890_1234567890_1234567890"
				  "_qwertyuiop"),
		  0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/file4"), 0);
}

TEST_F(getdents, 2)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0755), 0);

	PMEMfile *f = pmemfile_open(pfp, "/dir1",
				    PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	char buf[32758];
	struct linux_dirent *dirents = (struct linux_dirent *)buf;
	struct linux_dirent64 *dirents64 = (struct linux_dirent64 *)buf;

	int r = pmemfile_getdents(pfp, f, dirents, sizeof(buf));
	ASSERT_GT(r, 0);
	dump_linux_dirents(buf, (unsigned)r);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir1/file1", PMEMFILE_O_EXCL,
					 0644));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir1/file2", PMEMFILE_O_EXCL,
					 0644));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir1/file3", PMEMFILE_O_EXCL,
					 0644));

	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET), 0);
	r = pmemfile_getdents64(pfp, f, dirents64, sizeof(buf));
	ASSERT_GT(r, 0);
	dump_linux_dirents64(buf, (unsigned)r);

	auto files = test_list_files(pfp, f, buf, (unsigned)r);
	ASSERT_TRUE(test_compare_dirs(files, std::vector<pmemfile_ls>{
						     {040755, 2, 4000, "."},
						     {040777, 3, 4000, ".."},
						     {0100644, 1, 0, "file1"},
						     {0100644, 1, 0, "file2"},
						     {0100644, 1, 0, "file3"},
					     }));

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/file1"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/file2"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/dir1/file3"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/dir1"), 0);
}

struct linux_dirent {
	long ino;
	off_t off;
	unsigned short reclen;
	char name[];
};

TEST_F(getdents, offset)
{
	/* create 50 files and 50 directories */
	size_t file_dir_count = 50;
	char path[1001];
	PMEMfile *f = NULL;

	ASSERT_TRUE(test_empty_dir(pfp, "/"));
	memset(path, 0xff, sizeof(path));

	for (size_t i = 0; i < file_dir_count; ++i) {
		sprintf(path, "/file%04zu", i);
		f = pmemfile_open(pfp, path, PMEMFILE_O_CREAT |
					  PMEMFILE_O_EXCL | PMEMFILE_O_WRONLY,
				  0644);
		ASSERT_NE(f, nullptr) << strerror(errno);
		pmemfile_close(pfp, f);

		sprintf(path, "/dir%04zu", i);
		ASSERT_EQ(pmemfile_mkdir(pfp, path, 0755), 0);
	}

	/* open main directory and see if lseek to end returns correct value */
	f = pmemfile_open(pfp, "/", PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	/* entry 102 - 100 files or dirs and '.' + '..' */
	size_t max_dir_number = (file_dir_count * 2) / 14;
	size_t max_dir_entry = (file_dir_count * 2) % 14;
	pmemfile_off_t val = ((pmemfile_off_t)max_dir_number << 32) +
		(pmemfile_off_t)max_dir_entry + 2;
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_END), val);

	/*
	 * getdents with offset 1 should return all entries except one
	 * with 50 files and 50 directories it should return 101 entries
	 * (50 files, 50 directories, '.' and '..')
	 */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 1, PMEMFILE_SEEK_SET), 1);

	int i = 0;
	char buf[32768];
	int nread = -1;
	linux_dirent *d;

	size_t entries_found = 0;
	while (nread != 0) {
		nread = pmemfile_getdents(pfp, f, (linux_dirent *)buf,
					  sizeof(buf));

		ASSERT_NE(nread, -1);

		for (int pos = 0; pos < nread;) {
			d = (linux_dirent *)(buf + pos);
			entries_found++;
			pos += d->reclen;
		}
	}

	ASSERT_EQ(entries_found, file_dir_count * 2 + 1);

	/* reset offset for getdents */
	ASSERT_EQ(pmemfile_lseek(pfp, f, 0, PMEMFILE_SEEK_SET), 0);

	/* fill offsets array */
	pmemfile_off_t *offsets = new pmemfile_off_t[file_dir_count * 2 + 3];
	offsets[0] = 0;
	offsets[file_dir_count * 2 + 2] = INT64_MAX;

	nread = -1;
	while (nread != 0) {
		nread = pmemfile_getdents(pfp, f, (linux_dirent *)buf,
					  sizeof(buf));

		ASSERT_NE(nread, -1);

		for (int pos = 0; pos < nread;) {
			d = (linux_dirent *)(buf + pos);
			offsets[++i] = d->off;
			pos += d->reclen;
		}
	}

	/*
	 * run getdents with bigger offset and check if seeking with offset
	 * affects output from getdents - each getdents call should return
	 * one less entry than previous one
	 */
	for (size_t i = 0; i < file_dir_count * 2 + 3; i++) {
		pmemfile_off_t offset =
			pmemfile_lseek(pfp, f, offsets[i], PMEMFILE_SEEK_SET);
		ASSERT_EQ(offset, offsets[i]);

		size_t tofind = file_dir_count * 2 + 2 - i;
		size_t found = 0;

		nread = -1;

		while (nread != 0) {
			nread = pmemfile_getdents(pfp, f, (linux_dirent *)buf,
						  sizeof(buf));

			ASSERT_NE(nread, -1);

			for (int pos = 0; pos < nread;) {
				found++;
				d = (linux_dirent *)(buf + pos);
				pos += d->reclen;
			}
		}

		ASSERT_EQ(found, tofind);
	}

	/* cleanup */
	pmemfile_close(pfp, f);

	for (size_t i = 0; i < file_dir_count; ++i) {
		sprintf(path, "/file%04zu", i);

		int ret = pmemfile_unlink(pfp, path);
		ASSERT_EQ(ret, 0) << strerror(errno);

		sprintf(path, "/dir%04zu", i);
		ASSERT_EQ(pmemfile_rmdir(pfp, path), 0);
	}

	delete[] offsets;
}

TEST_F(getdents, short_buffer)
{
	PMEMfile *f = pmemfile_open(pfp, "/",
				    PMEMFILE_O_DIRECTORY | PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr) << strerror(errno);

	char buf[50];
	for (int i = 0; i < 20; ++i) {
		sprintf(buf, "/file%d", i);
		ASSERT_TRUE(test_pmemfile_create(pfp, buf, 0, 0644));
	}

	struct linux_dirent *dirents = (struct linux_dirent *)buf;

	int r = pmemfile_getdents(pfp, f, dirents, sizeof(buf));
	ASSERT_GT(r, 0);
	dump_linux_dirents(buf, (unsigned)r);

	r = pmemfile_getdents(pfp, f, dirents, sizeof(buf));
	ASSERT_GT(r, 0);
	dump_linux_dirents(buf, (unsigned)r);

	for (int i = 0; i < 20; ++i) {
		sprintf(buf, "/file%d", i);
		ASSERT_EQ(pmemfile_unlink(pfp, buf), 0);
	}

	pmemfile_close(pfp, f);
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
