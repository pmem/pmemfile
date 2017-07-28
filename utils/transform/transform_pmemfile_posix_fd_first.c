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
 * transform_pmemfile_posix_fd_first.c
 *
 * Print wrappers around functions declared (and defined) in the
 * pmemfile-posix-wrappers.h header file.
 * These wrappers merely make it easier to call some of these functions,
 * by casting `long` arguments from libsyscall_intercept to the appropirate
 * types. It also forwards a struct fd_association pointer argument
 * as a pool pointer + a file pointer argument.
 * This is only meant to apply to some pmemfile_* functions which
 * accept a file as their second argument -- corresponding to libc
 * functions which accept an fd as their first argument.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "function_decl_finder.h"

#include "fd_first_relevant_functions.h"

static const char prefix[] = "fd_first_";
static const char original_prefix[] = "wrapper_";

static const char *prologue =
	"/* Generated source file, do not edit manually! */\n"
	"\n"
	"#ifndef LIBPMEMFILE_POSIX_WRAPPERS2_H\n"
	"#define LIBPMEMFILE_POSIX_WRAPPERS2_H\n"
	"\n"
	"#include \"libpmemfile-posix-wrappers.h\"\n"
	"\n";

static const char *epilogue =
	"\n"
	"#endif\n";

static bool
is_relevant_function(const char *name)
{
	const char *const *item = relevant_functions;
	while (*item != NULL) {
		if (strcmp(*item, name) == 0)
			return true;

		++item;
	}

	return false;
}

static bool
is_pool_pointer(const struct arg_desc *arg)
{
	return strcmp(arg->type.name, "PMEMfilepool *") == 0;
}

static bool
is_file_pointer(const struct arg_desc *arg)
{
	return strcmp(arg->type.name, "PMEMfile *") == 0;
}

static void
print_prototype(const struct func_desc *desc)
{
	printf("static inline %s\n", desc->return_type.name);
	printf("%s%s(struct fd_association *%s",
		prefix, desc->name + strlen(original_prefix),
		desc->args[1].name);

	for (int i = 2; i < desc->arg_count; ++i)
		printf(",\n\t\tlong %s", desc->args[i].name);

	puts(")");
}

static void
print_forward_args(const struct func_desc *desc)
{
	printf("%s->pool->pool, %s->file",
		desc->args[1].name, desc->args[1].name);

	for (int i = 2; i < desc->arg_count; ++i) {
		const struct arg_desc *arg = desc->args + i;

		printf(",\n\t\t(%s)%s", arg->type.name, arg->name);
	}
}

static void
print_wrapper(struct func_desc *desc)
{
	print_prototype(desc);
	printf("{\n\t");

	if (!desc->return_type.is_void)
		printf("return ");

	printf("%s(", desc->name);

	print_forward_args(desc);

	puts(");");
	puts("}");
	puts("");
}

static void
check_args(const struct func_desc *desc)
{
	if (desc->arg_count < 2 || desc->is_variadic) {
		fprintf(stderr, "Unexpected argument count at %s\n",
				desc->name);
		exit(1);
	}

	if (!is_pool_pointer(desc->args)) {
		fprintf(stderr, "Unexpected first argument at %s\n",
				desc->name);
		exit(1);
	}

	if (!is_file_pointer(desc->args + 1)) {
		fprintf(stderr, "Unexpected second argument at %s\n",
				desc->name);
		exit(1);
	}

	for (int i = 2; i < desc->arg_count; ++i) {
		if (is_pool_pointer(desc->args + i) ||
		    is_file_pointer(desc->args + i)) {
			fprintf(stderr, "Unexpected argument at %s\n",
					desc->name);
			exit(1);
		}
	}
}

static int
process_function(struct func_desc *desc)
{
	if (is_relevant_function(desc->name)) {
		check_args(desc);
		print_wrapper(desc);
	}

	return 0;
}

int
main(int argc, char **argv)
{
	if (argc < 3)
		return 1;

	char *input = argv[1];
	char *output = argv[2];

	if (freopen(output, "w", stdout) == NULL)
		return 1;

	argc -= 3;
	argv += 3;
	fputs(prologue, stdout);
	if (visit_function_decls(input, process_function, argc, argv) != 0)
		return 1;

	fputs(epilogue, stdout);

	return 0;
}
