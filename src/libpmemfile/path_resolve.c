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
 * Gosh, this path resolving was pain to write.
 * XXX: clean up this whole file, and add some more explanations.
 */

#include <assert.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdlib.h>
#include <inttypes.h>
#include <syscall.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include "libsyscall_intercept_hook_point.h"
#include "libpmemfile-posix.h"

#include "preload.h"

/*
 * get_stat - stat equivalent, fills a struct stat by asking
 * either the kernel, or libpmemfile-posix.
 */
static int
get_stat(struct resolved_path *result, struct stat *buf)
{
	if (result->at_pool == NULL) {
		long error_code = syscall_no_intercept(SYS_newfstatat,
			result->at_kernel,
			result->path,
			buf,
			AT_SYMLINK_NOFOLLOW);
		if (error_code == 0) {
			return 0;
		} else {
			result->error_code = error_code;
			return -1;
		}
	} else {
		int r = pmemfile_fstatat(result->at_pool->pool,
			result->at_dir,
			result->path,
			(pmemfile_stat_t *)buf,
			AT_SYMLINK_NOFOLLOW);

		if (r == 0) {
			return 0;
		} else {
			result->error_code = -errno;
			return -1;
		}
	}
}

/*
 * resolve_symlink - replaces the last component of a path with
 * the symlink's target.
 *
 * The last component is expected to be a symlink. If the symlinks
 * target is an absolute path, then of course the whole path is
 * replaced.
 */
static void
resolve_symlink(struct resolved_path *result,
		size_t *resolved, size_t *end, size_t *size,
		bool *is_last_component)
{
	char link_buf[sizeof(result->path)];
	long link_len;

	result->path[*end] = '\0';

	if (result->at_pool == NULL) {
		link_len = syscall_no_intercept(SYS_readlinkat,
			result->at_kernel,
			result->path,
			link_buf,
			sizeof(link_buf) - 1);

		if (link_len < 0) {
			result->error_code = -link_len;
			return;
		}
	} else {
		link_len = pmemfile_readlinkat(result->at_pool->pool,
				result->at_dir,
				result->path,
				link_buf,
				sizeof(link_buf) - 1);

		if (link_len < 0) {
			assert(errno != 0);
			result->error_code = -errno;
			return;
		}
	}

	if (! *is_last_component)
		result->path[*end] = '/';

	/*
	 * If the link target doesn't fit in this buffer, readlinkat
	 * is expected to return ENAMETOOLONG.
	 */
	assert((size_t)link_len < sizeof(link_buf));
	assert(link_len > 0);

	link_buf[link_len] = '\0';

	size_t link_insert;

	if (link_buf[0] == '/')
		link_insert = 0;
	else
		link_insert = *resolved;

	size_t postfix_insert = link_insert + (size_t)link_len;

	/*
	 * At this point, link_buf holds the destination of the symlink.
	 * The link_insert offset shows where to insert it in the path, and
	 * the postfix_insert offset shows where to move the part of the
	 * path that follows the path component which is the symlink.
	 *
	 * E.g.: "/usr/lib/a/b/" where "/usr/lib" is a symlink to "other" :
	 *
	 * "/usr/lib/a/b/"
	 *       ^    ^postfix_insert
	 *       |link_insert
	 *
	 * The first step in altering the path is the relocation of the
	 * postfix part, as in:
	 *
	 * "/usr/lib/a/b/" --> "/usr/...../a/b"
	 *                  postfix_insert^
	 *
	 * The second step is copying the link destination into the path:
	 *
	 * "/usr/lib/a/b/" --> "/usr/...../a/b" --> "/usr/other/a/b"
	 *                  postfix_insert^    link_insert^
	 *
	 * Processing a symlink to an absolute path is similar, but the
	 * link destination overwrites the whole path prefix.
	 * E.g.: where "/usr/lib" is a symlink to "/other" :
	 *
	 * "/usr/lib/a/b/" --> "....../a/b" -> "/other/a/b"
	 *              postfix_insert^         ^link_insert
	 *
	 */
	char *postfix_dst = result->path + postfix_insert;
	char *link_dst = result->path + link_insert;

	/*
	 * The postfix starts at offset *end, i.e. after the symlink path
	 * component.
	 */
	char *postfix_src = result->path + *end;

	/*
	 * It spans till the end of the path, all that plus the
	 * terminating null character is moved.
	 */
	size_t postfix_len = *size - *end + 1;

	if (postfix_insert + postfix_len >= sizeof(result->path)) {
		/* The path just doesn't fit in the available buffer */
		result->error_code = -ENOMEM;
		return;
	}

	/*
	 * The actual transformation happens in the following two lines
	 * Note: if the link would be copied first, it could overwrite parts
	 * of the postfix.
	 */
	memmove(postfix_dst, postfix_src, postfix_len);
	memcpy(link_dst, link_buf, (size_t)link_len);

	/* Adjust the offsets used by the path resolving loop */
	*size = postfix_insert + postfix_len - 1;
	*resolved = link_insert;

	if (link_buf[0] == '/')
		result->at_pool = NULL;
}

/*
 * enter_pool - continue resolving the remaining part of path
 * inside a pmemfile pool.
 *
 */
static void
enter_pool(struct resolved_path *result, struct pool_description *pool,
		size_t *resolved, size_t end, size_t *size)
{
	memmove(result->path, result->path + end, *size - end);
	result->path[0] = '/';
	result->at_pool = pool;

	/*
	 * The at_dir field doesn't matter here, since result->path
	 * refers to an absolute path.
	 */
	result->at_dir = PMEMFILE_AT_CWD;

	*resolved = 1;
	*size -= end;
	if (*size == 0)
		*size = 1;
	result->path[*size] = '\0';
}

