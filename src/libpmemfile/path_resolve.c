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
 * TODO: clean up this whole file, and add some more explanations.
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

static int
get_stat(struct resolved_path *result, struct stat *buf)
{
	if (is_fda_null(&result->at.pmem_fda)) {
		long error_code = syscall_no_intercept(SYS_newfstatat,
			result->at.kernel_fd,
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
		int r = pmemfile_fstatat(result->at.pmem_fda.pool->pool,
			result->at.pmem_fda.file,
			result->path,
			(struct pmemfile_stat *)buf,
			AT_SYMLINK_NOFOLLOW);

		if (r == 0) {
			return 0;
		} else {
			result->error_code = -errno;
			return -1;
		}
	}
}

static void
resolve_symlink(struct resolved_path *result,
		size_t *resolved, size_t *end, size_t *size,
		bool *is_last_component)
{
	char link_buf[0x200];
	long link_len;

	result->path[*end] = '\0';

	if (is_fda_null(&result->at.pmem_fda)) {
		link_len = syscall_no_intercept(SYS_readlinkat,
			result->at.kernel_fd,
			result->path,
			link_buf,
			sizeof(link_buf));
	} else {
		link_len = pmemfile_readlinkat(result->at.pmem_fda.pool->pool,
				result->at.pmem_fda.file,
				result->path,
				link_buf,
				sizeof(link_buf));

		if (link_len < 0 && errno != 0) {
			result->error_code = -errno;
			return;
		}
	}

	if (! *is_last_component)
		result->path[*end] = '/';

	if (link_len < 0 || (size_t)link_len >= sizeof(result->path)) {
		result->error_code = -ENOMEM;
		return;
	}
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
		// The path just doesn't fit in the available buffer
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

	// Adjust the offsets used by the path resolving loop
	*size = postfix_insert + postfix_len - 1;
	*resolved = link_insert;

	if (link_buf[0] == '/')
		result->at.pmem_fda.pool = NULL;
}

static void
enter_pool(struct resolved_path *result, struct pool_description *pool,
		size_t *resolved, size_t end, size_t *size)
{
	memmove(result->path, result->path + end, *size - end);
	result->path[0] = '/';
	result->at.pmem_fda.pool = pool;
	result->at.pmem_fda.file = PMEMFILE_AT_CWD;
	*resolved = 1;
	*size -= end;
	if (*size == 0)
		*size = 1;
	result->path[*size] = '\0';
}

static void
exit_pool(struct resolved_path *result,
		size_t *resolved, size_t end, size_t *size)
{
	result->at.kernel_fd = result->at.pmem_fda.pool->fd;
	result->at.pmem_fda.pool = NULL;
	memcpy(result->path, result->path + end + 1, *size - end);
	*resolved = 0;
	*size -= end;
}

static size_t
skip_slashes(const char *path, size_t resolved)
{
	for (; path[resolved] == '/'; ++resolved)
		;

	return resolved;
}

void
resolve_path(struct fd_desc at,
			const char *path,
			struct resolved_path *result,
			int follow_last)
{
	if (path == NULL || path[0] == '\0') {
		result->error_code = -ENOTDIR;
		return;
	}

	result->at = at;
	result->error_code = 0;

	size_t resolved; // How many chars are resolved already?
	size_t size; // The length of the whole path to be resolved.
	bool last_component_is_dir = false;

	for (size = 0; path[size] != '\0'; ++size)
		result->path[size] = path[size];

	if (result->path[size - 1] == '/') {
		last_component_is_dir = true;
		while (result->path[size - 1] == '/')
			--size;
	}

	result->path[size] = '\0';

	if (path[0] == '/')
		result->at.pmem_fda.pool = NULL;

	/*
	 * XXX
	 * This code is too convoluted, and new bugs are found in it weekly.
	 * Must be rewritten after the holidays.
	 */
	for (resolved = skip_slashes(result->path, 0);
	    result->path[resolved] != '\0' && result->error_code == 0;
	    resolved = skip_slashes(result->path, resolved)) {
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

		if (is_last_component && follow_last == NO_RESOLVE_LAST_SLINK)
			break;

		result->path[end] = '\0';

		struct stat stat_buf;
		if (get_stat(result, &stat_buf) != 0)
			break;

		if (!is_last_component)
			result->path[end] = '/';

		if (S_ISLNK(stat_buf.st_mode)) {
			resolve_symlink(result,
				&resolved, &end, &size, &is_last_component);
			continue;
		} else if (!S_ISDIR(stat_buf.st_mode)) {
			if (!is_last_component)
				result->error_code = -ENOTDIR;

			break;
		} else if (is_fda_null(&result->at.pmem_fda)) {
			struct pool_description *pool;

			pool = lookup_pd_by_inode(stat_buf.st_ino);
			if (pool != NULL) {
				if (pool->pool == NULL) {
					result->error_code = -EIO;
					return;
				}
				enter_pool(result, pool, &resolved, end, &size);
				continue;
			}
		} else {
			if (stat_buf.st_ino ==
			    result->at.pmem_fda.pool->pmem_stat.st_ino) {
				exit_pool(result, &resolved, end, &size);
				continue;
			}
		}

		for (resolved = end; result->path[resolved] == '/'; ++resolved)
			;
	}

	if (last_component_is_dir && result->path[size - 1] != '/') {
		result->path[size] = '/';
		++size;
		result->path[size] = '\0';
	}
}
