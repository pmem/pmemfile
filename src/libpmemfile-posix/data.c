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

#define _GNU_SOURCE

#include <limits.h>

#include "callbacks.h"
#include "compiler_utils.h"
#include "data.h"
#include "inode.h"
#include "internal.h"
#include "locks.h"
#include "out.h"
#include "pool.h"
#include "os_thread.h"
#include "valgrind_internal.h"
#include "ctree.h"
#include "block_array.h"
#include "utils.h"

/*
 * expand_to_full_pages
 * Alters two file offsets to be pmemfile-page aligned. This is not
 * necessarily the same as memory page alignment!
 * The resulting offset refer to an interval that contains the original
 * interval.
 */
static void
expand_to_full_pages(uint64_t *offset, uint64_t *length)
{
	/* align the offset */
	*length += *offset % FILE_PAGE_SIZE;
	*offset -= *offset % FILE_PAGE_SIZE;

	/* align the length */
	*length = page_roundup(*length);
}

/*
 * narrow_to_full_pages
 * Alters two file offsets to be pmemfile-page aligned. This is not
 * necessarily the same as memory page alignment!
 * The resulting offset refer to an interval that is contained by the original
 * interval. This new interval can end up being empty, i.e. *length can become
 * zero.
 */
static void
narrow_to_full_pages(uint64_t *offset, uint64_t *length)
{
	uint64_t end = page_rounddown(*offset + *length);
	*offset = page_roundup(*offset);
	if (end > *offset)
		*length = end - *offset;
	else
		*length = 0;
}

/*
 * block_cache_insert_block -- inserts block into the tree
 */
static int
block_cache_insert_block(struct ctree *c, struct pmemfile_block_desc *block)
{
	if (ctree_insert(c, block->offset, (uintptr_t)block)) {
		if (pmemobj_tx_stage() == TX_STAGE_WORK)
			pmemfile_tx_abort(errno);
		else
			return -errno;
	}

	return 0;
}

static void
block_cache_insert_block_in_tx(struct ctree *c,
		struct pmemfile_block_desc *block)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);
	(void) block_cache_insert_block(c, block);
}
/*
 * find_last_block - find the block with the highest offset in the file
 */
static struct pmemfile_block_desc *
find_last_block(const struct pmemfile_vinode *vinode)
{
	uint64_t off = UINT64_MAX;
	return (void *)(uintptr_t)ctree_find_le(vinode->blocks, &off);
}

/*
 * vinode_rebuild_block_tree -- rebuilds runtime tree of blocks
 */
static int
vinode_rebuild_block_tree(struct pmemfile_vinode *vinode)
{
	struct ctree *c = ctree_new();
	if (!c)
		return -errno;
	struct pmemfile_block_array *block_array =
			&vinode->inode->file_data.blocks;
	struct pmemfile_block_desc *first = NULL;

	while (block_array != NULL) {
		for (unsigned i = 0; i < block_array->length; ++i) {
			struct pmemfile_block_desc *block =
					&block_array->blocks[i];

			if (block->size == 0)
				break;

			int err = block_cache_insert_block(c, block);
			if (err) {
				ctree_delete(c);
				return err;
			}
			if (first == NULL || block->offset < first->offset)
				first = block;
		}

		block_array = D_RW(block_array->next);
	}

	vinode->first_block = first;
	vinode->blocks = c;

	return 0;
}

/*
 * is_offset_in_block -- check if the given offset is in the range
 * specified by the block metadata
 */
static bool
is_offset_in_block(const struct pmemfile_block_desc *block, uint64_t offset)
{
	if (block == NULL)
		return false;

	return block->offset <= offset && offset < block->offset + block->size;
}

/*
 * is_block_data_initialized
 * This is just a wrapper around checking a flag. The BLOCK_INITIALIZED flag
 * in the block metadata is not set when allocating a new block, thus the
 * underlying memory region (pointed to by block->data) does not need to
 * be zeroed.
 */
static bool
is_block_data_initialized(const struct pmemfile_block_desc *block)
{
	ASSERT(block != NULL);

	return (block->flags & BLOCK_INITIALIZED) != 0;
}

/*
 * find_block -- look up block metadata with the highest offset
 * lower than or equal to the offset argument
 */
static struct pmemfile_block_desc *
find_block(struct pmemfile_vinode *vinode, uint64_t off)
{
	return (void *)(uintptr_t)ctree_find_le(vinode->blocks, &off);
}

/*
 * find_block_with_hint -- look up block metadata with the highest offset
 * lower than or equal to the offset argument
 *
 * using the proposed last_block
 */
static struct pmemfile_block_desc *
find_block_with_hint(struct pmemfile_vinode *vinode, uint64_t offset,
		struct pmemfile_block_desc *last_block)
{
	if (is_offset_in_block(last_block, offset))
		return last_block;

	return find_block(vinode, offset);
}

/*
 * vinode_destroy_data_state -- destroys file state related to data
 *
 * This is used as a callback passed to cb_push_front, that is why the pfp
 * argument is used.
 */
void
vinode_destroy_data_state(PMEMfilepool *pfp, struct pmemfile_vinode *vinode)
{
	(void) pfp;

	if (vinode->blocks) {
		ctree_delete(vinode->blocks);
		vinode->blocks = NULL;
	}

	memset(&vinode->first_free_block, 0, sizeof(vinode->first_free_block));
}

/*
 * file_allocate_block_data -- allocates new block data.
 * The block metadata must be already allocated, and passed as the block
 * pointer argument.
 */
static void
file_allocate_block_data(PMEMfilepool *pfp,
		struct pmemfile_block_desc *block,
		size_t count,
		bool use_usable_size)
{
	ASSERT(count > 0);
	ASSERT(count % FILE_PAGE_SIZE == 0);

	uint32_t size;

	if (pmemfile_posix_block_size != 0) {
		ASSERT(pmemfile_posix_block_size <= MAX_BLOCK_SIZE);
		ASSERT(pmemfile_posix_block_size % FILE_PAGE_SIZE == 0);

		size = (uint32_t)pmemfile_posix_block_size;
	} else {
		if (count <= MAX_BLOCK_SIZE)
			size = (uint32_t)count;
		else
			size = (uint32_t)MAX_BLOCK_SIZE;
	}

	block->data = TX_XALLOC(char, size, POBJ_XALLOC_NO_FLUSH);
	if (use_usable_size) {
		size_t usable = pmemobj_alloc_usable_size(block->data.oid);
		ASSERT(usable >= size);
		if (usable > MAX_BLOCK_SIZE)
			size = MAX_BLOCK_SIZE;
		else
			size = (uint32_t)page_rounddown(usable);
	}

#ifdef DEBUG
	/* poison block data */
	void *data = D_RW(block->data);
	VALGRIND_ADD_TO_TX(data, size);
	pmemobj_memset_persist(pfp->pop, data, 0x66, size);
	VALGRIND_REMOVE_FROM_TX(data, size);
	VALGRIND_DO_MAKE_MEM_UNDEFINED(data, size);
#endif

	block->size = size;

	block->flags = 0;
}

