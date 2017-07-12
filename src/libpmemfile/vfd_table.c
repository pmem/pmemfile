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

#include "vfd_table.h"

#include <assert.h>
#include <stdbool.h>
#include <fcntl.h>
#include <syscall.h>

#include <libsyscall_intercept_hook_point.h>
#include <libpmemfile-posix.h>

#include "sys_util.h"
#include "preload.h"

struct vfile_description {
	struct pool_description *pool;
	PMEMfile *file;
	int kernel_cwd_fd;
	bool is_special_cwd_desc;
	int ref_count;
};

static void
vf_ref_count_inc(struct vfile_description *entry)
{
	__atomic_add_fetch(&entry->ref_count, 1, __ATOMIC_ACQ_REL);
}

static void
ref_entry(struct vfile_description *entry)
{
	if (entry != NULL)
		vf_ref_count_inc(entry);
}

static int
vf_ref_count_dec_and_fetch(struct vfile_description *entry)
{
	return __atomic_sub_fetch(&entry->ref_count, 1, __ATOMIC_ACQ_REL);
}

static struct vfile_description *cwd_entry;
static struct vfile_description *vfd_table[0x8000];

static pthread_mutex_t vfd_table_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * The fetch_free_file_slot and mark_as_free_file_slot functions can be
 * used to allocate, and deallocate vfile_description entries.
 *
 */
static struct vfile_description *free_vfile_slots[2 * ARRAY_SIZE(vfd_table)];
static pthread_mutex_t free_vfile_slot_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
mark_as_free_file_slot(struct vfile_description *entry)
{
	static unsigned insert_index;

	util_mutex_lock(&free_vfile_slot_mutex);

	assert(entry->ref_count == 0);

	free_vfile_slots[insert_index] = entry;
	++insert_index;
	insert_index %= ARRAY_SIZE(free_vfile_slots);

	util_mutex_unlock(&free_vfile_slot_mutex);
}

static struct vfile_description *
fetch_free_file_slot(void)
{
	static unsigned fetch_index;

	struct vfile_description *entry;

	util_mutex_lock(&free_vfile_slot_mutex);

	entry = free_vfile_slots[fetch_index];
	++fetch_index;
	fetch_index %= ARRAY_SIZE(free_vfile_slots);

	util_mutex_unlock(&free_vfile_slot_mutex);

	return entry;
}

/*
 * setup_free_slots -- fills the free list with pointers to vfile_description
 * entries, allocated in BSS.
 * Must be called during startup.
 */
static void
setup_free_slots(void)
{
	static struct vfile_description store[ARRAY_SIZE(free_vfile_slots) - 1];

	for (unsigned i = 0; i < ARRAY_SIZE(store); ++i)
		mark_as_free_file_slot(store + i);
}

static struct vfd_reference
pmemfile_ref_vfd_under_mutex(int vfd)
{
	struct vfile_description *entry = vfd_table[vfd];

	if (entry == NULL) {
		/*
		 * Return a vfd_reference with the field called internal
		 * set to NULL.
		 */
		return (struct vfd_reference) {.kernel_fd = vfd, };
	}

	vf_ref_count_inc(entry);

	return (struct vfd_reference) {
	    .pool = entry->pool, .file = entry->file, .internal = entry, };
}

/*
 * is_in_vfd_table_range -- check if the number can be used as an index
 * for the vfd_table array.
 */
static bool
is_in_vfd_table_range(int number)
{
	return (number >= 0) && (number < (int)ARRAY_SIZE(vfd_table));
}

/*
 * can_be_in_vfd_table -- check if the vfd can considered to be one
 * not handled by pmemfile, i.e. not in the vfd_table array.
 * This is done without holding the vfd_table_mutex. Determining
 * that a vfd is not in the array is an atomic operation, but the
 * opposite (determining that is in in the array) requires the mutex to
 * be locked, as that involves the second step of increasing a ref count.
 * Thus if this function returns true, one must acquire the vfd_table_mutex,
 * and check again.
 */
static bool
can_be_in_vfd_table(int vfd)
{
	if (!is_in_vfd_table_range(vfd))
		return false;

	return __atomic_load_n(vfd_table + vfd, __ATOMIC_CONSUME) != NULL;
}

/*
 * pmemfile_vfd_ref -- return a vfd_reference, which is either actually
 * a refernce to vfile_description entry, or just holding an fd handled by
 * the kernel.
 */
