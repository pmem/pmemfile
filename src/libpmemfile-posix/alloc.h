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

#ifndef PF_ALLOC_H
#define PF_ALLOC_H

#include <stdlib.h>

#ifdef FAULT_INJECTION

void *_pf_malloc(size_t, const char *);
void *_pf_calloc(size_t, size_t, const char *);
void _pf_free(void *, const char *);
void *_pf_realloc(void *, size_t, const char *);

#define pf_malloc(size) _pf_malloc(size, __func__)
#define pf_calloc(nmemb, size) _pf_calloc(nmemb, size, __func__)
#define pf_free(ptr) _pf_free(ptr, __func__)
#define pf_realloc(ptr, size) _pf_realloc(ptr, size, __func__)
#else
#define pf_malloc(size) malloc(size)
#define pf_calloc(nmemb, size) calloc(nmemb, size)
#define pf_free(ptr) free(ptr)
#define pf_realloc(ptr, size) realloc(ptr, size)
#endif

#endif