/*
 * is_append - is a write operation to be performed on a file going to append
 *  to the file?
 */
static bool
is_append(struct pmemfile_vinode *vinode, struct pmemfile_inode *inode,
		uint64_t offset, uint64_t size)
{
	if (inode->size >= offset + size)
		return false; /* not writing past file size */

	struct pmemfile_block_desc *block = find_last_block(vinode);

	/* Writing past the last allocated block? */

	if (block == NULL)
		return true;

	return (block->offset + block->size) < (offset + size);
}

/*
 * overallocate_size - determines what size to request from pmemobj when
 *  doing an overallocation
 *
 * This is used while appending to a file.
 */
static uint64_t
overallocate_size(uint64_t size)
{
	if (size <= 4096)
		return 16 * 1024;
	else if (size <= 64 * 1024)
		return 256 * 1024;
	else if (size <= 1024 * 1024)
		return 4 * 1024 * 1024;
	else if (size <= 64 * 1024 * 1024)
		return 64 * 1024 * 1024;
	else
		return size;
}

/*
 * vinode_allocate_interval - makes sure an interval in a file is allocated
 *
 * This is used in fallocate, truncate, and before writing to a file.
 * A write to a file refers to an offset, and a length. The interval specified
 * by offset and length might contain holes (where no block is allocated). This
 * routine fills those holes, so when actually writing to the file, no
 * allocation checks need to be made.
 *
 * One example:
 *
 *  _file offset zero
 * |                    _ offset                       _ offset + length
 * |                   |                              |
 * |                   |                              |
 * +---------------------------------------------------------------------------
 *     | block #0 | block #1 |     | block #2 |           | block #3 |
 *     |          |          |     |          |           |          |
 * +---------------------------------------------------------------------------
 *                            ^    ^           ^      ^
 *                            \    /           \      |
 *                             \  /             \     |
 *                              \/               \    |
 *                            hole between        \   |
 *                            block#1 & block#2    hole at the
 *                                                 end of the interval
 *
 * The vinode_allocate_interval routine iterates over the existing blocks
 * which intersect with the interval, and fills any holes found. In the example
 * above, the allocation of two new blocks is required.
 *
 * The iterations of the loop inside vinode_allocate_interval considering
 * the above example:
 *
 * ============================================================================
 * before iteration #0 :
 * block points to block #1
 *
 *                      _ offset                       _ offset + size
 *                     |                              |
 *                     |                              |
 * +---------------------------------------------------------------------------
 *     | block #0 | block #1 |     | block #2 |           | block #3 |
 *     |          |          |     |          |           |          |
 * +---------------------------------------------------------------------------
 *
 * iteration #0:
 * offset not in a hole
 * shrink the interval, to exclude the intersection of block#1 and the interval
 *
 * ============================================================================
 * after iteration #0 :
 * block points to block #1
 *
 *                             _offset                 _ offset + size
 *                            |                       |
 *                            |                       |
 * +---------------------------------------------------------------------------
 *     | block #0 | block #1 |     | block #2 |           | block #3 |
 *     |          |          |     |          |           |          |
 * +---------------------------------------------------------------------------
 *
 * iteration #1:
 * In a hole between block#1 and block#2
 * Allocate a new block (block#4) to cover the left edge of the interval, and
 *  set block to point to this new block
 *
 *
 *
 * ============================================================================
 * after iteration #1 :
 * block points to block #4
 *
 *                             _offset                 _ offset + size
 *                            |                       |
 *                            |                       |
 * +---------------------------------------------------------------------------
 *     | block #0 | block #1 | b#4 | block #2 |           | block #3 |
 *     |          |          |     |          |           |          |
 * +---------------------------------------------------------------------------
 *
 * iteration #2:
 * offset not in a hole
 * shrink the interval, exclude block #4
 *
 * ============================================================================
 * after iteration #2 :
 * block points to block #4
 *
 *                                   _offset           _ offset + size
 *                                  |                 |
 *                                  |                 |
 * +---------------------------------------------------------------------------
 *     | block #0 | block #1 | b#4 | block #2 |           | block #3 |
 *     |          |          |     |          |           |          |
 * +---------------------------------------------------------------------------
 *
 * iteration #3:
 * offset after between block#4 and block#2, but there is no hole
 * set block to point to the next block, block#2
 *
 * ============================================================================
 * after iteration #3 :
 * block points to block #2
 *
 *                                   _offset           _ offset + size
 *                                  |                 |
 *                                  |                 |
 * +---------------------------------------------------------------------------
 *     | block #0 | block #1 | b#4 | block #2 |           | block #3 |
 *     |          |          |     |          |           |          |
 * +---------------------------------------------------------------------------
 *
 * iteration #4:
 * offset not in a hole
 * shrink the interval, exclude block #2
 *
 * ============================================================================
 * after iteration #4 :
 * block points to block #2
 *
 *                                      offset_        _ offset + size
 *                                             |      |
 *                                             |      |
 * +---------------------------------------------------------------------------
 *     | block #0 | block #1 | b#4 | block #2 |           | block #3 |
 *     |          |          |     |          |           |          |
 * +---------------------------------------------------------------------------
 *
 * iteration #5:
 * offset between block#2 and block#3, there is a hole to be filled
 * allocate a new block (block#5), and set block to point to this new block
 *
 * ============================================================================
 * after iteration #5 :
 * block points to block #5
 *
 *                                      offset_        _ offset + size
 *                                             |      |
 *                                             |      |
 * +---------------------------------------------------------------------------
 *     | block #0 | block #1 | b#4 | block #2 |block#5|   | block #3 |
 *     |          |          |     |          |       |   |          |
 * +---------------------------------------------------------------------------
 *
 * iteration #6:
 * offset not in a hole
 * shrink the interval, exclude block #5
 *
 * ============================================================================
 * after iteration #6 :
 * block points to block #5
 *
 *                                             offset_ _ offset + size
 *                                                    |
 *                                                    |
 * +---------------------------------------------------------------------------
 *     | block #0 | block #1 | b#4 | block #2 |block#5|   | block #3 |
 *     |          |          |     |          |       |   |          |
 * +---------------------------------------------------------------------------
 *                                                      ^
 *                                                      |
 *                                                      there is a hole left
 *                                                      after the interval
 *
 * The remaining interval has zero size, ending the loop.
 */
