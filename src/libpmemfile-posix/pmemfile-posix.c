/*
 * Copyright 2016-2017, Intel Corporation
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
 * pmemfile-posix.c -- library constructor / destructor
 */

#define _GNU_SOURCE

#include <limits.h>

#include "blocks.h"
#include "callbacks.h"
#include "compiler_utils.h"
#include "data.h"
#include "locks.h"
#include "out.h"
#include "valgrind_internal.h"

#include "verify_consts.h"

#define PMEMFILE_POSIX_LOG_PREFIX "libpmemfile-posix"
#define PMEMFILE_POSIX_LOG_LEVEL_VAR "PMEMFILE_POSIX_LOG_LEVEL"
#define PMEMFILE_POSIX_LOG_FILE_VAR "PMEMFILE_POSIX_LOG_FILE"

bool pmemfile_overallocate_on_append = false;

#ifdef ANY_VG_TOOL_ENABLED
/* initialized to true if the process is running inside Valgrind */
unsigned _On_valgrind;
#endif

/*
 * libpmemfile_posix_init -- load-time initialization for libpmemfile-posix
 *
 * Called automatically by the run-time loader.
 */
pf_constructor void
libpmemfile_posix_init(void)
{
#ifdef ANY_VG_TOOL_ENABLED
	_On_valgrind = RUNNING_ON_VALGRIND;
#endif

	out_init(PMEMFILE_POSIX_LOG_PREFIX, PMEMFILE_POSIX_LOG_LEVEL_VAR,
			PMEMFILE_POSIX_LOG_FILE_VAR, PMEMFILE_MAJOR_VERSION,
			PMEMFILE_MINOR_VERSION);
	LOG(LDBG, NULL);
	cb_init();

	size_t pmemfile_posix_block_size = 0;

	char *env = getenv("PMEMFILE_BLOCK_SIZE");
	if (env) {
		char *end;
		unsigned long long blk_size = strtoull(env, &end, 0);
		if (env[0] == '\0' || blk_size == ULLONG_MAX ||
				end[0] != '\0' || blk_size < MIN_BLOCK_SIZE) {
			LOG(LUSR, "Invalid value of PMEMFILE_BLOCK_SIZE");
			pmemfile_posix_block_size = 0;
		} else if (blk_size > MAX_BLOCK_SIZE) {
			pmemfile_posix_block_size = MAX_BLOCK_SIZE;
		} else {
			pmemfile_posix_block_size =
					block_roundup((size_t)blk_size);
		}
	}
	LOG(LINF, "block size %zu", pmemfile_posix_block_size);

	if (pmemfile_posix_block_size == 0) {
		env = getenv("PMEMFILE_OVERALLOCATE_ON_APPEND");
		if (env && env[0] == '1')
			pmemfile_overallocate_on_append = true;
	} else {
		set_block_size(pmemfile_posix_block_size);
	}
	LOG(LINF, "overallocate_on_append flag is %s",
		(pmemfile_overallocate_on_append ? "set" : "not set"));
}

/*
 * libpmemfile_posix_fini -- libpmemfile-posix cleanup routine
 *
 * Called automatically when the process terminates.
 */
pf_destructor void
libpmemfile_posix_fini(void)
{
	LOG(LDBG, NULL);
	cb_fini();
	out_fini();
}

/*
 * pmemfile_errormsg -- return last error message
 */
const char *
pmemfile_errormsg(void)
{
	return out_get_errormsg();
}
