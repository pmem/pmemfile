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
 * mt.cpp -- multithreaded test for pmemfile_*
 */
#include <sched.h>
#include <unistd.h>

#include "pmemfile_test.hpp"

class mt : public pmemfile_test {
public:
	mt() : pmemfile_test()
	{
	}
};

static int ops = 20;

static PMEMfilepool *global_pfp;

static void *
open_close_worker(void *arg)
{
	const char *path = (const char *)arg;
	sched_yield();

	for (int i = 0; i < ops; ++i) {
		PMEMfile *f1 = pmemfile_open(global_pfp, path, 0);
		if (f1)
			pmemfile_close(global_pfp, f1);
	}

	return NULL;
}

static void *
create_close_unlink_worker(void *arg)
{
	const char *path = (const char *)arg;
	sched_yield();

	for (int i = 0; i < ops; ++i) {
		PMEMfile *f1 =
			pmemfile_open(global_pfp, path, PMEMFILE_O_CREAT, 0644);
		if (f1)
			pmemfile_close(global_pfp, f1);
		pmemfile_unlink(global_pfp, path);
	}

	return NULL;
}

TEST_F(mt, 0)
{
	long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	int i = 0;
	pthread_t *threads =
		(pthread_t *)malloc(sizeof(threads[0]) * (size_t)ncpus * 2);
	ASSERT_NE(threads, nullptr);
	int ret;
	global_pfp = pfp;

	for (int j = 0; j < ncpus / 2; ++j) {
		ret = pthread_create(&threads[i++], NULL, open_close_worker,
				     (void *)"/aaa");
		ASSERT_EQ(ret, 0) << strerror(errno);
		ret = pthread_create(&threads[i++], NULL,
				     create_close_unlink_worker,
				     (void *)"/aaa");
		ASSERT_EQ(ret, 0) << strerror(errno);
	}

	for (int j = 0; j < ncpus / 2; ++j) {
		ret = pthread_create(&threads[i++], NULL, open_close_worker,
				     (void *)"/bbb");
		ASSERT_EQ(ret, 0) << strerror(errno);
		ret = pthread_create(&threads[i++], NULL,
				     create_close_unlink_worker,
				     (void *)"/bbb");
		ASSERT_EQ(ret, 0) << strerror(errno);
	}

	while (i > 0) {
		ret = pthread_join(threads[--i], NULL);
		ASSERT_EQ(ret, 0) << strerror(errno);
	}

	free(threads);
}

int
main(int argc, char *argv[])
{
	START();

	if (argc < 2) {
		fprintf(stderr, "usage: %s global_path [ops]", argv[0]);
		exit(1);
	}

	global_path = argv[1];

	if (argc >= 3)
		ops = atoi(argv[2]);

	T_OUT("ops %d", ops);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
