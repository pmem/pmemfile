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

#include <stdlib.h>
#include <string.h>

#include <libpmemfile-posix.h>

#include "utils.h"

char *
merge_paths(const char *path1, const char *path2)
{
	size_t path1_size = 0;
	size_t path2_size = 0;

	if (path1 != NULL) {
		path1_size = strlen(path1);
	}
	if (path2 != NULL) {
		path2_size = strlen(path2);
		if (path2[0] != '/')
			path2_size++;
	}

	char *result = malloc(path1_size + path2_size + 1);
	result[0] = '\0';

	if (path1 != NULL) {
		strcat(result, path1);
	}
	if (path2 != NULL) {
		if (path2[0] != '/')
			strcat(result, "/");
		strcat(result, path2);
	}

	return result;
}

bool
is_tmpfile(int flags)
{
	return (flags & PMEMFILE_O_TMPFILE) == PMEMFILE_O_TMPFILE;
}

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

char *
replace(const char *str, char *from, char *to)
{
	size_t strlength = strlen(str);
	size_t fromlength = strlen(from);
	size_t tolength = strlen(to);

	if (fromlength < 1 || strlength < 1) {
		return NULL;
	}

	size_t from_count = count_occurences(str, from);
	size_t diff = tolength - fromlength;

	size_t newsize = strlength + (size_t)(diff * from_count);

	char *ret = malloc(newsize + 1);
	ret[0] = '\0';
	char *origret = ret;
	char *found = strstr(str, from);
	char *oldfound = (char *)str;

	while (found != NULL) {
		ret = strncat(ret, oldfound, (size_t)(found - oldfound)) +
			(found - oldfound);
		ret = strcat(ret, to) + tolength;
		oldfound = found + fromlength;
		found = strstr(found + fromlength, from);
	}

	// case if none has been found
	if (ret == origret) {
		strcpy(origret, str);
	} else {
		strcat(ret, oldfound);
	}

	return origret;
}

bool
ends_with(char *path, char c)
{
	size_t length = strlen(path);
	if (length > 0)
		return path[length - 1] == c;
	return false;
}

char *
path_fix(const char *path, bool remove_trailing_slash)
{
	char *ret = replace(path, "//", "/");

	if (ret == NULL)
		return NULL;

	if (remove_trailing_slash && ends_with(ret, '/')) {
		ret[strlen(ret) - 1] = '\0';
	}
	return ret;
}
