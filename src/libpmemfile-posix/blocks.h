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

#include <stdlib.h>
#include "libpmemfile-posix.h"
#include "pool.h"

#ifndef PMEMFILE_BLOCKS_H
#define PMEMFILE_BLOCKS_H

struct pmem_block_info {

	size_t size;

	unsigned units_per_block;

	uint64_t class_id;
};

#define MIN_BLOCK_SIZE ((size_t)0x4000)


/* block_alignment value is always equal to the smallest block size */
extern size_t block_alignment;

#define MAX_BLOCK_SIZE (UINT32_MAX - (UINT32_MAX % block_alignment))

static inline size_t
block_rounddown(size_t n)
{
	return n & ~(block_alignment - 1);
}

static inline size_t
block_roundup(size_t n)
{
	return block_rounddown(n + block_alignment - 1);
}

void set_block_size(size_t size);

void expand_to_full_pages(uint64_t *offset, uint64_t *length);

const struct pmem_block_info *metadata_block_info(void);

const struct pmem_block_info *data_block_info(size_t size, size_t limit);

void initialize_alloc_classes(PMEMobjpool *pop);

#endif
