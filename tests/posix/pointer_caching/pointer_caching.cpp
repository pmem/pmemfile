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
 * pointer_caching.cpp -- unit test excerising pointer caches in pmemfile
 */
#include "pmemfile_test.hpp"

class pointer_caching : public pmemfile_test {
public:
	pointer_caching() : pmemfile_test()
	{
	}
};

/*
 * Modifying a file via one file pointer (f1), and reading that
 * file via another file pointer (f1). If pmemfile-posix caches
 * certain pointers to file data in data structures associated with
 * f2, those should be invalidated when the corresponding data
 * is modified via f1.
 */
TEST_F(pointer_caching, open_write_read_two_file_refs)
{
	constexpr const char path[] = "/aaa";
	PMEMfile *f1, *f2;
	pmemfile_ssize_t r;

	f1 = pmemfile_open(pfp, path,
			   PMEMFILE_O_RDWR | PMEMFILE_O_CREAT | PMEMFILE_O_EXCL,
			   0777);
	ASSERT_NE(f1, nullptr);

	f2 = pmemfile_open(pfp, path, PMEMFILE_O_RDWR);
	ASSERT_NE(f2, nullptr);

	std::vector<char> buffer_w(0x1000, 'p');
	std::vector<char> buffer_r(0x1000);

	buffer_w[0] = '0';
	buffer_w[1] = '1';
	constexpr size_t write_count = 0x200;

	/* many small writes via f1 */
	for (size_t i = 0; i < write_count; ++i) {
		r = pmemfile_write(pfp, f1, buffer_w.data(), buffer_w.size());
		ASSERT_EQ(r, (decltype(r))buffer_w.size()) << COND_ERROR(r);
	}

	/* read back some of the precious write via f2, but not all */
	for (size_t i = 0; i < write_count - 3; ++i) {
		r = pmemfile_read(pfp, f2, buffer_r.data(), buffer_r.size());
		ASSERT_EQ(r, (decltype(r))buffer_r.size()) << COND_ERROR(r);
		ASSERT_TRUE(memcmp(buffer_r.data(), buffer_w.data(),
				   buffer_r.size()) == 0);
	}

	/*
	 * At this point, the offset associated with f2 points to somewhere
	 * close to the end of the file, and should read some more of the data
	 * written via f1, if used.
	 * Making this other modification via f1 should change that -- following
	 * this hole punching, f2 should read zeros from that offset.
	 */
	r = pmemfile_fallocate(pfp, f1, PMEMFILE_FALLOC_FL_PUNCH_HOLE |
				       PMEMFILE_FALLOC_FL_KEEP_SIZE,
			       0x1111, 0x200000);
	ASSERT_EQ(r, 0) << COND_ERROR(r);

	/*
	 * Check if the above modification is observable via f2. This can
	 * result in unpredictable errors, if f2 caches some pointers to
	 * data in the underlying file, and such caches are not invalidated
	 * due to f2 not detecting the modifications.
	 */
	r = pmemfile_read(pfp, f2, buffer_r.data(), buffer_r.size());
	ASSERT_EQ(r, (decltype(r))buffer_r.size()) << COND_ERROR(r);
	ASSERT_TRUE(is_zeroed(buffer_r.data(), buffer_r.size()));

	pmemfile_close(pfp, f1);
	pmemfile_close(pfp, f2);
	ASSERT_EQ(pmemfile_unlink(pfp, path), 0);
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