static void
vinode_allocate_interval(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		uint64_t offset, uint64_t size)
{
	ASSERT(size > 0);
	ASSERT(offset + size > offset);

	struct pmemfile_inode *inode = vinode->inode;

	bool over = pmemfile_overallocate_on_append &&
	    is_append(vinode, inode, offset, size);

	if (over)
		size = overallocate_size(size);

	expand_to_full_pages(&offset, &size);

	/*
	 * Start at block with the highest offset lower than or equal to
	 * the start of the requested interval.
	 * This block does not necessarily intersect the interval.
	 */
	struct pmemfile_block_desc *block = find_block(vinode, offset);

	/*
	 * The following loop decreases the size of the interval to be
	 * processed, until there is nothing more left to process.
	 * The beginning of the interval (the edge with the lower offset) is
	 * processed in each iteration.
	 * At the beginning of each iteration, the offset variable points to
	 * the current lower edge of the interval, and block points to a block
	 * at the largest offset lower than or equal to the interval's offset.
	 *
	 * If such a block is found, two cases can be distinguished.
	 *
	 * 1) The block intersects the interval:
	 *
	 *
	 *           _ offset                       _ offset + length
	 *          |                              |
	 *          |                              |
	 *  +-------------------------------------------------------------
	 *     | block |
	 *     |       |
	 *  +-------------------------------------------------------------
	 *          ^   ^
	 *          \___\__intersection
	 *
	 *  In this case, the loop can ignore the intersection (there is nothing
	 *  to allocate there), and go on to the next iteration with a reduced
	 *  interval, i.e.: the original interval minus the intersection.
	 *
	 * 2) The block does not intersect the interval:
	 *
	 *                  _ offset                       _ offset + length
	 *                 |                              |
	 *                 |                              |
	 *  +-------------------------------------------------------------
	 *     | block |
	 *     |       |
	 *  +-------------------------------------------------------------
	 *
	 *  In this case a new block must be allocated. The new block's file
	 *  offset is set to the interval's offset.
	 *   This case is further split into two sub cases:
	 *    When there is no other block following the one already found,
	 *    one can try to allocate a block large enough to cover the whole
	 *    interval.
	 *    When there is another block following the currently treated
	 *    blocks, one must fill the hole between these two
	 *    blocks -- allocating larger space than the block would only waste
	 *    space.
	 *
	 *
	 * Besides cases 1) and 2), there is the possibility that no appropriate
	 * block is found.
	 *
	 * 3) There are no blocks in the file at all.
	 *      One must allocate the very first block, hopefully being able to
	 *      cover the whole interval with this new block.
	 *
	 * 4) There are no blocks at or before offset, but there are blocks at
	 *    higher offsets.
	 *      One must allocate the very first block. This new first block
	 *      should not intersect with the original first block, as that
	 *      would waste space.
	 */
	do {
		if (is_offset_in_block(block, offset)) {
			/* case 1) */
			/* Not in a hole, skip over the intersection */

			uint64_t available = block->size;
			available -= offset - block->offset;

			if (available >= size)
				return;

			offset += available;
			size -= available;
		} else if (block == NULL && vinode->first_block == NULL) {
			/* case 3) */
			/* File size is zero, no blocks in the file so far */

			block = block_list_insert_after(vinode, NULL);
			block->offset = offset;
			file_allocate_block_data(pfp, block, size, over);
			block_cache_insert_block_in_tx(vinode->blocks, block);
		} else if (block == NULL && vinode->first_block != NULL) {
			/* case 4) */
			/* In a hole before the first block */

			size_t count = size;
			uint64_t first_offset = vinode->first_block->offset;

			if (offset + count > first_offset)
				count = (uint32_t)(first_offset - offset);

			block = block_list_insert_after(vinode, NULL);
			block->offset = offset;
			file_allocate_block_data(pfp, block, count, false);
			block_cache_insert_block_in_tx(vinode->blocks, block);
		} else if (TOID_IS_NULL(block->next)) {
			/* case 2) */
			/* After the last allocated block */

			block = block_list_insert_after(vinode, block);
			block->offset = offset;
			file_allocate_block_data(pfp, block, size, over);
			block_cache_insert_block_in_tx(vinode->blocks, block);
		} else {
			/* case 2) */
			/* between two allocated blocks */
			/* potentially in a hole between two allocated blocks */

			struct pmemfile_block_desc *next = D_RW(block->next);

			/* How many bytes in this hole can be used? */
			uint64_t hole_count = next->offset - offset;

			/* Are all those bytes needed? */
			if (hole_count > size)
				hole_count = size;

			if (hole_count > 0) { /* Is there any hole at all? */
				block = block_list_insert_after(vinode, block);
				block->offset = offset;
				file_allocate_block_data(pfp, block, hole_count,
				    false);
				block_cache_insert_block_in_tx(vinode->blocks,
						block);

				if (block->size > hole_count)
					block->size = (uint32_t)hole_count;
			} else {
				block = next;
			}
		}
	} while (size > 0);
}

/*
 * find_following_block
 * Returns the block following the one supplied as argument, according
 * to file offsets. A NULL pointer as argument is considered to mean the
 * beginning of the file.
 */
static struct pmemfile_block_desc *
find_following_block(struct pmemfile_vinode *vinode,
	struct pmemfile_block_desc *block)
{
	if (block != NULL)
		return D_RW(block->next);
	else
		return vinode->first_block;
}

enum cpy_direction { read_from_blocks, write_to_blocks };

/*
 * read_block_range - copy data to user supplied buffer
 */
