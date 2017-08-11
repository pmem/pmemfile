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
#ifndef PMEMFILE_FILE_H
#define PMEMFILE_FILE_H

/*
 * Runtime state structures.
 */

#include <stdbool.h>
#include <stddef.h>
#include "inode.h"
#include "layout.h"
#include "os_thread.h"

#define PFILE_READ (1ULL << 0)
#define PFILE_WRITE (1ULL << 1)
#define PFILE_NOATIME (1ULL << 2)
#define PFILE_APPEND (1ULL << 3)
#define PFILE_PATH (1ULL << 4)

/* file handle */
struct pmemfile_file {
	/* volatile inode */
	struct pmemfile_vinode *vinode;

	/*
	 * Protects against changes to offset / position cache from multiple
	 * threads.
	 */
	os_mutex_t mutex;

	/*
	 * Counters corresponding to the modification counters in the vinode
	 * associated with this pmemfile_file. They help in the detection of
	 * vinode modifications since the last time the vinode's rwlock was held
	 * via a particular pmemfile_file instance.
	 *
	 * Only used for regular files.
	 */
	uint64_t last_data_modification_observed;
	uint64_t last_metadata_modification_observed;

	/* flags */
	uint64_t flags;

	/* requested/current position */
	size_t offset;

	/* current position cache, the latest block used */
	struct pmemfile_block_desc *block_pointer_cache;

	/* current position cache if directory */
	struct pmemfile_dir_pos {
		/* current directory list */
		struct pmemfile_dir *dir;

		/* id of the current directory list */
		unsigned dir_id;
	} dir_pos;
};

static inline bool
is_data_modification_indicated(const struct pmemfile_file *file)
{
	return file->last_data_modification_observed !=
		file->vinode->data_modification_counter;
}

static inline bool
is_metadata_modification_indicated(const struct pmemfile_file *file)
{
	return file->last_metadata_modification_observed !=
		file->vinode->metadata_modification_counter;
}

static inline bool
is_modification_indicated(const struct pmemfile_file *file)
{
	/*
	 * As of now, there is no data modification without metadata
	 * modification, thus checking the metadata modification counter
	 * is enough to detect any modification.
	 */
	return is_metadata_modification_indicated(file);
}

#endif
