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

#ifndef LEVEL2_RELEVANT_FUNCTIONS_H
#define LEVEL2_RELEVANT_FUNCTIONS_H

static const char *const relevant_functions[] = {
	"wrapper_pmemfile_write",
	"wrapper_pmemfile_writev",
	"wrapper_pmemfile_read",
	"wrapper_pmemfile_readv",
	"wrapper_pmemfile_lseek",
	"wrapper_pmemfile_fstat",
	"wrapper_pmemfile_pread",
	"wrapper_pmemfile_pwrite",
	"wrapper_pmemfile_getdents",
	"wrapper_pmemfile_getdents64",
	"wrapper_pmemfile_close",
	"wrapper_pmemfile_preadv",
	"wrapper_pmemfile_pwritev",
	"wrapper_pmemfile_flock",
	"wrapper_pmemfile_ftruncate",
	"wrapper_pmemfile_fchmod",
	"wrapper_pmemfile_fchown",
	"wrapper_pmemfile_fallocate",
	NULL
};

#endif
