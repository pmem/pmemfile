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
 * file_core_getdents.c -- unit test for pmemfile_getdents & pmemfile_getdents64
 */

#include "pmemfile_test.h"
#include "unittest.h"
#include <ctype.h>

static void
dump_linux_dirents(void *dirp, unsigned length)
{
	char *buf = (void *)dirp;
	for (int i = 0; i < length; ) {
		long ino = *(long *)&buf[i];
		UT_OUT("d_ino.txt: 0x%016lx", ino);
		UT_OUTF(OF_NONL, "d_ino.bin: ");
		for (int j = 0; j < 8; ++j, ++i)
			UT_OUTF(OF_NONL | OF_NOPREFIX, "0x%02hhx ", buf[i]);
		UT_OUTF(OF_NOPREFIX, " ");

		long off = *(long *)&buf[i];
		UT_OUT("d_off.txt: 0x%016lx", off);
		UT_OUTF(OF_NONL, "d_off.bin: ");
		for (int j = 0; j < 8; ++j, ++i)
			UT_OUTF(OF_NONL | OF_NOPREFIX, "0x%02hhx ", buf[i]);
		UT_OUTF(OF_NOPREFIX, " ");

		short int reclen = *(short *)&buf[i];
		UT_OUT("d_reclen.txt: %hd", reclen);
		UT_OUTF(OF_NONL, "d_reclen.bin: ");
		for (int j = 0; j < 2; ++j, ++i)
			UT_OUTF(OF_NONL | OF_NOPREFIX, "0x%02hhx ", buf[i]);
		UT_OUTF(OF_NOPREFIX, " ");

		UT_OUT("d_name.txt: \"%s\"", buf + i);
		UT_OUTF(OF_NONL, "d_name.bin: ");
		for (int j = 0; j < reclen - 8 - 8 - 2; ++j, ++i)
			UT_OUTF(OF_NONL | OF_NOPREFIX, "0x%02hhx (%c) ",
					buf[i], isprint(buf[i]) ? buf[i] : '?');
		UT_OUTF(OF_NOPREFIX, " ");
		UT_OUT("-");
	}
	UT_OUT("---");
}

static void
dump_linux_dirents64(void *dirp, unsigned length)
{
	char *buf = (void *)dirp;
	for (int i = 0; i < length; ) {
		long ino = *(long *)&buf[i];
		UT_OUT("d_ino.txt: 0x%016lx", ino);
		UT_OUTF(OF_NONL, "d_ino.bin: ");
		for (int j = 0; j < 8; ++j, ++i)
			UT_OUTF(OF_NONL | OF_NOPREFIX, "0x%02hhx ", buf[i]);
		UT_OUTF(OF_NOPREFIX, " ");

		long off = *(long *)&buf[i];
		UT_OUT("d_off.txt: 0x%016lx", off);
		UT_OUTF(OF_NONL, "d_off.bin: ");
		for (int j = 0; j < 8; ++j, ++i)
			UT_OUTF(OF_NONL | OF_NOPREFIX, "0x%02hhx ", buf[i]);
		UT_OUTF(OF_NOPREFIX, " ");

		short int reclen = *(short *)&buf[i];
		UT_OUT("d_reclen.txt: %hd", reclen);
		UT_OUTF(OF_NONL, "d_reclen.bin: ");
		for (int j = 0; j < 2; ++j, ++i)
			UT_OUTF(OF_NONL | OF_NOPREFIX, "0x%02hhx ", buf[i]);
		UT_OUTF(OF_NOPREFIX, " ");

		char type = *(char *)&buf[i];
		UT_OUT("d_type.txt: %hhd", type);
		UT_OUTF(OF_NONL, "d_type.bin: ");
		for (int j = 0; j < 1; ++j, ++i)
			UT_OUTF(OF_NONL | OF_NOPREFIX, "0x%02hhx ", buf[i]);
		UT_OUTF(OF_NOPREFIX, " ");

		UT_OUT("d_name.txt: \"%s\"", buf + i);
		UT_OUTF(OF_NONL, "d_name.bin: ");
		for (int j = 0; j < reclen - 8 - 8 - 2 - 1; ++j, ++i)
			UT_OUTF(OF_NONL | OF_NOPREFIX, "0x%02hhx (%c) ",
					buf[i], isprint(buf[i]) ? buf[i] : '?');
		UT_OUTF(OF_NOPREFIX, " ");
		UT_OUT("-");
	}

	UT_OUT("---");
}

