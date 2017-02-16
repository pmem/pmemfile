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
 * file_core_mt.c -- multithreaded test for pmemfile_*
 */

#include "unittest.h"

static int ops = 20;

static PMEMfilepool *
create_pool(const char *path)
{
	PMEMfilepool *pfp = pmemfile_mkfs(path, PMEMOBJ_MIN_POOL,
			S_IWUSR | S_IRUSR);
	if (!pfp)
		UT_FATAL("!pmemfile_mkfs: %s", path);
	return pfp;
}

static PMEMfilepool *pfp;

static void *
open_close_worker(void *arg)
{
	const char *path = arg;
	sched_yield();

	for (int i = 0; i < ops; ++i) {
		PMEMfile *f1 = pmemfile_open(pfp, path, 0);
		if (f1)
			pmemfile_close(pfp, f1);
	}

	return NULL;
}

static void *
create_close_unlink_worker(void *arg)
{
	const char *path = arg;
	sched_yield();

	for (int i = 0; i < ops; ++i) {
		PMEMfile *f1 = pmemfile_open(pfp, path, O_CREAT, 0644);
		if (f1)
			pmemfile_close(pfp, f1);
		pmemfile_unlink(pfp, path);
	}

	return NULL;
}

int
main(int argc, char *argv[])
{
	if (argc < 2)
		UT_FATAL("usage: %s file-name [ops]", argv[0]);

	const char *path = argv[1];

	if (argc >= 3)
		ops = atoi(argv[2]);
	UT_OUT("ops %d", ops);

	long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	int i = 0;
	pthread_t *threads = MALLOC(sizeof(threads[0]) * ncpus * 2);
	pfp = create_pool(path);

	for (int j = 0; j < ncpus / 2; ++j) {
		PTHREAD_CREATE(&threads[i++], NULL,
				open_close_worker, "/aaa");
		PTHREAD_CREATE(&threads[i++], NULL,
				create_close_unlink_worker, "/aaa");
	}

	for (int j = 0; j < ncpus / 2; ++j) {
		PTHREAD_CREATE(&threads[i++], NULL,
				open_close_worker, "/bbb");
		PTHREAD_CREATE(&threads[i++], NULL,
				create_close_unlink_worker, "/bbb");
	}

	while (i > 0)
		PTHREAD_JOIN(threads[--i], NULL);

	FREE(threads);

	pmemfile_pool_close(pfp);
}