/*
 * exit_pool - continue resolving the remaining part of path by asking
 * the kernel.
 * E.g.: after referring a ".." entry at the root of a pmemfile pool.
 */
static void
exit_pool(struct resolved_path *result, size_t resolved, size_t *size)
{
	result->at_kernel = result->at_pool->fd;
	result->at_pool = NULL;
	memmove(result->path, result->path + resolved, *size - resolved + 1);
	*size -= resolved;
}

/*
 * resolve_path - the main logic for resolving paths containing arbitrary
 * combinations of path components in the kernel's vfs and pmemfile pools.
 *
 * The at argument describes the starting directory of the path resolution,
 * It can refer to either a directory in pmemfile pool, or a directory accessed
 * via the kernel.
 */
void
resolve_path(struct vfd_reference at,
			const char *path,
			struct resolved_path *result,
			int flags)
{
	if (path == NULL) {
		result->error_code = -EFAULT;
		return;
	}

	result->at_kernel = at.kernel_fd;
	result->at_pool = at.pool;
	result->at_dir = at.file;
	result->error_code = 0;

	size_t resolved; /* How many chars are resolved already? */
	size_t size; /* The length of the whole path to be resolved. */
	bool last_component_is_dir = false;

	struct stat stat_buf;
	result->path[0] = '.';
	result->path[1] = 0;

	if (get_stat(result, &stat_buf) != 0)
		return;

	bool at_pmem_root = at.pool &&
			same_inode(&stat_buf, &at.pool->pmem_stat);

	for (size = 0; path[size] != '\0'; ++size) {
		/* leave one more byte for the null terminator */
		if (size == sizeof(result->path) - 1) {
			result->error_code = -ENAMETOOLONG;
			return;
		}
		result->path[size] = path[size];
	}

	if (size == 0) { /* empty string */
		result->error_code = -ENOTDIR;
		return;
	}

	if (result->path[size - 1] == '/') {
		last_component_is_dir = true;
		while (size > 1 && result->path[size - 1] == '/')
			--size;
	}

	result->path[size] = '\0';

	if (path[0] == '/')
		result->at_pool = NULL;

	int num_symlinks = 0;
	struct pool_description *last_pool = NULL;

	/*
	 * XXX
	 * Path resolution needs more tests.
	 */
	for (resolved = strspn(result->path, "/");
	    result->path[resolved] != '\0' && result->error_code == 0;
	    resolved += strspn(result->path + resolved, "/")) {
		size_t end = resolved;

		while (result->path[end] != '\0' && result->path[end] != '/')
			++end;

		/*
		 * At this point, resolved points to the first character
		 * of the path component to be resolved, end points
		 * to one past the last character of the same path
		 * component. E.g.:
		 *
		 *   /usr/lib/a/b/c
		 *        ^  ^
		 * resolved   end
		 */

		bool is_last_component = (result->path[end] == '\0');

		if (is_last_component && ((flags & RESOLVE_LAST_SLINK_MASK) ==
						NO_RESOLVE_LAST_SLINK))
			break;

		result->path[end] = '\0';

		if (get_stat(result, &stat_buf) != 0)
			break;

		if (!is_last_component)
			result->path[end] = '/';

		/*
		 * If we are at "/" and the next component is ".." we have to
		 * exit the pmemfile pool and reevaluate the path using
		 * syscalls.
		 */
		if (at_pmem_root && (end - resolved) == 2 &&
				memcmp(&result->path[resolved], "..", 2) == 0) {
			last_pool = result->at_pool;
			exit_pool(result, resolved, &size);
			at_pmem_root = false;
			continue;
		}

		at_pmem_root = false;

		if (S_ISLNK(stat_buf.st_mode)) {
			resolve_symlink(result,
				&resolved, &end, &size, &is_last_component);
			num_symlinks++;
			/*
			 * 40 is the same value as used by Linux.
			 * See path_resolution(7).
			 */
			if (num_symlinks > 40) {
				result->error_code = -ELOOP;
				break;
			}

			continue;
		} else if (!S_ISDIR(stat_buf.st_mode)) {
			if (!is_last_component)
				result->error_code = -ENOTDIR;

			break;
		} else if (result->at_pool == NULL) {
			struct pool_description *pool;

			pool = lookup_pd_by_inode(&stat_buf);
			if (pool != NULL) {
				if (pool->pool == NULL) {
					result->error_code = -EIO;
					return;
				}
				enter_pool(result, pool, &resolved, end, &size);
				at_pmem_root = true;
				continue;
			}
		} else {
			if (same_inode(&stat_buf, &result->at_pool->pmem_stat))
				at_pmem_root = true;
		}

		for (resolved = end; result->path[resolved] == '/'; ++resolved)
			;
	}

	if (last_component_is_dir && result->path[size - 1] != '/') {
		result->path[size] = '/';
		++size;
		result->path[size] = '\0';
	}

	/*
	 * If everything succeeded, we have a path that doesn't point to
	 * pmemfile and is relative to a mount point and user wants path for
	 * interfaces that do not have *at variant, then prepend the path with
	 * the mount point path.
	 */
	if (result->error_code == 0 &&
			result->at_pool == NULL &&
			(flags & NO_AT_PATH) &&
			last_pool) {
		size_t rem_len = strlen(result->path);
		size_t mnt_len = strlen(last_pool->mount_point);

		if (mnt_len + 1 + rem_len + 1 > sizeof(result->path)) {
			result->error_code = ENAMETOOLONG;
			return;
		}

		memmove(result->path + mnt_len + 1, result->path, rem_len + 1);
		memcpy(result->path, last_pool->mount_point, mnt_len);
		result->path[mnt_len] = '/';
	}
}
