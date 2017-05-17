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

#ifndef PMEMFILE_TEST_HPP
#define PMEMFILE_TEST_HPP

#include <errno.h>
#include <map>
#include <string>
#include <vector>

#include "../test_backtrace.h"
#include "libpmemfile-posix.h"
#include "gtest/gtest.h"

#define START()                                                                \
	do {                                                                   \
		test_register_sighandlers();                                   \
	} while (0)
#define T_OUT(...) fprintf(stderr, __VA_ARGS__)
#define COND_ERROR(ret) (ret < 0 ? strerror(errno) : "")

/*
 * is_zeroed -- check if given memory range is all zero
 */
static inline int
is_zeroed(const void *addr, size_t len)
{
	/* XXX optimize */
	const char *a = (const char *)addr;
	while (len-- > 0)
		if (*a++)
			return 0;
	return 1;
}

/* XXX move these functions to pmemfile_test class and get rid of pfp arg */

/* pmemfile stuff */
bool test_pmemfile_create(PMEMfilepool *pfp, const char *path, int flags = 0,
			  pmemfile_mode_t mode = 0777);
/* utilities */

class pmemfile_ls {
public:
	pmemfile_mode_t mode;
	pmemfile_nlink_t nlink;
	pmemfile_off_t size;
	const char *name;
	const char *link;

	pmemfile_uid_t uid;
	pmemfile_gid_t gid;
};

bool test_pmemfile_stats_match(PMEMfilepool *pfp, unsigned inodes,
			       unsigned dirs, unsigned block_arrays,
			       unsigned blocks);
pmemfile_ssize_t test_pmemfile_file_size(PMEMfilepool *pfp, PMEMfile *file);
pmemfile_ssize_t test_pmemfile_path_size(PMEMfilepool *pfp, const char *path);

class file_attrs {
public:
	pmemfile_stat_t stat;
	std::string link;

	file_attrs(const pmemfile_stat_t &stat, const char *link = nullptr)
	    : stat(stat), link(link)
	{
	}
};

static inline std::ostream &
operator<<(std::ostream &stream, const file_attrs &attrs)
{
	stream << " mode " << std::hex << "0x" << attrs.stat.st_mode << std::dec
	       << " nlink " << attrs.stat.st_nlink << " size "
	       << attrs.stat.st_size << " uid " << attrs.stat.st_uid << " gid "
	       << attrs.stat.st_gid << " link " << attrs.link;

	return stream;
}

static inline std::ostream &
operator<<(std::ostream &stream, const std::map<std::string, file_attrs> &files)
{
	for (auto it = files.cbegin(); it != files.cend(); ++it)
		stream << "name " << it->first << it->second << '\n';

	return stream;
}

std::map<std::string, file_attrs> test_list_files(PMEMfilepool *pfp,
						  PMEMfile *dir,
						  const char *dirp,
						  unsigned length);

std::map<std::string, file_attrs> test_list_files(PMEMfilepool *pfp,
						  const char *path);

bool test_compare_dirs(const std::map<std::string, file_attrs> &files,
		       const std::vector<pmemfile_ls> &expected,
		       bool check_attrs = false);
bool test_compare_dirs(PMEMfilepool *pfp, const char *path,
		       const std::vector<pmemfile_ls> &expected,
		       bool check_attrs = false);
bool test_empty_dir(PMEMfilepool *pfp, const char *path);

extern std::string global_path;

class pmemfile_test : public testing::Test {
protected:
	std::string path;
	PMEMfilepool *pfp;
	size_t poolsize;
	bool test_empty_dir_on_teardown;

public:
	pmemfile_test(size_t poolsize = 8 * 1024 * 1024)
	    : path(global_path + "/poolfile"),
	      pfp(NULL),
	      poolsize(poolsize),
	      test_empty_dir_on_teardown(true)
	{
	}

	void
	SetUp()
	{
		std::remove(path.c_str());

		pfp = pmemfile_mkfs(path.c_str(), poolsize,
				    PMEMFILE_S_IWUSR | PMEMFILE_S_IRUSR);
		EXPECT_NE(pfp, nullptr) << strerror(errno);
		/*
		 * Lower-case asserts are here on purpose. ASSERTs return
		 * internally - they stop this function, but don't stop
		 * test execution, so gtest happily performs a test on not
		 * fully set up environment.
		 */
		assert(pfp != NULL);

		assert(test_empty_dir(pfp, "/"));

		assert(test_pmemfile_stats_match(pfp, 1, 0, 0, 0));
	}

	void
	TearDown()
	{
		// XXX always enable
		if (test_empty_dir_on_teardown)
			/* Again. Lower-case assert on purpose. */
			assert(test_empty_dir(pfp, "/"));

		pmemfile_pool_close(pfp);
		std::remove(path.c_str());
	}
};

#endif