struct vfd_reference
pmemfile_vfd_ref(int vfd)
{
	if (!can_be_in_vfd_table(vfd)) {
		/*
		 * Return a vfd_reference with the field called internal
		 * set to NULL.
		 */
		return (struct vfd_reference) {.kernel_fd = vfd, };
	}

	util_mutex_lock(&vfd_table_mutex);

	struct vfd_reference result = pmemfile_ref_vfd_under_mutex(vfd);

	util_mutex_unlock(&vfd_table_mutex);

	return result;
}

static struct vfd_reference
get_fdcwd_reference(void)
{
	struct vfd_reference result;

	util_mutex_lock(&vfd_table_mutex);

	vf_ref_count_inc(cwd_entry);

	result.internal = cwd_entry;

	result.kernel_fd = cwd_entry->kernel_cwd_fd;
	result.pool = cwd_entry->pool;
	result.file = cwd_entry->file;

	util_mutex_unlock(&vfd_table_mutex);

	return result;
}

/*
 * pmemfile_resolve_at_vfd -- same as pmemfile_resolve_vfd,
 * except it can resolve AT_FDCWD.
 * This should only be used for the at directory in appropriate
 * syscalls.
 * Calling getdents (and many other syscalls) with AT_FDCWD should
 * result in EBADF, so pmemfile_vfd_ref should be used for those
 * cases.
 */
struct vfd_reference
pmemfile_vfd_at_ref(int vfd)
{
	if (vfd == AT_FDCWD)
		return get_fdcwd_reference();
	else
		return pmemfile_vfd_ref(vfd);
}

/*
 * unref_entry -- internal function, decrases the ref count of an entry, and
 * release the resources it holds, if needed.
 *
 * There is no need to hold the vfd_table mutex, as this operation does
 * not touch the vfd table.
 */
static void
unref_entry(struct vfile_description *entry)
{
	if (entry == NULL)
		return;

	if (vf_ref_count_dec_and_fetch(entry) == 0) {
		if (entry->is_special_cwd_desc)
			syscall_no_intercept(SYS_close, entry->kernel_cwd_fd);
		else
			pmemfile_close(entry->pool->pool, entry->file);
		mark_as_free_file_slot(entry);
	}
}

/*
 * pmemfile_vfd_unref -- decrease the ref count associated with an entry
 * referenced for the user.
 * There is no need to hold the vfd_table mutex, as this operation does
 * not touch the vfd table.
 */
void
pmemfile_vfd_unref(struct vfd_reference ref)
{
	unref_entry(ref.internal);
}

/*
 * pmemfile_vfd_dup -- creates a new reference to a vfile_description entry,
 * if the vfd refers to one.
 */
int
pmemfile_vfd_dup(int vfd)
{
	if (!can_be_in_vfd_table(vfd))
		return (int)syscall_no_intercept(SYS_dup, vfd);

	int new_vfd;
	util_mutex_lock(&vfd_table_mutex);

	/*
	 * First acquire the underlying fd from the kernel. This can
	 * be the duplicate of a memfd.
	 */
	new_vfd = (int)syscall_no_intercept(SYS_dup, vfd);

	if (new_vfd >= 0 && vfd_table[vfd] != NULL) {
		if (is_in_vfd_table_range(new_vfd)) {
			assert(vfd_table[new_vfd] == NULL);

			vf_ref_count_inc(vfd_table[vfd]);
			__atomic_store_n(vfd_table + new_vfd,
					vfd_table[vfd], __ATOMIC_RELEASE);
		} else {
			/* new_vfd can't be used to index the vfd_table */
			syscall_no_intercept(SYS_close, new_vfd);
			new_vfd = -ENOMEM;
		}
	}

	util_mutex_unlock(&vfd_table_mutex);

	return new_vfd;
}

/*
 * vfd_dup2_under_mutex -- perform dup2
 * If the old_vfd refers to entry, increase the corresponding ref_count.
 * If the new_vfd refers to entry, decrease the corresponding ref_count.
 * Overwrite the entry pointer in the vfd_table.
 * The order of the three operations does not matter, as long as the entries
 * as different, and all three happen while holding the vfd_table_mutex.
 *
 * Important: dup2 must atomic from the user's point of view.
 */
