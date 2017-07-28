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

/*
 * function_decl_finder.h - interface for a library that interacts with
 * libclang. This is created to make processing the source files relevant
 * to pmemfile easier. Using this interface provides less flexibility
 * than using libclang directly, but in return, one does not need to
 * worry about clang_getCString, clang_disposeString, etc...
 */

#ifndef PMEMFILE_UTILS_FUNCTION_DECL_FINDER_H
#define PMEMFILE_UTILS_FUNCTION_DECL_FINDER_H

#include <stdbool.h>

/*
 * The fields in the following struct should be self explanatory (well,
 * that is the intention). The choice of what information is provided
 * in these structs can appear to be quite arbitrary, they are only influenced
 * influenced by what one happens to need in libpmemfile.
 */
struct type_desc {
	bool is_void;
	bool is_pointer;
	bool is_pointer_to_const;
	bool is_integral;
	bool is_signed_integral;
	bool is_unsigned_integral;
	const char *name;
};

struct arg_desc {
	struct type_desc type;
	const char *name;
};

struct func_desc {
	const char *name;
	struct type_desc return_type;
	bool is_variadic;
	int arg_count;
	struct arg_desc *args;
};

/*
 * visit_function_decls - iterate over all function declarations in the source
 * file at `path`. The callback provided is called with each declaration as
 * argument. The argc and argv arguments are forwarded to libclang, thus one can
 * use clang command line arguments to control parsing of the source file.
 *
 * The callback function can modify anything not const qualified in the
 * func_desc it receives - it just can't free() any of the pointers in it.
 *
 * Returns zero on success, non-zero on failure.
 */
int visit_function_decls(const char *path, int (*callback)(struct func_desc *),
		int argc, char **argv);

#endif
