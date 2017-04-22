/*
 * Copyright 2016, Intel Corporation
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
 * pmemfile-cat.c -- pmemfile cat command source file
 */
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libpmemfile-core.h"

static void
print_version(void)
{
	puts("pmemfile-cat v0");
}

static void
print_usage(FILE *stream, const char *progname)
{
	fprintf(stream, "Usage: %s [OPTION]... POOL FILE...\n", progname);
}

static void
dump_file(PMEMfilepool *pool, const char *path)
{
	char buffer[0x10000];
	ssize_t read_size;

	errno = 0;

	PMEMfile *file = pmemfile_open(pool, path, O_RDONLY, 0);

	if (file == NULL) {
		perror(path);
		exit(1);
	}

	do {
		read_size = pmemfile_read(pool, file, buffer, sizeof(buffer));

		if (read_size < 0 || errno != 0) {
			perror(path);
			exit(1);
		}

		if (fwrite(buffer, (size_t)read_size, 1, stdout) != 1)
			abort();

	} while (read_size == sizeof(buffer));

	pmemfile_close(pool, file);
}

int
main(int argc, char *argv[])
{
	int opt;
	PMEMfilepool *pool;

	while ((opt = getopt(argc, argv, "vh")) >= 0) {
		switch (opt) {
		case 'v':
		case 'V':
			print_version();
			return 0;
		case 'h':
		case 'H':
			print_usage(stdout, argv[0]);
			return 0;
		default:
			print_usage(stderr, argv[0]);
			return 2;
		}
	}

	if (optind == argc) {
		print_usage(stderr, argv[0]);
		return 2;
	}

	pool = pmemfile_pool_open(argv[optind]);

	if (pool == NULL) {
		perror(argv[optind]);
		return 1;
	}

	++optind;

	while (optind < argc)
		dump_file(pool, argv[optind++]);

	pmemfile_pool_close(pool);

	return 0;
}
