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
#include <stdio.h>

#include "alloc.h"
#include "offset_mapping.h"
#include "blocks.h"
#include "out.h"
#include "utils.h"

#define N_CHILDREN_POW 4
#define N_CHILDREN (1 << N_CHILDREN_POW)

#define RANGE(i) ((uint64_t)MIN_BLOCK_SIZE) << (N_CHILDREN_POW * (i))

const static uint64_t range[] = {
	RANGE(0),
	RANGE(1),
	RANGE(2),
	RANGE(3),
	RANGE(4),
	RANGE(5),
	RANGE(6),
	RANGE(7),
	RANGE(8),
	RANGE(9),
	RANGE(10),
	RANGE(11),
	RANGE(12)
};

/*
 * Check wheter range has correct number of entries.
 * If number is correct last entry should be non zero,
 * calculating RANGE for bigger size should overflow
 * and give 0
 */
COMPILE_ERROR_ON(RANGE(ARRAY_SIZE(range) - 1) == 0);
COMPILE_ERROR_ON(RANGE(ARRAY_SIZE(range)) != 0);

/*
 * create new offset_map
 */
struct offset_map *
offset_map_new(PMEMfilepool *pfp)
{
	struct offset_map *m = pf_calloc(1, sizeof(struct offset_map));
	m->top_level = 0;
	m->pfp = pfp;

	return m;
}

/*
 * recursively remove entry in offset_map
 */
static void
offset_entry_delete(struct offset_map_entry *e, unsigned level)
{
	if (e->internal) {
		struct offset_map_entry *children = e->child;
		for (unsigned i = 0; i < N_CHILDREN; ++i)
			offset_entry_delete(children + i, level - 1);

		pf_free(children);
	}
}

/*
 * remove entire offset_map
 */
void
offset_map_delete(struct offset_map *m)
{
	offset_entry_delete(&m->entry, m->top_level);

	pf_free(m);
}

/*
 * adds new level to tree, doesn't allocate memory if there
 * are no entries
 */
static int
add_new_level(struct offset_map *m)
{
	m->top_level++;

	if (m->entry.child != NULL) {
		/*
		 * if current root had any children we must allocate
		 * new array(level) and move child to first entry
		 * in the array
		 */
		struct offset_map_entry *new_entries =
			pf_calloc(N_CHILDREN,
				sizeof(struct offset_map_entry));

		if (new_entries == NULL)
			return 1;

		new_entries[0] = m->entry;
		m->entry.internal = true;
		m->entry.child = new_entries;
	} else {
		m->entry.internal = false;
	}

	return 0;
}

/*
 * finds closest block with offset equal or smaller than
 * requested
 */
struct pmemfile_block_desc *
block_find_closest(struct offset_map *m, uint64_t offset)
{
	unsigned level = m->top_level;
	struct offset_map_entry *entry = &m->entry, *children;

	/* make sure we don't go beyond allocated range */
	if (offset >= range[m->top_level])
		offset = range[m->top_level] - MIN_BLOCK_SIZE;

	/* find entry corresponding to requested offset */
	while (entry->internal) {
		level--;
		children = entry->child;
		entry = &children[offset / range[level]];
		offset %= range[level];
	}

	/* if found entry is not NULL it is the requested block */
	if (entry->child != NULL) {
		return entry->child;
	} else {
		/*
		 * if entry at requested offset was NULL
		 * find first not NULL entry with smaller offset
		 */

		if (level == m->top_level)
			return NULL;

		struct offset_map_entry *e;

		/* look for block in entries with lower offset */
		e = entry - 1;
		while (e >= children) {
			if (e->child == NULL)
				e--;
			else if (!e->internal)
				return e->child;
			else {
				children = e->child;
				e = children + N_CHILDREN - 1;
			}
		}

		/*
		 * look for block in entries with higher offset,
		 * if found, then return previous block
		 */
		e = entry + 1;
		while (e < children + N_CHILDREN) {
			if (e->child == NULL)
				e++;
			else if (!e->internal) {
				struct pmemfile_block_desc *desc = e->child;

				return PF_RW(m->pfp, desc->prev);
			} else {
				children = e->child;
				e = children;
			}
		}

		return NULL;
	}
}

/*
 * frees memory used by 'child' if  all child entries are NULL
 */

static void
check_and_free_range(struct offset_map_entry *entry, unsigned level)
{
	for (unsigned i = 0; i < N_CHILDREN; ++i) {
		struct offset_map_entry *e = entry->child;
		if (e[i].child != NULL)
			return;
	}

	pf_free(entry->child);
	entry->child = NULL;
	entry->internal = false;
}

/*
 * put (or delete) block to offset_map
 * block can occupy one or more entries in map
 */
static int
set_range(struct offset_map_entry *entry, void *block, size_t offset,
	size_t remaining, unsigned level)
{
	entry += offset / range[level];

	while (remaining > 0) {
		if (offset % range[level] == 0 && remaining >= range[level]) {
			/* case when block covers whole range */
			entry->internal = false;
			entry->child = block;

			offset += range[level];
			remaining -= range[level];
		} else {
			/* case when block covers only part of range */
			if (entry->child == NULL) {
				entry->child = pf_calloc(N_CHILDREN,
					sizeof(struct offset_map_entry));

				if (entry->child == NULL)
					return 1;

				entry->internal = true;
			}

			size_t sub_offset = offset % range[level];
			size_t sub_remaining = range[level] - sub_offset;

			if (remaining < sub_remaining)
				sub_remaining = remaining;

			int ret = set_range(entry->child, block,
				sub_offset, sub_remaining, level - 1);

			if (ret)
				return ret;

			offset += sub_remaining;
			remaining -= sub_remaining;

			if (block == NULL) /* removing block */
				check_and_free_range(entry, level);
		}

		entry++;
	}

	return 0;
}

/*
 * insert block to offset_map
 */
int
insert_block(struct offset_map *m, struct pmemfile_block_desc *block)
{
	int ret = 0;

	/*
	 * add as many levels as necessary to cover range from 0 to end
	 * of the block
	 */
	while (range[m->top_level] <= block->offset + block->size) {
		ret = add_new_level(m);
		if (ret)
			return ret;
	}


	return set_range(&m->entry, block, block->offset, block->size,
		m->top_level);
}

/*
 * remove block from offset_map
 */
int
remove_block(struct offset_map *m, struct pmemfile_block_desc *block)
{
	int ret = set_range(&m->entry, NULL, block->offset,
						block->size, m->top_level);

	if (ret)
		return ret;

	/*
	 * cleans up offset_map tree
	 * if at the top level only first entry is internal and not null
	 * it's children can be transfered one level up and height of
	 * the tree can be decresed
	 */
	while (m->top_level > 0) {
		if (!m->entry.internal) {
			m->top_level = 0;
		} else {
			struct offset_map_entry *child = m->entry.child;

			/* if first entry is leaf, no cleanup is needed */
			if (!child[0].internal)
				return 0;

			/* check if all entries excpet first are NULL */
			for (unsigned i = 1; i < N_CHILDREN; ++i) {
				if (child[i].child != NULL)
					return 0;
			}

			struct offset_map_entry *grandchild = child[0].child;
			ASSERT(grandchild != NULL);

			pf_free(m->entry.child);
			m->entry.child = grandchild;

			m->top_level--;
		}
	}

	return 0;
}