static void
vfd_dup2_under_mutex(int old_vfd, int new_vfd)
{
	/*
	 * "If oldfd is a valid file descriptor, and newfd has the same value
	 * as oldfd, then dup2() does nothing, and returns newfd."
	 *
	 * It is easily verified if the old vfd is valid or not, by asking
	 * the kernel to dup2 the underlying (possible) memfd -- see the
	 * function calling this function.
	 */
	if (old_vfd == new_vfd)
		return;

	if (vfd_table[old_vfd] == vfd_table[new_vfd])
		return;

	ref_entry(vfd_table[old_vfd]);
	unref_entry(vfd_table[new_vfd]);
	__atomic_store_n(vfd_table + new_vfd, vfd_table[old_vfd],
			__ATOMIC_RELEASE);
}

/*
 * pmemfile_vfd_dup2 -- create a new reference to a vfile_description entry,
 * potentially replacing a reference to another one.
 */
int
pmemfile_vfd_dup2(int old_vfd, int new_vfd)
{
	if (!(can_be_in_vfd_table(old_vfd) || can_be_in_vfd_table(new_vfd)))
		return (int)syscall_no_intercept(SYS_dup2, old_vfd, new_vfd);

	int result;

	util_mutex_lock(&vfd_table_mutex);

	result = (int)syscall_no_intercept(SYS_dup2, old_vfd, new_vfd);

	if (result >= 0) {
		assert(result == new_vfd);
		vfd_dup2_under_mutex(old_vfd, new_vfd);
	}

	util_mutex_unlock(&vfd_table_mutex);

	return result;
}

/*
 * pmemfile_vfd_close -- remove a reference from the vfd_table array (if
 * there was one at vfd_table[vfd].
 * This does not necessarily close an underlying pmemfile file, as some
 * vfd_reference structs given to the user might still reference that entry.
 */
long
pmemfile_vfd_close(int vfd)
{
	struct vfile_description *entry = NULL;

	util_mutex_lock(&vfd_table_mutex);

	entry = vfd_table[vfd];
	vfd_table[vfd] = NULL;

	long result = syscall_no_intercept(SYS_close, vfd);

	util_mutex_unlock(&vfd_table_mutex);

	if (entry != NULL) {
		unref_entry(entry);
		result = 0;
	}

	return result;
}

/*
 * setup_cwd -- initializes an entry to hold the cwd, and cwd_entry pointer
 * to point to it.
 * Must be called at startup, other functions in this TU expect the cwd_entry
 * pointer to be non-null.
 */
static void
setup_cwd(void)
{
	long fd = syscall_no_intercept(SYS_open, ".", O_DIRECTORY | O_RDONLY);
	if (fd < 0)
		exit_with_msg(1, "setup_cwd");

	cwd_entry = fetch_free_file_slot();
	*cwd_entry = (struct vfile_description) {
		.pool = NULL, .file = NULL,
		.kernel_cwd_fd = (int)fd,
		.is_special_cwd_desc = true,
		.ref_count = 1};
}

static void
chdir_exchange_entry(struct vfile_description *new_cwd_entry)
{
	struct vfile_description *old_cwd_entry;

	/*
	 * Overwrite the original cwd entry with the new one. This looks
	 * like it could be done using __atomic_exchange_n, since all that
	 * happens under the mutex is the exchange of a single integer. But
	 * that could race with pmemfile_vfd_ref. In the example below, the
	 * vf_ref_count_inc in step #4 refers to an entry that is already
	 * deallocated in step #3 (and possibly allocated again for some
	 * other file).
	 *
	 *   |T0:                        | T1:                           |
	 *   |pmemfile_vfd_ref call      | chdir_exchange_entry call     |
	 *   |                           |                               |
	 * 0:|lock(vfd_table_mutex)      |                               |
	 * 1:|entry = vfd_table[vfd];    |                               |
	 * 2:|                           | exchange(&cwd_entry, entry);  |
	 * 3:|                           | unref(old_cwd_entry);         |
	 * 4:|vf_ref_count_inc(entry);   |                               |
	 * 5:|unlock(vfd_table_mutex);   |                               |
	 *
	 */

	old_cwd_entry = cwd_entry;
	cwd_entry = new_cwd_entry;

	unref_entry(old_cwd_entry);
}

/*
 * pmemfile_vfd_chdir_pf -- change the current working directory to
 * a pmemfile handled directory.
 */
long
pmemfile_vfd_chdir_pf(struct pool_description *pool, struct pmemfile_file *file)
{
	long result;

	util_mutex_lock(&vfd_table_mutex);

	if (pmemfile_fchdir(pool->pool, file) != 0) {
		result = -errno;
	} else {
		struct vfile_description *entry = fetch_free_file_slot();

		*entry = (struct vfile_description) {
			.pool = pool, .file = file,
			.kernel_cwd_fd = -1,
			.is_special_cwd_desc = false,
			.ref_count = 1};

		chdir_exchange_entry(entry);

		result = 0;
	}

	util_mutex_unlock(&vfd_table_mutex);

	return result;
}

