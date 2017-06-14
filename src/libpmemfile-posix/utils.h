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

#ifndef PMEMFILE_UTILS_H
#define PMEMFILE_UTILS_H

#include "inode.h"
#include "layout.h"
#include "libpmemfile-posix.h"

#define ASSERT_IN_TX() ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK)
#define ASSERT_NOT_IN_TX() ASSERTeq(pmemobj_tx_stage(), TX_STAGE_NONE)

static inline pf_noreturn void
pmemfile_tx_abort(int err)
{
	pmemobj_tx_abort(err);
	__builtin_unreachable();
}

/*
 * The size of data allocated for each block is a positive integer multiple
 * of BLOCK_ALIGNMENT.
 *
 * XXX: The current code can read from / write to blocks with any positive size,
 * any offset alignment, so this information doesn't necessarily have to be
 * part of the on-media layout.
 * But later the code might (probably will) depend on this.
 */
#define MIN_BLOCK_SIZE ((size_t)0x1000)

#define BLOCK_ALIGNMENT ((size_t)0x1000)

COMPILE_ERROR_ON(MIN_BLOCK_SIZE % BLOCK_ALIGNMENT != 0);

#define MAX_BLOCK_SIZE (UINT32_MAX - (UINT32_MAX % BLOCK_ALIGNMENT))

static inline size_t
block_rounddown(size_t n)
{
	return n & ~(BLOCK_ALIGNMENT - 1);
}

static inline size_t
block_roundup(size_t n)
{
	return block_rounddown(n + BLOCK_ALIGNMENT - 1);
}

void *pmemfile_direct(PMEMfilepool *pfp, PMEMoid oid);

#define PF_RW(pfp, o) (\
{__typeof__(o) _o; _o._type = NULL; (void)_o;\
(__typeof__(*(o)._type) *)pmemfile_direct(pfp, (o).oid); })
#define PF_RO(pfp, o) \
	((const __typeof__(*(o)._type) *)pmemfile_direct(pfp, (o).oid))

int get_current_time(struct pmemfile_time *t);
void tx_get_current_time(struct pmemfile_time *t);

bool is_zeroed(const void *addr, size_t len);

int str_compare(const char *s1, const char *s2, size_t s2n);
bool str_contains(const char *str, size_t len, char c);
bool more_than_1_component(const char *path);
size_t component_length(const char *path);

char *pmfi_strndup(const char *c, size_t len);

#ifdef DEBUG
const char *pmfi_path(struct pmemfile_vinode *vinode);
#else
static inline const char *pmfi_path(struct pmemfile_vinode *vinode)
{
	(void) vinode;
	return NULL;
}
#endif

void expand_to_full_pages(uint64_t *offset, uint64_t *length);
void narrow_to_full_pages(uint64_t *offset, uint64_t *length);

#endif