static void
read_block_range(const struct pmemfile_block_desc *block,
	uint64_t offset, uint64_t len, char *buf)
{
	ASSERT(len > 0);
	ASSERT(block == NULL || offset < block->size);
	ASSERT(block == NULL || offset + len <= block->size);

	/* block == NULL means reading from a hole in a sparse file */

	/*
	 * !is_block_data_initialized(block) means reading from an
	 * fallocate-ed region in a file, a region that was allocated,
	 * but never initialized.
	 */

	if ((block != NULL) && is_block_data_initialized(block)) {
		const char *read_from = D_RO(block->data) + offset;
		memcpy(buf, read_from, len);
	} else {
		memset(buf, 0, len);
	}
}

/*
 * read_block_range - copy data from user supplied buffer
 *
 * A corresponding block is expected to be already allocated.
 */
static void
write_block_range(PMEMfilepool *pfp, struct pmemfile_block_desc *block,
	uint64_t offset, uint64_t len, const char *buf)
{
	ASSERT(block != NULL);
	ASSERT(len > 0);
	ASSERT(offset < block->size);
	ASSERT(offset + len <= block->size);

	char *data = D_RW(block->data);

	if (!is_block_data_initialized(block)) {
		char *start_zero = data;
		size_t count = offset;
		if (count != 0) {
			VALGRIND_ADD_TO_TX(start_zero, count);
			pmemobj_memset_persist(pfp->pop, start_zero, 0, count);
			VALGRIND_REMOVE_FROM_TX(start_zero, count);
		}

		start_zero = data + offset + len;
		count = block->size - (offset + len);
		if (count != 0) {
			VALGRIND_ADD_TO_TX(start_zero, count);
			pmemobj_memset_persist(pfp->pop, start_zero, 0, count);
			VALGRIND_REMOVE_FROM_TX(start_zero, count);
		}

		TX_ADD_FIELD_DIRECT(block, flags);
		block->flags |= BLOCK_INITIALIZED;
	}

	VALGRIND_ADD_TO_TX(data + offset, len);
	pmemobj_memcpy_persist(pfp->pop, data + offset, buf, len);
	VALGRIND_REMOVE_FROM_TX(data + offset, len);
}

/*
 * iterate_on_file_range - loop over a file range, and copy from/to user buffer
 *
 * When cpy_direction specifies writing, this routine expects the corresponding
 * blocks to be already allocated. In case of reading, it is ok to skip holes
 * between blocks.
 */
static struct pmemfile_block_desc *
iterate_on_file_range(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		struct pmemfile_block_desc *starting_block, uint64_t offset,
		uint64_t len, char *buf, enum cpy_direction dir)
{
	struct pmemfile_block_desc *block = starting_block;
	struct pmemfile_block_desc *last_block = starting_block;

	while (len > 0) {
		/* Remember the pointer to block used last time */
		if (block == NULL)
			ASSERT(dir == read_from_blocks);

		if ((block == NULL) ||
		    !is_offset_in_block(block, offset)) {
			/*
			 * The offset points into a hole in the file, or
			 * into a region fallocate-ed, but not yet initialized.
			 * This routine assumes all blocks to be already
			 * allocated during writing, so holes should only
			 * happen during reading. This routine also
			 * assumes that the range for reading doesn't
			 * reach past the end of the file.
			 */
			ASSERT(dir == read_from_blocks);

			struct pmemfile_block_desc *next_block =
			    find_following_block(vinode, block);

			/*
			 * How many zero bytes should be read?
			 *
			 * If the hole is at the end of the file, i.e.
			 * no more blocks are allocated after the hole,
			 * then read the whole len. If there is a block
			 * allocated after the hole, then just read until
			 * that next block, and continue with the next iteration
			 * of this loop.
			 */
			uint64_t read_hole_count = len;
			if (next_block != NULL) {
				/* bytes till the end of this hole */
				uint64_t hole_end = next_block->offset - offset;

				if (hole_end < read_hole_count)
					read_hole_count = hole_end;

				block = next_block;
			}

			/*
			 * Reading from holes should just read zeros.
			 */

			read_block_range(NULL, 0, read_hole_count, buf);

			offset += read_hole_count;
			len -= read_hole_count;
			buf += read_hole_count;

			continue;
		}

		ASSERT(is_offset_in_block(block, offset));

		/*
		 * Multiple blocks might be used, but the first and last
		 * blocks are special, in the sense that not necessarily
		 * all of their content is copied.
		 */

		/*
		 * Offset to data used from the block.
		 * It should be zero, unless it is the first block in
		 * the range.
		 */
		uint64_t in_block_start = offset - block->offset;

		/*
		 * The number of bytes used from this block.
		 * Unless it is the last block in the range, all
		 * data till the end of the block is used.
		 */
		uint64_t in_block_len = block->size - in_block_start;

		if (len < in_block_len) {
			/*
			 * Don't need all the data till the end of this block?
			 */
			in_block_len = len;
		}

		ASSERT(in_block_start < block->size);
		ASSERT(in_block_start + in_block_len <= block->size);

		if (dir == read_from_blocks)
			read_block_range(block,
			    in_block_start, in_block_len, buf);
		else
			write_block_range(pfp, block,
			    in_block_start, in_block_len, buf);

		offset += in_block_len;
		len -= in_block_len;
		buf += in_block_len;
		last_block = block;
		block = D_RW(block->next);
	}

	return last_block;
}

/*
 * vinode_write -- writes to file
 */
static void
vinode_write(PMEMfilepool *pfp, struct pmemfile_vinode *vinode, size_t offset,
		struct pmemfile_block_desc **last_block,
		const char *buf, size_t count)
{
	ASSERT(count > 0);
	struct pmemfile_inode *inode = vinode->inode;

	/*
	 * Two steps:
	 * - Zero Fill some new blocks, in case the file is extended by
	 *   writing to the file after seeking past file size ( optionally )
	 * - Copy the data from the users buffer
	 */

	uint64_t original_size = inode->size;
	uint64_t new_size = inode->size;

	if (offset + count > original_size)
		new_size = offset + count;

	/* All blocks needed for writing are properly allocated at this point */

	struct pmemfile_block_desc *block =
			find_block_with_hint(vinode, offset, *last_block);

	block = iterate_on_file_range(pfp, vinode, block, offset,
			count, (char *)buf, write_to_blocks);

	if (block)
		*last_block = block;

	if (new_size != original_size) {
		TX_ADD_FIELD_DIRECT(inode, size);
		inode->size = new_size;
	}
}