/*
 * pmemfile_vfd_chdir_kernel_fd -- change the current working directory to
 * a kernel handled directory.
 */
long
pmemfile_vfd_chdir_kernel_fd(int fd)
{
	long result;

	util_mutex_lock(&vfd_table_mutex);

	result = syscall_no_intercept(SYS_fchdir, fd);

	if (result == 0) {
		struct vfile_description *entry = fetch_free_file_slot();

		*entry = (struct vfile_description) {
			.pool = NULL, .file = NULL,
			.kernel_cwd_fd = fd,
			.is_special_cwd_desc = true,
			.ref_count = 1};

		chdir_exchange_entry(entry);
	}

	util_mutex_unlock(&vfd_table_mutex);

	return result;
}

static bool is_memfd_syscall_available;

#ifdef SYS_memfd_create

static void
check_memfd_syscall(void)
{
	long fd = syscall_no_intercept(SYS_memfd_create, "check", 0);
	if (fd >= 0) {
		is_memfd_syscall_available = true;
		syscall_no_intercept(SYS_close, fd);
	}
}

#else

#define SYS_memfd_create 0
#define check_memfd_syscall()

#endif

/*
 * acquire_new_fd - grab a new file descriptor from the kernel
 */
static long
acquire_new_fd(const char *path)
{
	long fd;

	if (is_memfd_syscall_available) {
		fd = syscall_no_intercept(SYS_memfd_create, path, 0);
		/* memfd_create can fail for too long name */
		if (fd < 0) {
			fd = syscall_no_intercept(SYS_open, "/dev/null",
					O_RDONLY);
		}
	} else {
		fd = syscall_no_intercept(SYS_open, "/dev/null", O_RDONLY);
	}

	if (fd >= (long)ARRAY_SIZE(vfd_table)) {
		syscall_no_intercept(SYS_close, fd);
		return -ENFILE;
	}

	return fd;
}

/*
 * pmemfile_vfd_assign -- return an fd that can be used by an application
 * in the future to refer to the given pmemfile file.
 *
 * Creates a new vfile_description entry with ref_count = one.
 */
long
pmemfile_vfd_assign(struct pool_description *pool,
			struct pmemfile_file *file,
			const char *path)
{
	long new_vfd = acquire_new_fd(path);

	if (new_vfd < 0)
		return new_vfd;

	struct vfile_description *entry = fetch_free_file_slot();

	*entry = (struct vfile_description) {
		.pool = pool, .file = file,
		.kernel_cwd_fd = -1,
		.is_special_cwd_desc = false,
		.ref_count = 1};

	util_mutex_lock(&vfd_table_mutex);

	vfd_table[new_vfd] = entry;

	util_mutex_unlock(&vfd_table_mutex);

	return new_vfd;
}

long
pmemfile_vfd_fchdir(int vfd)
{
	long result;
	struct vfile_description *old_cwd_entry = NULL;

	util_mutex_lock(&vfd_table_mutex);

	if (is_in_vfd_table_range(vfd) && vfd_table[vfd] != NULL) {
		vf_ref_count_inc(vfd_table[vfd]);
		old_cwd_entry = cwd_entry;
		cwd_entry = vfd_table[vfd];
		result = 0;
	} else {
		long new_fd = syscall_no_intercept(SYS_dup, vfd);
		if (new_fd >= 0)
			result = syscall_no_intercept(SYS_fchdir, new_fd);
		else
			result = new_fd;

		if (result == 0) {
			struct vfile_description *entry;
			entry = fetch_free_file_slot();

			*entry = (struct vfile_description) {
				.pool = NULL, .file = NULL,
				.kernel_cwd_fd = (int)new_fd,
				.is_special_cwd_desc = true,
				.ref_count = 1};

			old_cwd_entry = cwd_entry;
			cwd_entry = entry;
			result = 0;
		}
	}

	util_mutex_unlock(&vfd_table_mutex);

	if (old_cwd_entry != NULL)
		unref_entry(old_cwd_entry);

	return result;
}

void
pmemfile_vfd_table_init(void)
{
	check_memfd_syscall();
	setup_free_slots();
	setup_cwd();
}
