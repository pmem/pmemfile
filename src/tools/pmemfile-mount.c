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
 * pmemfile-mount.c -- pmemfile mount command source file
 */

#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>

static const char *progname;

static void
print_version(void)
{
	puts("pmemfile-mount v1");
}

static void
print_usage(FILE *stream)
{
	fprintf(stream,
	    "Usage: %s [-v] [-h] pool-path mount-point\n"
	    "Options:\n"
	    "  -v      print version\n"
	    "  -h      print this help text\n",
	    progname);
}


int
main(int argc, char *argv[])
{
	int opt;
	const char *pool_path;
	const char *mount_point;

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

	pool_path = argv[optind];
	mount_point = argv[optind + 1];

	char mount_source[PATH_MAX];
	sprintf(mount_source, "pmemfile:%s", pool_path);

	if (mount(mount_source, mount_point, "tmpfs",
			MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME,
			"size=4k")) {
		perror("mount");
		return 1;
	}

	return 0;
}
