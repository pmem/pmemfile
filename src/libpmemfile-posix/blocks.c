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

#define CONST_BLOCK_N_UNITS 16

static struct pmem_block_info metadata_block =
	{ METADATA_BLOCK_SIZE, 128 };

static struct pmem_block_info data_blocks[] = {
	{ MIN_BLOCK_SIZE,	128 },
	{ 256 * 1024,		16 },
	{ 2 * 1024 * 1024,	8 },
	{ 0 } /* terminator */
};

size_t block_alignment = MIN_BLOCK_SIZE;

/*
 * set const block size
 */
void
set_block_size(size_t size)
{
	data_blocks[0].size = size;
	data_blocks[0].units_per_block = CONST_BLOCK_N_UNITS;

	data_blocks[1].size = 0;

	block_alignment = size;
}

const struct pmem_block_info *
metadata_block_info(void)
{
	return &metadata_block;
}

/*
 * returns block which is smaller or equal to 'limit'
 * if it's possible (limit value is large enough) returned block
 * will be the smallest block larger than 'size'
 */
const struct pmem_block_info *
data_block_info(size_t size, size_t limit)
{
	ASSERT(limit >= data_blocks[0].size);
	const struct pmem_block_info *block;

	for (block = data_blocks; block->size != 0; ++block) {
		if (block->size > limit)
			return block - 1;

		if (size <= block->size)
			return block;
	}

	return block - 1;
}

/* if allocation classes are supported */
#ifdef POBJ_CLASS_ID
static int
set_alloc_class(PMEMobjpool *pop, struct pmem_block_info *b,
	int id)
{
	char query[30];

	sprintf(query, "heap.alloc_class.%d.desc", id);

	struct pobj_alloc_class_desc desc;

	desc.unit_size = b->size;
	desc.units_per_block = b->units_per_block;
	desc.header_type = POBJ_HEADER_NONE;

	int ret = pmemobj_ctl_set(pop, query, &desc);
	if (ret) {
		ERR("cannot register allocation class");
		return ret;
	}

	b->class_id = POBJ_CLASS_ID(id);

	return 0;
}
#else
static int
set_alloc_class(PMEMobjpool *pop, struct pmem_block_info *b,
	int id)
{
	ERR("allocation classes not supported");
	return 1;
}
#endif

int
initialize_alloc_classes(PMEMobjpool *pop)
{
	int ret = set_alloc_class(pop, &metadata_block, METADATA_ID);
	if (ret)
		return ret;

	struct pmem_block_info *block;

	for (block = data_blocks; block->size != 0; ++block) {
		int index = (int) (block - data_blocks);
		ret = set_alloc_class(pop, block, FIRST_BLOCK_ID + index);
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
