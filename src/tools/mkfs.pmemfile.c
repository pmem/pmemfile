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
 * mkfs-pmemfile.c -- pmemfile mkfs command source file
 *
 * The work is done in the pmemfile_mkfs function, this
 * tool is basically only a wrapper around it.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libpmemfile-core.h"

static const char *progname;

static void
print_version(void)
{
	puts("mkfs-pmemfile v0 - experimental");
}

static void
print_usage(FILE *stream)
{
	fprintf(stream,
	    "Usage: %s [-v] [-h] path fs-size\n"
	    "Options:\n"
	    "  -v      print version\n"
	    "  -h      print this help text\n",
	    progname);
}

static void
invalid_size(void)
{
	fputs("Invalid size\n", stderr);
	print_usage(stderr);
	exit(2);
}

static unsigned long long
mul_1024(unsigned long long size)
{
	if (size > ULLONG_MAX / 1024)
		invalid_size();

	return size * 1024;
}

static size_t
parse_size(const char *str)
{
	unsigned long long size;
	char *endptr;

	errno = 0;
	size = strtoull(str, &endptr, 0);

	if (errno != 0)
		invalid_size();

	switch (tolower((unsigned char)*endptr)) {
		case 'p': // Well, you never know what the future brings
			size = mul_1024(size);
			/* fallthrough */
		case 't':
			size = mul_1024(size);
			/* fallthrough */
		case 'g':
			size = mul_1024(size);
			/* fallthrough */
		case 'm':
			size = mul_1024(size);
			/* fallthrough */
		case 'k':
			size = mul_1024(size);
			if (endptr[1] != '\0')
				invalid_size();
			break;
		case '\0':
			break;
		default:
			invalid_size();
	}

	/*
	 * Whish I could use C11 in 2016, but this needs to compile
	 * on MSVC, so no static_assert for you.
	 */
	if (SIZE_MAX != ULLONG_MAX) {
		if (size > SIZE_MAX)
			invalid_size();
	}

	return (size_t)size;
}

int
main(int argc, char *argv[])
{
	int opt;
	size_t size;
	const char *path;

	progname = argv[0];

	while ((opt = getopt(argc, argv, "vh")) >= 0) {
		switch (opt) {
		case 'v':
		case 'V':
			print_version();
			return 0;
		case 'h':
		case 'H':
			print_usage(stdout);
			return 0;
		default:
			print_usage(stderr);
			return 2;
		}
	}

	if (optind + 2 > argc) {
		print_usage(stderr);
		return 2;
	}

	path = argv[optind];

	size = parse_size(argv[optind + 1]);

	if (pmemfile_mkfs(path, size, S_IWUSR | S_IRUSR) == NULL) {
		perror("pmemfile_mkfs ");
		return 1;
	}

	return 0;
}
