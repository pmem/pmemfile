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
#include "utils.h"

/*
 * update_first_block_info
 * The vinode structs contain some precomputed information about the block
 * arrays in the file. This routine updates these if needed.
 * Upon opening a file (creating a new vinode), the binfo->arr pointer
 * is set to NULL. This is checked for in the `binfo->arr != NULL` condition.
 * Every routine dealing directly with block allocation (two of them) calls
 * this routine before using the block_info struct, thus lazy-initializing it.
 * If new blocks are never allocated, and existing blocks are never removed
 * during the lifetime of a vinode, this initialization never happens.
 * Once this initialization happens, the allocating routines are expected
 * to keep this data up to date.
 */
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

/*
 * has_free_block_entry - is there a free slot in the already
 *  allocated block_arrays to hold another block's metadata?
 *
 * If not, a new block_array should be allocated.
 */
static bool
has_free_block_entry(struct pmemfile_vinode *vinode)
{
	struct block_info *binfo = &vinode->first_free_block;

	return binfo->idx < binfo->arr->length;
}

/*
 * allocate_new_block_array
 * Allocates a new block_array, and links it into the linked list of
 * block_arrays associated with the file. It is assumed, that there is no free
 * slot available in any of the existing block_arrays - hence the need to
 * allocate a new one.
 *
 * The new block_array is linked in as the first item into the linked list.
 * There is always a zeroth item, the one stored internally in struct inode.
 * See the layout.h header file about that.
 *
 * Before (0th block array is full, 1st block array is full):
 * +--------------------------+
 * | struct pmemfile_inode    |
 * |     +-----------------+  |   +------------------+
 * |     | 0th block array |  |   | 1st block array  |
 * |     |    next->-------+--+-->|     next->       |
 * |     |---------------- |  |   | ---------------- |
 * |     ||b |b |b |b |b | |  |   | |b |b |b |b |b | |
 * |     |---------------- |  |   | ---------------- |
 * |     +-----------------+  |   +------------------+
 * |                          |
 * +--------------------------+                   ||
 *                                                ||
 *                                                \\ old 1st block array becomes
 *                                                 \\ the new 2nd block array
 *                                                  \\
 *                                                   \\
 * After:                                             \\
 * (0th and 2nd block arrays are full,                 \\
 * 1st block array is empty)                            \\
 * +--------------------------+                          \\
 * | struct pmemfile_inode    |
 * |     +-----------------+  |   +------------------+  +------------------+
 * |     | 0th block array |  |   | new block array  |  | 2nd block array  |
 * |     |    next->-------+--+-->|     next->-------+->|     next->       |
 * |     |---------------- |  |   | ---------------- |  | ---------------- |
 * |     ||b |b |b |b |b | |  |   | |  |  |  |  |  | |  | |b |b |b |b |b | |
 * |     |---------------- |  |   | --^------------- |  | ---------------- |
 * |     +-----------------+  |   +---^--------------+  +------------------+
 * |                          |       ^
 * +--------------------------+       ^
 *                                    ^
 *                                    |
 *                 The next free slot for block metadata
 */
static void
allocate_new_block_array(struct pmemfile_vinode *vinode)
{
	ASSERT(!has_free_block_entry(vinode));

	TOID(struct pmemfile_block_array) new =
			TX_ZALLOC(struct pmemfile_block_array, FILE_PAGE_SIZE);
	D_RW(new)->length = (uint32_t)
			((page_rounddown(pmemobj_alloc_usable_size(new.oid)) -
			sizeof(struct pmemfile_block_array)) /
			sizeof(struct pmemfile_block_desc));

	D_RW(new)->next = vinode->inode->file_data.blocks.next;
	TX_SET_DIRECT(&vinode->inode->file_data.blocks, next, new);
	vinode->first_free_block.arr = D_RW(new);
	vinode->first_free_block.idx = 0;
}

