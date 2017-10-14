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
 * timestamps.cpp -- unit test for pmemfile_*utime*
 */
#include "pmemfile_test.hpp"

class timestamps : public pmemfile_test {
public:
	timestamps() : pmemfile_test()
	{
	}
};

/*
 * ext4 seems to use kernel's timer to get current time, used in utime and other
 * timestamp related functions. This is not always accurate, as timer frequency
 * is 250Hz by default, which means timer is updated every 4 ms.
 * Default config values include: 100Hz, 250Hz, 500Hz, 1000Hz.
 * In worst case timer would be updated every 10 ms.
 * This function will wait 11 ms to ensure, that new timestamp is different than
 * previous one.
 */
static inline void
pmemfile_pop_sleep()
{
	if (is_pmemfile_pop)
		usleep(11000);
}

TEST_F(timestamps, utime)
{
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file"));

	pmemfile_stat_t st;
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st), 0);

	pmemfile_utimbuf_t tm;
	tm.actime = 12345;
	tm.modtime = 56789;
	ASSERT_EQ(pmemfile_utime(pfp, "/file", &tm), 0);

	pmemfile_stat_t st2;
	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_EQ(st2.st_atim.tv_sec, tm.actime);
	ASSERT_EQ(st2.st_atim.tv_nsec, 0);

	ASSERT_EQ(st2.st_mtim.tv_sec, tm.modtime);
	ASSERT_EQ(st2.st_mtim.tv_nsec, 0);

	pmemfile_pop_sleep();

	ASSERT_EQ(pmemfile_utime(pfp, "/file", NULL), 0);

	if (_pmemfile_fault_injection_enabled()) {
		_pmemfile_inject_fault_at(PF_GET_CURRENT_TIME, 1,
					  "vinode_file_time_set");

		errno = 0;
		ASSERT_EQ(pmemfile_utime(pfp, "/file", NULL), -1);
		EXPECT_EQ(errno, EINVAL);
	}

	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_GE(st2.st_atim.tv_sec, st.st_atim.tv_sec);
	ASSERT_GE(st2.st_atim.tv_nsec, 0);
	ASSERT_LT(st2.st_atim.tv_nsec, 1000000000);
	if (st2.st_atim.tv_sec == st.st_atim.tv_sec)
		ASSERT_GT(st2.st_atim.tv_nsec, st.st_atim.tv_nsec);

	ASSERT_GE(st2.st_mtim.tv_sec, st.st_mtim.tv_sec);
	ASSERT_GE(st2.st_mtim.tv_nsec, 0);
	ASSERT_LT(st2.st_mtim.tv_nsec, 1000000000);
	if (st2.st_mtim.tv_sec == st.st_mtim.tv_sec)
		ASSERT_GT(st2.st_mtim.tv_nsec, st.st_mtim.tv_nsec);

	tm.actime = std::numeric_limits<decltype(tm.actime)>::max();
	tm.modtime = std::numeric_limits<decltype(tm.modtime)>::max();
	ASSERT_EQ(pmemfile_utime(pfp, "/file", &tm), 0);

	tm.actime = -123;
	tm.modtime = -456;
	ASSERT_EQ(pmemfile_utime(pfp, "/file", &tm), 0);

	if (_pmemfile_fault_injection_enabled()) {
		pmemfile_gid_t groups[1] = {1002};
		ASSERT_EQ(pmemfile_setgroups(pfp, 1, groups), 0);
		_pmemfile_inject_fault_at(PF_MALLOC, 1, "copy_cred");
		errno = 0;
		ASSERT_EQ(pmemfile_utime(pfp, "/file", &tm), -1);
		EXPECT_EQ(errno, ENOMEM);
	}

	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_EQ(st2.st_atim.tv_sec, tm.actime);
	ASSERT_GE(st2.st_atim.tv_nsec, 0);
	ASSERT_LT(st2.st_atim.tv_nsec, 1000000000);

	ASSERT_EQ(st2.st_mtim.tv_sec, tm.modtime);
	ASSERT_GE(st2.st_atim.tv_nsec, 0);
	ASSERT_LT(st2.st_atim.tv_nsec, 1000000000);

	errno = 0;
	ASSERT_EQ(pmemfile_utime(pfp, NULL, NULL), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_utime(NULL, "/file", NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_utime(pfp, "/fileXXX", NULL), -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
}

TEST_F(timestamps, utimes)
{
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file"));

	pmemfile_stat_t st;
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st), 0);

	pmemfile_timeval_t tm[2] = {{12345, 999999}, {56789, 999999}};
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), 0);

	pmemfile_stat_t st2;
	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_EQ(st2.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_EQ(st2.st_atim.tv_nsec, tm[0].tv_usec * 1000);

	ASSERT_EQ(st2.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_EQ(st2.st_mtim.tv_nsec, tm[1].tv_usec * 1000);

	pmemfile_pop_sleep();

	ASSERT_EQ(pmemfile_utimes(pfp, "/file", NULL), 0);

	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_GE(st2.st_atim.tv_sec, st.st_atim.tv_sec);
	ASSERT_GE(st2.st_atim.tv_nsec, 0);
	ASSERT_LT(st2.st_atim.tv_nsec, 1000000000);
	if (st2.st_atim.tv_sec == st.st_atim.tv_sec)
		ASSERT_GT(st2.st_atim.tv_nsec, st.st_atim.tv_nsec);

	ASSERT_GE(st2.st_mtim.tv_sec, st.st_mtim.tv_sec);
	ASSERT_GE(st2.st_mtim.tv_nsec, 0);
	ASSERT_LT(st2.st_mtim.tv_nsec, 1000000000);
	if (st2.st_mtim.tv_sec == st.st_mtim.tv_sec)
		ASSERT_GT(st2.st_mtim.tv_nsec, st.st_mtim.tv_nsec);

	tm[0] = {std::numeric_limits<decltype(tm[0].tv_sec)>::max(), 1};
	tm[1] = {std::numeric_limits<decltype(tm[1].tv_sec)>::max(), 1};
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), 0);

	tm[0] = {-12, 1};
	tm[1] = {-34, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), 0);

	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_GE(st2.st_atim.tv_sec, -12);
	ASSERT_GE(st2.st_atim.tv_nsec, 0);
	ASSERT_LT(st2.st_atim.tv_nsec, 1000000000);

	ASSERT_GE(st2.st_mtim.tv_sec, -34);
	ASSERT_GE(st2.st_mtim.tv_nsec, 0);
	ASSERT_LT(st2.st_mtim.tv_nsec, 1000000000);

	tm[0] = {1, -1};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1000000};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, std::numeric_limits<decltype(tm[0].tv_usec)>::max()};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, -1};
	errno = 0;
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, 1000000};
	errno = 0;
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, std::numeric_limits<decltype(tm[1].tv_usec)>::max()};
	errno = 0;
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), -1);
	EXPECT_EQ(errno, EINVAL);

	errno = 0;
	ASSERT_EQ(pmemfile_utimes(pfp, NULL, NULL), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_utimes(NULL, "/file", NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_utimes(pfp, "/fileXXX", NULL), -1);
	EXPECT_EQ(errno, ENOENT);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
}

