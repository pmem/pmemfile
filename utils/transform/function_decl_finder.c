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

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <clang-c/Index.h>

#include "function_decl_finder.h"

/*
 * The full_func_desc structure holds references to C strings
 * from clang (which one gets via clang_getCString and must dispose of
 * using clang_disposeString. It also holds references to some strings
 * malloc'd by this translation unit.
 * A pointer to the member desc is passed to the client of this library,
 * that holds pointers to the same strings - but the client is allowed
 * to modify anything in desc, so those pointers can't be relied on.
 */
struct full_func_desc {
	struct func_desc desc;

	CXString return_type_name_ref;
	CXString name_ref;

	size_t clang_str_ref_count;
	size_t max_ref_count;
	CXString *clang_refs;
	size_t alloc_count;
	size_t max_alloc_count;
	char **allocated_strings;
	void *client_args;
};

static void *
xmalloc(size_t size)
{
	void *address = malloc(size);
	if (address == NULL)
		abort();

	return address;
}

static void
setup_ref_arrays(struct full_func_desc *full_desc)
{
	full_desc->max_ref_count = 4 + (size_t)full_desc->desc.arg_count * 4;
	full_desc->max_alloc_count = 4 + (size_t)full_desc->desc.arg_count * 4;
	full_desc->clang_str_ref_count = 0;
	full_desc->alloc_count = 0;
	full_desc->clang_refs =
	    xmalloc(sizeof(*(full_desc->clang_refs)) *
			    full_desc->max_ref_count);
	full_desc->allocated_strings =
	    xmalloc(sizeof(*(full_desc->allocated_strings)) *
			    full_desc->max_alloc_count);
}

static void
add_clang_str_ref(struct full_func_desc *full_desc, CXString ref)
{
	if (full_desc->max_ref_count == full_desc->clang_str_ref_count) {
		fprintf(stderr, "function_decl_finder internal error %s:%d\n",
				__FILE__, __LINE__);
		abort();
	}

	full_desc->clang_refs[full_desc->clang_str_ref_count] = ref;
	full_desc->clang_str_ref_count++;
}

static void
add_allocated_str(struct full_func_desc *full_desc, char *buffer)
{
	if (full_desc->max_alloc_count == full_desc->alloc_count) {
		fprintf(stderr, "function_decl_finder internal error %s:%d\n",
				__FILE__, __LINE__);
		abort();
	}

	full_desc->allocated_strings[full_desc->alloc_count] = buffer;
	full_desc->alloc_count++;
}

static void
fetch_function_name(CXCursor func, struct full_func_desc *full_desc)
{
	CXString name = clang_getCursorSpelling(func);
	add_clang_str_ref(full_desc, name);
	full_desc->desc.name = clang_getCString(name);
}

static void
describe_decayed_array(CXType type, struct full_func_desc *full_desc,
			struct type_desc *tdesc)
{
	CXString name = clang_getTypeSpelling(type);

	const char *name_cstr = clang_getCString(name);

	char *buffer = xmalloc(strlen(name_cstr) + 2);
	add_allocated_str(full_desc, buffer);
	strcpy(buffer, name_cstr);
	strcat(buffer, " *");

	clang_disposeString(name);
	tdesc->name = buffer;
	tdesc->is_void = false;
	tdesc->is_pointer = true;
	tdesc->is_pointer_to_const = clang_isConstQualifiedType(type);
	tdesc->is_signed_integral = false;
	tdesc->is_unsigned_integral = false;
}

static void
describe_regular_type(CXType type, struct full_func_desc *full_desc,
			struct type_desc *tdesc)
{
	CXString name = clang_getTypeSpelling(type);
	tdesc->name = clang_getCString(name);

	if (strcmp(tdesc->name, "unsigned int") == 0) {
		/* cstyle complains about 'unsigned int' */
		tdesc->name = "unsigned";
		clang_disposeString(name);
	} else {
		add_clang_str_ref(full_desc, name);
	}

	tdesc->is_void = (type.kind == CXType_Void);

	tdesc->is_pointer = (type.kind == CXType_Pointer);

	if (tdesc->is_pointer) {
		CXType pointee = clang_getPointeeType(type);
		tdesc->is_pointer_to_const =
			(clang_isConstQualifiedType(pointee) != 0);
	} else {
		tdesc->is_pointer_to_const = false;
	}

