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
 * libpmemfile-posix-stubs.h -- definitions of not yet implemented
 * libpmemfile-posix entry points. Do not use these. All the routines
 * just set errno to ENOTSUP.
 * This is header file, and the symbols exported are used while designing
 * the interface of the library.
 * Everything here is subject to change at any time.
 *
 * If/when some pmemfile functionality is implemented, the corresponding
 * header declarations should be moved to the libpmemfile-posix.h header file.
 *
 * This file is expected to be removed eventually.
 */
#ifndef LIBPMEMFILE_POSIX_H
#error Never include this header file directly
#endif

#ifndef LIBPMEMFILE_POSIX_STUBS_H
#define LIBPMEMFILE_POSIX_STUBS_H

int pmemfile_flock(PMEMfilepool *, PMEMfile *file, int operation);

// De we need dup, dup2 in libpmemfile-posix? Maybe, dunno...
PMEMfile *pmemfile_dup(PMEMfilepool *, PMEMfile *);
PMEMfile *pmemfile_dup2(PMEMfilepool *, PMEMfile *file, PMEMfile *file2);

// Memory mapping pmemfiles, these need extra suppport in the preloadable lib
void *pmemfile_mmap(PMEMfilepool *, void *addr, size_t len,
		int prot, int flags, PMEMfile *file, pmemfile_off_t off);
int pmemfile_munmap(PMEMfilepool *, void *addr, size_t len);
void *pmemfile_mremap(PMEMfilepool *, void *old_addr, size_t old_size,
			size_t new_size, int flags, void *new_addr);
int pmemfile_msync(PMEMfilepool *, void *addr, size_t len, int flags);
int pmemfile_mprotect(PMEMfilepool *, void *addr, size_t len, int prot);

pmemfile_ssize_t pmemfile_copy_file_range(PMEMfilepool *,
		PMEMfile *file_in, pmemfile_off_t *off_in,
		PMEMfile *file_out, pmemfile_off_t *off_out,
		size_t len, unsigned flags);

int pmemfile_mknodat(PMEMfilepool *, PMEMfile *dir, const char *path,
		pmemfile_mode_t mode, pmemfile_dev_t dev);

/*
 * Other:
 *	sendfile
 *	tee
 *	splice
 *	vmsplice
 *	statfs
 *	fstatfs
 *	statvfs
 *	fstatvfs
 *	pathconf
 *	fpathconf
 *	name_to_handle_at
 *	open_by_handle_at
 *	ioctl
 *
 * AIO:
 *	aio_read
 *	aio_write
 *	aio_fsync
 *	aio_error
 *	aio_return
 *	aio_suspend
 *	aio_cancel
 *	lio_listio
 *	io_submit
 *	io_cancel
 *	io_destroy
 *	io_getevents
 *	io_setup
 */
#endif
