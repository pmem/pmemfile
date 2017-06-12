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

/*
 * getdents.c -- pmemfile_getdents* implementation
 */

#include <limits.h>

#include "file.h"
#include "inode.h"
#include "internal.h"
#include "libpmemfile-posix.h"
#include "out.h"

#define DIRENT_ID_MASK 0xffffffffULL

#define DIR_ID(offset) ((offset) >> 32)
#define DIRENT_ID(offset) ((offset) & DIRENT_ID_MASK)

/*
 * file_seek_dir - translates between file->offset and dir/dirent
 *
 * returns 0 on EOF
 * returns !0 on successful translation
 */
static int
file_seek_dir(PMEMfilepool *pfp, PMEMfile *file, struct pmemfile_dir **dir,
		unsigned *dirent)
{
	struct pmemfile_inode *inode = file->vinode->inode;

	if (file->offset == 0) {
		file->dir_pos.dir = &inode->file_data.dir;
		file->dir_pos.dir_id = 0;

		*dir = file->dir_pos.dir;
	} else if (DIR_ID(file->offset) == file->dir_pos.dir_id) {
		*dir = file->dir_pos.dir;
		if (*dir == NULL)
			return 0;
	} else {
		*dir = &inode->file_data.dir;

		unsigned dir_id = 0;
		while (DIR_ID(file->offset) != dir_id) {
			if (TOID_IS_NULL((*dir)->next))
				return 0;
			*dir = D_RW((*dir)->next);
			++dir_id;
		}

		file->dir_pos.dir = *dir;
		file->dir_pos.dir_id = dir_id;
	}
	*dirent = DIRENT_ID(file->offset);

	while (*dirent >= (*dir)->num_elements) {
		if (TOID_IS_NULL((*dir)->next))
			return 0;

		*dirent -= (*dir)->num_elements;
		*dir = D_RW((*dir)->next);

		file->dir_pos.dir = *dir;
		file->dir_pos.dir_id++;
	}

	file->offset = ((size_t)file->dir_pos.dir_id) << 32 | *dirent;

	return 1;
}

/*
 * inode_type - returns inode type, as returned by getdents
 */
static char
inode_type(const struct pmemfile_inode *inode)
{
	if (inode_is_regular_file(inode))
		return PMEMFILE_DT_REG;

	if (inode_is_symlink(inode))
		return PMEMFILE_DT_LNK;

	if (inode_is_dir(inode))
		return PMEMFILE_DT_DIR;

	ASSERT(0);
	return PMEMFILE_DT_UNKNOWN;
}

/*
 * align_dirent_size - aligns dirent size to 8 bytes and returns alignment
 */
static unsigned short
align_dirent_size(unsigned short *slen)
{
	unsigned short alignment = (unsigned short)(8 - (*slen & 7));
	if (alignment == 8)
		alignment = 0;
	*slen = (unsigned short)(*slen + alignment);
	return alignment;
}

/*
 * get_next_dirent_off - returns (lseek) offset of next directory entry
 */
static uint64_t
get_next_dirent_off(PMEMfile *file, struct pmemfile_dir *dir,
		unsigned dirent_id)
{
	uint64_t next_off = file->offset + 1;
	if (dirent_id + 1 >= dir->num_elements)
		next_off = ((next_off >> 32) + 1) << 32;
	return next_off;
}

/*
 * fill_dirent32 -- fills data with dirent information using 32-bit getdents ABI
 */
static unsigned short
fill_dirent32(PMEMfilepool *pfp, struct pmemfile_dirent *dirent,
		uint64_t next_off, unsigned left, char *data)
{
	size_t namelen = strlen(dirent->name);
	/* minimum size required */
	unsigned short slen = (unsigned short)
			(8 /* sizeof d_ino */ +
			8 /* sizeof d_off */ +
			2 /* sizeof d_reclen */ +
			namelen + 1 /* strlen(d_name) + 1 */ +
			1 /* sizeof d_type */);
	/* add for the whole structure to be 8 bytes aligned */
	unsigned short alignment = align_dirent_size(&slen);

	if (slen > left)
		return 0;

	COMPILE_ERROR_ON(sizeof(dirent->inode.oid.off) != 8);
	memcpy(data, &dirent->inode.oid.off, 8);
	data += 8;

	COMPILE_ERROR_ON(sizeof(next_off) != 8);
	memcpy(data, &next_off, 8);
	data += 8;

	COMPILE_ERROR_ON(sizeof(slen) != 2);
	memcpy(data, &slen, 2);
	data += 2;

	memcpy(data, dirent->name, namelen + 1);
	data += namelen + 1;

	while (alignment--)
		*data++ = 0;

	COMPILE_ERROR_ON(sizeof(inode_type(D_RO(dirent->inode))) != 1);
	*data++ = inode_type(D_RO(dirent->inode));

	return slen;
}

