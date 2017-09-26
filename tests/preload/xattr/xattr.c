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
 * xattr.c - validate that *xattr functions work for paths outside of pmemfile
 * pool where path crosses mount point
 */

#define _GNU_SOURCE

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/xattr.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	if (argc < 2)
		return -1;

	char path[PATH_MAX];
	sprintf(path, "%s/file", argv[1]);
	int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
	if (fd < 0)
		err(1, "open %s", path);

	close(fd);

	char value[1024];
	ssize_t size = getxattr(path, "user.attr1", value, sizeof(value));
	if (size != -1)
		err(2, "attr1 already exists");

	static const char lorem[] =
		"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";

	if (setxattr(path, "user.attr1", lorem, sizeof(lorem),
			XATTR_CREATE) != 0)
		err(3, "setxattr failed");

	size = getxattr(path, "user.attr1", value, sizeof(value));
	if (size < 0)
		err(4, "attr1 is empty");
	if ((size_t)size != sizeof(lorem))
		errx(5, "attr1 has unexpected value %ld", size);

	if (memcmp(lorem, value, sizeof(lorem)) != 0)
		err(6, "unexpected attr1 value: %s", value);

	const char *fmt = "%s/mount_point/../file";
	if (strlen(argv[1]) + strlen(fmt) >= sizeof(path))
		err(61, "too long path");
	sprintf(path, fmt, argv[1]);

	memset(value, 0, sizeof(value));
	size = getxattr(path, "user.attr1", value, sizeof(value));
	if (size < 0)
		err(7, "attr1 is empty (2)");
	if ((size_t)size != sizeof(lorem))
		errx(8, "attr1 has unexpected value %ld (2)", size);

	if (memcmp(lorem, value, sizeof(lorem)) != 0)
		err(9, "unexpected attr1 value: %s", value);

	if (setxattr(path, "user.attr1", "meh", 4, XATTR_REPLACE) != 0)
		err(10, "setxattr failed (2)");

	memset(value, 0, sizeof(value));
	size = getxattr(path, "user.attr1", value, sizeof(value));
	if (size < 0)
		err(11, "attr1 is empty (3)");
	if ((size_t)size != 4)
		errx(12, "attr1 has unexpected value %ld (3)", size);

	if (memcmp("meh", value, 4) != 0)
		err(13, "unexpected attr1 value: %s (2)", value);

	memset(value, 0, sizeof(value));
	size = listxattr(path, value, sizeof(value));
	if (size < 0)
		err(14, "listxattr failed");
	if (size != 11)
		errx(15, "listxattr returned unexpected value: %ld", size);

	if (lremovexattr(path, "user.attr1"))
		err(16, "lremovexattr failed");

	return 0;
}