static void
test1(PMEMfilepool *pfp)
{
	PMEMFILE_CREATE(pfp, "/file1", O_EXCL, 0644);

	PMEMFILE_CREATE(pfp, "/file2with_long_name", O_EXCL, 0644);

	PMEMFILE_CREATE(pfp, "/file3with_very_long_name"
			"_1234567890_1234567890_1234567890_1234567890"
			"_1234567890_1234567890_1234567890_1234567890"
			"_1234567890_1234567890_1234567890_1234567890"
			"_1234567890_1234567890_1234567890_1234567890"
			"_1234567890_1234567890_1234567890_1234567890"
			"_qwertyuiop", O_EXCL, 0644);

	PMEMFILE_CREATE(pfp, "/file4", O_EXCL, 0644);

	PMEMfile *f = PMEMFILE_OPEN(pfp, "/", O_DIRECTORY | O_RDONLY);

	char buf[32758];
	int r = pmemfile_getdents(pfp, f, (void *)buf, sizeof(buf));
	UT_ASSERT(r > 0);

	dump_linux_dirents(buf, r);

	r = pmemfile_getdents(pfp, f, (void *)buf, sizeof(buf));
	UT_ASSERT(r == 0);

	r = pmemfile_lseek(pfp, f, 0, SEEK_SET);
	UT_ASSERT(r == 0);

	r = pmemfile_getdents64(pfp, f, (void *)buf, sizeof(buf));
	UT_ASSERT(r > 0);

	dump_linux_dirents64(buf, r);

	r = pmemfile_getdents64(pfp, f, (void *)buf, sizeof(buf));
	UT_ASSERT(r == 0);

	PMEMFILE_CLOSE(pfp, f);
}

static void
test2(PMEMfilepool *pfp)
{
	PMEMFILE_MKDIR(pfp, "/dir1", 0755);

	PMEMfile *f = PMEMFILE_OPEN(pfp, "/dir1", O_DIRECTORY | O_RDONLY);

	char buf[32758];

	int r = pmemfile_getdents(pfp, f, (void *)buf, sizeof(buf));
	UT_ASSERT(r > 0);
	dump_linux_dirents(buf, r);

	PMEMFILE_CREATE(pfp, "/dir1/file1", O_EXCL, 0644);

	PMEMFILE_CREATE(pfp, "/dir1/file2", O_EXCL, 0644);

	PMEMFILE_CREATE(pfp, "/dir1/file3", O_EXCL, 0644);

	PMEMFILE_LSEEK(pfp, f, 0, SEEK_SET, 0);
	r = pmemfile_getdents64(pfp, f, (void *)buf, sizeof(buf));
	UT_ASSERT(r > 0);
	dump_linux_dirents64(buf, r);

	static const struct pmemfile_ls expected[] = {
	    {040755, 2, 4008, "."},
	    {040777, 3, 4008, ".."},
	    {0100644, 1, 0, "file1"},
	    {0100644, 1, 0, "file2"},
	    {0100644, 1, 0, "file3"},
	    {}};

	const struct pmemfile_ls *end;

	end = PMEMFILE_PRINT_FILES64(pfp, f, buf, r, expected, 0);
	UT_ASSERT(end->name == NULL);

	PMEMFILE_CLOSE(pfp, f);

	PMEMFILE_UNLINK(pfp, "/dir1/file1");
	PMEMFILE_UNLINK(pfp, "/dir1/file2");
	PMEMFILE_UNLINK(pfp, "/dir1/file3");
	PMEMFILE_RMDIR(pfp, "/dir1");
}

int
main(int argc, char *argv[])
{
	START(argc, argv, "file_core_getdents");

	if (argc < 2)
		UT_FATAL("usage: %s file-name", argv[0]);

	const char *path = argv[1];

	PMEMfilepool *pfp = PMEMFILE_MKFS(path);

	test1(pfp);
	test2(pfp);

	pmemfile_pool_close(pfp);

	DONE(NULL);
}
