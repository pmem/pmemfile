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
#ifndef PMEMFILE_INODE_ARRAY_H
#define PMEMFILE_INODE_ARRAY_H

#include "libpmemfile-posix.h"
#include "inode.h"
#include "layout.h"

void inode_array_add(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode_array) array,
		TOID(struct pmemfile_inode) tinode,
		struct pmemfile_inode_array **ins,
		unsigned *ins_idx);
void inode_array_unregister(PMEMfilepool *pfp,
		struct pmemfile_inode_array *cur,
		unsigned idx);

typedef void (*inode_cb)(PMEMfilepool *pfp, TOID(struct pmemfile_inode) inode);

void inode_array_traverse(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode_array) arr,
		inode_cb inode_cb);

void inode_array_free(TOID(struct pmemfile_inode_array) arr);

TOID(struct pmemfile_inode_array) inode_array_alloc(void);

bool inode_array_empty(TOID(struct pmemfile_inode_array) tarr);

bool inode_array_is_small(TOID(struct pmemfile_inode_array) tarr);

#endif