TEST_F(timestamps, futimes)
{
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file"));
	PMEMfile *f = pmemfile_open(pfp, "/file", PMEMFILE_O_WRONLY);
	ASSERT_NE(f, nullptr);

	pmemfile_stat_t st;
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st), 0);

	pmemfile_timeval_t tm[2] = {{12345, 999999}, {56789, 999999}};
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), 0);

	pmemfile_stat_t st2;
	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_EQ(st2.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_EQ(st2.st_atim.tv_nsec, tm[0].tv_usec * 1000);

	ASSERT_EQ(st2.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_EQ(st2.st_mtim.tv_nsec, tm[1].tv_usec * 1000);

	pmemfile_pop_sleep();

	ASSERT_EQ(pmemfile_futimes(pfp, f, NULL), 0);

	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_GE(st2.st_atim.tv_sec, st.st_atim.tv_sec);
	ASSERT_GE(st2.st_atim.tv_nsec, 0);
	ASSERT_LT(st2.st_atim.tv_nsec, 1000000000);
	if (st2.st_atim.tv_sec == st.st_atim.tv_sec)
		ASSERT_GT(st2.st_atim.tv_nsec, st.st_atim.tv_nsec);

	ASSERT_GE(st2.st_mtim.tv_sec, st.st_mtim.tv_sec);
	ASSERT_GE(st2.st_mtim.tv_nsec, 0);
	ASSERT_LT(st2.st_mtim.tv_nsec, 1000000000);
	if (st2.st_mtim.tv_sec == st.st_mtim.tv_sec)
		ASSERT_GT(st2.st_mtim.tv_nsec, st.st_mtim.tv_nsec);

	tm[0] = {std::numeric_limits<decltype(tm[0].tv_sec)>::max(), 1};
	tm[1] = {std::numeric_limits<decltype(tm[1].tv_sec)>::max(), 1};
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), 0);

	tm[0] = {-12, 1};
	tm[1] = {-34, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), 0);

	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_GE(st2.st_atim.tv_sec, -12);
	ASSERT_GE(st2.st_atim.tv_nsec, 0);
	ASSERT_LT(st2.st_atim.tv_nsec, 1000000000);

	ASSERT_GE(st2.st_mtim.tv_sec, -34);
	ASSERT_GE(st2.st_mtim.tv_nsec, 0);
	ASSERT_LT(st2.st_mtim.tv_nsec, 1000000000);

	tm[0] = {1, -1};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1000000};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, std::numeric_limits<decltype(tm[0].tv_usec)>::max()};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, -1};
	errno = 0;
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, 1000000};
	errno = 0;
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, std::numeric_limits<decltype(tm[1].tv_usec)>::max()};
	errno = 0;
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	errno = 0;
	ASSERT_EQ(pmemfile_futimes(pfp, NULL, NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_futimes(NULL, f, NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr);

	tm[0] = {1, 2};
	tm[1] = {3, 4};
	errno = 0;
	ASSERT_EQ(pmemfile_futimes(pfp, f, tm), 0);
	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_EQ(st2.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_EQ(st2.st_atim.tv_nsec, tm[0].tv_usec * 1000);
	ASSERT_EQ(st2.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_EQ(st2.st_mtim.tv_nsec, tm[1].tv_usec * 1000);

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
}

TEST_F(timestamps, futimens)
{
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file"));
	PMEMfile *f = pmemfile_open(pfp, "/file", PMEMFILE_O_WRONLY);
	ASSERT_NE(f, nullptr);

	pmemfile_stat_t st;
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st), 0);

	pmemfile_timespec_t tm[2] = {{12345, 999999999}, {56789, 999999999}};
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), 0);

	pmemfile_stat_t st2;
	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_EQ(st2.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_EQ(st2.st_atim.tv_nsec, tm[0].tv_nsec);

	ASSERT_EQ(st2.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_EQ(st2.st_mtim.tv_nsec, tm[1].tv_nsec);

	pmemfile_pop_sleep();

	ASSERT_EQ(pmemfile_futimens(pfp, f, NULL), 0);

	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_GE(st2.st_atim.tv_sec, st.st_atim.tv_sec);
	ASSERT_GE(st2.st_atim.tv_nsec, 0);
	ASSERT_LT(st2.st_atim.tv_nsec, 1000000000);
	if (st2.st_atim.tv_sec == st.st_atim.tv_sec)
		ASSERT_GT(st2.st_atim.tv_nsec, st.st_atim.tv_nsec);

	ASSERT_GE(st2.st_mtim.tv_sec, st.st_mtim.tv_sec);
	ASSERT_GE(st2.st_mtim.tv_nsec, 0);
	ASSERT_LT(st2.st_mtim.tv_nsec, 1000000000);
	if (st2.st_mtim.tv_sec == st.st_mtim.tv_sec)
		ASSERT_GT(st2.st_mtim.tv_nsec, st.st_mtim.tv_nsec);

	tm[0] = {std::numeric_limits<decltype(tm[0].tv_sec)>::max(), 1};
	tm[1] = {std::numeric_limits<decltype(tm[1].tv_sec)>::max(), 1};
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), 0);

	tm[0] = {-12, 1};
	tm[1] = {-34, 1};
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), 0);

	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_GE(st2.st_atim.tv_sec, -12);
	ASSERT_GE(st2.st_atim.tv_nsec, 0);
	ASSERT_LT(st2.st_atim.tv_nsec, 1000000000);

	ASSERT_GE(st2.st_mtim.tv_sec, -34);
	ASSERT_GE(st2.st_mtim.tv_nsec, 0);
	ASSERT_LT(st2.st_mtim.tv_nsec, 1000000000);

	tm[0] = {1, -1};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1000000000};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, std::numeric_limits<decltype(tm[0].tv_nsec)>::max()};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, -1};
	errno = 0;
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, 1000000000};
	errno = 0;
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, std::numeric_limits<decltype(tm[1].tv_nsec)>::max()};
	errno = 0;
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), -1);
	EXPECT_EQ(errno, EINVAL);

	errno = 0;
	ASSERT_EQ(pmemfile_futimens(pfp, NULL, NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_futimens(NULL, f, NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	pmemfile_close(pfp, f);

	f = pmemfile_open(pfp, "/file", PMEMFILE_O_RDONLY);
	ASSERT_NE(f, nullptr);

	tm[0] = {1, 2};
	tm[1] = {3, 4};
	errno = 0;
	ASSERT_EQ(pmemfile_futimens(pfp, f, tm), 0);
	memset(&st2, 0, sizeof(st2));
	ASSERT_EQ(pmemfile_stat(pfp, "/file", &st2), 0);

	ASSERT_EQ(st2.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_EQ(st2.st_atim.tv_nsec, tm[0].tv_nsec);
	ASSERT_EQ(st2.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_EQ(st2.st_mtim.tv_nsec, tm[1].tv_nsec);

	pmemfile_close(pfp, f);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
}

TEST_F(timestamps, lutimes)
{
	ASSERT_TRUE(test_pmemfile_create(pfp, "/file"));
	ASSERT_EQ(pmemfile_symlink(pfp, "/file", "/sym"), 0);

	pmemfile_timeval_t tm[2] = {{12345, 67890}, {56789, 4321}};
	ASSERT_EQ(pmemfile_utimes(pfp, "/file", tm), 0);

	pmemfile_timeval_t tm2[2] = {{99999, 66666}, {44444, 33333}};
	ASSERT_EQ(pmemfile_lutimes(pfp, "/sym", tm2), 0);

	pmemfile_stat_t st;
	memset(&st, 0, sizeof(st));
	ASSERT_EQ(pmemfile_lstat(pfp, "/file", &st), 0);
	EXPECT_EQ(st.st_atim.tv_sec, tm[0].tv_sec);
	EXPECT_EQ(st.st_atim.tv_nsec, tm[0].tv_usec * 1000);
	EXPECT_EQ(st.st_mtim.tv_sec, tm[1].tv_sec);
	EXPECT_EQ(st.st_mtim.tv_nsec, tm[1].tv_usec * 1000);

	memset(&st, 0, sizeof(st));
	ASSERT_EQ(pmemfile_lstat(pfp, "/sym", &st), 0);
	EXPECT_EQ(st.st_atim.tv_sec, tm2[0].tv_sec);
	EXPECT_EQ(st.st_atim.tv_nsec, tm2[0].tv_usec * 1000);
	EXPECT_EQ(st.st_mtim.tv_sec, tm2[1].tv_sec);
	EXPECT_EQ(st.st_mtim.tv_nsec, tm2[1].tv_usec * 1000);

	ASSERT_EQ(pmemfile_lutimes(pfp, "/sym", NULL), 0);

	memset(&st, 0, sizeof(st));
	ASSERT_EQ(pmemfile_lstat(pfp, "/file", &st), 0);
	EXPECT_EQ(st.st_atim.tv_sec, tm[0].tv_sec);
	EXPECT_EQ(st.st_atim.tv_nsec, tm[0].tv_usec * 1000);
	EXPECT_EQ(st.st_mtim.tv_sec, tm[1].tv_sec);
	EXPECT_EQ(st.st_mtim.tv_nsec, tm[1].tv_usec * 1000);

	memset(&st, 0, sizeof(st));
	ASSERT_EQ(pmemfile_lstat(pfp, "/sym", &st), 0);
	EXPECT_GT(st.st_atim.tv_sec, tm2[0].tv_sec);
	EXPECT_GT(st.st_mtim.tv_sec, tm2[1].tv_sec);

	ASSERT_EQ(pmemfile_unlink(pfp, "/file"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/sym"), 0);
}

TEST_F(timestamps, utimensat)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/d", 0755), 0);
	ASSERT_TRUE(test_pmemfile_create(pfp, "/d/file"));
	ASSERT_EQ(pmemfile_symlink(pfp, "file", "/d/sym"), 0);

	PMEMfile *d = pmemfile_open(pfp, "/d", 0);
	ASSERT_NE(d, nullptr);

	pmemfile_stat_t fst;
	ASSERT_EQ(pmemfile_stat(pfp, "/d/file", &fst), 0);

	pmemfile_stat_t dst;
	ASSERT_EQ(pmemfile_stat(pfp, "/d", &dst), 0);

	pmemfile_stat_t sst;
	ASSERT_EQ(pmemfile_stat(pfp, "/d/sym", &sst), 0);

	pmemfile_timespec_t tm[2] = {{12345, 999999}, {56789, 999999}};
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "sym", tm,
				     PMEMFILE_AT_SYMLINK_NOFOLLOW),
		  0);

	pmemfile_stat_t sst2;
	memset(&sst2, 0, sizeof(sst2));
	ASSERT_EQ(pmemfile_lstat(pfp, "/d/sym", &sst2), 0);

	ASSERT_EQ(sst2.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_EQ(sst2.st_atim.tv_nsec, tm[0].tv_nsec);

	ASSERT_EQ(sst2.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_EQ(sst2.st_mtim.tv_nsec, tm[1].tv_nsec);

	pmemfile_stat_t fst2;
	memset(&fst2, 0, sizeof(fst2));
	ASSERT_EQ(pmemfile_stat(pfp, "/d/sym", &fst2), 0);

	ASSERT_EQ(fst2.st_atim.tv_sec, fst.st_atim.tv_sec);
	ASSERT_EQ(fst2.st_atim.tv_nsec, fst.st_atim.tv_nsec);

	ASSERT_EQ(fst2.st_mtim.tv_sec, fst.st_mtim.tv_sec);
	ASSERT_EQ(fst2.st_mtim.tv_nsec, fst.st_mtim.tv_nsec);

	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), 0);

	memset(&fst2, 0, sizeof(fst2));
	ASSERT_EQ(pmemfile_stat(pfp, "/d/file", &fst2), 0);

	ASSERT_EQ(fst2.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_EQ(fst2.st_atim.tv_nsec, tm[0].tv_nsec);

	ASSERT_EQ(fst2.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_EQ(fst2.st_mtim.tv_nsec, tm[1].tv_nsec);

	pmemfile_pop_sleep();

	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", NULL, 0), 0);

	memset(&fst2, 0, sizeof(fst2));
	ASSERT_EQ(pmemfile_stat(pfp, "/d/file", &fst2), 0);

	ASSERT_GE(fst2.st_atim.tv_sec, fst.st_atim.tv_sec);
	ASSERT_GE(fst2.st_atim.tv_nsec, 0);
	ASSERT_LT(fst2.st_atim.tv_nsec, 1000000000);
	if (fst2.st_atim.tv_sec == fst.st_atim.tv_sec)
		ASSERT_GT(fst2.st_atim.tv_nsec, fst.st_atim.tv_nsec);

	ASSERT_GE(fst2.st_mtim.tv_sec, fst.st_mtim.tv_sec);
	ASSERT_GE(fst2.st_mtim.tv_nsec, 0);
	ASSERT_LT(fst2.st_mtim.tv_nsec, 1000000000);
	if (fst2.st_mtim.tv_sec == fst.st_mtim.tv_sec)
		ASSERT_GT(fst2.st_mtim.tv_nsec, fst.st_mtim.tv_nsec);

	pmemfile_pop_sleep();

	tm[0] = {7, PMEMFILE_UTIME_NOW};
	tm[1] = {9, PMEMFILE_UTIME_OMIT};

	if (_pmemfile_fault_injection_enabled()) {
		_pmemfile_inject_fault_at(PF_GET_CURRENT_TIME, 1,
					  "vinode_file_time_set");

		errno = 0;
		ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), -1);
		EXPECT_EQ(errno, EINVAL);
	}

	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), 0);

	pmemfile_stat_t fst3;
	memset(&fst3, 0, sizeof(fst3));
	ASSERT_EQ(pmemfile_stat(pfp, "/d/file", &fst3), 0);

	ASSERT_NE(fst3.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_NE(fst3.st_atim.tv_nsec, tm[0].tv_nsec);

	ASSERT_NE(fst3.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_NE(fst3.st_mtim.tv_nsec, tm[1].tv_nsec);

	ASSERT_NE(fst3.st_atim.tv_nsec, fst2.st_mtim.tv_nsec);

	ASSERT_EQ(fst3.st_mtim.tv_sec, fst2.st_mtim.tv_sec);
	ASSERT_EQ(fst3.st_mtim.tv_nsec, fst2.st_mtim.tv_nsec);

	tm[0] = {std::numeric_limits<decltype(tm[0].tv_sec)>::max(), 1};
	tm[1] = {std::numeric_limits<decltype(tm[0].tv_sec)>::max(), 1};
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), 0);

	tm[0] = {-12, 1};
	tm[1] = {-34, 1};
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), 0);

	memset(&sst2, 0, sizeof(sst2));
	ASSERT_EQ(pmemfile_stat(pfp, "/d/file", &sst2), 0);

	ASSERT_GE(sst2.st_atim.tv_sec, -12);
	ASSERT_GE(sst2.st_atim.tv_nsec, 0);
	ASSERT_LT(sst2.st_atim.tv_nsec, 1000000000);

	ASSERT_GE(sst2.st_mtim.tv_sec, -34);
	ASSERT_GE(sst2.st_mtim.tv_nsec, 0);
	ASSERT_LT(sst2.st_mtim.tv_nsec, 1000000000);

	tm[0] = {1, -1};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1000000000};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, std::numeric_limits<decltype(tm[0].tv_nsec)>::max()};
	tm[1] = {1, 1};
	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, -1};
	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, 1000000000};
	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {1, 1};
	tm[1] = {1, std::numeric_limits<decltype(tm[1].tv_nsec)>::max()};
	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), -1);
	EXPECT_EQ(errno, EINVAL);

	ASSERT_EQ(pmemfile_utimensat(pfp, d, NULL, NULL, 0), -1);
	EXPECT_EQ(errno, ENOENT);

	tm[0] = {1, 1};
	tm[1] = {2, 2};
	ASSERT_EQ(pmemfile_utimensat(pfp, PMEMFILE_AT_CWD, "d/file", tm, 0), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, NULL, "file", NULL, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(NULL, d, "file", NULL, 0), -1);
	EXPECT_EQ(errno, EFAULT);

	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, NULL, NULL, NULL, 0), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, NULL, NULL, tm, 0), -1);
	EXPECT_EQ(errno, ENOENT);

	errno = 0;
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", NULL, -1), -1);
	EXPECT_EQ(errno, EINVAL);

	tm[0] = {14, PMEMFILE_UTIME_OMIT};
	tm[1] = {15, PMEMFILE_UTIME_OMIT};
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "fileXXX", tm, 0), 0);
	ASSERT_EQ(pmemfile_utimensat(pfp, NULL, "/fileXXX", tm, 0), 0);
	ASSERT_EQ(pmemfile_utimensat(pfp, BADF, "/fileXXX", tm, 0), 0);

	ASSERT_EQ(pmemfile_fchmodat(pfp, d, "file", 0, 0), 0);
	errno = 0;

	tm[0] = {1, 2};
	tm[1] = {3, 4};
	ASSERT_EQ(pmemfile_utimensat(pfp, d, "file", tm, 0), 0);

	memset(&fst2, 0, sizeof(fst2));
	ASSERT_EQ(pmemfile_stat(pfp, "/d/file", &fst2), 0);

	ASSERT_EQ(fst2.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_EQ(fst2.st_atim.tv_nsec, tm[0].tv_nsec);

	ASSERT_EQ(fst2.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_EQ(fst2.st_mtim.tv_nsec, tm[1].tv_nsec);

	pmemfile_close(pfp, d);

	ASSERT_EQ(pmemfile_unlink(pfp, "/d/file"), 0);
	ASSERT_EQ(pmemfile_unlink(pfp, "/d/sym"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/d"), 0);
}

TEST_F(timestamps, futimesat)
{
	ASSERT_EQ(pmemfile_mkdir(pfp, "/d", 0755), 0);
	ASSERT_TRUE(test_pmemfile_create(pfp, "/d/file"));

	PMEMfile *d = pmemfile_open(pfp, "/d", 0);
	ASSERT_NE(d, nullptr);

	pmemfile_stat_t fst;
	ASSERT_EQ(pmemfile_stat(pfp, "/d/file", &fst), 0);

	pmemfile_timeval_t tm[2] = {{12345, 999999}, {56789, 999999}};
	ASSERT_EQ(pmemfile_futimesat(pfp, d, "file", tm), 0);

	pmemfile_stat_t fst2;
	ASSERT_EQ(pmemfile_stat(pfp, "/d/file", &fst2), 0);

	ASSERT_EQ(fst2.st_atim.tv_sec, tm[0].tv_sec);
	ASSERT_EQ(fst2.st_atim.tv_nsec, tm[0].tv_usec * 1000);

	ASSERT_EQ(fst2.st_mtim.tv_sec, tm[1].tv_sec);
	ASSERT_EQ(fst2.st_mtim.tv_nsec, tm[1].tv_usec * 1000);

	ASSERT_EQ(pmemfile_futimesat(pfp, d, "file", NULL), 0);
	ASSERT_EQ(pmemfile_futimesat(pfp, NULL, "/d/file", NULL), 0);
	ASSERT_EQ(pmemfile_futimesat(pfp, BADF, "/d/file", NULL), 0);

	errno = 0;
	ASSERT_EQ(pmemfile_futimesat(pfp, NULL, "file", NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	pmemfile_stat_t fst3;
	ASSERT_EQ(pmemfile_stat(pfp, "/d/file", &fst3), 0);

	ASSERT_NE(fst2.st_atim.tv_sec, fst3.st_atim.tv_sec);
	ASSERT_NE(fst2.st_mtim.tv_sec, fst3.st_mtim.tv_sec);

	pmemfile_close(pfp, d);

	ASSERT_EQ(pmemfile_unlink(pfp, "/d/file"), 0);
	ASSERT_EQ(pmemfile_rmdir(pfp, "/d"), 0);
}

int
main(int argc, char *argv[])
{
	START();

	if (argc < 2) {
		fprintf(stderr, "usage: %s global_path", argv[0]);
		exit(1);
	}

	global_path = argv[1];

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
