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
 * offset_mapping.cpp -- unit test for offset mapping tree
 */
#include "offset_mapping_wrapper.h"
#include "pmemfile_test.hpp"

class offset_mapping : public pmemfile_test {
protected:
	struct offset_map *map;

public:
	offset_mapping() : pmemfile_test(), map(offset_map_new_wrapper(pfp))
	{
	}

	~offset_mapping()
	{
		offset_map_delete_wrapper(map);
	}
};

uint64_t
pow64(uint64_t x, unsigned y)
{
	uint64_t result = 1;

	while (y--) {
		result *= x;
	}

	return result;
}

class block_desc {
public:
	struct pmemfile_block_desc *ptr;
	uint64_t offset;
	uint32_t size;

	block_desc(uint64_t offset, uint32_t size,
		   struct pmemfile_block_desc *prev = nullptr)
	    : offset(offset), size(size)
	{
		ptr = create_block(offset, size, prev);
	}

	~block_desc()
	{
		free(ptr);
	}
};

constexpr static uint32_t block_size = 0x4000;

TEST_F(offset_mapping, basic)
{
	block_desc block(0, block_size * block_size);

	int r = insert_block_wrapper(map, block.ptr);
	ASSERT_EQ(r, 0);

	for (uint32_t offset = 0; offset <= block.size; offset += block_size) {
		ASSERT_EQ(block.ptr, block_find_closest_wrapper(map, offset));
	}

	ASSERT_EQ(block.ptr,
		  block_find_closest_wrapper(map, block.size + block_size));

	r = remove_block_wrapper(map, block.ptr);
	ASSERT_EQ(r, 0);

	for (uint32_t offset = 0; offset <= block.size; offset += block_size) {
		ASSERT_EQ(nullptr, block_find_closest_wrapper(map, offset));
	}

	ASSERT_EQ(nullptr,
		  block_find_closest_wrapper(map, block.size + block_size));
}

TEST_F(offset_mapping, find_max)
{
	block_desc block1(0, block_size),
		block2(block_size, uint32_t(pow64(block_size, 2))),
		block3(pow64(block_size, 3), block_size),
		block4(pow64(block_size, 4), block_size),
		block5(INT64_MAX - block_size + 1, block_size);

	int r = insert_block_wrapper(map, block1.ptr);
	ASSERT_EQ(r, 0);

	r = insert_block_wrapper(map, block2.ptr);
	ASSERT_EQ(r, 0);

	r = insert_block_wrapper(map, block3.ptr);
	ASSERT_EQ(r, 0);

	r = insert_block_wrapper(map, block4.ptr);
	ASSERT_EQ(r, 0);

	ASSERT_EQ(block4.ptr, block_find_closest_wrapper(map, UINT64_MAX));

	r = remove_block_wrapper(map, block4.ptr);
	ASSERT_EQ(r, 0);

	r = remove_block_wrapper(map, block3.ptr);
	ASSERT_EQ(r, 0);

	ASSERT_EQ(block2.ptr, block_find_closest_wrapper(map, UINT64_MAX));

	r = remove_block_wrapper(map, block2.ptr);
	ASSERT_EQ(r, 0);

	ASSERT_EQ(block1.ptr, block_find_closest_wrapper(map, UINT64_MAX));

	r = remove_block_wrapper(map, block1.ptr);
	ASSERT_EQ(r, 0);

	ASSERT_EQ(nullptr, block_find_closest_wrapper(map, UINT64_MAX));

	r = insert_block_wrapper(map, block5.ptr);
	ASSERT_EQ(r, 0);

	ASSERT_EQ(block5.ptr, block_find_closest_wrapper(map, UINT64_MAX));
}

TEST_F(offset_mapping, big_offset)
{
	block_desc block1(0, block_size),
		block2(INT64_MAX - block_size + 1, block_size, block1.ptr);

	int r = insert_block_wrapper(map, block1.ptr);
	ASSERT_EQ(r, 0);

	r = insert_block_wrapper(map, block2.ptr);
	ASSERT_EQ(r, 0);

	ASSERT_EQ(block2.ptr, block_find_closest_wrapper(map, block2.offset));
	ASSERT_EQ(block2.ptr, block_find_closest_wrapper(map, UINT64_MAX));

	ASSERT_EQ(block1.ptr, block_find_closest_wrapper(map, block1.offset));
	ASSERT_EQ(block1.ptr,
		  block_find_closest_wrapper(map, block2.offset - block_size));
}

TEST_F(offset_mapping, find_block)
{
	block_desc block1(0, block_size),
		block2(2 * block_size, block_size, block1.ptr),
		block3(6 * block_size, block_size * block_size, block2.ptr);

	int r = insert_block_wrapper(map, block1.ptr);
	ASSERT_EQ(r, 0);

	r = insert_block_wrapper(map, block2.ptr);
	ASSERT_EQ(r, 0);

	r = insert_block_wrapper(map, block3.ptr);
	ASSERT_EQ(r, 0);

	ASSERT_EQ(block1.ptr, block_find_closest_wrapper(map, block1.offset));
	ASSERT_EQ(block2.ptr, block_find_closest_wrapper(map, block2.offset));
	ASSERT_EQ(block3.ptr, block_find_closest_wrapper(map, block3.offset));

	r = remove_block_wrapper(map, block2.ptr);
	ASSERT_EQ(r, 0);

	ASSERT_EQ(block1.ptr, block_find_closest_wrapper(map, block1.offset));
	ASSERT_EQ(block1.ptr, block_find_closest_wrapper(map, block2.offset));
	ASSERT_EQ(block3.ptr, block_find_closest_wrapper(map, block3.offset));

	r = remove_block_wrapper(map, block1.ptr);
	ASSERT_EQ(r, 0);

	/*
	 * when there is only block3, return value will be equal to block3->prev
	 * for all offsets < block.offset
	 */
	ASSERT_EQ(block2.ptr, block_find_closest_wrapper(map, block1.offset));
	ASSERT_EQ(block2.ptr, block_find_closest_wrapper(map, block2.offset));
	ASSERT_EQ(block3.ptr, block_find_closest_wrapper(map, block3.offset));

	r = remove_block_wrapper(map, block3.ptr);
	ASSERT_EQ(r, 0);

	ASSERT_EQ(nullptr, block_find_closest_wrapper(map, block1.offset));
	ASSERT_EQ(nullptr, block_find_closest_wrapper(map, block2.offset));
	ASSERT_EQ(nullptr, block_find_closest_wrapper(map, block3.offset));
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
