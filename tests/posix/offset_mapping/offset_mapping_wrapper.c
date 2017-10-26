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

#include "layout.h"
#include "pool.h"
#include "utils.h"
#include "offset_mapping.h"
#include "offset_mapping_wrapper.h"
#include <stdio.h>

void *
pmemfile_direct(PMEMfilepool *pfp, PMEMoid oid)
{
	if (oid.off == 0)
		return NULL;

	/* for tests - discard pfp->pop */
	return (void *)(oid.off);
}

struct pmemfile_block_desc *
create_block(uint64_t offset, uint32_t size,
			 struct pmemfile_block_desc *prev)
{
	struct pmemfile_block_desc *desc = calloc(1, sizeof(*desc));

	desc->offset = offset;
	desc->size = size;
	desc->prev.oid.off = (uintptr_t)prev;

	return desc;
}

struct offset_map *
offset_map_new_wrapper(PMEMfilepool *pfp)
{
	return offset_map_new(pfp);
}

void
offset_map_delete_wrapper(struct offset_map *m)
{
	return offset_map_delete(m);
}

struct pmemfile_block_desc *
block_find_closest_wrapper(struct offset_map *map, uint64_t offset)
{
	return block_find_closest(map, offset);
}

int
insert_block_wrapper(struct offset_map *map, struct pmemfile_block_desc *block)
{
	return insert_block(map, block);
}

int
remove_block_wrapper(struct offset_map *map, struct pmemfile_block_desc *block)
{
	return remove_block(map, block);
}
