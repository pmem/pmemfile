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

#include "ctree.h"
#include "layout.h"
#include "inode.h"
#include "internal.h"
#include "out.h"
#include "block_array.h"

static void
update_first_block_info(struct pmemfile_vinode *vinode)
{
	struct block_info *binfo = &vinode->first_free_block;

	if (binfo->arr != NULL) {
		/*
		 * If the block_info is not null, it means it was kept
		 * up-to-date by the allocating routines.
		 */
		return;
	}

	/*
	 * If binfo was not used before, it must be initialized.
	 */

	/*
	 * Find the block_array containing the next free block metadata
	 * slot. This is either the block_array stored right in the
	 * inode, ...
	 */
	binfo->arr = &vinode->inode->file_data.blocks;
	/*
	 * ... or if there is more than one block_array, it is
	 * the one linked to it with the next field.
	 */
	if (!TOID_IS_NULL(binfo->arr->next))
		binfo->arr = D_RW(binfo->arr->next);

	binfo->idx = 0;

	/* Find the first free entry in the block array. */
	while (binfo->idx < binfo->arr->length &&
	    binfo->arr->blocks[binfo->idx].size != 0)
		binfo->idx++;
}

static bool
has_free_block_entry(struct pmemfile_vinode *vinode)
{
	struct block_info *binfo = &vinode->first_free_block;

	return binfo->idx < binfo->arr->length;
}

static void
allocate_new_block_array(struct pmemfile_vinode *vinode)
{
	TOID(struct pmemfile_block_array) new =
			TX_ZALLOC(struct pmemfile_block_array, FILE_PAGE_SIZE);
	D_RW(new)->length = (uint32_t)
			((page_rounddown(pmemobj_alloc_usable_size(new.oid)) -
			sizeof(struct pmemfile_block_array)) /
			sizeof(struct pmemfile_block));

	D_RW(new)->next = vinode->inode->file_data.blocks.next;
	TX_SET_DIRECT(&vinode->inode->file_data.blocks, next, new);
	vinode->first_free_block.arr = D_RW(new);
	vinode->first_free_block.idx = 0;
}

/*
 * is_zeroed -- check if given memory range is all zero
 */
static inline bool
is_zeroed(const void *addr, size_t len)
{
	/* XXX optimize */
	const char *a = (const char *)addr;
	while (len-- > 0)
		if (*a++)
			return false;
	return true;
}

static struct pmemfile_block *
acquire_new_entry(struct pmemfile_vinode *vinode)
{
	if (!has_free_block_entry(vinode))
		allocate_new_block_array(vinode);

	ASSERT(has_free_block_entry(vinode));

	struct block_info *binfo = &vinode->first_free_block;
	struct pmemfile_block *block = binfo->arr->blocks + binfo->idx++;

	ASSERT(is_zeroed(block, sizeof(*block)));

	/* XXX, snapshot separated to let pmemobj use small object cache  */
	pmemobj_tx_add_range_direct(block, 32);
	pmemobj_tx_add_range_direct((char *)block + 32, 32);
	COMPILE_ERROR_ON(sizeof(*block) != 64);

	return block;
}

struct pmemfile_block *
block_list_insert_after(struct pmemfile_vinode *vinode,
			struct pmemfile_block *prev)
{
	update_first_block_info(vinode);

	struct pmemfile_block *block = acquire_new_entry(vinode);

	if (prev == NULL) {
		if (vinode->first_block != NULL) {
			block->next = blockp_as_oid(vinode->first_block);
			TX_SET_DIRECT(vinode->first_block,
			    prev, blockp_as_oid(block));
		}
		vinode->first_block = block;
	} else {
		block->prev = blockp_as_oid(prev);
		block->next = prev->next;
		TX_SET_DIRECT(prev, next, blockp_as_oid(block));
		struct pmemfile_block *next = D_RW(block->next);
		if (next != NULL)
			TX_SET_DIRECT(next, prev, blockp_as_oid(block));
	}

	return block;
}

static struct pmemfile_block *
last_used_block(struct pmemfile_vinode *vinode)
{
	struct block_info *binfo = &vinode->first_free_block;

	ASSERT(binfo->idx > 0);

	return binfo->arr->blocks + (binfo->idx - 1);
}

static void
unlink_block(struct pmemfile_block *block)
{
	if (!TOID_IS_NULL(block->prev))
		TX_SET(block->prev, next, block->next);

	if (!TOID_IS_NULL(block->next))
		TX_SET(block->next, prev, block->prev);
}

static void
relocate_block(struct pmemfile_block *dst, struct pmemfile_block *src)
{
	ASSERT(dst != src);

	TX_ADD_DIRECT(dst);

	if (!TOID_IS_NULL(src->prev))
		TX_SET(src->prev, next, blockp_as_oid(dst));

	if (!TOID_IS_NULL(src->next))
		TX_SET(src->next, prev, blockp_as_oid(dst));

	TX_MEMCPY(dst, src, sizeof(*src));
}

static bool
must_remove_a_block_array(struct pmemfile_vinode *vinode)
{
	if (vinode->first_free_block.idx != 0)
		return false;

	return vinode->first_free_block.arr !=
	    &vinode->inode->file_data.blocks;
}

static void
remove_a_block_array(struct pmemfile_vinode *vinode)
{
	TOID(struct pmemfile_block_array) to_remove;
	TOID(struct pmemfile_block_array) new_next;
	struct block_info *binfo;

	binfo = &vinode->first_free_block;

	to_remove = vinode->inode->file_data.blocks.next;

	new_next = D_RW(to_remove)->next;
	TX_SET_DIRECT(&vinode->inode->file_data.blocks, next, new_next);
	if (TOID_IS_NULL(new_next))
		binfo->arr = &vinode->inode->file_data.blocks;
	else
		binfo->arr = D_RW(new_next);

	TX_FREE(to_remove);
	binfo->idx = binfo->arr->length;
}

struct pmemfile_block *
block_list_remove(struct pmemfile_vinode *vinode,
		struct pmemfile_block *block)
{
	struct pmemfile_block *prev;

	update_first_block_info(vinode);

	ASSERT(vinode->first_free_block.idx > 0);

	struct pmemfile_block *moving_block = last_used_block(vinode);

	unlink_block(block);

	prev = D_RW(block->prev);

	if (moving_block == prev)
		prev = block;

	if (vinode->first_block == block)
		vinode->first_block = D_RW(block->next);

	if (!TOID_IS_NULL(block->data))
		TX_FREE(block->data);

	if (moving_block != block) {
		if (vinode->first_block == moving_block)
			vinode->first_block = block;
		ctree_remove_unlocked(vinode->blocks, moving_block->offset, 1);
		relocate_block(block, moving_block);
		ctree_insert_unlocked(vinode->blocks, block->offset,
		    (uint64_t)block);
	}

	TX_MEMSET(moving_block, 0, sizeof(*moving_block));

	vinode->first_free_block.idx--;

	if (must_remove_a_block_array(vinode))
		remove_a_block_array(vinode);

	return prev;
}
