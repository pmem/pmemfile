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
#include <list>
#include <thread>

#include "pmemfile_test.hpp"

static int ops = 20;

static PMEMfilepool *global_pfp;

class mt : public pmemfile_test {
public:
	unsigned ncpus;
	std::list<std::thread *> threads;

	mt() : pmemfile_test(256 << 20)
	{
		ncpus = std::thread::hardware_concurrency();
	}

	void
	SetUp()
	{
		pmemfile_test::SetUp();
		global_pfp = pfp;
	}

	~mt()
	{
	}
};

static void
open_close_worker(const char *path)
{
	for (int i = 0; i < ops; ++i) {
		PMEMfile *f1 = pmemfile_open(global_pfp, path, 0);
		if (f1)
			pmemfile_close(global_pfp, f1);
		else {
			if (errno != ENOENT) {
				ADD_FAILURE() << errno;
				abort();
			}
		}
	}
}

static void
create_close_unlink_worker(const char *path)
{
	for (int i = 0; i < ops; ++i) {
		PMEMfile *f1 =
			pmemfile_open(global_pfp, path, PMEMFILE_O_CREAT, 0644);
		if (f1)
			pmemfile_close(global_pfp, f1);
		else {
			if (errno != ENOENT && errno != EEXIST) {
				ADD_FAILURE() << errno;
				abort();
			}
		}
		pmemfile_unlink(global_pfp, path);
	}
}

TEST_F(mt, open_close_create_unlink)
{
	for (unsigned j = 0; j < ncpus / 2; ++j) {
		threads.push_back(new std::thread(open_close_worker, "/aaa"));
		threads.push_back(
			new std::thread(create_close_unlink_worker, "/aaa"));
	}

	for (unsigned j = 0; j < ncpus / 2; ++j) {
		threads.push_back(new std::thread(open_close_worker, "/bbb"));
		threads.push_back(
			new std::thread(create_close_unlink_worker, "/bbb"));
	}

	while (!threads.empty()) {
		std::thread *t = threads.front();
		t->join();
		delete t;
		threads.pop_front();
	}
}

static void
pread_worker(PMEMfile *file)
{
	char buf[1024];
	char bufpat[1024];

	for (int i = 0; i < ops * 100; ++i) {
		pmemfile_off_t block = rand() % 128;
		pmemfile_off_t off = block << 10;
		memset(buf, 0, sizeof(buf));
		pmemfile_ssize_t ret =
			pmemfile_pread(global_pfp, file, buf, sizeof(buf), off);
		if (ret < 0)
			abort();
		if ((size_t)ret != sizeof(buf))
			abort();

		char pat = (char)(block % 256);
		memset(bufpat, pat, sizeof(bufpat));
		if (memcmp(buf, bufpat, sizeof(buf)) != 0)
			abort();
	}
}

TEST_F(mt, pread)
{
	PMEMfile *file =
		pmemfile_open(pfp, "/file1", PMEMFILE_O_CREAT | PMEMFILE_O_RDWR,
			      PMEMFILE_S_IRWXU);
	ASSERT_NE(file, nullptr);

	char buf[1024];
	for (int i = 0; i < 128; ++i) {
		memset(buf, i % 256, sizeof(buf));
		ASSERT_EQ(pmemfile_write(pfp, file, buf, sizeof(buf)),
			  (pmemfile_ssize_t)sizeof(buf))
			<< strerror(errno);
	}
	ASSERT_EQ(pmemfile_lseek(pfp, file, 0, PMEMFILE_SEEK_CUR), 128 << 10);
	ASSERT_EQ(pmemfile_lseek(pfp, file, 0, PMEMFILE_SEEK_SET), 0);

	for (unsigned j = 0; j < ncpus; ++j)
		threads.push_back(new std::thread(pread_worker, file));

	while (!threads.empty()) {
		std::thread *t = threads.front();
		t->join();
		delete t;
		threads.pop_front();
	}

	pmemfile_close(pfp, file);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
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
