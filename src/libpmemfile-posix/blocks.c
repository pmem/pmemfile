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

#include <libpmemobj.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "blocks.h"
#include "out.h"

#define METADATA_ID 128

#define FIRST_BLOCK_ID 129

#define CONST_BLOCK_ID 254

static const struct pobj_alloc_class_desc metadata_block =
	{ METADATA_BLOCK_SIZE,	100, POBJ_HEADER_NONE, METADATA_ID };

static const struct pobj_alloc_class_desc data_blocks[] = {
	{ MIN_BLOCK_SIZE,	100, POBJ_HEADER_NONE, FIRST_BLOCK_ID },
	{ 256 * 1024,		50, POBJ_HEADER_NONE, FIRST_BLOCK_ID + 1 },
	{ 4 * 1024 * 1024,	10, POBJ_HEADER_NONE, FIRST_BLOCK_ID + 2 }
};

#define DATA_BLOCK_COUNT (sizeof(data_blocks) / sizeof(data_blocks[0]))

COMPILE_ERROR_ON(DATA_BLOCK_COUNT + FIRST_BLOCK_ID >= CONST_BLOCK_ID);

struct pobj_alloc_class_desc pmemfile_posix_block =
	{ 0, 1000, POBJ_HEADER_NONE, CONST_BLOCK_ID};

size_t block_alignment = MIN_BLOCK_SIZE;

static struct alloc_class_info
get_alloc_class_info(const struct pobj_alloc_class_desc *desc)
{
	struct alloc_class_info info;

	info.size = desc->unit_size;
	info.class_id = desc->class_id;

	return info;
}

struct alloc_class_info
metadata_block_info(void)
{
	return get_alloc_class_info(&metadata_block);
}

struct alloc_class_info
data_block_info(size_t size, size_t limit)
{
	if (pmemfile_posix_block.unit_size != 0) {
		ASSERT(limit >= pmemfile_posix_block.unit_size);

		return get_alloc_class_info(&pmemfile_posix_block);
	}

	ASSERT(limit >= MIN_BLOCK_SIZE);

	for (unsigned i = 0; i < DATA_BLOCK_COUNT; i++) {
		if (data_blocks[i].unit_size > limit)
			return get_alloc_class_info(&data_blocks[i - 1]);

		if (size <= data_blocks[i].unit_size)
			return get_alloc_class_info(&data_blocks[i]);
	}

	return get_alloc_class_info(&data_blocks[DATA_BLOCK_COUNT - 1]);
}

static int
set_alloc_class(PMEMobjpool *pop, const struct pobj_alloc_class_desc *ac)
{
	char query[30];
	struct pobj_alloc_class_desc desc = *ac;

	sprintf(query, "heap.alloc_class.%d.desc", desc.class_id);

	int ret = pmemobj_ctl_set(pop, query, &desc);
	if (ret)
		return ret;

	ASSERT(desc.class_id == ac->class_id);

	return 0;
}

int
initialize_alloc_classes(PMEMobjpool *pop)
{
	int ret = set_alloc_class(pop, &metadata_block);
	if (ret)
		return ret;

	if (pmemfile_posix_block.unit_size != 0)
		return set_alloc_class(pop, &pmemfile_posix_block);

	for (unsigned i = 0; i < DATA_BLOCK_COUNT; i++) {
		ret = set_alloc_class(pop, &data_blocks[i]);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * expand_to_full_pages
 * Alters two file offsets to be pmemfile-page aligned. This is not
 * necessarily the same as memory page alignment!
 * The resulting offset refer to an interval that contains the original
 * interval.
 */
void
expand_to_full_pages(uint64_t *offset, uint64_t *length)
{
	/* align the offset */
	*length += *offset % block_alignment;
	*offset -= *offset % block_alignment;

	/* align the length */
	*length = block_roundup(*length);
}
