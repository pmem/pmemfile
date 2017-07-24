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

#include <stdio.h>
#include <string.h>

#include "function_decl_finder.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const char *prologue =
	"/* Generated source file, do not edit manually! */\n"
	"\n"
	"#ifndef LIBPMEMFILE_POSIX_WRAPPERS_H\n"
	"#define LIBPMEMFILE_POSIX_WRAPPERS_H\n"
	"\n"
	"#include \"libpmemfile-posix.h\"\n"
	"#include \"preload.h\"\n"
	"#include <stdint.h>\n"
	"\n";

static const char *epilogue =
	"\n"
	"#endif\n";

static const char prefix[] = "wrapper_";

static void
print_type_and_name(const char *type, const char *name)
{
	if (type[strlen(type) - 1] == '*')
		printf("%s%s", type, name);
	else
		printf("%s %s", type, name);
}

static void
print_prototype(const struct func_desc *desc)
{
	printf("static inline %s\n", desc->return_type.name);
	printf("%s%s(", prefix, desc->name);

	if (desc->arg_count == 0)
		printf("void");

	for (int i = 0; i < desc->arg_count; ++i) {
		if (i > 0)
			printf(",\n\t\t");
		print_type_and_name(desc->args[i].type.name,
					desc->args[i].name);
	}
	puts(")");
}

static void
print_forward_call(const struct func_desc *desc)
{
	printf("%s(", desc->name);
	for (int i = 0; i < desc->arg_count; ++i) {
		if (i > 0)
			printf(",\n\t\t");
		printf("%s", desc->args[i].name);
	}
	puts(");");
}

static bool
is_printable_cstr_type(const char *type_name)
{
	static const char *const accepted_types[] = {
		"const char *"
	};

	for (size_t i = 0; i < ARRAY_SIZE(accepted_types); ++i)
		if (strcmp(type_name, accepted_types[i]) == 0)
			return true;

	return false;
}

static bool
is_printable_cstr_name(const char *name)
{
	static const char *const accepted_names[] = {
		"path",
		"pathname",
		"oldpath",
		"newpath",
		"old_path",
		"new_path"
	};

	for (size_t i = 0; i < ARRAY_SIZE(accepted_names); ++i)
		if (strcmp(name, accepted_names[i]) == 0)
			return true;

	return false;
}

static bool
is_arg_printable_cstr(const char *type_name, const char *name)
{
	return is_printable_cstr_type(type_name) &&
		is_printable_cstr_name(name);
}

static void
print_format(const struct type_desc *type, const char *name)
{
	if (is_arg_printable_cstr(type->name, name))
		fputs("\\\"%s\\\"", stdout);
	else if (strcmp(type->name, "size_t") == 0)
		fputs("%zu", stdout);
	else if (strcmp(type->name, "pmemfile_ssize_t") == 0)
		fputs("%zd", stdout);
	else if (strcmp(type->name, "pmemfile_mode_t") == 0)
		fputs("%3jo", stdout);
	else if (type->is_pointer)
		fputs("%p", stdout);
	else if (type->is_signed_integral)
		fputs("%jd", stdout);
	else /* treating it as an unsigned integral type */
		fputs("%jx", stdout);
}

static void
print_format_argument(const struct type_desc *type, const char *name)
{
	if (is_arg_printable_cstr(type->name, name))
		fputs(name, stdout);
	else if (strcmp(type->name, "size_t") == 0)
		fputs(name, stdout);
	else if (strcmp(type->name, "pmemfile_ssize_t") == 0)
		fputs(name, stdout);
	else if (type->is_pointer_to_const)
		printf("(const void *)%s", name);
	else if (type->is_pointer)
		printf("(void *)%s", name);
	else if (type->is_signed_integral)
		printf("(intmax_t)%s", name);
	else
		printf("(uintmax_t)%s", name);
}

static void
print_log_write(const struct func_desc *desc)
{

	printf("\tlog_write(\n\t    \"%s(", desc->name);

	for (int i = 0; i < desc->arg_count; ++i) {
		if (i > 0)
			printf(", ");

		print_format(&desc->args[i].type, desc->args[i].name);
	}
	printf(")");

	if (!desc->return_type.is_void) {
		printf(" = ");
		print_format(&desc->return_type, "");
	}
	printf("\"");


	for (int i = 0; i < desc->arg_count; ++i) {
		printf(",\n\t\t");
		print_format_argument(&desc->args[i].type, desc->args[i].name);
	}

	if (!desc->return_type.is_void) {
		printf(", ");
		print_format_argument(&desc->return_type, "");
		printf("ret");
	}

	puts(");");
}

static void
print_errno_handler(void)
{
	puts("\tif (ret < 0)");
	puts("\t\tret = -errno;");
}

static void
print_wrapper(struct func_desc *desc)
{
	print_prototype(desc);
	puts("{");

	if (desc->return_type.is_void) {
		printf("\t");
	} else {
		putchar('\t');
		print_type_and_name(desc->return_type.name, "ret");
		printf(";\n\n\tret = ");
	}

	print_forward_call(desc);

	if (strcmp(desc->return_type.name, "int") == 0 ||
	    strcmp(desc->return_type.name, "pmemfile_ssize_t") == 0)
		print_errno_handler();

	putchar('\n');
	print_log_write(desc);
	putchar('\n');

	if (!desc->return_type.is_void) {
		puts("");
		puts("\treturn ret;");
	}

	puts("}");
	puts("");
}

static bool
has_arg_name(const struct func_desc *desc, const char *name)
{
	for (int i = 0; i < desc->arg_count; ++i) {
		if (strcmp(desc->args[i].name, name) == 0)
			return true;
	}

	return false;
}

static int
fill_arg_name(struct func_desc *desc, struct arg_desc *arg)
{
	if (strcmp(arg->type.name, "PMEMfilepool *") == 0) {
		if (has_arg_name(desc, "pfp"))
			return -1;

		arg->name = "pfp";
		return 0;
	}

	if (strcmp(arg->type.name, "PMEMfile *") == 0) {
		if (has_arg_name(desc, "file")) {
			if (has_arg_name(desc, "file2"))
				return -1;

			arg->name = "file2";
		} else {
			arg->name = "file";
		}

		return 0;
	}

	return -1;
}

static int
fix_args(struct func_desc *desc)
{
	for (int i = 0; i < desc->arg_count; ++i) {
		struct arg_desc *arg = desc->args + i;

		if (arg->name == NULL || arg->name[0] == '\0') {
			if (fill_arg_name(desc, arg) != 0)
				return -1;
		}
	}

	return 0;
}

static int
process_function(struct func_desc *desc)
{
	static const char orig_prefix[] = "pmemfile_";

	if (desc->is_variadic)
		return 0;

	if (strncmp(desc->name, orig_prefix, strlen(orig_prefix)) != 0)
		return 0;

	if (fix_args(desc) != 0)
		return -1;

	print_wrapper(desc);

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
