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
#include "data.h"
#include "inode.h"
#include "internal.h"
#include "locks.h"
#include "out.h"
#include "pool.h"
#include "os_thread.h"
#include "util.h"
#include "valgrind_internal.h"
#include "ctree.h"

/*
 * block_cache_insert_block -- inserts block into the tree
 */
static void
block_cache_insert_block(struct ctree *c, struct pmemfile_block *block)
{
	ctree_insert_unlocked(c, block->offset, (uintptr_t)block);
}

static struct pmemfile_block *
find_last_block(const struct pmemfile_vinode *vinode)
{
	uint64_t off = UINT64_MAX;
	return (void *)(uintptr_t)ctree_find_le_unlocked(vinode->blocks, &off);
}

/*
 * vinode_rebuild_block_tree -- rebuilds runtime tree of blocks
 */
static void
vinode_rebuild_block_tree(struct pmemfile_vinode *vinode)
{
	struct ctree *c = ctree_new();
	if (!c)
		return;
	struct pmemfile_block_array *block_array =
			&vinode->inode->file_data.blocks;
	struct pmemfile_block *first = NULL;

	while (block_array != NULL) {
		for (unsigned i = 0; i < block_array->length; ++i) {
			struct pmemfile_block *block = &block_array->blocks[i];

			if (block->size == 0)
				break;

			block_cache_insert_block(c, block);
			if (first == NULL || block->offset < first->offset)
				first = block;
		}

		block_array = D_RW(block_array->next);
	}

	vinode->first_block = first;
	vinode->blocks = c;
}

/*
 * is_offset_in_block -- check if the given offset is in the range
 * specified by the block metadata
 */
static bool
is_offset_in_block(const struct pmemfile_block *block, uint64_t offset)
{
	if (block == NULL)
		return false;

	return block->offset <= offset && offset < block->offset + block->size;
}

static bool
is_block_data_initialized(const struct pmemfile_block *block)
{
	ASSERT(block != NULL);

	return (block->flags & BLOCK_INITIALIZED) != 0;
}

/*
 * file_find_block -- look up block metadata with the highest offset
 * lower than or equal to the offset argument
 *
 * using the block_pointer_cache field in struct pmemfile_file
 */
static struct pmemfile_block *
file_find_block(struct pmemfile_file *file, uint64_t offset)
{
	if (is_offset_in_block(file->block_pointer_cache, offset))
		return file->block_pointer_cache;

	struct pmemfile_block *block;

	block = (void *)(uintptr_t)ctree_find_le_unlocked(file->vinode->blocks,
	    &offset);

	if (block != NULL)
		file->block_pointer_cache = block;

	return block;
}

/*
 * find_block -- look up block metadata with the highest offset
 * lower than or equal to the offset argument
 */
static struct pmemfile_block *
find_block(struct pmemfile_vinode *vinode, uint64_t off)
{
	return (void *)(uintptr_t)ctree_find_le_unlocked(vinode->blocks, &off);
}

/*
 * vinode_destroy_data_state -- destroys file state related to data
 */