static pmemfile_ssize_t
pmemfile_pwritev_internal(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode,
		struct pmemfile_block_desc **last_block,
		uint64_t file_flags,
		size_t offset,
		const pmemfile_iovec_t *iov,
		int iovcnt)
{
	LOG(LDBG, "vinode %p iov %p iovcnt %d", vinode, iov, iovcnt);

	if (!vinode_is_regular_file(vinode)) {
		errno = EINVAL;
		return -1;
	}

	if (!(file_flags & PFILE_WRITE)) {
		errno = EBADF;
		return -1;
	}

	if (iovcnt == 0)
		return 0;

	int error = 0;

	struct pmemfile_inode *inode = vinode->inode;

	os_rwlock_wrlock(&vinode->rwlock);

	vinode_snapshot(vinode);

	size_t ret = 0;

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!vinode->blocks) {
			int err = vinode_rebuild_block_tree(vinode);
			if (err)
				pmemfile_tx_abort(err);
		}

		if (file_flags & PFILE_APPEND)
			offset = inode->size;

		size_t sum_len = 0;
		for (int i = 0; i < iovcnt; ++i) {
			size_t len = iov[i].iov_len;

			if ((pmemfile_ssize_t)len < 0)
				len = SSIZE_MAX;

			if ((pmemfile_ssize_t)(sum_len + len) < 0)
				len = SSIZE_MAX - ret;

			/* overflow check */
			if (offset + sum_len + len < offset)
				len = SIZE_MAX - offset - sum_len;

			sum_len += len;

			if (len != iov[i].iov_len)
				break;
		}

		if (sum_len > 0)
			vinode_allocate_interval(pfp, vinode, offset, sum_len);

		for (int i = 0; i < iovcnt; ++i) {
			size_t len = iov[i].iov_len;

			if ((pmemfile_ssize_t)len < 0)
				len = SSIZE_MAX;

			if ((pmemfile_ssize_t)(ret + len) < 0)
				len = SSIZE_MAX - ret;

			if (offset + len < offset) /* overflow check */
				len = SIZE_MAX - offset;

			if (len > 0)
				vinode_write(pfp, vinode, offset, last_block,
						iov[i].iov_base, len);

			ret += len;
			offset += len;

			if (len != iov[i].iov_len)
				break;
		}

		if (ret > 0) {
			struct pmemfile_time tm;
			get_current_time(&tm);
			TX_SET_DIRECT(inode, mtime, tm);
		}
	} TX_ONABORT {
		error = errno;
		vinode_restore_on_abort(vinode);
	} TX_END

	os_rwlock_unlock(&vinode->rwlock);

	if (error) {
		errno = error;
		return -1;
	}

	return (pmemfile_ssize_t)ret;
}

/*
 * pmemfile_write -- writes to file
 */
pmemfile_ssize_t
pmemfile_write(PMEMfilepool *pfp, PMEMfile *file, const void *buf, size_t count)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	pmemfile_iovec_t vec;
	vec.iov_base = (void *)buf;
	vec.iov_len = count;

	pmemfile_ssize_t ret = pmemfile_pwritev_internal(pfp, file->vinode,
			&last_block, file->flags, file->offset, &vec, 1);
	if (ret >= 0) {
		file->offset += (size_t)ret;
		file->block_pointer_cache = last_block;
	}

	os_mutex_unlock(&file->mutex);

	return ret;
}

pmemfile_ssize_t
pmemfile_writev(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;

	pmemfile_ssize_t ret = pmemfile_pwritev_internal(pfp, file->vinode,
			&last_block, file->flags, file->offset, iov, iovcnt);
	if (ret >= 0) {
		file->offset += (size_t)ret;
		file->block_pointer_cache = last_block;
	}

	os_mutex_unlock(&file->mutex);

	return ret;
}

pmemfile_ssize_t
pmemfile_pwrite(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
		size_t count, pmemfile_off_t offset)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	struct pmemfile_vinode *vinode = file->vinode;
	uint64_t flags = file->flags;

	os_mutex_unlock(&file->mutex);

	pmemfile_iovec_t vec;
	vec.iov_base = (void *)buf;
	vec.iov_len = count;

	return pmemfile_pwritev_internal(pfp, vinode, &last_block, flags,
			(size_t)offset, &vec, 1);
}

pmemfile_ssize_t
pmemfile_pwritev(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt, pmemfile_off_t offset)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	struct pmemfile_vinode *vinode = file->vinode;
	uint64_t flags = file->flags;

	os_mutex_unlock(&file->mutex);

	return pmemfile_pwritev_internal(pfp, vinode, &last_block, flags,
			(size_t)offset, iov, iovcnt);
}

/*
 * vinode_read -- reads file
 */
static size_t
vinode_read(PMEMfilepool *pfp, struct pmemfile_vinode *vinode, size_t offset,
		struct pmemfile_block_desc **last_block, char *buf,
		size_t count)
{
	uint64_t size = vinode->inode->size;

	/*
	 * Start reading at offset, stop reading
	 * when end of file is reached, or count bytes were read.
	 * The following two branches compute how many bytes are
	 * going to be read.
	 */
	if (offset >= size)
		return 0; /* EOF already */

	if (size - offset < count)
		count = size - offset;

	struct pmemfile_block_desc *block =
			find_block_with_hint(vinode, offset, *last_block);

	block = iterate_on_file_range(pfp, vinode, block, offset,
			count, buf, read_from_blocks);

	if (block)
		*last_block = block;

	return count;
}

static int
time_cmp(const struct pmemfile_time *t1, const struct pmemfile_time *t2)
{
	if (t1->sec < t2->sec)
		return -1;
	if (t1->sec > t2->sec)
		return 1;
	if (t1->nsec < t2->nsec)
		return -1;
	if (t1->nsec > t2->nsec)
		return 1;
	return 0;
}

