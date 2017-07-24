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
	struct func_desc desc; /* data presented to client */

	CXString *clang_refs; /* an array of max_ref_count items */
	size_t clang_str_ref_count; /* actual items used */

	char **allocated_strings; /* array of pointers to C strings */
	size_t alloc_count;

	/* address of malloc'd array of args, passed to the client */
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

static void *
xrealloc(void *address, size_t size)
{
	address = realloc(address, size);
	if (address == NULL)
		abort();

	return address;
}

static void
setup_ref_arrays(struct full_func_desc *full_desc)
{
	full_desc->clang_str_ref_count = 0;
	full_desc->alloc_count = 0;
	full_desc->clang_refs = NULL;
	full_desc->allocated_strings = NULL;
}

static void
add_clang_str_ref(struct full_func_desc *full_desc, CXString ref)
{
	size_t size = sizeof(full_desc->clang_refs[0]);
	size *= (full_desc->clang_str_ref_count + 1);

	full_desc->clang_refs = xrealloc(full_desc->clang_refs, size);
	full_desc->clang_refs[full_desc->clang_str_ref_count++] = ref;
}

static void
add_allocated_str(struct full_func_desc *full_desc, char *buffer)
{
	size_t size = sizeof(full_desc->allocated_strings[0]);
	size *= (full_desc->alloc_count + 1);

	full_desc->allocated_strings =
	    xrealloc(full_desc->allocated_strings, size);
	full_desc->allocated_strings[full_desc->alloc_count++] = buffer;
}

static void
fetch_function_name(CXCursor func, struct full_func_desc *full_desc)
{
	CXString name = clang_getCursorSpelling(func);
	add_clang_str_ref(full_desc, name);
	full_desc->desc.name = clang_getCString(name);
}

/*
 * describe_regular_type -- see comments for describe_type below.
 * This routine describes what an array type is like after it decays to
 * a pointer type.
 * "type []" -> "type" -> "type *"
 */
static void
describe_decayed_array(CXType type,
			struct full_func_desc *full_desc,
			struct type_desc *tdesc)
{
	CXType element; /* type of array elements */

	element = clang_getArrayElementType(type); /* "type []" -> "type" */
	CXString name = clang_getTypeSpelling(element);

	const char *name_cstr = clang_getCString(name);

	char *buffer = xmalloc(strlen(name_cstr) + 2);
	add_allocated_str(full_desc, buffer);
	strcpy(buffer, name_cstr);
	strcat(buffer, " *"); /* "type" -> "type *" */

	clang_disposeString(name);
	tdesc->name = buffer;
	tdesc->is_void = false;
	tdesc->is_pointer = true;
	tdesc->is_pointer_to_const = clang_isConstQualifiedType(element);
	tdesc->is_signed_integral = false;
	tdesc->is_unsigned_integral = false;
}

/*
 * describe_regular_type -- see comments for describe_type below.
 */
static void
describe_regular_type(CXType type, struct full_func_desc *full_desc,
			struct type_desc *tdesc)
{
	/*
	 * The name of the type, as appears in the source -- e.g. "size_t"
	 * instead of some underlying type that could be reached by follwoing
	 * typedefs, and macros expansions.
	 */
	CXString name = clang_getTypeSpelling(type);
	tdesc->name = clang_getCString(name);

	if (strcmp(tdesc->name, "unsigned int") == 0) {
		/* cstyle complains about 'unsigned int' */
		tdesc->name = "unsigned";
		clang_disposeString(name);
	} else {
		add_clang_str_ref(full_desc, name);
	}

	/*
	 * The arbitrary fields about what kind of type it is.
	 * This can be easily extended to handle some more arbitrary
	 * fields in struct type_desc. For now, this information
	 * happens to be enough.
	 */
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

/*
 * describe_type -- fill the fields of a struct type_desc instance, based on a
 * CXType instance from libclang.
 * This can be the type of a function argument, or the type of a function's
 * return value.
 *
 * The cases of array typed arguments are treated as special cases.
 * The name of such a function argument's type includes braces. E.g. the
 * following prototype `int x(char arg[2])`
 * results in getting "char []" as type name, and "arg" as argument name from
 * libclang. This can cause problems while trying to use such a type name, for
 * example while attempting to produce a function with an equivalent argument
 * list, such strings can not be used verbatim:
 * `int wrapper_x(char [] arg)` <-- invalid C code.
 * The describe_decayed_array represents the decay of array arguments to
 * pointers in the strings representing them, transforming the
 * above "char []" to "char *".
 */
static void
describe_type(CXType type, struct full_func_desc *full_desc,
			struct type_desc *tdesc)
{
	if (type.kind == CXType_ConstantArray ||
	    type.kind == CXType_IncompleteArray ||
	    type.kind == CXType_VariableArray ||
	    type.kind == CXType_DependentSizedArray) {
		describe_decayed_array(type, full_desc, tdesc);
	} else {
		describe_regular_type(type, full_desc, tdesc);
	}

	tdesc->is_integral = tdesc->is_signed_integral ||
			tdesc->is_unsigned_integral;
}

/*
 * describe_arg_count -- gets information about the number of arguments used for
 * a specific function prototype, and wether the argument list is variadic.
 * Also allocated the array holding the argument descriptions passed to the
 * client of this library.
 */
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

/*
 * describe_arg -- collect information about a single argument of a function.
 * Asks libclang about the type, and name of an argument.
 */
static void
describe_arg(CXCursor arg_cursor, struct full_func_desc *full_desc,
		struct arg_desc *arg)
{
	describe_type(clang_getCursorType(arg_cursor), full_desc, &arg->type);

	/* get the argument name (note: it can be an empty string) */
	CXString name = clang_getCursorSpelling(arg_cursor);
	add_clang_str_ref(full_desc, name);
	arg->name = clang_getCString(name);
}

/*
 * describe_function -- setup an uninitialized full_func_desc instance
 * with information about one specific function declaration.
 */
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

/*
 * dispose_function_desc -- release resources associated with a full_func_desc
 * instance.
 */
static void
dispose_function_desc(struct full_func_desc *desc)
{
	for (size_t i = 0; i < desc->clang_str_ref_count; ++i)
		clang_disposeString(desc->clang_refs[i]);

	free(desc->clang_refs);

	for (size_t i = 0; i < desc->alloc_count; ++i)
		free(desc->allocated_strings[i]);

	free(desc->allocated_strings);

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
