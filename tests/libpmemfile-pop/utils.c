/*
 * Copyright 2017, Intel Corporation
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpmemfile-posix.h>

#include "utils.h"

/*
 * The goal of this function is to merge paths, pool relative paths used with
 * pmemfile-posix need to be converted to absolute paths.
 * Returned value needs to be freed later.
 * Example:
 * /mnt/pmem/pool - pool
 * /abc - path in pmemfile pool
 * /mnt/pmem/pool/abc - final result
 */
char *
merge_paths(const char *path1, const char *path2)
{
	char *result;
	const char *empty = "";
	const char *slash = "/";
	const char *path1_result, *slash_result, *path2_result;
	path1_result = slash_result = path2_result = empty;

	if (path1 != NULL)
		path1_result = path1;
	if (path2 != NULL) {
		path2_result = path2;
		if (path2[0] != '/')
			slash_result = slash;
	}

	int print_result = asprintf(&result, "%s%s%s", path1_result,
				    slash_result, path2_result);
	if (print_result < 0)
		return NULL;

	return result;
}

bool
is_tmpfile(int flags)
{
	return (flags & PMEMFILE_O_TMPFILE) == PMEMFILE_O_TMPFILE;
}

/*
 * Count occurences of a string in another string. Used by replace.
 */
size_t
count_occurences(const char *str, const char *find)
{
	size_t result = 0;
	size_t findlen = strlen(find);
	if (findlen == 0) {
		return 0;
	}
	char *found = strstr(str, find);
	while (found != NULL) {
		result++;
		if ((size_t)(found - str) < strlen(str)) {
			found = strstr(found + findlen, find);
		} else {
			found = NULL;
		}
	}
	return result;
}

/*
 * Check if string ends with passed character
 */
bool
ends_with(char *path, char c)
{
	size_t length = strlen(path);
	if (length > 0)
		return path[length - 1] == c;
	return false;
}

/*
 * Check if string1 starts with string2
 */
bool
starts_with(char *string1, char *string2)
{
	if (!string1 || !string2)
		return false;

	size_t len1 = strlen(string1);
	size_t len2 = strlen(string2);

	if (len2 > len1)
		return false;

	return !memcmp(string1, string2, len2);
}
