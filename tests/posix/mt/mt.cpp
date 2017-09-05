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
#include <cstdlib>
#include <list>
#include <thread>

#include "pmemfile_test.hpp"

static int ops = 20;

static PMEMfilepool *global_pfp;

class mt : public pmemfile_test {
public:
	unsigned ncpus;
	std::list<std::thread> threads;

	mt() : pmemfile_test(256 << 20)
	{
		ncpus = std::thread::hardware_concurrency();

		/*
		 * Pmemobj requires some pmem space for each thread, but it's
		 * not possible to get the information how much exactly.
		 * As there's not much point in testing more than a couple
		 * of threads we can limit the number of threads to some
		 * arbitrary (but big enough to entertain most cases) number.
		 */
		if (ncpus > 16)
			ncpus = 16;
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
	unsigned n = ncpus / 2;
	if (n == 0) /* when ncpus == 1 */
		n = 1;
	n++; /* add some randomness */

	for (unsigned j = 0; j < n; ++j) {
		threads.emplace_back(open_close_worker, "/aaa");
		threads.emplace_back(create_close_unlink_worker, "/aaa");
	}

	for (unsigned j = 0; j < n; ++j) {
		threads.emplace_back(open_close_worker, "/bbb");
		threads.emplace_back(create_close_unlink_worker, "/bbb");
	}

	for (auto &t : threads)
		t.join();
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

	unsigned randomness = 1;
	for (unsigned j = 0; j < ncpus + randomness; ++j)
		threads.emplace_back(pread_worker, file);

	for (auto &t : threads)
		t.join();

	pmemfile_close(pfp, file);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file1"), 0);
}

static void
test_rename(const char *path1, const char *path2)
{
	pmemfile_rename(global_pfp, path1, path2);
	pmemfile_rename(global_pfp, path2, path1);
}

static void
test_rename_loop(const char *path1, const char *path2)
{
	for (int i = 0; i < ops; ++i)
		test_rename(path1, path2);
}

static void
test_exchange(const char *path1, const char *path2)
{
	pmemfile_renameat2(global_pfp, NULL, path1, NULL, path2,
			   PMEMFILE_RENAME_EXCHANGE);
	pmemfile_renameat2(global_pfp, NULL, path2, NULL, path1,
			   PMEMFILE_RENAME_EXCHANGE);
}

static void
test_exchange_loop(const char *path1, const char *path2)
{
	for (int i = 0; i < ops; ++i)
		test_exchange(path1, path2);
}

/* same-directory file renames */
static void
rename_worker1(void)
{
	test_rename_loop("/dir1/file1", "/dir1/file11");
}

/*
 * same-directory file renames (other file in the same directory as previous
 * one)
 */
static void
rename_worker2(void)
{
	test_rename_loop("/dir1/file2", "/dir1/file21");
}

/* cross-directory file renames */
static void
rename_worker3(void)
{
	test_rename_loop("/dir2/file1", "/dir3/file11");
}

/*
 * cross-directory file renames (other file in the same directory as previous
 * one)
 */
static void
rename_worker4(void)
{
	test_rename_loop("/dir2/file2", "/dir3/file22");
}

/* cross-directory directory renames */
static void
rename_worker5(void)
{
	test_rename_loop("/dir4/dir1", "/dir4/dir2/dir6");
}

/* cross-directory directory-file exchange */
static void
rename_worker6(void)
{
	test_exchange_loop("/dir4/dir3", "/dir4/dir2/file4");
}

TEST_F(mt, rename)
{
	test_empty_dir_on_teardown = false;
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir1", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir2", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir3", 0755), 0);

	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir4", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir4/dir1", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir4/dir2", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir4/dir3", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir4/dir1/dir5", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir4/dir2/dir6", 0755), 0);
	ASSERT_EQ(pmemfile_mkdir(pfp, "/dir4/dir3/dir7", 0755), 0);

	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir4/file1"));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir4/dir1/file2"));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir4/dir1/dir5/file3"));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir4/dir2/file4"));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir4/dir2/dir6/file5"));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir4/dir3/file6"));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir4/dir3/dir7/file8"));

	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir1/file1"));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir1/file2"));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir2/file1"));
	ASSERT_TRUE(test_pmemfile_create(pfp, "/dir2/file2"));

	for (int i = 0; i < 2; ++i) {
		threads.emplace_back(rename_worker1);
		threads.emplace_back(rename_worker2);
		threads.emplace_back(rename_worker3);
		threads.emplace_back(rename_worker4);
		threads.emplace_back(rename_worker5);
		threads.emplace_back(rename_worker6);
	}

	for (auto &t : threads)
		t.join();
}

