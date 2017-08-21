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
 * stats.c -- pmemfile_stats implementation
 */

#include "blocks.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "layout.h"
#include "utils.h"

static bool
cmp(uint32_t version, uint32_t requested_version)
{
	/* only compare 24 least significant bits - discard version number */
	return (version & 0xFFFFFF) == (requested_version & 0xFFFFFF);
}

static void
stats_header(PMEMfilepool *pfp, unsigned t, struct pmemfile_stats *stats)
{
	if (t == TOID_TYPE_NUM(struct pmemfile_inode))
		stats->inodes++;
	else if (t == TOID_TYPE_NUM(struct pmemfile_dir))
		stats->dirs++;
	else if (t == TOID_TYPE_NUM(struct pmemfile_block_array))
		stats->block_arrays++;
	else if (t == TOID_TYPE_NUM(struct pmemfile_inode_array))
		stats->inode_arrays++;
	else if (t == TOID_TYPE_NUM(char))
		stats->blocks++;
	else
		FATAL("unknown type %u", t);
}
static void
stats_alloc_class(PMEMfilepool *pfp, PMEMoid oid, struct pmemfile_stats *stats)
{
	size_t size = pmemobj_alloc_usable_size(oid);

	if (size == METADATA_BLOCK_SIZE) {
		uint32_t v = *((uint32_t *) pmemfile_direct(pfp, oid));

	if (cmp(v, PMEMFILE_INODE_VERSION(0)))
		stats->inodes++;
	else if (cmp(v, PMEMFILE_DIR_VERSION(0)))
		stats->dirs++;
	else if (cmp(v, PMEMFILE_BLOCK_ARRAY_VERSION(0)))
		stats->block_arrays++;
	else if (cmp(v, PMEMFILE_INODE_ARRAY_VERSION(0)))
		stats->inode_arrays++;
	} else if (data_block_info(size, MAX_BLOCK_SIZE)->size
				== size) {
			stats->blocks++;
	} else {
		FATAL("unknown block");
	}
}

/*
 * pmemfile_stats -- get pool statistics
 */
void
pmemfile_stats(PMEMfilepool *pfp, struct pmemfile_stats *stats)
{
	PMEMoid oid;
	stats->inodes = 0;
	stats->dirs = 0;
	stats->block_arrays = 0;
	stats->inode_arrays = 0;
	stats->blocks = 0;

	POBJ_FOREACH(pfp->pop, oid) {
		unsigned t = (unsigned)pmemobj_type_num(oid);

		if (t != 0)
			stats_header(pfp, t, stats);
		else
			stats_alloc_class(pfp, oid, stats);
	}
}