/*
 * acquire_new_entry
 * Finds a new slot for block metadata inside a block_array associated
 * with the file. The array itself does not store the number of elements
 * already used, this is only tracked in the vinode:
 * the field vinode->first_free_block->idx is the index of the first free
 * element. This routine simply increases this index. As long as this index
 * is smaller than the number of elements available (block_array->length),
 * there is no need to allocate new block_arrays from pmemobj.
 * See also the has_free_block_entry function above.
 *
 *
 * +------------------+
 * | block array      |
 * | ---------------- |
 * | |b |b |  |  |  | |
 * | -------^-------- |
 * +--------^---------+
 *          ^
 *          ^
 *          \__return the address of this pmemfile_block
 *
 */
static struct pmemfile_block_desc *
acquire_new_entry(struct pmemfile_vinode *vinode)
{
	if (!has_free_block_entry(vinode))
		allocate_new_block_array(vinode);

	ASSERT(has_free_block_entry(vinode));

	struct block_info *binfo = &vinode->first_free_block;
	struct pmemfile_block_desc *block = binfo->arr->blocks + binfo->idx++;

	ASSERT(is_zeroed(block, sizeof(*block)));

	/* XXX, snapshot separated to let pmemobj use small object cache  */
	pmemobj_tx_add_range_direct(block, 32);
	pmemobj_tx_add_range_direct((char *)block + 32, 32);
	COMPILE_ERROR_ON(sizeof(*block) != 64);

	return block;
}

/*
 * block_list_insert_after
 * Finds a free slot in the block_arrays associated with the file (allocate
 * a new block_array if needed).
 * Links the block into the linked list of blocks right after the already
 * existing block supplied in the 'prev' argument.
 * Returns a pointer to the free slot found.
 *
 */
struct pmemfile_block_desc *
block_list_insert_after(struct pmemfile_vinode *vinode,
			struct pmemfile_block_desc *prev)
{
	/* lazy init vinode->first_free_block */
	update_first_block_info(vinode);

	struct pmemfile_block_desc *block = acquire_new_entry(vinode);

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
		struct pmemfile_block_desc *next = D_RW(block->next);
		if (next != NULL)
			TX_SET_DIRECT(next, prev, blockp_as_oid(block));
	}

	return block;
}

/*
 * last_used_block
 * Returns a pointer to last (last in terms of allocated most recently) block
 * metadata. This is always the block right before the first free block.
 */
static struct pmemfile_block_desc *
last_used_block(struct pmemfile_vinode *vinode)
{
	struct block_info *binfo = &vinode->first_free_block;

	ASSERT(binfo->idx > 0);

	return binfo->arr->blocks + (binfo->idx - 1);
}

/*
 * unlink_block - removes the block metadata from the linked list of blocks
 * Note: does not deallocate the block metadata, only unlinks it.
 */
static void
unlink_block(struct pmemfile_block_desc *block)
{
	if (!TOID_IS_NULL(block->prev))
		TX_SET(block->prev, next, block->next);

	if (!TOID_IS_NULL(block->next))
		TX_SET(block->next, prev, block->prev);
}

/*
 * relocate_block - overwrite *dst with *src
 * The block metadata at src is moved to the place at dst - while also updating
 * the appropriate prev and next pmem pointers to keep track of the new
 * location of the relocated block metadata.
 *
 * Whatever was at *dst, is discarded.
 *
 * Does not alter the data at *src, but no other pmemfile_block points to it
 * after this operation.
 */
static void
relocate_block(struct pmemfile_block_desc *dst, struct pmemfile_block_desc *src)
{
	ASSERT(dst != src);

	TX_ADD_DIRECT(dst);

	if (!TOID_IS_NULL(src->prev))
		TX_SET(src->prev, next, blockp_as_oid(dst));

	if (!TOID_IS_NULL(src->next))
		TX_SET(src->next, prev, blockp_as_oid(dst));

	TX_MEMCPY(dst, src, sizeof(*src));
}

/*
 * is_first_block_array_empty
 * Is the first block_array empty?
 * Is this block_array not the one stored inside the pmemfile_vinode struct?
 */
static bool
is_first_block_array_empty(struct pmemfile_vinode *vinode)
{
	if (vinode->first_free_block.idx != 0)
		return false; /* it is not empty */

	/*
	 * Is this the one stored inside the vinode?
	 * If yes then in a sense it is the zeroth block array, not the first.
	 */
	return vinode->first_free_block.arr !=
	    &vinode->inode->file_data.blocks;
}

