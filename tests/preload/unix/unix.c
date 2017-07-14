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
 * unix.c - validate that unix sockets functions work for paths outside of
 * pmemfile pool when path crosses mount point
 */

#define _GNU_SOURCE

#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	if (argc < 2)
		return -1;

	const char *add = "/mount_point/../file";
	struct sockaddr_un sockaddr;
	struct stat statbuf;

	if (strlen(argv[1]) >= sizeof(sockaddr.sun_path) - strlen(add))
		err(1, "too long path (%zd >= %zd)", strlen(argv[1]),
				sizeof(sockaddr.sun_path) - strlen(add));

	/* test non-pmemfile path */
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		err(2, "socket failed");

	sockaddr.sun_family = AF_UNIX;
	sprintf(sockaddr.sun_path, "%s/file", argv[1]);

	if (stat(sockaddr.sun_path, &statbuf) == 0)
		err(3, "file already exists");

	if (bind(fd, &sockaddr, sizeof(sockaddr)))
		err(4, "bind failed");

	close(fd);

	if (stat(sockaddr.sun_path, &statbuf) != 0)
		err(5, "file doesn't exist");

	if (unlink(sockaddr.sun_path))
		err(6, "unlink failed");

	if (stat(sockaddr.sun_path, &statbuf) == 0)
		err(7, "file still exists");

	/* and now exercise pmemfile path */

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		err(8, "socket failed");

	sprintf(sockaddr.sun_path, "%s%s", argv[1], add);

	if (bind(fd, &sockaddr, sizeof(sockaddr)))
		err(9, "bind failed");

	close(fd);

	if (stat(sockaddr.sun_path, &statbuf) != 0)
		err(10, "file doesn't exist");

	if (unlink(sockaddr.sun_path))
		err(11, "unlink failed");

	if (stat(sockaddr.sun_path, &statbuf) == 0)
		err(12, "file still exists");

	return 0;
}
