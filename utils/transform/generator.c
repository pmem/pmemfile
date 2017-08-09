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

#include "generator.h"

#include <stdio.h>

static void
write_license(FILE *f, const char **copyrights)
{
	fputs("/*\n", f);

	while (*copyrights != NULL)
		fprintf(f, " * %s\n", *copyrights++);

	fputs(" *\n"
		" * Redistribution and use in source and binary forms, with or without\n"
		" * modification, are permitted provided that the following conditions\n"
		" * are met:\n"
		" *\n"
		" *     * Redistributions of source code must retain the above copyright\n"
		" *       notice, this list of conditions and the following disclaimer.\n"
		" *\n"
		" *     * Redistributions in binary form must reproduce the above copyright\n"
		" *       notice, this list of conditions and the following disclaimer in\n"
		" *       the documentation and/or other materials provided with the\n"
		" *       distribution.\n"
		" *\n"
		" *     * Neither the name of the copyright holder nor the names of its\n"
		" *       contributors may be used to endorse or promote products derived\n"
		" *       from this software without specific prior written permission.\n"
		" *\n"
		" * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n"
		" * \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n"
		" * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n"
		" * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT\n"
		" * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n"
		" * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT\n"
		" * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n"
		" * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n"
		" * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
		" * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n"
		" * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
		" */\n", f);
}

static void
write_epilogue(FILE *f)
{
	fputs("#endif\n", f);
}

static void
write_prologue(FILE *f, const char *guard_macro)
{
	fprintf(f, "\n"
		"/* Generated source file, do not edit manually! */\n"
		"\n"
		"#ifndef %s\n"
		"#define %s\n"
		"\n", guard_macro, guard_macro);
}

static void
write_includes(FILE *f, const char **include)
{
	while (*include != NULL)
		fprintf(f, "#include %s\n", *include++);
}

int
generate_source(struct generator_parameters parameters)
{
	FILE *output = fopen(parameters.output_path, "w");
	if (output == NULL)
		return 1;

	write_license(output, parameters.copyrights);
	write_prologue(output, parameters.include_guard_macro);
	write_includes(output, parameters.includes);
	fputc('\n', output);

	if (visit_function_decls(parameters.input_path, parameters.callback,
			output,
			parameters.clang_argc, parameters.clang_argv) != 0)
		return 1;

	write_epilogue(output);

	fclose(output);

	return 0;
}