/*
 * remove_first_block_array - unlinks a block_array from the linked list
 *  of block arrays, and releases the memory using TX_FREE
 *
 * Also: updates the vinode->first_free_block data structure as needed.
 */
static void
remove_first_block_array(struct pmemfile_vinode *vinode)
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

/*
 * block_list_remove - remove a pmemfile_block item, and deallocate the block
 *  data associated with it
 *
 * This routine makes sure an invariant is held: all the free pmemfile_block
 * slots associated with this file are in a single block_array.
 * In order to achieve this, it must sometimes relocate one block to fill an
 * empty slot appearing after removing a block.
 *
 * Diagram with 11 blocks numbered hexadecimally, b0 - bb, removing block b6:
 *
 * Before removing b6 (invariant held):
 *
 * +--------------------------+
 * | struct pmemfile_inode    |
 * |     +-----------------+  |   +------------------+  +------------------+
 * |     | 0th block array |  |   | 1st block array  |  | 2nd block array  |
 * |     |    next->-------+--+-->|     next->-------+->|     next->       |
 * |     |---------------- |  |   | ---------------- |  | ---------------- |
 * |     ||b0|b1|b2|b3|b4| |  |   | |ba|bb|  |  |  | |  | |b5|b6|b7|b8|b9| |
 * |     |---------------- |  |   | -------^-------- |  | ---------------- |
 * |     +-----------------+  |   +--------^---------+  +------------------+
 * |                          |            ^
 * +--------------------------+            \__free slots
 *
 * After removing b6 (invariant not held):
 *
 * +--------------------------+
 * | struct pmemfile_inode    |
 * |     +-----------------+  |   +------------------+  +------------------+
 * |     | 0th block array |  |   | 1st block array  |  | 2nd block array  |
 * |     |    next->-------+--+-->|     next->-------+->|     next->       |
 * |     |---------------- |  |   | ---------------- |  | ---------------- |
 * |     ||b0|b1|b2|b3|b4| |  |   | |ba|bb|  |  |  | |  | |b5|  |b7|b8|b9| |
 * |     |---------------- |  |   | -------^-------- |  | ----^----------- |
 * |     +-----------------+  |   +--------^---------+  +-----^------------+
 * |                          |            ^                  ^
 * +--------------------------+            \__free slots      \__new free slot
 *
 * Restoring the invariant (filling the new free slot with bb):
 *
 * +--------------------------+
 * | struct pmemfile_inode    |
 * |     +-----------------+  |   +------------------+  +------------------+
 * |     | 0th block array |  |   | 1st block array  |  | 2nd block array  |
 * |     |    next->-------+--+-->|     next->-------+->|     next->       |
 * |     |---------------- |  |   | ---------------- |  | ---------------- |
 * |     ||b0|b1|b2|b3|b4| |  |   | |ba|  |  |  |  | |  | |b5|bb|b7|b8|b9| |
 * |     |---------------- |  |   | ----^----------- |  | ----^----------- |
 * |     +-----------------+  |   +-----^------------+  +-----^------------+
 * |                          |         ^                     ^
 * +--------------------------+         \__free slots         \__relocated block
 *
 *
 */
struct pmemfile_block_desc *
block_list_remove(struct pmemfile_vinode *vinode,
		struct pmemfile_block_desc *block)
{
	struct pmemfile_block_desc *prev;

	/* lazy init vinode->first_free_block */
	update_first_block_info(vinode);

	ASSERT(vinode->first_free_block.idx > 0);

	struct pmemfile_block_desc *moving_block = last_used_block(vinode);

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
		if (ctree_insert_unlocked(vinode->blocks, block->offset,
		    (uint64_t)block))
			pmemfile_tx_abort(errno);
	}

	TX_MEMSET(moving_block, 0, sizeof(*moving_block));

	vinode->first_free_block.idx--;

	if (is_first_block_array_empty(vinode))
		remove_first_block_array(vinode);

	return prev;
}