static pmemfile_ssize_t
pmemfile_preadv_internal(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode,
		struct pmemfile_block_desc **last_block,
		uint64_t file_flags,
		size_t offset,
		const pmemfile_iovec_t *iov,
		int iovcnt)
{
	LOG(LDBG, "vinode %p iov %p iovcnt %d", vinode, iov, iovcnt);

	if (!vinode_is_regular_file(vinode)) {
		errno = EINVAL;
		return -1;
	}

	if (!(file_flags & PFILE_READ)) {
		errno = EBADF;
		return -1;
	}

	if (iovcnt == 0)
		return 0;

	struct pmemfile_inode *inode = vinode->inode;

	/*
	 * We want read to be performed under read lock, but we need the block
	 * tree to exist. If it doesn't exist we have to drop the lock we hold,
	 * take it in write mode (because other thread may want to do the same),
	 * check that it doesn't exist (another thread may already did that),
	 * drop the lock again, take it in read mode and check AGAIN (because
	 * another thread may have destroyed the block tree while we weren't
	 * holding the lock).
	 */
	os_rwlock_rdlock(&vinode->rwlock);
	while (!vinode->blocks) {
		os_rwlock_unlock(&vinode->rwlock);
		os_rwlock_wrlock(&vinode->rwlock);

		int err = 0;
		if (!vinode->blocks)
			err = vinode_rebuild_block_tree(vinode);
		os_rwlock_unlock(&vinode->rwlock);

		if (err) {
			errno = err;
			return -1;
		}

		os_rwlock_rdlock(&vinode->rwlock);
	}

	size_t ret = 0;

	for (int i = 0; i < iovcnt; ++i) {
		size_t len = iov[i].iov_len;
		if ((pmemfile_ssize_t)(ret + len) < 0)
			len = SSIZE_MAX - ret;
		ASSERT((pmemfile_ssize_t)(ret + len) >= 0);

		size_t bytes_read = vinode_read(pfp, vinode, offset, last_block,
				iov[i].iov_base, len);

		ret += bytes_read;
		offset += bytes_read;
		if (bytes_read != len)
			break;
	}

	bool update_atime = !(file_flags & PFILE_NOATIME);
	struct pmemfile_time tm;

	if (update_atime) {
		struct pmemfile_time tm1d;
		get_current_time(&tm);
		tm1d.nsec = tm.nsec;
		tm1d.sec = tm.sec - 86400;

		/* relatime */
		update_atime =	time_cmp(&inode->atime, &tm1d) < 0 ||
				time_cmp(&inode->atime, &inode->ctime) < 0 ||
				time_cmp(&inode->atime, &inode->mtime) < 0;
	}

	os_rwlock_unlock(&vinode->rwlock);

	if (update_atime) {
		os_rwlock_wrlock(&vinode->rwlock);

		TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
			TX_SET_DIRECT(inode, atime, tm);
		} TX_ONABORT {
			LOG(LINF, "can not update inode atime");
		} TX_END

		os_rwlock_unlock(&vinode->rwlock);
	}

	return (pmemfile_ssize_t)ret;
}

/*
 * pmemfile_read -- reads file
 */
pmemfile_ssize_t
pmemfile_read(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	pmemfile_iovec_t vec;
	vec.iov_base = buf;
	vec.iov_len = count;

	pmemfile_ssize_t ret = pmemfile_preadv_internal(pfp, file->vinode,
			&last_block, file->flags, file->offset, &vec, 1);
	if (ret >= 0) {
		file->offset += (size_t)ret;
		file->block_pointer_cache = last_block;
	}

	os_mutex_unlock(&file->mutex);

	return ret;
}

pmemfile_ssize_t
pmemfile_readv(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;

	pmemfile_ssize_t ret = pmemfile_preadv_internal(pfp, file->vinode,
			&last_block, file->flags, file->offset, iov, iovcnt);
	if (ret >= 0) {
		file->offset += (size_t)ret;
		file->block_pointer_cache = last_block;
	}

	os_mutex_unlock(&file->mutex);

	return ret;
}

pmemfile_ssize_t
pmemfile_pread(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count,
		pmemfile_off_t offset)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	struct pmemfile_vinode *vinode = file->vinode;
	uint64_t flags = file->flags;

	os_mutex_unlock(&file->mutex);

	pmemfile_iovec_t vec;
	vec.iov_base = buf;
	vec.iov_len = count;

	return pmemfile_preadv_internal(pfp, vinode, &last_block, flags,
			(size_t)offset, &vec, 1);
}


pmemfile_ssize_t
pmemfile_preadv(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt, pmemfile_off_t offset)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	struct pmemfile_vinode *vinode = file->vinode;
	uint64_t flags = file->flags;

	os_mutex_unlock(&file->mutex);

	return pmemfile_preadv_internal(pfp, vinode, &last_block, flags,
			(size_t)offset, iov, iovcnt);
}

/*
 * lseek_seek_data -- part of the lseek implementation
 * Looks for data (not a hole), starting at the specified offset.
 */
static pmemfile_off_t
lseek_seek_data(struct pmemfile_vinode *vinode, pmemfile_off_t offset,
		pmemfile_off_t fsize)
{
	if (vinode->blocks == NULL) {
		int err = vinode_rebuild_block_tree(vinode);
		if (err)
			return err;
	}

	struct pmemfile_block_desc *block =
			find_block(vinode, (uint64_t)offset);
	if (block == NULL) {
		/* offset is before the first block */
		if (vinode->first_block == NULL)
			return fsize; /* No data in the whole file */
		else
			return (pmemfile_off_t)vinode->first_block->offset;
	}

	if (is_offset_in_block(block, (uint64_t)offset))
		return offset;

	block = D_RW(block->next);

	if (block == NULL)
		return fsize; /* No more data in file */

	return (pmemfile_off_t)block->offset;
}

/*
 * lseek_seek_hole -- part of the lseek implementation
 * Looks for a hole, starting at the specified offset.
 */
static pmemfile_off_t
lseek_seek_hole(struct pmemfile_vinode *vinode, pmemfile_off_t offset,
		pmemfile_off_t fsize)
{
	if (vinode->blocks == NULL) {
		int err = vinode_rebuild_block_tree(vinode);
		if (err)
			return err;
	}

	struct pmemfile_block_desc *block =
			find_block(vinode, (uint64_t)offset);

	while (block != NULL && offset < fsize) {
		pmemfile_off_t block_end =
				(pmemfile_off_t)block->offset + block->size;

		struct pmemfile_block_desc *next = D_RW(block->next);

		if (block_end >= offset)
			offset = block_end; /* seek to the end of block */

		if (next == NULL)
			break; /* the rest of the file is treated as a hole */
		else if (offset < (pmemfile_off_t)next->offset)
			break; /* offset is in a hole between two blocks */

		block = next;
	}

	return offset;
}

/*
 * lseek_seek_data_or_hole -- part of the lseek implementation
 * Expects the vinode to be locked while being called.
 */