static void
rename_helper(const std::string &p1, const std::string &p2)
{
	if (pmemfile_rename(global_pfp, p1.c_str(), p2.c_str()) == 0)
		pmemfile_rename(global_pfp, p2.c_str(), p1.c_str());
}

static std::vector<std::string>
get_dirs(void)
{
	std::vector<std::string> dirs;
	dirs.emplace_back("/A");
	dirs.emplace_back("/A/B");
	dirs.emplace_back("/A/B/C");
	dirs.emplace_back("/A/B/C/D");
	dirs.emplace_back("/A/B/C/D/E");
	dirs.emplace_back("/A/B/C/D/E/F");
	dirs.emplace_back("/A/B/C/D/E/F/G");
	dirs.emplace_back("/A/B/C/D/E/F/G/H");
	dirs.emplace_back("/1");
	dirs.emplace_back("/1/2");
	dirs.emplace_back("/1/2/3");
	dirs.emplace_back("/1/2/3/4");
	dirs.emplace_back("/1/2/3/4/5");
	dirs.emplace_back("/1/2/3/4/5/6");
	dirs.emplace_back("/1/2/3/4/5/6/7");
	dirs.emplace_back("/1/2/3/4/5/6/7/8");

	return dirs;
}

const std::string &
rand_path(const std::vector<std::string> &dirs)
{
	return dirs[(size_t)std::rand() % dirs.size()];
}

TEST_F(mt, rename_random_paths)
{
	test_empty_dir_on_teardown = false;
	std::vector<std::string> dirs = get_dirs();
	for (auto p : dirs)
		ASSERT_EQ(pmemfile_mkdir(pfp, p.c_str(), 0755), 0);

	for (int i = 0; i < ops; ++i) {
		threads.emplace_back(rename_helper, rand_path(dirs),
				     rand_path(dirs));
		threads.emplace_back(rename_helper, rand_path(dirs),
				     rand_path(dirs));
		threads.emplace_back(rename_helper, rand_path(dirs),
				     rand_path(dirs));

		for (auto &t : threads)
			t.join();
		threads.clear();
	}
}

static void
exchange_helper(const std::string &p1, const std::string &p2)
{
	if (pmemfile_renameat2(global_pfp, NULL, p1.c_str(), NULL, p2.c_str(),
			       PMEMFILE_RENAME_EXCHANGE))
		pmemfile_renameat2(global_pfp, NULL, p2.c_str(), NULL,
				   p1.c_str(), PMEMFILE_RENAME_EXCHANGE);
}

TEST_F(mt, exchange_random_paths)
{
	test_empty_dir_on_teardown = false;
	std::vector<std::string> dirs = get_dirs();
	for (auto p : dirs)
		ASSERT_EQ(pmemfile_mkdir(pfp, p.c_str(), 0755), 0);

	for (int i = 0; i < ops; ++i) {
		threads.emplace_back(exchange_helper, rand_path(dirs),
				     rand_path(dirs));
		threads.emplace_back(exchange_helper, rand_path(dirs),
				     rand_path(dirs));
		threads.emplace_back(exchange_helper, rand_path(dirs),
				     rand_path(dirs));

		for (auto &t : threads)
			t.join();
		threads.clear();
	}
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

	T_OUT("ops %d\n", ops);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