/*
 * fill_dirent64 -- fills data with dirent information using 64-bit getdents ABI
 */
static unsigned short
fill_dirent64(PMEMfilepool *pfp, struct pmemfile_dirent *dirent,
		uint64_t next_off, unsigned left, char *data)
{
	size_t namelen = strlen(dirent->name);
	/* minimum size required */
	unsigned short slen = (unsigned short)
			(8 /* sizeof d_ino */ +
			8 /* sizeof d_off */ +
			2 /* sizeof d_reclen */ +
			1 /* sizeof d_type */ +
			namelen + 1 /* strlen(d_name) + 1 */);
	/* add for the whole structure to be 8 bytes aligned */
	unsigned short alignment = align_dirent_size(&slen);

	if (slen > left)
		return 0;

	COMPILE_ERROR_ON(sizeof(dirent->inode.oid.off) != 8);
	memcpy(data, &dirent->inode.oid.off, 8);
	data += 8;

	COMPILE_ERROR_ON(sizeof(next_off) != 8);
	memcpy(data, &next_off, 8);
	data += 8;

	COMPILE_ERROR_ON(sizeof(slen) != 2);
	memcpy(data, &slen, 2);
	data += 2;

	COMPILE_ERROR_ON(sizeof(inode_type(D_RO(dirent->inode))) != 1);
	*data++ = inode_type(D_RO(dirent->inode));

	memcpy(data, dirent->name, namelen + 1);
	data += namelen + 1;

	while (alignment--)
		*data++ = 0;

	return slen;
}

typedef unsigned short (*fill_dirent_type)(PMEMfilepool *pfp,
		struct pmemfile_dirent *dirent, uint64_t next_off,
		unsigned left, char *data);

/*
 * pmemfile_getdents_worker -- traverses directory and fills dirent information
 */
static int
pmemfile_getdents_worker(PMEMfilepool *pfp, PMEMfile *file, char *data,
		unsigned count, fill_dirent_type fill_dirent)
{
	struct pmemfile_dir *dir;
	unsigned dirent_id;

	if (file_seek_dir(pfp, file, &dir, &dirent_id) == 0)
		return 0;

	int read1 = 0;

	while (true) {
		if (dirent_id >= dir->num_elements) {
			if (TOID_IS_NULL(dir->next))
				break;

			dir = D_RW(dir->next);
			file->dir_pos.dir = dir;
			file->dir_pos.dir_id++;
			dirent_id = 0;
			file->offset = ((size_t)file->dir_pos.dir_id) << 32 | 0;
		}
		ASSERT(dir != NULL);

		struct pmemfile_dirent *dirent = &dir->dirents[dirent_id];
		if (TOID_IS_NULL(dirent->inode)) {
			++dirent_id;
			++file->offset;
			continue;
		}

		uint64_t next_off = get_next_dirent_off(file, dir, dirent_id);

		unsigned short slen = fill_dirent(pfp, dirent, next_off,
				count - (unsigned)read1, data);
		if (slen == 0)
			break;

		data += slen;
		read1 += slen;

		++dirent_id;
		++file->offset;
	}

	return read1;
}

/*
 * pmemfile_getdents_generic -- generic implementation of pmemfile_getdents
 * which allows caller to pick ABI (fill_dirent)
 */
static int
pmemfile_getdents_generic(PMEMfilepool *pfp, PMEMfile *file, char *data,
		unsigned count, fill_dirent_type fill_dirent)
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

	if (!data) {
		LOG(LUSR, "NULL data");
		errno = EFAULT;
		return -1;
	}

	struct pmemfile_vinode *vinode = file->vinode;

	ASSERT(vinode != NULL);
	if (!vinode_is_dir(vinode)) {
		errno = ENOTDIR;
		return -1;
	}

	if (!(file->flags & PFILE_READ)) {
		errno = EBADF;
		return -1;
	}

	if ((int)count < 0)
		count = INT_MAX;

	int bytes_read = 0;

	os_mutex_lock(&file->mutex);
	os_rwlock_rdlock(&vinode->rwlock);

	bytes_read = pmemfile_getdents_worker(pfp, file, data, count,
			fill_dirent);
	ASSERT(bytes_read >= 0);

	os_rwlock_unlock(&vinode->rwlock);
	os_mutex_unlock(&file->mutex);

	ASSERT((unsigned)bytes_read <= count);
	return bytes_read;
}

int
pmemfile_getdents(PMEMfilepool *pfp, PMEMfile *file,
			struct linux_dirent *dirp, unsigned count)
{
	return pmemfile_getdents_generic(pfp, file, (char *)dirp, count,
			fill_dirent32);
}

int
pmemfile_getdents64(PMEMfilepool *pfp, PMEMfile *file,
			struct linux_dirent64 *dirp, unsigned count)
{
	return pmemfile_getdents_generic(pfp, file, (char *)dirp, count,
			fill_dirent64);
}