static pmemfile_off_t
lseek_seek_data_or_hole(struct pmemfile_vinode *vinode, pmemfile_off_t offset,
			int whence)
{
	pmemfile_ssize_t fsize = (pmemfile_ssize_t)vinode->inode->size;

	if (!vinode_is_regular_file(vinode))
		return -EBADF; /* XXX directories are not supported here yet */

	if (offset > fsize) {
		/*
		 * From GNU man page: ENXIO if
		 * "...ENXIO  whence is SEEK_DATA or SEEK_HOLE, and the file
		 * offset is beyond the end of the file..."
		 */
		return -ENXIO;
	}

	if (offset < 0) /* this seems to be allowed by POSIX and Linux */
		offset = 0;

	if (whence == PMEMFILE_SEEK_DATA) {
		offset = lseek_seek_data(vinode, offset, fsize);
	} else {
		ASSERT(whence == PMEMFILE_SEEK_HOLE);
		offset = lseek_seek_hole(vinode, offset, fsize);
	}

	if (offset > fsize)
		offset = fsize;

	return offset;
}

/*
 * pmemfile_lseek_locked -- changes file current offset
 */
static pmemfile_off_t
pmemfile_lseek_locked(PMEMfilepool *pfp, PMEMfile *file, pmemfile_off_t offset,
		int whence)
{
	(void) pfp;

	LOG(LDBG, "file %p offset %ld whence %d", file, offset, whence);

	if (file->flags & PFILE_PATH) {
		errno = EBADF;
		return -1;
	}

	if (vinode_is_dir(file->vinode)) {
		if (whence == PMEMFILE_SEEK_END) {
			errno = EINVAL;
			return -1;
		}
	} else if (vinode_is_regular_file(file->vinode)) {
		/* Nothing to do for now */
	} else {
		errno = EINVAL;
		return -1;
	}

	struct pmemfile_vinode *vinode = file->vinode;
	struct pmemfile_inode *inode = vinode->inode;
	pmemfile_off_t ret;
	int new_errno = EINVAL;

	switch (whence) {
		case PMEMFILE_SEEK_SET:
			ret = offset;
			if (ret < 0) {
				/*
				 * From POSIX: EINVAL if
				 * "...the resulting file offset would be
				 * negative for a regular file..."
				 */
				new_errno = EINVAL;
			}
			break;
		case PMEMFILE_SEEK_CUR:
			ret = (pmemfile_off_t)file->offset + offset;
			if (ret < 0) {
				if (offset < 0) {
					new_errno = EINVAL;
				} else {
					/*
					 * From POSIX: EOVERFLOW if
					 * "...The resulting file offset would
					 * be a value which cannot be
					 * represented correctly in an object
					 * of type off_t..."
					 */
					new_errno = EOVERFLOW;
				}
			}
			break;
		case PMEMFILE_SEEK_END:
			os_rwlock_rdlock(&vinode->rwlock);
			ret = (pmemfile_off_t)inode->size + offset;
			os_rwlock_unlock(&vinode->rwlock);
			if (ret < 0) {
				/* Errors as in SEEK_CUR */
				if (offset < 0)
					new_errno = EINVAL;
				else
					new_errno = EOVERFLOW;
			}
			break;
		case PMEMFILE_SEEK_DATA:
		case PMEMFILE_SEEK_HOLE:
			/*
			 * We may need to rebuild the block tree, so we have to
			 * take vinode lock in write mode.
			 */
			os_rwlock_wrlock(&vinode->rwlock);
			ret = lseek_seek_data_or_hole(vinode, offset, whence);
			if (ret < 0) {
				new_errno = (int)-ret;
				ret = -1;
			}
			os_rwlock_unlock(&vinode->rwlock);
			break;
		default:
			ret = -1;
			break;
	}

	if (ret < 0) {
		ret = -1;
		errno = new_errno;
	} else {
		if (file->offset != (size_t)ret)
			LOG(LDBG, "off diff: old %lu != new %lu", file->offset,
					(size_t)ret);
		file->offset = (size_t)ret;
	}

	return ret;
}

/*
 * pmemfile_lseek -- changes file current offset
 */
pmemfile_off_t
pmemfile_lseek(PMEMfilepool *pfp, PMEMfile *file, pmemfile_off_t offset,
		int whence)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	COMPILE_ERROR_ON(sizeof(offset) != 8);
	pmemfile_off_t ret;

	os_mutex_lock(&file->mutex);
	ret = pmemfile_lseek_locked(pfp, file, offset, whence);
	os_mutex_unlock(&file->mutex);

	return ret;
}

/*
 * is_block_contained_by_interval -- see vinode_remove_interval
 * for explanation.
 */
static bool
is_block_contained_by_interval(struct pmemfile_block_desc *block,
		uint64_t start, uint64_t len)
{
	return block->offset >= start &&
		(block->offset + block->size) <= (start + len);
}

/*
 * is_interval_contained_by_block -- see vinode_remove_interval
 * for explanation.
 */
static bool
is_interval_contained_by_block(struct pmemfile_block_desc *block,
		uint64_t start, uint64_t len)
{
	return block->offset < start &&
		(block->offset + block->size) > (start + len);
}


/*
 * is_block_at_right_edge -- see vinode_remove_interval
 * for explanation.
 */
static bool
is_block_at_right_edge(struct pmemfile_block_desc *block,
		uint64_t start, uint64_t len)
{
	ASSERT(!is_block_contained_by_interval(block, start, len));

	return block->offset + block->size > start + len;
}

/*
 * vinode_remove_interval - punch a hole in a file - possibly at the end of
 * a file.
 *
 * From the Linux man page fallocate(2):
 *
 * "Deallocating file space
 *   Specifying the FALLOC_FL_PUNCH_HOLE flag (available since Linux 2.6.38) in
 *   mode deallocates space (i.e., creates a hole) in the byte range starting at
 *   offset and continuing for len bytes.  Within the specified range, partial
 *   filesystem blocks are zeroed, and whole filesystem blocks are removed from
 *   the file.  After a successful call, subsequent reads from this range will
 *   return zeroes."
 *
 *
 *
 *
 *          _____offset                offset + len____
 *         |                                           |
 *         |                                           |
 * ----+---+--------+------------+------------+--------+----+----
 *     |   block #1 |  block #2  |   block #3 |   block #4  |
 *     |   data     |  data      |   data     |   data      |
 *  ---+---+--------+------------+------------+-------------+---
 *         | memset | deallocate | deallocate | memset |
 *         | zero   | block #2   | block #3   | zero   |
 *         |        |            |            |        |
 *         +--------+------------+------------+--------+
 *
 * Note: The zeroed file contents at the left edge in the above drawing
 * must be snapshotted. Without doing this, a failed transaction can leave
 * the file contents in a inconsistent state, e.g.:
 * 1) pmemfile_ftruncate is called in order to make a file smaller,
 * 2) a pmemobj transaction is started
 * 3) some bytes are zeroed at the end of a file
 * 4) the transaction fails before commit
 *
 * At this point, the file size is not changed, but the corresponding file
 * contents would remain zero bytes, if they were not snapshotted.
 */
