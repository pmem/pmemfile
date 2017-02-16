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
 * core-util.c -- utility functions
 */

#include "inode.h"
#include "inode_array.h"
#include "locks.h"

/*
 * inode_array_add_single -- finds space for 1 inode in specified
 * array, inserts it there and returns success status
 */
static bool
inode_array_add_single(PMEMfilepool *pfp,
		struct pmemfile_inode_array *cur,
		struct pmemfile_vinode *vinode,
		struct pmemfile_inode_array **ins,
		unsigned *ins_idx)
{
	for (unsigned i = 0; i < NUMINODES_PER_ENTRY; ++i) {
		if (!TOID_IS_NULL(cur->inodes[i]))
			continue;

		mutex_tx_unlock_on_abort(&cur->mtx);

		TX_ADD_DIRECT(&cur->inodes[i]);
		cur->inodes[i] = vinode->inode;

		TX_ADD_DIRECT(&cur->used);
		cur->used++;

		if (ins)
			*ins = cur;
		if (ins_idx)
			*ins_idx = i;

		return true;
	}

	return false;
}

/*
 * inode_array_add -- adds inode to array, returns its position
 *
 * Must be called in transaction.
 */
void
inode_array_add(PMEMfilepool *pfp,
		TOID(struct pmemfile_inode_array) array,
		struct pmemfile_vinode *vinode,
		struct pmemfile_inode_array **ins,
		unsigned *ins_idx)
{
	bool found = false;
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	do {
		struct pmemfile_inode_array *cur = D_RW(array);

		pmemobj_mutex_lock_nofail(pfp->pop, &cur->mtx);

		if (cur->used < NUMINODES_PER_ENTRY)
			found = inode_array_add_single(pfp, cur,
					vinode, ins, ins_idx);

		bool modified = false;
		if (!found) {
			if (TOID_IS_NULL(cur->next)) {
				mutex_tx_unlock_on_abort(
						&cur->mtx);

				TX_SET_DIRECT(cur, next,
					TX_ZNEW(struct pmemfile_inode_array));
				D_RW(cur->next)->prev = array;

				modified = true;
			}

			array = cur->next;
		}

		if (found || modified)
			mutex_tx_unlock_on_commit(&cur->mtx);
		else
			pmemobj_mutex_unlock_nofail(pfp->pop, &cur->mtx);

	} while (!found);
}

/*
 * inode_array_unregister -- removes inode from specified place in
 * array
 *
 * Must be called in transaction.
 */
void
inode_array_unregister(PMEMfilepool *pfp,
		struct pmemfile_inode_array *cur,
		unsigned idx)
{
	mutex_tx_lock(pfp, &cur->mtx);

	ASSERT(cur->used > 0);

	TX_ADD_DIRECT(&cur->inodes[idx]);
	cur->inodes[idx] = TOID_NULL(struct pmemfile_inode);

	TX_ADD_DIRECT(&cur->used);
	cur->used--;

	mutex_tx_unlock_on_commit(&cur->mtx);

}
