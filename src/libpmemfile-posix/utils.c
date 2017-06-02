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

#include <errno.h>
#include <time.h>

#include "internal.h"
#include "out.h"
#include "utils.h"
#include "libpmemfile-posix.h"

/*
 * file_get_time -- sets *t to current time
 */
void
get_current_time(struct pmemfile_time *t)
{
	pmemfile_timespec_t tm;
	if (clock_gettime(CLOCK_REALTIME, &tm)) {
		ERR("!clock_gettime");
		pmemfile_tx_abort(errno);
	}
	t->sec = tm.tv_sec;
	t->nsec = tm.tv_nsec;
}

/*
 * is_zeroed -- check if given memory range is all zero
 */
bool
is_zeroed(const void *addr, size_t len)
{
	/* XXX optimize */
	const char *a = (const char *)addr;
	while (len-- > 0)
		if (*a++)
			return false;
	return true;
}

/*
 * str_compare -- compares 2 strings
 *
 * s1 is NUL-terminated,
 * s2 is not - its length is s2n
 */
int
str_compare(const char *s1, const char *s2, size_t s2n)
{
	int ret = strncmp(s1, s2, s2n);
	if (ret)
		return ret;
	if (s1[s2n] != 0)
		return 1;
	return 0;
}

/*
 * str_contains -- returns true if string contains specified character in first
 * len bytes
 */
bool
str_contains(const char *str, size_t len, char c)
{
	for (size_t i = 0; i < len; ++i)
		if (str[i] == c)
			return true;

	return false;
}

/*
 * more_than_1_component -- returns true if path contains more than one
 * component
 *
 * Deals with slashes at the end of path.
 */
bool
more_than_1_component(const char *path)
{
	path = strchr(path, '/');
	if (!path)
		return false;

	while (*path == '/')
		path++;

	if (*path == 0)
		return false;

	return true;
}

/*
 * component_length -- returns number of characters till the end of path
 * component
 */
size_t
component_length(const char *path)
{
	const char *slash = strchr(path, '/');
	if (!slash)
		return strlen(path);
	return (uintptr_t)slash - (uintptr_t)path;
}

/*
 * pmfi_strndup -- strndup (GNU extension) replacement
 */
char *
pmfi_strndup(const char *c, size_t len)
{
	char *cp = malloc(len + 1);
	if (!cp)
		return NULL;
	memcpy(cp, c, len);
	cp[len] = 0;
	return cp;
}

#ifdef DEBUG
/*
 * pmfi_path -- returns one of the full paths inode can be reached on
 *
 * Only for debugging.
 */
const char *
pmfi_path(struct pmemfile_vinode *vinode)
{
	if (!vinode)
		return NULL;
	if (!vinode->path)
		LOG(LTRC, "0x%lx: no vinode->path", vinode->tinode.oid.off);
	return vinode->path;
}
#endif

/*
 * expand_to_full_pages
 * Alters two file offsets to be pmemfile-page aligned. This is not
 * necessarily the same as memory page alignment!
 * The resulting offset refer to an interval that contains the original
 * interval.
 */
void
expand_to_full_pages(uint64_t *offset, uint64_t *length)
{
	/* align the offset */
	*length += *offset % MIN_BLOCK_SIZE;
	*offset -= *offset % MIN_BLOCK_SIZE;

	/* align the length */
	*length = page_roundup(*length);
}

/*
 * narrow_to_full_pages
 * Alters two file offsets to be pmemfile-page aligned. This is not
 * necessarily the same as memory page alignment!
 * The resulting offset refer to an interval that is contained by the original
 * interval. This new interval can end up being empty, i.e. *length can become
 * zero.
 */
void
narrow_to_full_pages(uint64_t *offset, uint64_t *length)
{
	uint64_t end = page_rounddown(*offset + *length);
	*offset = page_roundup(*offset);
	if (end > *offset)
		*length = end - *offset;
	else
		*length = 0;
}
