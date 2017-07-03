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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>

#define BUFFER_SIZE 1024 * 1024

static int fd;
static char buffer[BUFFER_SIZE];
static char *filename;

static void *
fd_close(void *x)
{
	for (int i = 0; i < 100000; i++) {
		close(fd);

		int new_fd = open(filename, O_RDWR);

		if (new_fd != fd)
			close(new_fd);
	}

	return NULL;
}

int
main(int argc, char **argv)
{
	if (argc < 2)
		return 1;

	memset(buffer, '1', BUFFER_SIZE);
	filename = argv[1];

	fd = open(filename, O_RDWR | O_CREAT, 0777);

	pthread_t t1, t2;
	if (pthread_create(&t1, NULL, fd_close, NULL))
		return 1;

	if (pthread_create(&t2, NULL, fd_close, NULL))
		return 1;

	for (int i = 0; i < 1000; i++) {
		ssize_t r = write(fd, buffer, BUFFER_SIZE);
		(void) r;
	}

	if (pthread_join(t1, NULL))
		return 1;

	if (pthread_join(t2, NULL))
		return 1;

	close(fd);

	return 0;
}
