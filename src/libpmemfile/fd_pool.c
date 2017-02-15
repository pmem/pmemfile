/*
 * Copyright 2016, Intel Corporation
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

#include "fd_pool.h"
#include "libsyscall_intercept_hook_point.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <fcntl.h>
#include <syscall.h>
#include <sys/mman.h>
#include <asm-generic/errno.h>
#include <stdlib.h>


/*
 * O(1) lookup: is fd allocated for the fd pool, i.e.:
 * it is either associated with a pmemfile, or just waiting in the pool.
 * Such file descriptors should not be handled by the kernel.
 */
static bool is_fd_allocated[PMEMFILE_MAX_FD + 1];

/*
 * A stack of file descriptors in the pool, not yet used for pmemfile.
 * This allows us to fetch a new fd in O(1), or to put an fd back into
 * the pool in O(1)
 */
static long fd_pool[PMEMFILE_MAX_FD + 1];

/*
 * allocated_count - how many file descriptors are fetch from the kernel with
 * open("/dev/null", O_RDONLY)
 *
 * Upon initialization, the fd_pool array contains all these fds.
 *
 * TODO: do we actually need to keep track of this?
 */
static size_t allocated_count;

/*
 * next_item_to_use - the index pointing to an element in the fd_pool array.
 * It is initialized to allocated_count - 1, and decreased each time an item is
 * used. Increased every time a previously used item item is placed back
 * into the pool. If this becomes negative, it means the pool is empty i.e.:
 * it might have a lot of fds allocated, but all of them are already used.
 */
static ptrdiff_t next_item_to_use = -1;

/*
 * During initialization fd_pool_create() preallocates at least
 * INIT_MIN_ALLOCATE_COUNT number of fds from the kernel, and at most
 * INIT_MAX_ALLOCATE_COUNT number of fds.
 *
 * The pool has initially
 * INIT_MIN_ALLOCATE_COUNT <= allocated_count <= INIT_MIN_ALLOCATE_COUNT
 * number of fds, and once it runs out of fds, it tries to allocate some
 * more. The constant ALLOCATE_CHUNK controls this.
 *
 * TODO: allow the user to set these from a config file.
 */
#define INIT_MIN_ALLOCATE_COUNT 0x80
#define INIT_MAX_ALLOCATE_COUNT 0x100
#define ALLOCATE_CHUNK 0x80

// What dummy path should the fds actually reference?
#define DUMMY_PATH "/dev/null"

static void
fetch_new_fds_from_kernel(size_t count)
{
	while (count > 0) {
		long fd;

		fd = syscall_no_intercept(SYS_open, DUMMY_PATH, O_RDONLY);

		if (fd < 0)
			return;

		if (fd > PMEMFILE_MAX_FD) {
			syscall_no_intercept(SYS_close, fd);
		}

		is_fd_allocated[fd] = true;
		fd_pool[++next_item_to_use] = fd;
		++allocated_count;

		--count;
	}
}

void
fd_pool_create(void)
{
	fetch_new_fds_from_kernel(INIT_MAX_ALLOCATE_COUNT);

	/*
	 * Fail hard, if for some reason can't open enough fds.
	 * The application might already a lot of fds open, or the
	 * fd limit is pretty low.
	 */
	if (allocated_count < INIT_MIN_ALLOCATE_COUNT)
		syscall_no_intercept(SYS_exit_group, 77);
}

// TODO: is this needed?
void
fd_pool_destroy(void)
{
	assert(next_item_to_use >= -1);
	assert((size_t)(next_item_to_use + 1) == allocated_count);

	while (allocated_count > 0) {
		long fd = fd_pool[allocated_count - 1];

		is_fd_allocated[fd] = false;
		syscall_no_intercept(SYS_close, fd);
		--allocated_count;
		--next_item_to_use;
	}
}

long
fd_pool_fetch_new_fd(void)
{
	if (next_item_to_use < 0)
		fetch_new_fds_from_kernel(ALLOCATE_CHUNK);

	if (next_item_to_use < 0)
		return -1;

	return fd_pool[next_item_to_use--];
}

void
fd_pool_release_fd(long fd)
{
	assert(allocated_count > 0);

	fd_pool[++next_item_to_use] = fd;
}

bool
fd_pool_has_allocated(long fd)
{
	if (fd >= 0 && fd <= PMEMFILE_MAX_FD)
		return __atomic_load_n(is_fd_allocated + fd, __ATOMIC_SEQ_CST);
	else
		return false;
}