void
vinode_destroy_data_state(struct pmemfile_vinode *vinode)
{
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
		struct pmemfile_block *block,
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

static bool
is_append(struct pmemfile_vinode *vinode, struct pmemfile_inode *inode,
		uint64_t offset, uint64_t size)
{
	if (inode->size >= offset + size)
		return false; /* not writing past file size */

	struct pmemfile_block *block = find_last_block(vinode);

	/* Writing past the last allocated block? */

	if (block == NULL)
		return true;

	return (block->offset + block->size) < (offset + size);
}

static uint64_t
overallocate_size(uint64_t count)
{
	if (count <= 4096)
		return 16 * 1024;
	else if (count <= 64 * 1024)
		return 256 * 1024;
	else if (count <= 1024 * 1024)
		return 4 * 1024 * 1024;
	else if (count <= 64 * 1024 * 1024)
		return 64 * 1024 * 1024;
	else
		return count;
}

static void
file_allocate_range(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		uint64_t offset, uint64_t size)
{
	ASSERT(size > 0);
	ASSERT(offset + size > offset);

	struct pmemfile_inode *inode = vinode->inode;

	bool over = pmemfile_overallocate_on_append &&
	    is_append(vinode, inode, offset, size);

	if (over)
		size = overallocate_size(size);

	/* align the offset */
	size += offset % FILE_PAGE_SIZE;
	offset -= offset % FILE_PAGE_SIZE;

	/* align the size */
	size = page_roundup(size);

	struct pmemfile_block *block = find_block(vinode, offset);

	do {
		if (is_offset_in_block(block, offset)) {
			/* Not in a hole */

			uint64_t available = block->size;
			available -= offset - block->offset;

			if (available >= size)
				return;

			offset += available;
			size -= available;
		} else if (block == NULL && vinode->first_block == NULL) {
			/* File size is zero */

			block = block_list_insert_after(vinode, NULL);
			block->offset = offset;
			file_allocate_block_data(pfp, block, size, over);
			block_cache_insert_block(vinode->blocks, block);
		} else if (block == NULL && vinode->first_block != NULL) {
			/* In a hole before the first block */

			size_t count = size;
			uint64_t first_offset = vinode->first_block->offset;

			if (offset + count > first_offset)
				count = (uint32_t)(first_offset - offset);

			block = block_list_insert_after(vinode, NULL);
			block->offset = offset;
			file_allocate_block_data(pfp, block, count, false);
			block_cache_insert_block(vinode->blocks, block);
		} else if (TOID_IS_NULL(block->next)) {
			/* After the last allocated block */

			block = block_list_insert_after(vinode, block);
			block->offset = offset;
			file_allocate_block_data(pfp, block, size, over);
			block_cache_insert_block(vinode->blocks, block);
		} else {
			/* In a hole between two allocated blocks */

			struct pmemfile_block *next = D_RW(block->next);

			/* How many bytes in this hole can be used? */
			uint64_t hole_count = next->offset - offset;

			/* Are all those bytes needed? */
			if (hole_count > size)
				hole_count = size;

			/* create a new block between previous and next */

			block = block_list_insert_after(vinode, block);
			block->offset = offset;
			file_allocate_block_data(pfp, block, hole_count, false);
			block_cache_insert_block(vinode->blocks, block);

			if (block->size > hole_count)
				block->size = (uint32_t)hole_count;
		}
	} while (size > 0);
}

static struct pmemfile_block *
find_following_block(PMEMfile * file,
	struct pmemfile_block *block)
{
	if (block != NULL)
		return D_RW(block->next);
	else
		return file->vinode->first_block;
}

enum cpy_direction { read_from_blocks, write_to_blocks };

static void
read_block_range(const struct pmemfile_block *block,
	uint64_t offset, uint64_t len, char *buf)
{
	ASSERT(len > 0);
	ASSERT(block == NULL || offset < block->size);
	ASSERT(block == NULL || offset + len <= block->size);

	/* block == NULL means reading from a hole in a sparse file */

	/*
	 * !is_block_data_initialized(block) means reading from an
	 * fallocat-ed region in a file, a region that was allocated,
	 * but never initialized.
	 */

	if ((block != NULL) && is_block_data_initialized(block)) {
		const char *read_from = D_RO(block->data) + offset;
		memcpy(buf, read_from, len);
	} else {
		memset(buf, 0, len);
	}
}

static void
write_block_range(PMEMfilepool *pfp, struct pmemfile_block *block,
	uint64_t offset, uint64_t len, const char *buf)
{
	ASSERT(block != NULL);
	ASSERT(len > 0);
	ASSERT(offset < block->size);
	ASSERT(offset + len <= block->size);

	char *data = D_RW(block->data);

	if ((block->flags & BLOCK_INITIALIZED) == 0) {
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

static void
iterate_on_file_range(PMEMfilepool *pfp, PMEMfile *file,
    uint64_t offset, uint64_t len, char *buf, enum cpy_direction dir)
{
	struct pmemfile_block *block = file_find_block(file, offset);

	while (len > 0) {
		/* Remember the pointer to block used last time */
		if (block != NULL)
			file->block_pointer_cache = block;
		else
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

			struct pmemfile_block *next_block =
			    find_following_block(file, block);

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
		 * blocks are special, in the sense that not necesseraly
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
		block = D_RW(block->next);
	}
}

/*
 * file_write -- writes to file
 */
static void
file_write(PMEMfilepool *pfp, PMEMfile *file, struct pmemfile_inode *inode,
		const char *buf, size_t count)
{
	ASSERT(count > 0);

	/*
	 * Three steps:
	 * - Append new blocks to end of the file ( optionally )
	 * - Zero Fill some new blocks, in case the file is extended by
	 *   writing to the file after seeking past file size ( optionally )
	 * - Copy the data from the users buffer
	 */

	file_allocate_range(pfp, file->vinode, file->offset, count);

	uint64_t original_size = inode->size;
	uint64_t new_size = inode->size;

	if (file->offset + count > original_size)
		new_size = file->offset + count;

	/* All blocks needed for writing are properly allocated at this point */

	iterate_on_file_range(pfp, file, file->offset, count, (char *)buf,
	    write_to_blocks);

	if (new_size != original_size) {
		TX_ADD_FIELD_DIRECT(inode, size);
		inode->size = new_size;
	}
}

static ssize_t
pmemfile_write_locked(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
		size_t count)
{
	LOG(LDBG, "file %p buf %p count %zu", file, buf, count);

	if (!vinode_is_regular_file(file->vinode)) {
		errno = EINVAL;
		return -1;
	}

	if (!(file->flags & PFILE_WRITE)) {
		errno = EBADF;
		return -1;
	}

	if ((ssize_t)count < 0)    /* Normally this will still   */
		count = SSIZE_MAX; /* try to write 2^63 bytes... */

	if (file->offset + count < file->offset) /* overflow check */
		count = SIZE_MAX - file->offset;

	if (count == 0)
		return 0;

	int error = 0;

	struct pmemfile_vinode *vinode = file->vinode;
	struct pmemfile_inode *inode = vinode->inode;

	os_rwlock_wrlock(&vinode->rwlock);

	vinode_snapshot(vinode);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!vinode->blocks)
			vinode_rebuild_block_tree(vinode);

		if (file->flags & PFILE_APPEND)
			file->offset = inode->size;

		file_write(pfp, file, inode, buf, count);

		if (count > 0) {
			struct pmemfile_time tm;
			file_get_time(&tm);
			TX_SET_DIRECT(inode, mtime, tm);
		}
	} TX_ONABORT {
		error = errno;
		vinode_restore_on_abort(vinode);
	} TX_ONCOMMIT {
		file->offset += count;
	} TX_END

	os_rwlock_unlock(&vinode->rwlock);

	if (error) {
		errno = error;
		return -1;
	}

	return (ssize_t)count;
}

/*
 * pmemfile_write -- writes to file
 */
ssize_t
pmemfile_write(PMEMfilepool *pfp, PMEMfile *file, const void *buf, size_t count)
{
	ssize_t ret;

	os_mutex_lock(&file->mutex);
	ret = pmemfile_write_locked(pfp, file, buf, count);
	os_mutex_unlock(&file->mutex);

	return ret;
}

/*
 * file_read -- reads file
 */
static size_t
file_read(PMEMfilepool *pfp, PMEMfile *file, struct pmemfile_inode *inode,
		char *buf, size_t count)
{
	uint64_t size = inode->size;

	/*
	 * Start reading at file->offset, stop reading
	 * when end of file is reached, or count bytes were read.
	 * The following two branches compute how many bytes are
	 * going to be read.
	 */
	if (file->offset >= size)
		return 0; /* EOF already */

	if (size - file->offset < count)
		count = size - file->offset;

	iterate_on_file_range(pfp, file, file->offset, count, buf,
	    read_from_blocks);

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

static ssize_t
pmemfile_read_locked(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count)
{
	LOG(LDBG, "file %p buf %p count %zu", file, buf, count);

	if (!vinode_is_regular_file(file->vinode)) {
		errno = EINVAL;
		return -1;
	}

	if (!(file->flags & PFILE_READ)) {
		errno = EBADF;
		return -1;
	}

	if ((ssize_t)count < 0)
		count = SSIZE_MAX;

	size_t bytes_read = 0;

	struct pmemfile_vinode *vinode = file->vinode;
	struct pmemfile_inode *inode = vinode->inode;

	os_rwlock_rdlock(&vinode->rwlock);
	while (!vinode->blocks) {
		os_rwlock_unlock(&vinode->rwlock);
		os_rwlock_wrlock(&vinode->rwlock);
		if (!vinode->blocks)
			vinode_rebuild_block_tree(vinode);
		os_rwlock_unlock(&vinode->rwlock);
		os_rwlock_rdlock(&vinode->rwlock);
	}

	bytes_read = file_read(pfp, file, inode, buf, count);

	bool update_atime = !(file->flags & PFILE_NOATIME);
	struct pmemfile_time tm;

	if (update_atime) {
		struct pmemfile_time tm1d;
		file_get_time(&tm);
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


	file->offset += bytes_read;

	ASSERT(bytes_read <= count);
	return (ssize_t)bytes_read;
}

/*
 * pmemfile_read -- reads file
 */
ssize_t
pmemfile_read(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count)
{
	ssize_t ret;

	os_mutex_lock(&file->mutex);
	ret = pmemfile_read_locked(pfp, file, buf, count);
	os_mutex_unlock(&file->mutex);

	return ret;
}

/*
 * pmemfile_lseek_locked -- changes file current offset
 */
static off_t
pmemfile_lseek_locked(PMEMfilepool *pfp, PMEMfile *file, off_t offset,
		int whence)
{
	(void) pfp;

	LOG(LDBG, "file %p offset %lu whence %d", file, offset, whence);

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
	off_t ret;
	int new_errno = EINVAL;

	switch (whence) {
		case PMEMFILE_SEEK_SET:
			ret = offset;
			break;
		case PMEMFILE_SEEK_CUR:
			ret = (off_t)file->offset + offset;
			break;
		case PMEMFILE_SEEK_END:
			os_rwlock_rdlock(&vinode->rwlock);
			ret = (off_t)inode->size + offset;
			os_rwlock_unlock(&vinode->rwlock);
			break;
		case PMEMFILE_SEEK_DATA:
			os_rwlock_rdlock(&vinode->rwlock);
			if (offset < 0) {
				ret = 0;
			} else if ((uint64_t)offset > inode->size) {
				ret = -1;
				new_errno = ENXIO;
			} else {
				ret = offset;
			}
			os_rwlock_unlock(&vinode->rwlock);
			break;
		case PMEMFILE_SEEK_HOLE:
			os_rwlock_rdlock(&vinode->rwlock);
			if ((uint64_t)offset > inode->size) {
				ret = -1;
				new_errno = ENXIO;
			} else {
				ret = (off_t)inode->size;
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
off_t
pmemfile_lseek(PMEMfilepool *pfp, PMEMfile *file, off_t offset, int whence)
{
	COMPILE_ERROR_ON(sizeof(offset) != 8);
	off_t ret;

	os_mutex_lock(&file->mutex);
	ret = pmemfile_lseek_locked(pfp, file, offset, whence);
	os_mutex_unlock(&file->mutex);

	return ret;
}

ssize_t
pmemfile_pread(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count,
		off_t offset)
{
	/* XXX this is hacky implementation */
	ssize_t ret;
	os_mutex_lock(&file->mutex);

	size_t cur_off = file->offset;

	if (pmemfile_lseek_locked(pfp, file, offset, PMEMFILE_SEEK_SET) !=
			offset) {
		ret = -1;
		goto end;
	}

	ret = pmemfile_read_locked(pfp, file, buf, count);

	file->offset = cur_off;

end:
	os_mutex_unlock(&file->mutex);

	return ret;
}

ssize_t
pmemfile_pwrite(PMEMfilepool *pfp, PMEMfile *file, const void *buf,
		size_t count, off_t offset)
{
	/* XXX this is hacky implementation */
	ssize_t ret;
	os_mutex_lock(&file->mutex);

	size_t cur_off = file->offset;

	if (pmemfile_lseek_locked(pfp, file, offset, PMEMFILE_SEEK_SET) !=
			offset) {
		ret = -1;
		goto end;
	}

	ret = pmemfile_write_locked(pfp, file, buf, count);

	file->offset = cur_off;

end:
	os_mutex_unlock(&file->mutex);

	return ret;
}

/*
 * vinode_truncate -- changes file size to 0
 */
void
vinode_truncate(struct pmemfile_vinode *vinode)
{
	struct pmemfile_inode *inode = vinode->inode;

	if (vinode->blocks == NULL)
		vinode_rebuild_block_tree(vinode);

	struct pmemfile_block *block = find_last_block(vinode);

	while (block != NULL)
		block = block_list_remove(vinode, block);

	TX_ADD_DIRECT(&inode->size);
	inode->size = 0;

	struct pmemfile_time tm;
	file_get_time(&tm);
	TX_SET_DIRECT(inode, mtime, tm);

	// we don't have to rollback destroy of data state on abort, because
	// it will be rebuilded when it's needed
	vinode_destroy_data_state(vinode);
}

void
vinode_snapshot(struct pmemfile_vinode *vinode)
{
	vinode->snapshot.first_free_block = vinode->first_free_block;
	vinode->snapshot.first_block = vinode->first_block;
}

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
