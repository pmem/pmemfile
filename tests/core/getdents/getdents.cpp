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

	char buf[32758];
	struct linux_dirent *dirents = (struct linux_dirent *)buf;
	struct linux_dirent64 *dirents64 = (struct linux_dirent64 *)buf;

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
						     {040755, 2, 4008, "."},
						     {040777, 3, 4008, ".."},
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
