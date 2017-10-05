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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "libpmemfile-posix.h"
#include "out.h"

#ifdef FAULT_INJECTION
static __thread int malloc_num;
static __thread int fail_malloc_num;
static __thread const char *fail_malloc_from;

void *
_pf_malloc(size_t size, const char *func)
{
	if (fail_malloc_from && strcmp(func, fail_malloc_from) == 0) {
		if (++malloc_num == fail_malloc_num) {
			errno = ENOMEM;
			return NULL;
		}
	}
	return malloc(size);
}

static __thread int calloc_num;
static __thread int fail_calloc_num;
static __thread const char *fail_calloc_from;

void *
_pf_calloc(size_t nmemb, size_t size, const char *func)
{
	if (fail_calloc_from && strcmp(func, fail_calloc_from) == 0) {
		if (++calloc_num == fail_calloc_num) {
			errno = ENOMEM;
			return NULL;
		}
	}
	return calloc(nmemb, size);
}

void
_pf_free(void *ptr, const char *func)
{
	free(ptr);
}

static __thread int realloc_num;
static __thread int fail_realloc_num;
static __thread const char *fail_realloc_from;

void *
_pf_realloc(void *ptr, size_t size, const char *func)
{
	if (fail_realloc_from && strcmp(func, fail_realloc_from) == 0) {
		if (++realloc_num == fail_realloc_num) {
			errno = ENOMEM;
			return NULL;
		}
	}
	return realloc(ptr, size);
}

void
pmemfile_inject_fault_at(enum pf_allocation_type type, int nth, const char *at)
{
	switch (type) {
		case PF_MALLOC:
			malloc_num = 0;
			fail_malloc_num = nth;
			fail_malloc_from = at;
			break;
		case PF_CALLOC:
			calloc_num = 0;
			fail_calloc_num = nth;
			fail_calloc_from = at;
			break;
		case PF_REALLOC:
			realloc_num = 0;
			fail_realloc_num = nth;
			fail_realloc_from = at;
			break;
		default:
			FATAL("unknown allocation type");
	}
}
#endif
