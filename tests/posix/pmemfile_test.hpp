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

#include "../../src/libpmemfile-posix/fault_injection.h"
#include "../test_backtrace.h"
#include "libpmemfile-posix.h"
#include "gtest/gtest.h"

#define START()                                                                \
	do {                                                                   \
		test_register_sighandlers();                                   \
	} while (0)
#define T_OUT(...) fprintf(stderr, __VA_ARGS__)
#define COND_ERROR(ret) (ret < 0 ? strerror(errno) : "")

extern bool is_pmemfile_pop;

// enum pf_allocation_type { PF_MALLOC, PF_CALLOC, PF_REALLOC };

/*
 * A bad file pointer, for test cases where libpmemfile-posix is expected to
 * ignore a file pointer.
 *
 * Note: it can't be a constexpr, that would result in:
 * "error: reinterpret_cast from integer to pointer"
 * Thanks, C++.
 *
 * Note: it can't be a simple variable either, that would result in:
 * "error: ‘badf’ defined but not used"
 */
#define BADF ((PMEMfile *)(uintptr_t)0xbad)

bool is_zeroed(const void *addr, size_t len);

/* XXX move these functions to pmemfile_test class and get rid of pfp arg */

/* pmemfile stuff */
bool test_pmemfile_create(PMEMfilepool *pfp, const char *path, int flags = 0,
			  pmemfile_mode_t mode = 0777);
/* utilities */

struct pmemfile_ls {
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

struct file_attrs {
public:
	pmemfile_stat_t stat;
	std::string link;

	file_attrs(const pmemfile_stat_t &stat, const char *link = nullptr)
	    : stat(stat), link(link)
	{
	}
};

std::ostream &operator<<(std::ostream &stream, const file_attrs &attrs);

std::ostream &operator<<(std::ostream &stream,
			 std::map<std::string, file_attrs> &files);

std::map<std::string, file_attrs> test_list_files(PMEMfilepool *pfp,
						  PMEMfile *dir,
						  const char *dirp,
						  unsigned length);

std::map<std::string, file_attrs> test_list_files(PMEMfilepool *pfp,
						  const char *path);

bool test_compare_dirs(const std::map<std::string, file_attrs> &files,
		       const std::vector<pmemfile_ls> &expected,
		       bool check_attrs = false, bool check_dir_size = true);
bool test_compare_dirs(PMEMfilepool *pfp, const char *path,
		       const std::vector<pmemfile_ls> &expected,
		       bool check_attrs = false, bool check_dir_size = true);
bool test_empty_dir(PMEMfilepool *pfp, const char *path);

extern std::string global_path;

class pmemfile_test : public testing::Test {
protected:
	std::string path;
	PMEMfilepool *pfp;
	size_t poolsize;
	bool test_empty_dir_on_teardown;

public:
	pmemfile_test(size_t poolsize = 16 * 1024 * 1024);
	void SetUp() override;
	void TearDown() override;
};

/* Tests expect a static count of 4 root directories */
static constexpr unsigned
root_count()
{
	return 4;
}

#endif
