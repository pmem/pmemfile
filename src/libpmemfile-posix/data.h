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
#ifndef PMEMFILE_DATA_H
#define PMEMFILE_DATA_H

#include "inode.h"

extern size_t pmemfile_posix_block_size;
extern bool pmemfile_overallocate_on_append;

void vinode_destroy_data_state(PMEMfilepool *pfp,
			struct pmemfile_vinode *vinode);

int vinode_rebuild_block_tree(struct pmemfile_vinode *vinode);
void vinode_remove_interval(struct pmemfile_vinode *vinode,
			uint64_t offset, uint64_t len);
void vinode_allocate_interval(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		uint64_t offset, uint64_t size);

struct pmemfile_block_desc *find_closest_block(struct pmemfile_vinode *vinode,
		uint64_t off);
struct pmemfile_block_desc *find_closest_block_with_hint(
		struct pmemfile_vinode *vinode, uint64_t offset,
		struct pmemfile_block_desc *last_block);

bool is_offset_in_block(const struct pmemfile_block_desc *block,
		uint64_t offset);

enum cpy_direction { read_from_blocks, write_to_blocks };

struct pmemfile_block_desc *iterate_on_file_range(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode,
		struct pmemfile_block_desc *starting_block, uint64_t offset,
		uint64_t len, char *buf, enum cpy_direction dir);

#endif
