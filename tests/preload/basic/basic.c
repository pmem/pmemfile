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
 * basic.c - a dummy prog using pmemfile, checking each return value
 */

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

int
main(int argc, char **argv)
{
	int fd;
	struct dirent *dir_entry;
	DIR  *dir_to_list;
	char buf0[] = "Hello #0 World!\n";
	char buf1[] = "Hello #1 World!\n";
	struct stat statbuf;

	/*
	 * First check if an empty path, or a path containing
	 * only slash characters trigger any crash in
	 * the path resolving code.
	 */
	(void) stat("", &statbuf);
	(void) stat("/", &statbuf);
	(void) stat("///////////", &statbuf);

	if (argc < 6)
		return 1;

	const char *full_path = argv[1];
	const char *chdir_path = argv[2];
	const char *relative_path = argv[3];
	const char *dir_to_list_path = argv[4];
	const char *inner_file = argv[5];

	/*
	 * Creating a file with an absolute path,
	 * and writing to it.
	 */
	if ((fd = open(full_path, O_CREAT | O_RDWR, 0666)) < 0)
		err(1, "open(\"%s\", O_CREAT | O_RDWR, 0666) ", full_path);

	if (close(fd) != 0)
		err(1, "close \"%s\"", full_path);

	if ((fd = open(full_path, O_WRONLY)) < 0)
		err(1, "open(\"%s\", O_WRONLY) ", full_path);

	if (write(fd, buf0, sizeof(buf0)) != sizeof(buf0))
		err(1, "write into \"%s\" ", full_path);

	if (fallocate(fd, FALLOC_FL_KEEP_SIZE, 1, 0x1111) != 0)
		err(1, "fallocate \"%s\" ", full_path);

	if (close(fd) != 0)
		err(1, "close \"%s\"", full_path);

	if (chdir(chdir_path) != 0)
		err(1, "chdir to \"%s\"", chdir_path);

	/* Test file access outside of pmemfile pool. */
	if (truncate("trunc_test", 10) == 0)
		err(1, "truncate trunc_test 10 unexpectedly succeeded");

	if (unlink("trunc_test") == 0)
		err(1, "unlink trunc_test unexpectedly succeeded");

	if ((fd = creat("trunc_test", 0777) < 0))
		err(1, "creat trunc_test");
	close(fd);

	if (truncate("trunc_test", 10))
		err(1, "truncate trunc_test 10");

	if (unlink("trunc_test"))
		err(1, "unlink trunc_test");

	/*
	 * Creating a file with an relative path,
	 * and writing to it.
	 */
	if ((fd = open(relative_path, O_CREAT | O_RDWR, 0666)) < 0)
		err(1, "open(\"%s\", O_CREAT | O_RDWR, 0666) ", relative_path);

	if (close(fd) != 0)
		err(1, "close \"%s\"", relative_path);

	if ((fd = open(relative_path, O_WRONLY, 0)) < 0)
		err(1, "open(\"%s\", O_WRONLY) ", relative_path);

	if (write(fd, buf1, sizeof(buf1)) != sizeof(buf1))
		err(1, "write into \"%s\" ", relative_path);

	errno = 0;
	if (ftruncate(fd, 0) != 0)
		err(1, "ftruncate \"%s\"", relative_path);

	if (truncate(relative_path, 2346) != 0)
		err(1, "truncate \"%s\"", relative_path);

	if (close(fd) != 0)
		err(1, "close \"%s\"", relative_path);

	/*
	 * Listing the entries in a directory, using
	 * the libc interface.
	 */
	if ((dir_to_list = opendir(dir_to_list_path)) == NULL)
		err(1, "opendir \"%s\"", dir_to_list_path);

	while ((dir_entry = readdir(dir_to_list)) != NULL)
		puts(dir_entry->d_name);

	if (errno != 0)
		err(1, "readdir at \"%s\"", dir_to_list_path);

	if (closedir(dir_to_list) != 0)
		err(1, "closedir at \"%s\"", dir_to_list_path);

	if ((fd = open(dir_to_list_path, O_RDONLY | O_DIRECTORY)) < 0)
		err(1, "open \"%s\"", dir_to_list_path);

	if (ftruncate(fd, 123) == 0)
		err(1, "ftruncate succeding on a directory");

	if (errno != EBADF && errno != EINVAL)
		err(1, "ftruncate not setting correct errno");

	errno = 0;

	struct stat stat_buf;
	if (fstatat(fd, inner_file, &stat_buf, 0) != 0)
		err(1, "fstatat \"%s/%s\"", dir_to_list_path, inner_file);

	if (fchdir(fd) != 0)
		err(1, "fchdir");

	if (unlinkat(AT_FDCWD, inner_file, AT_REMOVEDIR) == 0)
		errx(1, "unlinkat \"%s/%s\" AT_REMOVEDIR",
		    dir_to_list_path, inner_file);

	errno = 0;

	if (unlinkat(AT_FDCWD, inner_file, 0) != 0)
		err(1, "unlinkat \"%s/%s\"", dir_to_list_path, inner_file);

	if (close(fd) != 0)
		err(1, "close \"%s\"", dir_to_list_path);

	return 0;
}