	tdesc->is_signed_integral =
		type.kind == CXType_SChar ||
		type.kind == CXType_Short ||
		type.kind == CXType_Int ||
		type.kind == CXType_Long ||
		type.kind == CXType_LongLong ||
		type.kind == CXType_Int128;

	tdesc->is_unsigned_integral =
		type.kind == CXType_UChar ||
		type.kind == CXType_UShort ||
		type.kind == CXType_UInt ||
		type.kind == CXType_ULong ||
		type.kind == CXType_ULongLong ||
		type.kind == CXType_UInt128;
}

static void
describe_type(CXType type, struct full_func_desc *full_desc,
			struct type_desc *tdesc)
{
	if (type.kind == CXType_ConstantArray ||
	    type.kind == CXType_IncompleteArray ||
	    type.kind == CXType_VariableArray ||
	    type.kind == CXType_DependentSizedArray) {
		type = clang_getArrayElementType(type);
		describe_decayed_array(type, full_desc, tdesc);
	} else {
		describe_regular_type(type, full_desc, tdesc);
	}

	tdesc->is_integral = tdesc->is_signed_integral ||
			tdesc->is_unsigned_integral;
}

static void
describe_arg_count(CXCursor func_decl, struct full_func_desc *full_desc)
{
	full_desc->desc.is_variadic = (clang_Cursor_isVariadic(func_decl) != 0);
	int count = clang_Cursor_getNumArguments(func_decl);
	full_desc->desc.arg_count = count;
	if (count > 0) {
		full_desc->desc.args = full_desc->client_args =
		    xmalloc((size_t)count * sizeof(full_desc->desc.args[0]));
	} else {
		full_desc->desc.args = full_desc->client_args = NULL;
	}
}

static void
describe_arg(CXCursor arg_cursor, struct full_func_desc *full_desc,
		struct arg_desc *arg)
{
	describe_type(clang_getCursorType(arg_cursor), full_desc, &arg->type);
	CXString name = clang_getCursorSpelling(arg_cursor);
	add_clang_str_ref(full_desc, name);
	arg->name = clang_getCString(name);
}

static void
describe_function(CXCursor func_decl, struct full_func_desc *full_desc)
{
	describe_arg_count(func_decl, full_desc);
	setup_ref_arrays(full_desc);
	fetch_function_name(func_decl, full_desc);
	describe_type(clang_getCursorResultType(func_decl), full_desc,
			&full_desc->desc.return_type);

	for (int i = 0; i < full_desc->desc.arg_count; ++i) {
		describe_arg(clang_Cursor_getArgument(func_decl, (unsigned)i),
				full_desc, full_desc->desc.args + i);
	}
}

static void
dispose_function_desc(struct full_func_desc *desc)
{
	for (size_t i = 0; i < desc->clang_str_ref_count; ++i)
		clang_disposeString(desc->clang_refs[i]);

	for (size_t i = 0; i < desc->alloc_count; ++i)
		free(desc->allocated_strings[i]);

	free(desc->client_args);
}

static enum CXChildVisitResult
visitor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
	if (clang_getCursorKind(cursor) != CXCursor_FunctionDecl)
		return CXChildVisit_Recurse;

	struct full_func_desc full_desc;
	describe_function(cursor, &full_desc);
	int r = ((int (*)(struct func_desc *))client_data)(&full_desc.desc);
	dispose_function_desc(&full_desc);

	if (r == 0)
		return CXChildVisit_Continue;
	else
		return CXChildVisit_Break;
}

int
visit_function_decls(const char *path, int (* callback)(struct func_desc *),
		int argc, char **argv)
{
	CXIndex index = clang_createIndex(0, 0);
	CXTranslationUnit unit;

	enum CXErrorCode parse_result = clang_parseTranslationUnit2(
			index, path,
			(const char * const *)argv, argc,
			NULL, 0,
			CXTranslationUnit_None, &unit);

	if (parse_result != CXError_Success) {
		fprintf(stderr, "parse_result == %d\n", parse_result);
		return 1;
	}

	CXCursor cursor = clang_getTranslationUnitCursor(unit);

	clang_visitChildren(cursor, visitor, (CXClientData)callback);

	clang_disposeTranslationUnit(unit);
	clang_disposeIndex(index);

	return 0;
}
