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

#ifndef PMEMFILE_VFD_TABLE_H
#define PMEMFILE_VFD_TABLE_H

struct vfile_description;
struct pmemfile_file;
struct pool_description;

struct vfd_reference {
	struct pool_description *pool;
	struct pmemfile_file *file;
	int kernel_fd;
	struct vfile_description *internal;
};

struct vfd_reference pmemfile_vfd_ref(int vfd);

struct vfd_reference pmemfile_vfd_at_ref(int vfd);

void pmemfile_vfd_unref(struct vfd_reference);

int pmemfile_vfd_dup(int vfd);
int pmemfile_vfd_dup2(int old_vfd, int new_vfd);

long pmemfile_vfd_close(int vfd);

void pmemfile_vfd_table_init(void);

long pmemfile_vfd_chdir_pf(struct pool_description *pool,
				struct pmemfile_file *file);

long pmemfile_vfd_chdir_kernel_fd(int fd);

long pmemfile_vfd_fchdir(int vfd);

long pmemfile_vfd_assign(struct pool_description *pool,
			struct pmemfile_file *file,
			const char *path);

#endif
