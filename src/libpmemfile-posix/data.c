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

#include "block_array.h"
#include "ctree.h"
#include "data.h"
#include "internal.h"
#include "out.h"
#include "pool.h"
#include "valgrind_internal.h"
#include "utils.h"

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
	ASSERT_IN_TX();
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
int
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
bool
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
 * find_closest_block -- look up block metadata with the highest offset
 * lower than or equal to the offset argument
 */
struct pmemfile_block_desc *
find_closest_block(struct pmemfile_vinode *vinode, uint64_t off)
{
	return (void *)(uintptr_t)ctree_find_le(vinode->blocks, &off);
}

/*
 * find_closest_block_with_hint -- look up block metadata with the highest
 * offset lower than or equal to the offset argument
 *
 * using the proposed last_block
 */
struct pmemfile_block_desc *
find_closest_block_with_hint(struct pmemfile_vinode *vinode, uint64_t offset,
		struct pmemfile_block_desc *last_block)
{
	if (is_offset_in_block(last_block, offset))
		return last_block;

	return find_closest_block(vinode, offset);
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
	ASSERT_IN_TX();
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
void
vinode_allocate_interval(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		uint64_t offset, uint64_t size)
{
	ASSERT_IN_TX();
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
	struct pmemfile_block_desc *block = find_closest_block(vinode, offset);

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
	ASSERT_IN_TX();
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
struct pmemfile_block_desc *
iterate_on_file_range(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		struct pmemfile_block_desc *starting_block, uint64_t offset,
		uint64_t len, char *buf, enum cpy_direction dir)
{
	struct pmemfile_block_desc *block = starting_block;
	struct pmemfile_block_desc *last_block = starting_block;
	if (dir == write_to_blocks)
		ASSERT_IN_TX();

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
void
vinode_remove_interval(struct pmemfile_vinode *vinode,
			uint64_t offset, uint64_t len)
{
	ASSERT_IN_TX();
	ASSERT(len > 0);

	struct pmemfile_block_desc *block =
			find_closest_block(vinode, offset + len - 1);

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