static void
vinode_remove_interval(struct pmemfile_vinode *vinode,
			uint64_t offset, uint64_t len)
{
	ASSERT(len > 0);

	struct pmemfile_block_desc *block =
			find_block(vinode, offset + len - 1);

	while (block != NULL && block->offset + block->size > offset) {
		if (is_block_contained_by_interval(block, offset, len)) {
			/*
			 * Deallocate the whole block, if it is wholly contained
			 * by the specified interval.
			 *
			 *   offset                          offset + len
			 *   |                                |
			 * --+-------+-------+----------------+-----
			 *           | block |
			 */
			ctree_remove(vinode->blocks, block->offset, 1);
			block = block_list_remove(vinode, block);

		} else if (is_interval_contained_by_block(block, offset, len)) {
			/*
			 * No block is deallocated, but the corresponding
			 * interval in block->data is should be cleared.
			 *
			 *          offset    offset + len
			 *          |         |
			 * -----+---+---------+--+-----
			 *      |    block       |
			 */
			if (is_block_data_initialized(block)) {
				uint64_t block_offset = offset - block->offset;

				pmemobj_tx_add_range(block->data.oid,
				    block_offset, len);
				memset(D_RW(block->data) + block_offset, 0,
				    len);
			}

			/* definitely handled the whole interval already */
			break;

		} else if (is_block_at_right_edge(block, offset, len)) {
			/*
			 *  offset                          offset + len
			 *   |                                |
			 * --+----------------------------+---+---+
			 *                                | block |
			 *                                +---+---+
			 *                                |   |
			 *                                +---+
			 *                                 intersection
			 */

			if (is_block_data_initialized(block))
				TX_MEMSET(D_RW(block->data), 0,
				    offset + len - block->offset);

			block = D_RW(block->prev);
		} else {
			/*
			 *    offset                          offset + len
			 *     |                                |
			 * -+--+--------------------------------+----
			 *  | block |
			 *  +--+----+
			 *     |    |
			 *     +----+
			 *      intersection
			 */

			if (is_block_data_initialized(block)) {
				uint64_t block_offset = offset - block->offset;
				uint64_t zero_len = block->size - block_offset;

				pmemobj_tx_add_range(block->data.oid,
				    block_offset, zero_len);
				memset(D_RW(block->data) + block_offset, 0,
				    zero_len);
			}

			block = D_RW(block->prev);
		}
	}
}

/*
 * vinode_truncate -- changes file size to size
 *
 * Should only be called inside pmemobj transactions.
 */
void
vinode_truncate(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		uint64_t size)
{
	struct pmemfile_inode *inode = vinode->inode;

	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	if (vinode->blocks == NULL) {
		int err = vinode_rebuild_block_tree(vinode);
		if (err)
			pmemfile_tx_abort(err);
	}

	cb_push_front(TX_STAGE_ONABORT,
		(cb_basic)vinode_destroy_data_state,
		vinode);

	/*
	 * Might need to handle the special case where size == 0.
	 * Setting all the next and prev fields is pointless, when all the
	 * blocks are removed.
	 */
	vinode_remove_interval(vinode, size, UINT64_MAX - size);
	if (inode->size < size)
		vinode_allocate_interval(pfp, vinode,
		    inode->size, size - inode->size);

	if (inode->size != size) {
		TX_ADD_DIRECT(&inode->size);
		inode->size = size;

		struct pmemfile_time tm;
		get_current_time(&tm);
		TX_SET_DIRECT(inode, mtime, tm);
	}
}

int
vinode_fallocate(PMEMfilepool *pfp, struct pmemfile_vinode *vinode, int mode,
		uint64_t offset, uint64_t length)
{
	int error = 0;

	if (!vinode_is_regular_file(vinode))
		return EBADF;

	uint64_t off_plus_len = offset + length;

	if (mode & PMEMFILE_FL_PUNCH_HOLE)
		narrow_to_full_pages(&offset, &length);
	else
		expand_to_full_pages(&offset, &length);

	if (length == 0)
		return 0;

	vinode_snapshot(vinode);

	if (vinode->blocks == NULL) {
		error = vinode_rebuild_block_tree(vinode);
		if (error)
			return error;
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (mode & PMEMFILE_FL_PUNCH_HOLE) {
			ASSERT(mode & PMEMFILE_FL_KEEP_SIZE);
			vinode_remove_interval(vinode, offset, length);
		} else {
			vinode_allocate_interval(pfp, vinode, offset, length);
			if ((mode & PMEMFILE_FL_KEEP_SIZE) == 0) {
				if (vinode->inode->size < off_plus_len) {
					TX_ADD_DIRECT(&vinode->inode->size);
					vinode->inode->size = off_plus_len;
				}
			}
		}
	} TX_ONABORT {
		error = errno;
		vinode_restore_on_abort(vinode);
	} TX_END

	return error;
}

/*
 * vinode_snapshot
 * Saves some volatile state in vinode, that can be altered during a
 * transaction. These volatile data are not restored by pmemobj upon
 * transaction abort.
 */
void
vinode_snapshot(struct pmemfile_vinode *vinode)
{
	vinode->snapshot.first_free_block = vinode->first_free_block;
	vinode->snapshot.first_block = vinode->first_block;
}

/*
 * vinode_restore_on_abort - vinode_snapshot's counterpart
 * This must be added to abort handlers, where vinode_snapshot was
 * called in the transaction.
 */
void
vinode_restore_on_abort(struct pmemfile_vinode *vinode)
{
	vinode->first_free_block = vinode->snapshot.first_free_block;
	vinode->first_block = vinode->snapshot.first_block;

	/*
	 * The ctree is not restored here. It is rebuilt the next
	 * time the vinode is used.
	 */
	if (vinode->blocks) {
		ctree_delete(vinode->blocks);
		vinode->blocks = NULL;
	}
}
